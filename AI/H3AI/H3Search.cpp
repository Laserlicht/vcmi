/*
 * H3Search.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3Search.h"

#include "H3Constants.h"

#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/TurnInfo.h"

#include <algorithm>

namespace H3AI
{

H3Search::H3Search() = default;
H3Search::~H3Search() = default;

size_t H3Search::index(const int3 & tile) const
{
	// SS 4D.1 - the engine keeps two planes per map level, selected by cell flag 0x400
	// (the airborne / water-walking traversal state, resolved in SS 4F.3).
	// TODO: the second plane is not modelled here.  Reproducing it requires Fly and
	// Water Walk to create distinct search states for the same tile, which in turn
	// needs the engine's own layer handling; VCMI expresses that as EPathfindingLayer
	// rather than as a duplicated tile grid.
	return (static_cast<size_t>(tile.z) * mapSize.y + tile.y) * mapSize.x + tile.x;
}

bool H3Search::isInside(const int3 & tile) const
{
	return tile.x >= 0 && tile.y >= 0 && tile.z >= 0
		&& tile.x < mapSize.x && tile.y < mapSize.y && tile.z < mapSize.z;
}

const SearchCell & H3Search::at(const int3 & tile) const
{
	if(!isInside(tile))
		return unreachable;

	return cells[index(tile)];
}

SearchCell & H3Search::at(const int3 & tile)
{
	if(!isInside(tile))
		return unreachable;

	return cells[index(tile)];
}

void H3Search::clear()
{
	std::fill(cells.begin(), cells.end(), SearchCell());
	reached.clear();
}

void H3Search::compute(CCallback * cb, const CGHeroInstance * hero, const int3 & start, int movementLimit, int windowRadius)
{
	const int3 size = cb->getMapSize();

	if(size != mapSize || cells.size() != static_cast<size_t>(size.x) * size.y * size.z)
	{
		mapSize = size;
		cells.assign(static_cast<size_t>(size.x) * size.y * size.z, SearchCell());
	}
	else
	{
		// Only the cells the previous run touched need resetting: the influence map
		// (SS 4.5) runs one bounded search per object, so a full clear would dominate.
		for(const int3 & tile : reached)
			cells[index(tile)] = SearchCell();
	}

	reached.clear();
	origin = start;

	if(hero == nullptr || !isInside(start))
		return;

	PathfinderOptions options(*cb);
	options.useEmbarkAndDisembark = true;
	options.ignoreGuards = false;

	CPathfinderHelper helper(*cb, hero, options);

	const int maxMovePoints = std::max(1, hero->movementPointsLimit());
	const int baseMovementCost = static_cast<int>(cb->getSettings().getInteger(EGameSettings::HEROES_MOVEMENT_COST_BASE));

	SearchCell & startCell = at(start);
	startCell.reachable = true;
	startCell.cost = 0;
	startCell.turns = 0;
	startCell.predecessor = int3(-1, -1, -1);
	reached.push_back(start);

	// SS 4D.1 - a LIFO frontier popped from the back, re-pushed on improvement.
	std::vector<int3> frontier;
	frontier.push_back(start);

	while(!frontier.empty())
	{
		const int3 current = frontier.back();
		frontier.pop_back();

		const SearchCell here = at(current);

		if(!here.reachable)
			continue;

		const TerrainTile * sourceTile = cb->getTile(current, false);

		if(sourceTile == nullptr)
			continue;

		// CPathfinderHelper::getNeighbours dereferences the destination tile without a
		// null check, and a player-scoped callback returns nullptr for anything still
		// under fog - so the neighbours are enumerated here instead.
		static constexpr std::array<int3, 8> directions = {
			int3(-1, -1, 0), int3(0, -1, 0), int3(1, -1, 0),
			int3(-1,  0, 0),                 int3(1,  0, 0),
			int3(-1,  1, 0), int3(0,  1, 0), int3(1,  1, 0)
		};

		for(const int3 & direction : directions)
		{
			const int3 next = current + direction;

			if(!isInside(next))
				continue;

			if(windowRadius >= 0
				&& (std::abs(next.x - start.x) > windowRadius || std::abs(next.y - start.y) > windowRadius))
			{
				continue;
			}

			const TerrainTile * destTile = cb->getTile(next, false);

			if(destTile == nullptr)
			{
				// Still under fog.  SS 4.4 rewards walking into the unknown, so the tile
				// is recorded as a one-step-away leaf at the base movement cost and never
				// expanded further - its terrain cannot legitimately be read from here.
				SearchCell & fogCell = at(next);
				const int fogCost = here.cost + baseMovementCost;

				if(fogCost <= movementLimit && (!fogCell.reachable || fogCell.cost > fogCost))
				{
					const bool firstFogVisit = !fogCell.reachable;

					fogCell.reachable = true;
					fogCell.cost = fogCost;
					fogCell.turns = fogCost / maxMovePoints;
					fogCell.predecessor = current;

					if(firstFogVisit)
						reached.push_back(next);
				}

				continue;
			}

			if(!destTile->getTerrain()->isPassable())
				continue;

			// A tile carrying a blocking object cannot be entered at all; one that is
			// merely visitable can be entered only as the last step of a route, so it is
			// recorded but never expanded from.
			const bool blockedTile = destTile->blocked() && !destTile->visitable();

			if(blockedTile)
				continue;

			if(!helper.canMoveBetween(current, next))
				continue;

			const EPathfindingLayer destLayer = destTile->isWater()
				? EPathfindingLayer::SAIL
				: EPathfindingLayer::LAND;

			const int remaining = movementLimit - here.cost;

			if(remaining <= 0)
				continue;

			const int stepCost = helper.getMovementCost(current, next, destLayer, remaining, true, sourceTile, destTile);

			if(stepCost <= 0)
				continue;

			const int newCost = here.cost + stepCost;

			if(newCost > movementLimit)
				continue;

			SearchCell & target = at(next);

			if(target.reachable && target.cost <= newCost)
				continue;

			const bool firstVisit = !target.reachable;

			target.reachable = true;
			target.cost = newCost;
			// TODO: the original stores the turn count in cell + 0x1A, produced by the
			// engine's own per-turn movement accounting.  The report does not give that
			// accounting, so the turn index is derived from the accumulated cost here.
			target.turns = newCost / maxMovePoints;
			target.predecessor = current;

			if(firstVisit)
				reached.push_back(next);

			if(!destTile->visitable())
				frontier.push_back(next);
		}
	}
}

std::vector<int3> H3Search::buildPath(const int3 & destination) const
{
	// SS 4B.11 - searchArray::build_path leaves the route ordered destination -> start;
	// hero::AI_build_step_list is what reverses it into travel order.
	std::vector<int3> path;

	if(!isInside(destination) || !at(destination).reachable)
		return path;

	int3 current = destination;

	while(current.isValid() && current != origin)
	{
		path.push_back(current);
		current = at(current).predecessor;

		if(!isInside(current))
			return {};
	}

	std::reverse(path.begin(), path.end());

	return path;
}

void buildReachability(
	CCallback * cb,
	const CGHeroInstance * hero,
	int range,
	const HeroStateMap & heroStates,
	H3Search & out)
{
	// SS 4B.7 - hero::AI_build_reachability @ 0x42F570
	//
	//   1. our own reachability, bounded by movement points
	out.compute(cb, hero, hero->visitablePos(), std::min(range, hero->movementPointsRemaining()));

	//   2. for every OTHER hero we own, a SECOND flood with ITS movement allowance,
	//      whose reachable cells are struck out of ours.
	for(const CGHeroInstance * other : cb->getHeroesInfo())
	{
		if(other == hero || other->isGarrisoned())
			continue;

		// "if we can't reach it anyway, skip"
		if(!out.at(other->visitablePos()).reachable)
			continue;

		int3 goal = other->visitablePos();
		int handicap = 0;

		auto it = heroStates.find(other->id);

		if(it != heroStates.end() && it->second.hasDestination())
		{
			goal = it->second.destination;

			const int estimate = it->second.destinationCostEstimate;

			handicap = (estimate > other->movementPointsRemaining())
				? 0
				: estimate - other->movementPointsRemaining();
		}

		(void)goal;

		H3Search otherSearch;
		otherSearch.compute(cb, other, other->visitablePos(), other->movementPointsLimit());

		// SS 4B.7 - "walk tmp's reachable-cell list and suppress those cells in `visited`",
		// handicapped by how far that hero still is from its own goal.
		// TODO: the report shows the handicap being computed but never shows how it is
		// applied to the suppression test.  The cells are therefore suppressed outright.
		(void)handicap;

		for(const int3 & tile : otherSearch.reachedCells())
			out.at(tile).suppressed = true;
	}
}

}
