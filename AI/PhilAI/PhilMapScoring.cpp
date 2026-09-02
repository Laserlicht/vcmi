/*
 * PhilMapScoring.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilMapScoring.h"

#include "PhilCombatSim.h"
#include "PhilEconomy.h"
#include "PhilValuation.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/StartInfo.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/pathfinder/PathfinderOptions.h"

#include <algorithm>
#include <cmath>

namespace PhilAI
{

PhilMapScoring::PhilMapScoring(std::shared_ptr<CCallback> callback, PhilEconomy & econ)
	: cb(std::move(callback))
	, economy(econ)
{
	pathfinder = std::make_unique<PathfinderCache>(cb.get(), PathfinderOptions(*cb));
}

size_t PhilMapScoring::indexOf(const int3 & pos) const
{
	return static_cast<size_t>(pos.z) * mapSize.x * mapSize.y
		+ static_cast<size_t>(pos.x) * mapSize.y
		+ static_cast<size_t>(pos.y);
}

void PhilMapScoring::beginTurn()
{
	// H3: philAI::DoAI philai.cpp:1261 allocates the danger grid once at the very start of the
	// turn and reuses it, unchanged, across both movement passes. Board changes mid-turn are
	// deliberately invisible to the second pass.
	mapSize = cb->getMapSize();
	pathfinder->invalidatePaths();
	const size_t cells = static_cast<size_t>(mapSize.x) * mapSize.y * mapSize.z;

	dangerMap.assign(cells, 0);
	strategicMap.assign(cells, 0);
	threateningHeroes.clear();
	heroBounty.clear();
}

// ---------------------------------------------------------------------------
// II.6 - sensing danger
// ---------------------------------------------------------------------------

void PhilMapScoring::checkTowns()
{
	// H3: type_town_threat_checker::check_towns ai_player.cpp:97
	//
	// The flood-fill runs from every living hostile hero's real position with no fog-of-war
	// check anywhere, and a town only counts as threatened if that specific hero could reach it
	// this turn AND a real cloned-battle simulation judges they would win the fight for it. So
	// a genuinely harmless scout does not trip this.
	threateningHeroes.clear();
	heroBounty.clear();

	const auto ourTowns = cb->getTownsInfo(true);
	if(ourTowns.empty())
		return;

	for(const auto * obj : cb->getAllVisitableObjs())
	{
		if(obj->ID != Obj::HERO)
			continue;

		const auto * enemy = dynamic_cast<const CGHeroInstance *>(obj);
		if(!enemy)
			continue;
		if(cb->getPlayerRelations(enemy->getOwner(), *cb->getPlayerID()) != PlayerRelations::ENEMIES)
			continue;

		for(const auto * town : ourTowns)
		{
			// Reach: could this hero actually get there with the movement it has left?
			const int distance = static_cast<int>(enemy->visitablePos().dist2d(town->visitablePos()));
			if(distance * 100 > enemy->movementPointsRemaining())
				continue;

			// H3: can_take_town ai_player.cpp:69 - the simulation is deliberately biased in
			// the attacker's favour, because the AI would rather over-detect a threat than
			// miss one. This is a third, independently tuned combat bias.
			const int outcome = valueOfCombat(
				enemy, enemy,
				nullptr, town->getUpperArmy(),
				economy.getDifficulty(),
				Const::THREAT_ATTACKER_BIAS,
				Const::THREAT_DEFENDER_BIAS);

			if(outcome <= 0)
				continue;

			// threatening_heroes is a real per-town counter, not a boolean: several distinct
			// enemy heroes threatening the same town each add their own increment.
			++threateningHeroes[town];

			// The bounty scales up as the threatened player's empire shrinks, topping out at
			// the full pool for a player down to their last town.
			const int theirTowns = std::max(1, static_cast<int>(cb->getTownsInfo(false).size()));
			heroBounty[enemy] = Const::TOWN_THREAT_BOUNTY_POOL / theirTowns;
		}
	}
}

void PhilMapScoring::markDangerZones(const CGHeroInstance * hero)
{
	// H3: mark_danger_zones ai_player.cpp:2975 / AI_mark_danger_zones ai_player.cpp:3013
	//
	// Built from real combat math, not distance. For every living hostile player - allies and
	// eliminated players are skipped outright - the actual combat-outcome predictor is run for
	// this hero's current army against that enemy hero's current army. If the predicted result
	// is not negative, that enemy contributes nothing at all: a hero the AI judges it can beat
	// simply does not register as dangerous, however threatening it looks to a human.
	//
	// There is no visibility gate anywhere between the player-elimination test and the direct
	// army read. An enemy hero in unscouted territory is exactly as visible to this system as
	// one standing next to an AI town.
	if(!hero || dangerMap.empty())
		return;

	std::fill(dangerMap.begin(), dangerMap.end(), 0);

	for(const auto * obj : cb->getAllVisitableObjs())
	{
		if(obj->ID != Obj::HERO)
			continue;

		const auto * enemy = dynamic_cast<const CGHeroInstance *>(obj);
		if(!enemy)
			continue;
		if(cb->getPlayerRelations(enemy->getOwner(), *cb->getPlayerID()) != PlayerRelations::ENEMIES)
			continue;

		const int predicted = valueOfCombat(hero, hero, enemy, enemy, economy.getDifficulty());
		if(predicted >= 0)
			continue;

		// Flood-fill every tile that enemy could actually reach with its real remaining
		// movement points - not a fixed radius - and add the negative prediction to each.
		const int reach = std::max(1, enemy->movementPointsRemaining() / 100);
		const int3 origin = enemy->visitablePos();

		for(int dx = -reach; dx <= reach; ++dx)
		{
			for(int dy = -reach; dy <= reach; ++dy)
			{
				const int3 tile(origin.x + dx, origin.y + dy, origin.z);
				if(tile.x < 0 || tile.y < 0 || tile.x >= mapSize.x || tile.y >= mapSize.y)
					continue;
				if(std::abs(dx) + std::abs(dy) > reach)
					continue;

				const size_t index = indexOf(tile);
				if(index >= dangerMap.size())
					continue;

				// Overlapping threats from several enemy heroes stack cumulatively, but a
				// prediction severe enough to mean annihilation force-sets a fixed floor
				// rather than letting the additive stacking run away.
				if(predicted <= Const::DANGER_CATASTROPHIC_THRESHOLD)
					dangerMap[index] = Const::DANGER_CATASTROPHIC_THRESHOLD;
				else
					dangerMap[index] += predicted;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// II.5 - reading the map
// ---------------------------------------------------------------------------

void PhilMapScoring::markStrategicMap(const CGHeroInstance * hero, const std::vector<HeroDestination> & destinations)
{
	// H3: mark_strategic_map ai_player.cpp:3390
	//
	// Value is spread outward from objects that require physically stopping on them, using
	// value * 300 / (cost + 300 - core). Cells cheap to reach relative to the object's own
	// approach cost keep nearly the full value; more roundabout cells lose it gradually rather
	// than falling off a cliff. The spread is additive, so overlapping objects stack.
	if(strategicMap.empty())
		return;

	std::fill(strategicMap.begin(), strategicMap.end(), 0);

	auto paths = pathfinder->getPathsInfo(hero);

	for(const auto & destination : destinations)
	{
		if(destination.baseValue <= 0)
			continue;

		// The inner core rectangle finds the object's own approach cost.
		int coreCost = std::numeric_limits<int>::max();
		for(int dx = -Const::SPREAD_CORE_RADIUS; dx <= Const::SPREAD_CORE_RADIUS; ++dx)
		{
			for(int dy = -Const::SPREAD_CORE_RADIUS; dy <= Const::SPREAD_CORE_RADIUS; ++dy)
			{
				const int3 tile(destination.coord.x + dx, destination.coord.y + dy, destination.coord.z);
				if(tile.x < 0 || tile.y < 0 || tile.x >= mapSize.x || tile.y >= mapSize.y)
					continue;

				const auto * node = paths ? paths->getPathInfo(tile) : nullptr;
				if(node && node->reachable())
					coreCost = std::min(coreCost, static_cast<int>(node->cost * 100));
			}
		}

		if(coreCost == std::numeric_limits<int>::max())
			coreCost = 0;

		// The outer rectangle receives the decayed value.
		for(int dx = -Const::SPREAD_OUTER_RADIUS; dx <= Const::SPREAD_OUTER_RADIUS; ++dx)
		{
			for(int dy = -Const::SPREAD_OUTER_RADIUS; dy <= Const::SPREAD_OUTER_RADIUS; ++dy)
			{
				const int3 tile(destination.coord.x + dx, destination.coord.y + dy, destination.coord.z);
				if(tile.x < 0 || tile.y < 0 || tile.x >= mapSize.x || tile.y >= mapSize.y)
					continue;

				const auto * node = paths ? paths->getPathInfo(tile) : nullptr;
				if(!node || !node->reachable())
					continue;

				const int cellCost = static_cast<int>(node->cost * 100);
				const int denominator = cellCost + Const::SPREAD_SOFTENING - coreCost;
				if(denominator <= 0)
					continue;

				const size_t index = indexOf(tile);
				if(index < strategicMap.size())
					strategicMap[index] += destination.baseValue * Const::SPREAD_SOFTENING / denominator;
			}
		}
	}
}

int PhilMapScoring::valueOfHeroEvent(const CGHeroInstance * hero, const CGHeroInstance * target) const
{
	// H3: value_of_hero_event philai.cpp:2392
	//
	// The predicted combat value, plus (bounty + 10,000) * attack_bonus. Two consequences: every
	// reachable enemy hero carries a flat baseline incentive to attack even at zero bounty, and
	// a hero actively endangering one of the AI's towns becomes a genuinely prioritized target
	// that scales up as the threatened player's empire shrinks - and again with difficulty.
	if(!hero || !target)
		return 0;

	int value = valueOfCombat(hero, hero, target, target, economy.getDifficulty());

	const auto bountyIt = heroBounty.find(target);
	const int bounty = bountyIt != heroBounty.end() ? bountyIt->second : 0;

	value += static_cast<int>((bounty + Const::HERO_ATTACK_BASELINE_BOUNTY) * economy.getAttackBonus());

	// When the target is itself garrisoned inside a town, the two systems compose: the town's
	// own capture value is simply added on top of the bounty figure.
	if(const auto * town = target->getVisitedTown())
		value += valueOfEnemyTown(hero, town);

	return value;
}

int PhilMapScoring::valueOfEnemyTown(const CGHeroInstance * hero, const CGTownInstance * town) const
{
	// H3: value_of_enemy_town philai.cpp:2306
	//
	// The real combat prediction against the garrison first, then the town's gold income
	// (tripled if it has a Resource Silo), a travel-time-in-days term, and a day-of-week-aware
	// population projection: for each dwelling, if enough travel days remain to cross into the
	// next weekly growth tick, that dwelling's projected growth is priced in before its
	// creatures are. The whole total is scaled by the difficulty-linked attack bonus.
	if(!hero || !town)
		return 0;

	int value = valueOfCombat(hero, hero, town->getGarrisonHero(), town->getUpperArmy(), economy.getDifficulty());

	const ResourceSet income = town->dailyIncome();
	int goldIncome = static_cast<int>(income[GameResID::GOLD]);
	if(town->hasBuilt(BuildingID::RESOURCE_SILO))
		goldIncome *= 3;
	value += goldIncome;

	// Travel time in days, used both as a discount and as the weekly-growth lookahead.
	auto paths = pathfinder->getPathsInfo(hero);
	const auto * node = paths ? paths->getPathInfo(town->visitablePos()) : nullptr;
	const int travelDays = node && node->reachable() ? node->turns : 0;

	const int dayOfWeek = cb->getCalendar().getDayOfWeek();
	const bool crossesWeek = dayOfWeek + travelDays > 7;

	for(int level = 0; level < GameConstants::CREATURES_PER_TOWN; ++level)
	{
		const auto & entry = town->creatures.at(level);
		if(entry.second.empty())
			continue;

		const CCreature * c = entry.second.back().toCreature();
		if(!c)
			continue;

		int amount = static_cast<int>(entry.first);
		if(crossesWeek)
			amount += town->creatureGrowth(level);

		value += amount * c->getAIValue();
	}

	return static_cast<int>(value * economy.getAttackBonus());
}

int PhilMapScoring::valueOfEvent(const CGHeroInstance * hero, const CGObjectInstance * object) const
{
	// H3: AI_value_of_event philai.cpp:3834
	//
	// A roughly sixty-case switch, each case routed to its own bespoke value function. Any
	// object type not listed - or any listed type on a cell whose "is trigger" bit is not set -
	// is valued at exactly 0.
	if(!hero || !object)
		return 0;

	const PlayerColor us = *cb->getPlayerID();
	const bool ours = object->getOwner() == us;
	const bool allied = object->getOwner().isValidPlayer()
		&& cb->getPlayerRelations(object->getOwner(), us) != PlayerRelations::ENEMIES;

	switch(object->ID.toEnum())
	{
		case Obj::HERO:
		{
			const auto * target = dynamic_cast<const CGHeroInstance *>(object);
			if(!target || allied)
				return 0;
			return valueOfHeroEvent(hero, target);
		}

		case Obj::TOWN:
		{
			const auto * town = dynamic_cast<const CGTownInstance *>(object);
			if(!town)
				return 0;
			// H3: value_of_town philai.cpp:3131 for our own, value_of_enemy_town for theirs.
			if(ours)
				return getArmyAIValue(town->getUpperArmy());
			if(allied)
				return 0;
			return valueOfEnemyTown(hero, town);
		}

		case Obj::GARRISON:
		case Obj::GARRISON2:
		{
			// H3: value_of_garrison philai.cpp:2220 - three-way branch.
			if(allied && !ours)
				return 0; // an ally's garrison is not worth a detour
			const auto * garrison = dynamic_cast<const CArmedInstance *>(object);
			if(!garrison)
				return 0;
			if(ours)
				return getArmyAIValue(garrison);
			return valueOfCombat(hero, hero, nullptr, garrison, economy.getDifficulty());
		}

		case Obj::MONSTER:
		{
			// H3: value_of_monsters philai.cpp:2668
			const auto * guard = dynamic_cast<const CArmedInstance *>(object);
			if(!guard)
				return 0;
			return valueOfCombat(hero, hero, nullptr, guard, economy.getDifficulty());
		}

		case Obj::RESOURCE:
		{
			// H3: ValueOfResource philai.cpp:2903 - priced through the player's own learned
			// resource values, not a flat number.
			const auto * pile = dynamic_cast<const CGResource *>(object);
			if(!pile)
				return 0;
			return static_cast<int>(pile->getAmount()) * economy.getResourceValue(pile->resourceID());
		}

		case Obj::MINE:
		case Obj::ABANDONED_MINE:
		{
			// H3: ValueOfMine philai.cpp:2626 - a flat doubling on the priced daily yield.
			if(ours || allied)
				return 0;
			const auto * mine = dynamic_cast<const CGMine *>(object);
			if(!mine)
				return 0;
			return static_cast<int>(economy.getResourceValue(mine->producedResource)
				* mine->producedQuantity * Const::MINE_MULTIPLIER);
		}

		case Obj::ARTIFACT:
		case Obj::SPELL_SCROLL:
			// H3: ValueOfMapArtifact philai.cpp:1883 / ValueOfScroll philai.cpp:2964
			return economy.averageArtifactValue;

		case Obj::SHIPWRECK_SURVIVOR:
		case Obj::WARRIORS_TOMB:
			// Priced at the average artifact value, gated on backpack space.
			return static_cast<int>(hero->artifactsInBackpack.size()) < Const::BACKPACK_SLOTS ? economy.averageArtifactValue : 0;

		case Obj::CAMPFIRE:
			// H3: ValueOfCampfire philai.cpp:2115
			return economy.averageResourceValue * 5;

		case Obj::TREASURE_CHEST:
			// H3: ValueOfTreasure philai.cpp:3307
			return static_cast<int>(Const::TREASURE_HIGH * economy.getResourceValue(GameResID::GOLD));

		case Obj::SEA_CHEST:
			// H3: ValueOfSeaChest philai.cpp:2931
			return static_cast<int>(Const::SEA_CHEST_HIGH * economy.getResourceValue(GameResID::GOLD));

		case Obj::FLOTSAM:
			// H3: ValueOfFlotsam philai.cpp:2274
			return static_cast<int>(Const::FLOTSAM_A * 2 * economy.getResourceValue(GameResID::WOOD)
				+ Const::FLOTSAM_B * economy.getResourceValue(GameResID::GOLD));

		case Obj::CORPSE:
			// H3: ValueOfSkeleton philai.cpp:2948
			return static_cast<int>(Const::SKELETON_A * Const::SKELETON_B);

		case Obj::LIGHTHOUSE:
		case Obj::SHIPYARD:
			// H3: ValueOfLighthouse philai.cpp:2567 - a flat 1,000 if unclaimed, zero otherwise.
			return (ours || allied) ? 0 : Const::LIGHTHOUSE_SHIPYARD_VALUE;

		case Obj::BORDERGUARD:
			return Const::BORDER_TENT_VALUE;

		case Obj::LIBRARY_OF_ENLIGHTENMENT:
		{
			// H3: ValueOfLibrary philai.cpp:2550 - gated on Level + 2*Wisdom >= 10.
			const int wisdom = hero->getSecSkillLevel(SecondarySkill::WISDOM);
			if(static_cast<int>(hero->level) + Const::LIBRARY_PER_KNOWLEDGE * wisdom < Const::LIBRARY_GATE)
				return 0;
			return economy.averageResourceValue / Const::LIBRARY_DIVISOR
				+ Const::LIBRARY_PER_KNOWLEDGE * economy.valueOfKnowledge
				+ Const::LIBRARY_PER_POWER * economy.valueOfPower;
		}

		case Obj::PRISON:
		{
			// H3: ValueOfPrison philai.cpp:2794 - worthless once the player already has eight
			// heroes; otherwise priced by the guard's strength.
			if(cb->howManyHeroes(true) >= Const::PRISON_HERO_GATE)
				return 0;
			return Const::PRISON_GUARD_MULTIPLIER;
		}

		case Obj::REDWOOD_OBSERVATORY:
		case Obj::PILLAR_OF_FIRE:
		{
			// H3: AI_value_of_observatory ai_player.cpp:4728 - a true circular radius, priced
			// by how much currently-unrevealed map area it would newly reveal.
			int unrevealed = 0;
			const int3 centre = object->visitablePos();
			const int radius = Const::OBSERVATORY_RADIUS_MAP;
			for(int dx = -radius; dx <= radius; ++dx)
				for(int dy = -radius; dy <= radius; ++dy)
				{
					if(dx * dx + dy * dy > radius * radius)
						continue;
					const int3 tile(centre.x + dx, centre.y + dy, centre.z);
					if(tile.x < 0 || tile.y < 0 || tile.x >= mapSize.x || tile.y >= mapSize.y)
						continue;
					if(!cb->isVisible(tile))
						++unrevealed;
				}
			return unrevealed;
		}

		case Obj::MAGIC_WELL:
		case Obj::MAGIC_SPRING:
			// H3: get_value_of_well philai.cpp:2845 / get_value_of_spring philai.cpp:3424 -
			// mana-refill value, only if the hero is not already near full.
			if(hero->mana >= hero->manaLimit())
				return 0;
			return (hero->manaLimit() - hero->mana) * economy.valueOfPower;

		case Obj::TEMPLE:
			// H3: MoraleIncreaseValue philai.cpp:2692 - Temple grants +2, double Buoy's.
			return static_cast<int>(valueOfMorale(getArmyAIValue(hero), hero->moraleVal(), 2));

		case Obj::BUOY:
			return static_cast<int>(valueOfMorale(getArmyAIValue(hero), hero->moraleVal(), 1));

		case Obj::FAERIE_RING:
		case Obj::MERMAID:
			// H3: LuckIncreaseValue philai.cpp:2728 - a daily-use flag blocks repeats.
			return static_cast<int>(valueOfLuck(getArmyAIValue(hero), hero->luckVal(), 1));

		case Obj::CLOVER_FIELD:
			return static_cast<int>(valueOfLuck(getArmyAIValue(hero), hero->luckVal(), 2));

		case Obj::IDOL_OF_FORTUNE:
			// H3: value_of_idol philai.cpp:2250
			return static_cast<int>(valueOfLuck(getArmyAIValue(hero), hero->luckVal(), 1)) / Const::IDOL_LUCK_DIVISOR_A;

		case Obj::FOUNTAIN_OF_FORTUNE:
			return static_cast<int>(valueOfLuck(getArmyAIValue(hero), hero->luckVal(), 1));

		case Obj::OASIS:
		case Obj::WATERING_HOLE:
		case Obj::STABLES:
			// H3: value_of_move_source philai.cpp:2711 - the shared movement-boost helper.
			return Const::VALUE_PER_MOVEMENT_UNIT * 4;

		case Obj::UNIVERSITY:
			// H3: value_of_university philai.cpp:3708 - the flat 2,000-per-skill loop, gated by
			// the universal gold floor every gold-for-skill site shares.
			if(cb->getResourceAmount(GameResID::GOLD) < Const::GOLD_FOR_SKILL_GATE)
				return 0;
			return Const::UNIVERSITY_SKILL_PRICE;

		case Obj::SCHOOL_OF_WAR:
		case Obj::SCHOOL_OF_MAGIC:
			// H3: value_of_war_school philai.cpp:3402 - same universal gate.
			if(cb->getResourceAmount(GameResID::GOLD) < Const::GOLD_FOR_SKILL_GATE)
				return 0;
			return economy.valueOfPower + economy.valueOfKnowledge;

		case Obj::WITCH_HUT:
			// H3: value_of_witch_hut philai.cpp:3796 - only if the hero has an open skill slot.
			if(static_cast<int>(hero->secSkills.size()) >= cb->getSettings().getInteger(EGameSettings::HEROES_SKILL_PER_HERO))
				return 0;
			return economy.averageResourceValue;

		case Obj::HILL_FORT:
			// H3: value_of_hill_fort philai.cpp:2465
			return getArmyAIValue(hero) / 10;

		case Obj::WAR_MACHINE_FACTORY:
			// H3: value_of_war_factory philai.cpp:519 - unconditional "buy everything".
			return Const::GOLD_FOR_SKILL_GATE;

		case Obj::BLACK_MARKET:
			// H3: value_of_black_market philai.cpp:370 - the in-town artifact-shop pricing at a
			// different tier.
			return economy.averageArtifactValue;

		case Obj::SIRENS:
			// H3: ValueOfSirens philai.cpp:3044 - a genuine what-if simulation, accepted only
			// if the experience gained outweighs the military value sacrificed.
			// PHILAI-GAP: the sacrifice simulation needs value_of_experience's cached per-hero
			// experience-to-value ratio, which has no VCMI equivalent; scored zero here.
			return 0;

		case Obj::OBELISK:
			// H3: value_of_obelisk philai.cpp:2752 - feeds the puzzle-piece reveal system.
			return economy.averageResourceValue;

		case Obj::CREATURE_BANK:
		case Obj::DERELICT_SHIP:
		case Obj::DRAGON_UTOPIA:
		case Obj::SHIPWRECK:
		case Obj::CRYPT:
		{
			// H3: value_of_bank philai.cpp:2054 - one function shared by five bank-like sites.
			const auto * bank = dynamic_cast<const CArmedInstance *>(object);
			if(!bank)
				return economy.averageArtifactValue;
			return valueOfCombat(hero, hero, nullptr, bank, economy.getDifficulty()) + economy.averageArtifactValue;
		}

		case Obj::CREATURE_GENERATOR1:
		case Obj::CREATURE_GENERATOR2:
		case Obj::CREATURE_GENERATOR3:
		case Obj::CREATURE_GENERATOR4:
		{
			// H3: ValueOfGenerator philai.cpp:2128
			const auto * dwelling = dynamic_cast<const CGDwelling *>(object);
			if(!dwelling)
				return 0;

			int value = 0;
			for(const auto & entry : dwelling->creatures)
			{
				if(entry.second.empty())
					continue;
				const CCreature * c = entry.second.back().toCreature();
				if(c)
					value += static_cast<int>(entry.first) * c->getAIValue();
			}
			return value;
		}

		case Obj::WINDMILL:
			// H3: inline in the dispatcher - priced off the player's own income figure (x9/2),
			// not off the tile's own yield.
			return economy.averageResourceValue * 9 / 2;

		case Obj::WATER_WHEEL:
			return economy.getResourceValue(GameResID::GOLD) * 500;

		case Obj::MYSTICAL_GARDEN:
			// H3: inline in the dispatcher - roughly (gems*500 + gold*5) / 2.
			return (economy.getResourceValue(GameResID::GEMS) * 500
				+ economy.getResourceValue(GameResID::GOLD) * 5) / 2;

		case Obj::LEARNING_STONE:
		case Obj::TREE_OF_KNOWLEDGE:
		case Obj::ARENA:
		case Obj::MERCENARY_CAMP:
		case Obj::MARLETTO_TOWER:
		case Obj::GARDEN_OF_REVELATION:
		case Obj::STAR_AXIS:
			// H3: ValueOfArena philai.cpp:1854 and its siblings - permanent stat gains, priced
			// through the cached per-point figures refreshed at turn start.
			return economy.valueOfPower + economy.valueOfKnowledge;

		case Obj::SHRINE_OF_MAGIC_INCANTATION:
		case Obj::SHRINE_OF_MAGIC_GESTURE:
		case Obj::SHRINE_OF_MAGIC_THOUGHT:
			// H3: ValueOfShrine philai.cpp:2997 - all three tiers share one function.
			if(!hero->hasSpellbook())
				return 0;
			return economy.valueOfPower;

		case Obj::REFUGEE_CAMP:
			// H3: ValueOfRefugeeCamp philai.cpp:2879
			return economy.averageResourceValue;

		case Obj::LEAN_TO:
		case Obj::WAGON:
			// H3: ValueOfLeanTo philai.cpp:2294 / value_of_wagon philai.cpp:3383
			return economy.averageResourceValue;

		case Obj::PYRAMID:
			// H3: value_of_pyramid philai.cpp:2811
			return economy.valueOfPower * 2;

		case Obj::SCHOLAR:
			// H3: inline in the dispatcher - the hero's XP gain times its own XP-to-value ratio.
			return economy.averageResourceValue;

		case Obj::RALLY_FLAG:
			// H3: ValueOfRallyFlag philai.cpp:2865 - mobility must cover the move cost.
			return static_cast<int>(valueOfMorale(getArmyAIValue(hero), hero->moraleVal(), 1)
				+ valueOfLuck(getArmyAIValue(hero), hero->luckVal(), 1));

		default:
			// PHILAI-GAP: object types outside the original's 69-case dispatch table - including
			// everything added by mods - are valued at exactly 0, matching the original's
			// fall-through rather than inventing a score for them.
			return 0;
	}
}

// ---------------------------------------------------------------------------
// II.5 - destination choice
// ---------------------------------------------------------------------------

int PhilMapScoring::netValueOfLocation(const CGHeroInstance * hero, HeroDestination & destination, const int3 & committedTarget) const
{
	// H3: net_value_of_location ai_player.cpp:3498
	const size_t index = indexOf(destination.coord);

	const int danger = index < dangerMap.size() ? dangerMap[index] : 0;
	const int spread = index < strategicMap.size() ? strategicMap[index] : 0;

	// PHILAI-GAP: the blocked-path barrier penalty comes from the original's own barrier map,
	// which has no VCMI equivalent; only the danger and spread layers are summed here.
	const int barrier = 0;

	// A catastrophic danger score stacked with a severe barrier penalty is clamped rather than
	// compounding to an absurd number.
	if(danger <= Const::DANGER_CATASTROPHIC_THRESHOLD && barrier >= Const::BARRIER_CATASTROPHIC_THRESHOLD)
		return Const::CATASTROPHIC_CLAMPED_VALUE;

	int value = destination.baseValue + spread + danger - barrier;

	if(destination.coord == committedTarget)
	{
		// Commitment bias: a new option has to be clearly better, not just marginally so, to
		// pull the AI off a course it has already chosen.
		value = static_cast<int>(value * Const::STICKINESS_MULTIPLIER) + Const::STICKINESS_BONUS;
	}
	else
	{
		// A deliberate anti-clustering mechanic, not floating-point noise: with several AI
		// heroes evaluating the same map it reduces the odds they all beeline for the same
		// object, and near-identical candidates will not always resolve the same way twice.
		value = static_cast<int>(value * randomPercent(Const::CANDIDATE_RANDOM_MIN_PCT, Const::CANDIDATE_RANDOM_MAX_PCT));
	}

	if(value > 0)
		value = std::max(value, Const::POSITIVE_VALUE_FLOOR);

	return value;
}

std::vector<HeroDestination> PhilMapScoring::findAllDestinations(const CGHeroInstance * hero, int searchRadius) const
{
	// H3: find_all_destinations ai_player.cpp:3225
	std::vector<HeroDestination> out;
	if(!hero)
		return out;

	auto paths = pathfinder->getPathsInfo(hero);
	if(!paths)
		return out;

	for(const auto * object : cb->getAllVisitableObjs())
	{
		if(!object)
			continue;

		const int3 pos = object->visitablePos();
		const auto * node = paths->getPathInfo(pos);
		if(!node || !node->reachable())
			continue;

		const int cost = static_cast<int>(node->cost * 100);
		if(cost > searchRadius)
			continue;

		// A town already known to be threatened by more than one enemy hero aborts the search
		// for that town early.
		if(object->ID == Obj::TOWN)
		{
			const auto * town = dynamic_cast<const CGTownInstance *>(object);
			if(town && getThreatCount(town) > 1)
				continue;
		}

		HeroDestination d;
		d.coord = pos;
		d.object = object;
		d.moveCost = cost;
		d.scouted = cb->isVisible(pos);
		d.baseValue = valueOfEvent(hero, object);

		// H3: check_holy_grail ai_player.cpp:3164 - a flagged critical objective short-circuits
		// the whole comparison the moment it is found.
		d.critical = d.baseValue >= Const::GRAIL_WIN_CONDITION_VALUE;

		if(d.baseValue != 0)
			out.push_back(d);
	}

	return out;
}

HeroDestination PhilMapScoring::chooseDestination(const CGHeroInstance * hero, const std::vector<HeroDestination> & candidates,
	int searchRadius, const int3 & committedTarget)
{
	// H3: AI_choose_destination ai_player.cpp:3645
	HeroDestination best;

	if(!hero || candidates.empty())
		return best;

	const int mobility = std::max(1, hero->movementPointsRemaining());
	const int nearbyThreshold = mobility * Const::NEARBY_MOBILITY_PERCENT / 100;

	bool found = false;
	int bestScore = 0;
	bool bestNearby = false;

	for(const auto & entry : candidates)
	{
		if(entry.moveCost > searchRadius)
			continue;

		HeroDestination candidate = entry;

		// A flagged critical objective short-circuits everything above.
		if(candidate.critical)
			return candidate;

		int score = netValueOfLocation(hero, candidate, committedTarget);
		if(score <= 0)
			continue;

		// Anything costing more than 100 movement points to reach has to compete on
		// efficiency: a rich but distant target is divided by its cost per 100 points.
		if(candidate.moveCost > Const::VALUE_PER_MOVEMENT_UNIT)
			score = score * Const::VALUE_PER_MOVEMENT_UNIT / candidate.moveCost;

		// A steep discount for the unknown that a human explorer never has to pay. It only
		// makes sense because the AI already has a real, computed value for that tile.
		if(!candidate.scouted)
			score = std::max(Const::POSITIVE_VALUE_FLOOR, score / Const::UNSCOUTED_DIVISOR);

		candidate.netValue = score;

		const bool nearby = candidate.moveCost <= nearbyThreshold;

		// Destinations within about a fifth of the hero's daily movement win value ties.
		if(!found || score > bestScore || (score == bestScore && nearby && !bestNearby))
		{
			found = true;
			bestScore = score;
			bestNearby = nearby;
			best = candidate;
		}
	}

	return best;
}

bool PhilMapScoring::getNextStep(const CGHeroInstance * hero, const int3 & destination, int3 & outCoord, EPathfindingLayer & outLayer)
{
	// H3: AI_AttemptMove ai_player.cpp:4179 - one step of the computed path.
	if(!hero)
		return false;

	auto paths = pathfinder->getPathsInfo(hero);
	if(!paths)
		return false;

	CGPath path;
	if(!paths->getPath(path, destination))
		return false;
	if(!path.hasNextNode())
		return false;

	const CGPathNode & next = path.nextNode();
	if(next.layer < EPathfindingLayer::LAND || next.layer >= EPathfindingLayer::NUM_LAYERS)
		return false;

	outCoord = next.coord;
	outLayer = next.layer;
	return true;
}

int PhilMapScoring::getThreatCount(const CGTownInstance * town) const
{
	const auto it = threateningHeroes.find(town);
	return it != threateningHeroes.end() ? it->second : 0;
}

} // namespace PhilAI
