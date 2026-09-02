/*
 * PhilMapScoring.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PhilConstants.h"

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/int3.h"

#include "../../lib/pathfinder/PathfinderCache.h"

#include <map>
#include <memory>
#include <vector>

class CCallback;
class CGHeroInstance;
class CGObjectInstance;
class CGTownInstance;
struct CPathsInfo;

namespace PhilAI
{

class PhilEconomy;

/// One scored destination, the shape of H3's HeroDestination (ai_player.cpp:3213).
struct HeroDestination
{
	int3 coord = int3(-1, -1, -1);
	const CGObjectInstance * object = nullptr;
	int baseValue = 0;   ///< AI_value_of_event's verdict on the object itself
	int netValue = 0;    ///< after spread, danger, stickiness and the random de-weighting
	int moveCost = 0;
	bool critical = false;
	bool scouted = true;
};

/// H3: the map half of type_AI_player (ai_player.cpp) plus philai.cpp's object valuators.
///
/// A danger-value grid covering every cell on every level is built once at the start of the
/// turn and reused unchanged across both movement passes - the AI's sense of where the map is
/// dangerous deliberately does not refresh mid-turn.
class PhilMapScoring
{
	std::shared_ptr<CCallback> cb;
	PhilEconomy & economy;

	/// The original recomputes hero paths through the same pathfinder ordinary movement uses;
	/// this cache is the VCMI equivalent and is invalidated once per turn.
	std::unique_ptr<PathfinderCache> pathfinder;

	int3 mapSize = int3(0, 0, 0);

	/// H3: mark_danger_zones ai_player.cpp:2975 - flood-filled negative combat predictions,
	/// stacked cumulatively where several enemy heroes overlap.
	std::vector<int> dangerMap;

	/// H3: mark_strategic_map ai_player.cpp:3390 - object value spread outward with decay.
	std::vector<int> strategicMap;

	/// H3: type_town_threat_checker ai_player.cpp:97 - per-town counter, not a boolean.
	std::map<const CGTownInstance *, int> threateningHeroes;

	/// H3: mark_towns ai_player.cpp:146 - the bounty placed on a hero able to take a town.
	std::map<const CGHeroInstance *, int> heroBounty;

	size_t indexOf(const int3 & pos) const;

public:
	PhilMapScoring(std::shared_ptr<CCallback> callback, PhilEconomy & econ);

	/// Allocated once at the very start of the turn and reused unchanged by both movement
	/// passes, exactly as philAI::DoAI does.
	void beginTurn();

	/// H3: type_town_threat_checker::check_towns ai_player.cpp:97 and can_take_town
	/// ai_player.cpp:69 - an omniscient flood-fill from every living hostile hero's real
	/// position, confirmed by a cloned-battle simulation biased 1.25x / 0.75x in the
	/// attacker's favour, because the AI would rather over-detect a threat than miss one.
	void checkTowns();

	/// H3: mark_danger_zones ai_player.cpp:2975 / AI_mark_danger_zones ai_player.cpp:3013
	void markDangerZones(const CGHeroInstance * hero);

	/// H3: mark_strategic_map ai_player.cpp:3390
	void markStrategicMap(const CGHeroInstance * hero, const std::vector<HeroDestination> & destinations);

	/// H3: AI_value_of_event philai.cpp:3834 - the roughly sixty-case object rulebook.
	int valueOfEvent(const CGHeroInstance * hero, const CGObjectInstance * object) const;

	/// H3: value_of_hero_event philai.cpp:2392 - the predicted combat value plus
	/// (bounty + 10,000) * attack_bonus, so killing enemy heroes always carries some pull.
	int valueOfHeroEvent(const CGHeroInstance * hero, const CGHeroInstance * target) const;

	/// H3: value_of_enemy_town philai.cpp:2306 - a dedicated town-capture valuation, distinct
	/// from valuing one of the AI's own towns.
	int valueOfEnemyTown(const CGHeroInstance * hero, const CGTownInstance * town) const;

	/// H3: net_value_of_location ai_player.cpp:3498
	int netValueOfLocation(const CGHeroInstance * hero, HeroDestination & destination, const int3 & committedTarget) const;

	/// H3: find_all_destinations ai_player.cpp:3225
	std::vector<HeroDestination> findAllDestinations(const CGHeroInstance * hero, int searchRadius) const;

	/// H3: AI_choose_destination ai_player.cpp:3645
	/// Takes the destination list already gathered by the caller: the search radius escalates
	/// several times per hero, and rebuilding the list - each entry costing a full cloned-army
	/// prediction - once per escalation step is what starves the rest of the client.
	HeroDestination chooseDestination(const CGHeroInstance * hero, const std::vector<HeroDestination> & candidates,
		int searchRadius, const int3 & committedTarget);

	/// H3: AI_AttemptMove ai_player.cpp:4179 walks the computed path one step at a time rather
	/// than teleporting along it. This hands back that next step, with the layer VCMI's move
	/// request requires (the default AUTO layer trips an assert inside CCallback::moveHero).
	bool getNextStep(const CGHeroInstance * hero, const int3 & destination, int3 & outCoord, EPathfindingLayer & outLayer);

	/// Hero paths are cached per hero; every step a hero takes makes its cached path stale, so
	/// the next step would be computed from the position it has already left.
	void invalidatePaths() { pathfinder->invalidatePaths(); }

	int getThreatCount(const CGTownInstance * town) const;
	bool anyTownThreatened() const { return !threateningHeroes.empty(); }
};

} // namespace PhilAI
