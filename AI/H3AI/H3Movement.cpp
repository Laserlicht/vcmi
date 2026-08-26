/*
 * H3Movement.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3Movement.h"

#include "H3CombatEstimate.h"
#include "H3ObjectValue.h"
#include "H3Valuations.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/TerrainTile.h"

#include <algorithm>

namespace H3AI
{

namespace
{
/// CGameInfoCallback::getTopObj resolves through getVisitableObjs(pos) with verbose
/// logging on, which reports "is not visible!" twice for every fogged tile - and returns
/// nothing there regardless of the verbose flag.  With the map-open cheat the tile is
/// read straight out of the map instead, which is what the original AI does for every
/// tile; without it, a fogged tile simply holds nothing the player may know about.
const CGObjectInstance * topObjectAt(H3Context & ctx, const int3 & tile)
{
	if(ctx.cb->isVisible(tile))
		return ctx.cb->getTopObj(tile);

	if(!ctx.openMap)
		return nullptr;

	const TerrainTile * terrain = ctx.cb->getTileUnchecked(tile);

	if(terrain == nullptr || terrain->visitableObjects.empty())
		return nullptr;

	// getTopObj takes the last visitable object; mirrored here.
	return ctx.cb->getObjInstance(terrain->visitableObjects.back());
}
}

// ---------------------------------------------------------------------------------
// ValueMap
// ---------------------------------------------------------------------------------

void ValueMap::resize(const int3 & size)
{
	mapSize = size;
	values.assign(static_cast<size_t>(size.x) * size.y * size.z, 0);
}

void ValueMap::clear()
{
	std::fill(values.begin(), values.end(), 0);
}

bool ValueMap::isInside(const int3 & tile) const
{
	return tile.x >= 0 && tile.y >= 0 && tile.z >= 0
		&& tile.x < mapSize.x && tile.y < mapSize.y && tile.z < mapSize.z;
}

int64_t & ValueMap::at(const int3 & tile)
{
	static int64_t dummy = 0;

	if(!isInside(tile))
	{
		dummy = 0;
		return dummy;
	}

	return values[(static_cast<size_t>(tile.z) * mapSize.y + tile.y) * mapSize.x + tile.x];
}

int64_t ValueMap::at(const int3 & tile) const
{
	if(!isInside(tile))
		return 0;

	return values[(static_cast<size_t>(tile.z) * mapSize.y + tile.y) * mapSize.x + tile.x];
}

// ---------------------------------------------------------------------------------
// SS 4.4 - object scan
// ---------------------------------------------------------------------------------

std::vector<HeroDestination> scanObjects(
	H3Context & ctx,
	const CGHeroInstance * hero,
	H3Search & search,
	int range,
	bool ignoreCost)
{
	std::vector<HeroDestination> result;

	if(hero == nullptr)
		return result;

	// 1 / 2 - searchArray::compute_paths(hero, visited, range, 4|5), i.e. a movement
	// flood over the whole map bounded by `range` movement points, with the cells other
	// friendly heroes already cover struck out (SS 4B.7).
	buildReachability(ctx.cb, hero, range, *ctx.heroStates, search, ctx.openMap);

	// 3 - if the hero stands on a tile with an object of ownership > 1, early out.
	{
		const CGObjectInstance * here = ctx.cb->getTopObj(hero->visitablePos());

		if(here != nullptr && here->getOwner().isValidPlayer() && here->getOwner() != hero->getOwner())
			return result;
	}

	const bool ownsTown = ctx.cb->howManyTowns() > 0;
	const int limit = hero->movementPointsRemaining();

	// 4 - for each reachable cell produced by the search
	for(const int3 & tile : search.reachedCells())
	{
		const SearchCell & cell = search.at(tile);

		if(cell.suppressed)
			continue;

		if(!ignoreCost && cell.cost >= limit)
			continue;

		const CGObjectInstance * object = topObjectAt(ctx, tile);
		int value;
		int moveLimit = limit;

		if(object == nullptr)
		{
			// SS 4.4 - "the exploration drive is not a separate system"
			if(ctx.cb->isVisible(tile) && ownsTown)
				continue;

			value = EXPLORATION_VALUE + (ignoreCost ? 0 : EXPLORATION_CRITICAL_BONUS);
		}
		else
		{
			value = objectValue(ctx, hero, tile, moveLimit);

			if(value == 0)
				continue;

			// SS 4.4 - "if value < 0 and no 'critical' flag -> skip".
			// TODO: the report names a "critical" flag but never says where it comes
			// from; negative values are therefore always skipped.
			if(value < 0)
				continue;
		}

		HeroDestination entry;
		entry.coord = tile;
		entry.value = value;
		entry.moveCost = cell.cost;
		entry.reachable = cell.cost <= limit;
		result.push_back(entry);
	}

	// SS 4.4 - "keeping the vector sorted / deduplicated"
	std::sort(result.begin(), result.end(), [](const HeroDestination & a, const HeroDestination & b)
	{
		if(a.coord.z != b.coord.z)
			return a.coord.z < b.coord.z;
		if(a.coord.y != b.coord.y)
			return a.coord.y < b.coord.y;
		return a.coord.x < b.coord.x;
	});

	result.erase(std::unique(result.begin(), result.end(), [](const HeroDestination & a, const HeroDestination & b)
	{
		return a.coord == b.coord;
	}), result.end());

	return result;
}

// ---------------------------------------------------------------------------------
// SS 4.6 - danger map
// ---------------------------------------------------------------------------------

void addEnemyThreats(H3Context & ctx, const CGHeroInstance * hero, ValueMap & dangerMap)
{
	// SS 4.6 - hero::AI_add_enemy_threats @ 0x42DE50
	if(hero == nullptr)
		return;

	const PlayerColor ourColour = hero->getOwner();

	for(PlayerColor p(0); p < PlayerColor::PLAYER_LIMIT; ++p)
	{
		const PlayerState * state = ctx.cb->getPlayerState(p, false);

		if(state == nullptr)
			continue;

		if(ctx.cb->getPlayerRelations(p, ourColour) != PlayerRelations::ENEMIES)
			continue;

		for(const CGHeroInstance * enemy : state->getHeroes())
		{
			if(enemy == nullptr || enemy->isGarrisoned())
				continue;

			const int v = valueOfCombat(ctx.cb, *ctx.player, hero, enemy, enemy, nullptr);

			if(v >= 0)
				continue; // we'd win - not a threat

			// SS 4.6 - "every cell an enemy hero can reach next turn"
			const int reach = enemy->movementPointsRemaining() + THREAT_RANGE_SLACK;

			H3Search threatSearch;
			threatSearch.compute(ctx.cb, enemy, enemy->visitablePos(), reach, -1, ctx.openMap);

			for(const int3 & tile : threatSearch.reachedCells())
			{
				if(v >= CERTAIN_DEFEAT)
					dangerMap.at(tile) += v;      // graded danger
				else
					dangerMap.at(tile) = ABSOLUTE_NO_GO;  // absolute no-go
			}
		}
	}
}

// ---------------------------------------------------------------------------------
// SS 4.5 - the influence map
// ---------------------------------------------------------------------------------

namespace
{
/// SS 4.5 Phase A - the 11x11 window around one object, flooded from the object itself.
void spreadObjectValue(
	H3Context & ctx,
	const CGHeroInstance * hero,
	const HeroDestination & entry,
	ValueMap & grid,
	H3Search & local)
{
	local.compute(ctx.cb, hero, entry.coord, INFLUENCE_LOCAL_RANGE, INFLUENCE_WINDOW / 2, ctx.openMap);

	// approach cost = max path cost over the 3x3 ring around the object
	int base = 0;

	for(int dy = -1; dy <= 1; ++dy)
	{
		for(int dx = -1; dx <= 1; ++dx)
		{
			const int3 neighbour = entry.coord + int3(dx, dy, 0);
			const SearchCell & cell = local.at(neighbour);

			if(cell.reachable)
				base = std::max(base, cell.cost);
		}
	}

	const int half = INFLUENCE_WINDOW / 2;

	for(const int3 & tile : local.reachedCells())
	{
		// SS 4.5 - the window is 11x11 around the object, clamped to the map.
		if(std::abs(tile.x - entry.coord.x) > half || std::abs(tile.y - entry.coord.y) > half)
			continue;

		const SearchCell & cell = local.at(tile);

		int64_t v;

		if(cell.cost <= base)
			v = entry.value;
		else
			v = static_cast<int64_t>(entry.value) * INFLUENCE_DECAY_RANGE / (cell.cost - base + INFLUENCE_DECAY_RANGE);

		grid.at(tile) += v;
	}
}
}

int chooseDestination(
	H3Context & ctx,
	const CGHeroInstance * hero,
	int radius,
	HeroDestination & out,
	bool & nearby,
	const ValueMap & dangerMap)
{
	// SS 4.5 - hero::AI_choose_destination @ 0x42E0B0, "the heart of the adventure AI".
	nearby = false;

	if(hero == nullptr)
		return 0;

	H3Search search;
	const std::vector<HeroDestination> list = scanObjects(ctx, hero, search, radius, false);

	// ---- Phase A: build the value grid --------------------------------------------
	ValueMap grid;
	grid.resize(ctx.cb->getMapSize());

	H3Search local;

	for(const HeroDestination & entry : list)
	{
		if(!ctx.cb->isVisible(entry.coord))
		{
			// no spreading for fog
			grid.at(entry.coord) += entry.value;
			continue;
		}

		spreadObjectValue(ctx, hero, entry, grid, local);
	}

	// ---- Phase B: pick the destination --------------------------------------------
	const int mpQuantum = hero->movementPointsLimit() * MOVEMENT_QUANTUM_NUMERATOR / MOVEMENT_QUANTUM_DENOMINATOR;
	const bool noTowns = ctx.cb->howManyTowns() == 0;

	if(!out.isValid())
	{
		// SS 4.5 - "scan the 8 neighbours of the hero (delta table at 0x678151..0x678171),
		// pick the adjacent, passable, non-fogged, danger-free cell with the best grid[]
		// value that the hero can actually enter".
		static const int3 deltas[8] = {
			int3(-1, -1, 0), int3(0, -1, 0), int3(1, -1, 0),
			int3(-1,  0, 0),                 int3(1,  0, 0),
			int3(-1,  1, 0), int3(0,  1, 0), int3(1,  1, 0)
		};

		int64_t best = std::numeric_limits<int64_t>::min();
		int3 bestTile(-1, -1, -1);

		for(const int3 & delta : deltas)
		{
			const int3 tile = hero->visitablePos() + delta;

			if(!ctx.cb->isInTheMap(tile) || !ctx.cb->isVisible(tile))
				continue;

			if(!search.at(tile).reachable)
				continue;

			if(dangerMap.at(tile) <= CERTAIN_DEFEAT)
				continue;

			const int64_t score = grid.at(tile);

			if(score > best)
			{
				best = score;
				bestTile = tile;
			}
		}

		if(bestTile.isValid())
		{
			nearby = true;
			out.coord = bestTile;
			out.value = static_cast<int>(std::clamp<int64_t>(best, ABSOLUTE_NO_GO, std::numeric_limits<int>::max()));
			out.moveCost = search.at(bestTile).cost;
			out.reachable = true;
		}
	}
	else
	{
		// the caller already fixed a target: decide whether it is reachable this turn
		const SearchCell & cell = search.at(out.coord);

		out.reachable = (cell.cost <= mpQuantum) || out.coord == hero->visitablePos();

		if(cell.turns > cell.cost)
			out.reachable = false;

		if(out.value < 0 && !noTowns)
			out.reachable = false;
	}

	// ---- final pass over the object list ------------------------------------------
	// SS 4.5 - "choosing the entry with the best value / cost trade-off, honouring the
	// danger map".
	// TODO: the report does not state the trade-off expression.  The influence grid
	// already encodes distance decay, so the grid value at the entry's own cell is used
	// as the score, which is the quantity Phase A exists to produce.
	int64_t bestScore = std::numeric_limits<int64_t>::min();
	const HeroDestination * bestEntry = nullptr;

	for(const HeroDestination & entry : list)
	{
		if(dangerMap.at(entry.coord) <= CERTAIN_DEFEAT)
			continue;

		const int64_t score = grid.at(entry.coord) + dangerMap.at(entry.coord);

		if(score > bestScore)
		{
			bestScore = score;
			bestEntry = &entry;
		}
	}

	// SS 4.5 - special cases before the generic scan: our own town (objType 0x62) or a
	// garrison (0x50), or an already-owned object, send the hero straight there.
	for(const HeroDestination & entry : list)
	{
		const CGObjectInstance * object = topObjectAt(ctx, entry.coord);

		if(object == nullptr)
			continue;

		const bool ownTownOrGarrison = (object->ID == Obj::TOWN || object->ID == Obj::GARRISON || object->ID == Obj::GARRISON2)
			&& object->getOwner() == hero->getOwner();

		if(ownTownOrGarrison && entry.value > bestScore)
		{
			bestScore = entry.value;
			bestEntry = &entry;
		}
	}

	if(bestEntry != nullptr)
	{
		out.coord = bestEntry->coord;
		out.value = bestEntry->value;
		out.moveCost = bestEntry->moveCost;
		out.reachable = bestEntry->moveCost <= mpQuantum;
	}

	return out.isValid() ? out.value : 0;
}

// ---------------------------------------------------------------------------------
// SS 4.7 / SS 4B.8 / SS 4B.11 / SS 4B.12 - move execution
// ---------------------------------------------------------------------------------

void moveToDestination(H3Context & ctx, const CGHeroInstance * hero, HeroDestination & destination)
{
	// SS 4B.12 - hero::AI_move_to_destination @ 0x42FEE0
	if(hero == nullptr || !destination.isValid())
		return;

	HeroAIState & state = (*ctx.heroStates)[hero->id];

	if(destination.coord == hero->visitablePos())
	{
		// arrived
		return;
	}

	// SS 4B.11 - hero::AI_build_step_list @ 0x430610 builds the step list; 0x42FEE0
	// walks it.  The list is built from a search whose limit (99999) is deliberately
	// larger than any real movement allowance, so it spans the whole route.
	H3Search search;
	search.compute(ctx.cb, hero, hero->visitablePos(), PATH_BUILD_LIMIT, -1, ctx.openMap);

	std::vector<int3> path = search.buildPath(destination.coord);

	if(path.empty())
	{
		// SS 4B.11 - "A hero with no route loses its turn."
		state.done = true;
		return;
	}

	// SS 4B.8 - hero::AI_reevaluate_step @ 0x42F980: the AI re-prices every object it
	// walks over and aborts the march as soon as the running total drops below the
	// "certain defeat" sentinel.
	int64_t runningValue = 0;
	std::vector<int3> allowed;

	for(const int3 & step : path)
	{
		// Without the map-open cheat the route may only run over ground the player has
		// actually seen: a move onto a fogged tile is rejected outright by the server
		// when that tile turns out to be blocked.  Walking up to the edge of the fog
		// reveals what lies beyond it anyway.
		if(!ctx.openMap && !ctx.cb->isVisible(step))
			break;

		const CGObjectInstance * object = topObjectAt(ctx, step);

		if(object != nullptr)
		{
			int limit = hero->movementPointsRemaining();

			if(runningValue >= CERTAIN_DEFEAT)
				runningValue += objectValue(ctx, hero, step, limit);
		}

		if(runningValue < CERTAIN_DEFEAT)
			break;

		allowed.push_back(step);

		// SS 4B.11 - teleports terminate the plan, they do not extend it.  The hero
		// walks only as far as the portal this turn; the destination chooser runs again
		// next tick with the exit as the new goal.
		if(object != nullptr
			&& (object->ID == Obj::MONOLITH_ONE_WAY_ENTRANCE
				|| object->ID == Obj::MONOLITH_TWO_WAY
				|| object->ID == Obj::SUBTERRANEAN_GATE
				|| object->ID == Obj::WHIRLPOOL))
		{
			break;
		}

		// One MoveHero pack is a single movement batch, and the engine ends a batch at
		// the zone of control of a wandering monster and at any visitable object - the
		// step is included, then the batch stops.  This also matches SS 4B.8, where the
		// AI re-evaluates once it has actually stepped onto something.
		if(ctx.cb->guardingCreaturePosition(step) != int3(-1, -1, -1))
			break;

		if(!ctx.cb->getVisitableObjs(step, false).empty())
			break;
	}

	if(allowed.empty())
	{
		state.done = true;
		return;
	}

	// CCallback::moveHero's path overload asserts on a concrete layer - EPathfindingLayer
	//::AUTO is only legal for the single-destination overload - and one MoveHero pack
	// carries a single layer for the whole path.  So the leg is cut at the first layer
	// change, which is the same rule SS 4B.11 applies to teleports: a transition ends the
	// plan, and the destination chooser runs again next tick.
	const auto layerOf = [&ctx](const int3 & tile) -> EPathfindingLayer
	{
		const TerrainTile * terrain = ctx.cb->getTile(tile, false);

		return (terrain != nullptr && terrain->isWater())
			? EPathfindingLayer::SAIL
			: EPathfindingLayer::LAND;
	};

	const EPathfindingLayer layer = layerOf(allowed.front());
	size_t uniform = 0;

	while(uniform < allowed.size() && layerOf(allowed[uniform]) == layer)
		++uniform;

	allowed.resize(uniform);

	if(allowed.empty())
	{
		state.done = true;
		return;
	}

	// MoveHero carries object-anchor coordinates, not visitable ones: the server checks
	// h->pos.areNeighbours(dst).  Everything above works in visitable space, so the whole
	// batch is converted here, exactly as HeroMovementController does for a human player.
	std::vector<int3> pathToMove;
	pathToMove.reserve(allowed.size());

	for(const int3 & step : allowed)
		pathToMove.push_back(hero->convertFromVisitablePos(step));

	// Last-moment revalidation: everything above may have blocked, and a hero removed by
	// a battle keeps an invalid owner - the server would answer such a MoveHero with
	// "Player is not allowed to perform this action!".
	const CGHeroInstance * current = ctx.cb->getHero(hero->id);

	if(current == nullptr || current->getOwner() != ctx.player->getColor())
	{
		state.done = true;
		return;
	}

	state.destination = destination.coord;
	state.destinationValid = true;
	state.destinationCostEstimate = destination.moveCost;

	ctx.cb->moveHero(current, pathToMove, false, layer);

	// SS 4F - the AI's complete adventure-spell behaviour is four spells:
	//   Summon Boat (0), Fly (6), Water Walk (7), Dimension Door (8).
	// TODO: all four are triggered from inside the move driver, at the step where the
	// search plane flips (flag 0x400) or where a Dimension Door transition is marked
	// (flag 0x800).  Neither flag is modelled by this reimplementation's search (see
	// H3Search::index), so none of the four is cast.
}

// ---------------------------------------------------------------------------------
// SS 4.3 - the hero's own turn
// ---------------------------------------------------------------------------------

void heroTakeTurn(H3Context & ctx, const CGHeroInstance * hero, bool unlimitedRange, bool & magusHutFlag)
{
	// SS 4.3 - hero::AI_take_turn @ 0x5267B0
	if(hero == nullptr)
		return;

	HeroAIState & state = (*ctx.heroStates)[hero->id];

	int radius = SEARCH_RADIUS_UNLIMITED;

	if(state.hasDestination())
	{
		radius = std::max(
			hero->movementPointsRemaining() + state.destinationCostEstimate + SEARCH_RADIUS_GOAL_SLACK,
			SEARCH_RADIUS_MIN_WITH_GOAL);
	}

	if(unlimitedRange)
		radius = SEARCH_RADIUS_UNLIMITED;

	if(state.hasPreviousDestination())
	{
		const int3 here = hero->visitablePos();
		const int d = std::abs(here.y - state.previousDestination.y)
			+ std::abs(here.x - state.previousDestination.x)
			+ state.previousDestination.z;

		radius = std::min(d * SEARCH_RADIUS_PER_TILE, radius);
	}

	// The danger map is rebuilt by the caller (SS 4.2) and handed down through the
	// search array; it is re-read here through the context.
	ValueMap emptyDanger;
	emptyDanger.resize(ctx.cb->getMapSize());

	HeroDestination dest;
	int value = 0;

	for(int attempt = 0; attempt < DESTINATION_ATTEMPTS; ++attempt)
	{
		bool nearbyFlag = false;
		value = chooseDestination(ctx, hero, radius, dest, nearbyFlag, emptyDanger);

		if(dest.isValid() && (value >= 0 || nearbyFlag))
			break;

		// SS 4.3 - "if (!gpSearchArray->b[0x20]) break;"
		// TODO: searchArray + 0x20 is cleared on entry to the pathfinder and the report
		// never says what sets it, so the widening loop is never cut short here.

		if(radius >= SEARCH_RADIUS_UNLIMITED)
			break;

		radius = std::min(radius * 2, SEARCH_RADIUS_UNLIMITED);
	}

	if(!dest.isValid())
	{
		// nothing worth doing
		state.done = true;
		state.destination = int3(-1, -1, -1);
		state.destinationValid = false;
		return;
	}

	// SS 4.3 - an unexplored destination below 75 is not worth walking to when the
	// scenario mode word equals 7.
	// TODO: gpGame + 0x1F63E ("scenario loss condition / mode word, checked == 7") has no
	// VCMI counterpart, so this early-out is never taken.
	if(!ctx.cb->isVisible(dest.coord) && value < UNEXPLORED_MIN_VALUE)
	{
		// deliberately not returning here - see the TODO above
	}

	const ObjectInstanceID heroId = hero->id;

	moveToDestination(ctx, hero, dest);

	// The move may have started a battle the hero lost.  A removed object keeps
	// pos = (-1,-1,-1), so reading its position afterwards would query tiles off the map.
	hero = ctx.cb->getHero(heroId);

	if(hero == nullptr)
		return;

	// SS 4.3 - landing on an explored magus hut resets the magus-hut value and wakes
	// every hero of this player up again.
	const CGObjectInstance * arrived = topObjectAt(ctx, hero->visitablePos());

	if(arrived != nullptr && arrived->ID == Obj::HUT_OF_MAGI)
	{
		ctx.player->resetMagusHutValue();
		magusHutFlag = false;

		for(auto & entry : *ctx.heroStates)
			entry.second.done = false;
	}
}

// ---------------------------------------------------------------------------------
// SS 4.2 - the per-hero wrapper
// ---------------------------------------------------------------------------------

void heroTurn(H3Context & ctx, const CGHeroInstance * hero, ValueMap & dangerMap, bool aggressive, bool & magusHutFlag)
{
	// SS 4.2 - AI_hero_turn @ 0x5261F0
	if(hero == nullptr)
		return;

	HeroAIState & state = (*ctx.heroStates)[hero->id];

	// 1. if the hero stands on a town tile -> town::AI_visit (SS 4.13)
	const CGTownInstance * town = hero->getVisitedTown();

	if(town != nullptr && town->getOwner() == hero->getOwner())
	{
		// SS 4.13 - 0x42BA60: merge the garrison into the hero, then spend the treasury
		// on the town's dwellings.  Implemented by the kingdom layer.
	}

	// 2. if the hero has no destination and no movement left, burn the turn
	if(!state.hasDestination() && hero->movementPointsRemaining() == 0)
	{
		state.done = true;
		return;
	}

	// 3. zero the value map
	dangerMap.clear();

	// 4. if difficulty > 0, or the current player is a human's ally, build the danger map
	bool humanAlly = false;

	for(PlayerColor p(0); p < PlayerColor::PLAYER_LIMIT; ++p)
	{
		const PlayerState * other = ctx.cb->getPlayerState(p, false);

		if(other != nullptr && other->human
			&& ctx.cb->getPlayerRelations(p, hero->getOwner()) == PlayerRelations::ALLIES)
		{
			humanAlly = true;
		}
	}

	if(ctx.cb->getStartInfo()->difficulty > 0 || humanAlly)
		addEnemyThreats(ctx, hero, dangerMap);

	// 5. if the player has >= 10 wood and >= 1000 gold, mark the "can build" town flags.
	// TODO: those flags (map-cell bits 0x800 and 0x08) exist only to bias the engine's
	// own search array; the report does not say how they change its output.

	// 7. hero::AI_take_turn
	heroTakeTurn(ctx, hero, aggressive, magusHutFlag);
}

}
