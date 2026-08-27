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
	// (the airborne / water-walking traversal state, resolved in SS 4F.3).  One plane is
	// enough here: both spells that flip it last the whole day, so a hero is airborne or
	// it is not for an entire turn, and compute() reads which off the hero's own bonuses
	// (VCMI's EPathfindingLayer) instead of duplicating the grid.  What the second plane
	// bought the original - a route that exists only after casting - is produced by the
	// move driver casting first and re-running the flood (see H3AdventureSpells).
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

void H3Search::compute(CCallback * cb, const CGHeroInstance * hero, const int3 & start, int movementLimit, int windowRadius, bool openMap)
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

	// SS 4F.3 - which of the two traversal planes this hero is in.  Both are read from
	// the hero's live bonuses, so casting Fly or Water Walk and re-running the flood is
	// what moves it between planes.
	const bool flying = helper.isLayerAvailable(EPathfindingLayer::AIR);
	const bool waterWalking = helper.isLayerAvailable(EPathfindingLayer::WATER);

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

		const TerrainTile * sourceTile = openMap ? cb->getTileUnchecked(current) : cb->getTile(current, false);

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

			const TerrainTile * destTile = openMap ? cb->getTileUnchecked(next) : cb->getTile(next, false);

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

			// An airborne hero crosses the land / water boundary freely; on the ground
			// that transition is what canMoveBetween refuses.
			if(!flying && !helper.canMoveBetween(current, next))
				continue;

			EPathfindingLayer destLayer = EPathfindingLayer::LAND;

			if(flying)
				destLayer = EPathfindingLayer::AIR;
			else if(destTile->isWater())
				destLayer = waterWalking ? EPathfindingLayer::WATER : EPathfindingLayer::SAIL;

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
			// SS 4.5a - the 30-byte search-cell record has TWO int16 cost fields, both
			// seeded to 0, told apart only by their consumers:
			//   + 0x1A  the raw movement cost.  A monolith / gate step adds a flat 50 to
			//           THIS field, and it is what AI_object_value gets as its `limit`.
			//   + 0x18  the turn-adjusted cost, compared against visited[] in the
			//           friendly-hero suppression and against hero->mp when the chooser
			//           writes hero + 0x41.
			// This supersedes the looser earlier gloss that put cost at + 0x18 and a turn
			// count at + 0x1A: neither field is a turn index, so deriving one from the
			// accumulated cost is the correct reproduction.
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
	H3Search & out,
	bool openMap)
{
	// SS 4B.7 - hero::AI_build_reachability @ 0x42F570
	//
	//   1. our own reachability, bounded by movement points
	out.compute(cb, hero, hero->visitablePos(), std::min(range, hero->movementPointsRemaining()), -1, openMap);

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
		otherSearch.compute(cb, other, other->visitablePos(), other->movementPointsLimit(), -1, openMap);

		// SS 4B.7 / SS 4.5a - "walk tmp's reachable-cell list and suppress those cells in
		// `visited`", handicapped by how far that hero still is from its own goal.  The
		// handicap is applied as a COST FLOOR, not a mask: for every other friendly hero H,
		//   visited[cell] = min(visited[cell], H_cost + handicap)
		// with handicap <= 0.  AI_scan_objects then skips an object when
		//   g_objectSuppressible[objType] && ourCost > visited[cell]
		// so a strictly cheaper friendly hero claims the object and an equal one does not.
		// A hero still far from its own destination gets a NEGATIVE handicap, is treated
		// as closer than it is, and therefore claims more cells.
		//
		// One shipped bug worth knowing: g_objectSuppressible is filled by walking a list
		// of 37 object-type ids and writing 1 to d[0..36] rather than d[id], so the
		// suppressible set is object types 0-36 and overlaps the intended set only by
		// accident.
		for(const int3 & tile : otherSearch.reachedCells())
		{
			SearchCell & cell = out.at(tile);
			const int costFloor = otherSearch.at(tile).cost + handicap;

			if(cell.cost > costFloor)
				cell.suppressed = true;
		}
	}
}

}
