/*
 * H3Kingdom.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3Kingdom.h"

#include "H3ArmyPlanner.h"
#include "H3CombatEstimate.h"
#include "H3Movement.h"
#include "H3ObjectValue.h"
#include "H3Search.h"
#include "H3SecondarySkills.h"
#include "H3TownValue.h"
#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/Calendar.h"
#include "../../lib/constants/Enumerations.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/IMarket.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/logging/CLogger.h"

#include <algorithm>

namespace H3AI
{

namespace
{
const CBuilding * buildingOf(const CGTownInstance * town, const BuildingID & building)
{
	const CTown * townType = town->getTown();
	auto it = townType->buildings.find(building);

	return it != townType->buildings.end() ? it->second.get() : nullptr;
}

bool townTypeCanHave(const CGTownInstance * town, const BuildingID & building)
{
	return buildingOf(town, building) != nullptr;
}

/// SS 4A.4 - the prerequisite closure of a building, minus what the town already has.
std::set<BuildingID> prerequisiteClosure(const CGTownInstance * town, const BuildingID & building)
{
	std::set<BuildingID> closure;
	std::vector<BuildingID> pending{ building };

	while(!pending.empty())
	{
		const BuildingID current = pending.back();
		pending.pop_back();

		if(closure.count(current) > 0 || town->hasBuilt(current))
			continue;

		if(!townTypeCanHave(town, current))
			continue;

		closure.insert(current);

		const CBuilding * data = buildingOf(town, current);

		if(data == nullptr)
			continue;

		data->requirements.morph([&](const BuildingID & required) -> LogicalExpression<BuildingID>::Variant
		{
			pending.push_back(required);
			return required;
		});

		if(data->upgrade != BuildingID::NONE)
			pending.push_back(data->upgrade);
	}

	return closure;
}

/// SS 4A.4 - the creature a dwelling building produces.
CreatureID dwellingCreature(const CGTownInstance * town, const BuildingID & building)
{
	const int id = building.getNum();

	if(id < BuildingID::DWELL_LVL_1 || id > BuildingID::DWELL_LVL_7_UP)
		return CreatureID::NONE;

	const int level = (id - BuildingID::DWELL_LVL_1) % 7;
	const int upgrade = (id - BuildingID::DWELL_LVL_1) / 7;

	const CTown * townType = town->getTown();

	if(level >= static_cast<int>(townType->creatures.size()))
		return CreatureID::NONE;

	const auto & tier = townType->creatures[level];

	if(upgrade >= static_cast<int>(tier.size()))
		return CreatureID::NONE;

	return tier[upgrade];
}
}

// ---------------------------------------------------------------------------------
// SS 4A.4 - the per-building evaluator table
// ---------------------------------------------------------------------------------

int evaluateBuilding(H3Context & ctx, const CGTownInstance * town, const BuildingID & building)
{
	const int id = building.getNum();
	const bool matchesVictoryTown = ctx.victory.condition == H3VictoryCondition::UPGRADE_TOWN
		&& (ctx.victory.targetObject == town->id || ctx.victory.position == town->visitablePos());

	// ---- 8 Citadel, 9 Castle - 0x42B670 ------------------------------------------
	if(id == BuildingID::CITADEL || id == BuildingID::CASTLE)
	{
		int64_t value = 0;

		// "only from scenario week 5 onward"
		if(ctx.cb->getCalendar().getCurrentDay() >= 5 * ctx.cb->getCalendar().getDaysInWeek())
		{
			for(int level = 0; level < 7; ++level)
			{
				const CreatureID creature = dwellingCreature(town, BuildingID(BuildingID::DWELL_LVL_1 + level));
				const CCreature * data = creature.toCreature();

				if(data == nullptr)
					continue;

				// SS 4A.4 - 0x42B670 sums traits[creature].AI_value over the extra weekly
				// growth a Citadel (x2) or Castle (x3) gives, across the 14 dwelling
				// slots, and only from scenario week 5 onward.  VCMI's own growth model
				// is queried for the delta because it already encodes the same rule.
				value += data->getAIValue() * town->creatureGrowth(level);
			}
		}

		// SS 4C.3, condition 3 - the fort half.  Fires only while the building is missing.
		if(matchesVictoryTown && ctx.victory.fortLevel >= 0)
		{
			const BuildingID required(BuildingID::FORT + ctx.victory.fortLevel);

			if(!town->hasBuilt(required))
				value += VICTORY_CONDITION_OVERRIDE;
		}

		return static_cast<int>(value);
	}

	// ---- 10-13 Village / Town / City / Capitol Hall - 0x42B8B0 --------------------
	if(id >= BuildingID::VILLAGE_HALL && id <= BuildingID::CAPITOL)
	{
		const CBuilding * data = buildingOf(town, building);
		int64_t value = 0;

		if(data != nullptr)
			value = static_cast<int64_t>(data->produce[GameResID::GOLD] * ctx.player->goldValue());

		// SS 4C.3, condition 3 - the hall half.  Note the >=: a higher hall also qualifies.
		if(matchesVictoryTown && ctx.victory.hallLevel >= 0
			&& id >= BuildingID::VILLAGE_HALL + ctx.victory.hallLevel)
		{
			value += VICTORY_CONDITION_OVERRIDE;
		}

		return static_cast<int>(value);
	}

	// ---- 15 Resource Silo - 0x42AF52 ---------------------------------------------
	if(id == BuildingID::RESOURCE_SILO)
	{
		// SS 4A.4 - "-1 if the town is under threat, else 7 * AI_resource_cost(silo output)".
		// SS 4G.4 - "under threat" is town + 0x03 != 0: the count of ENEMY heroes that can
		// reach this town within about 1.5 turns (their max movement + 800), refilled once
		// per turn by kingdom-goal pass A (vftable 0x63B670).  The Silo arm returning -1
		// is the mirror image of the Stronghold/Fortress arms, which only pay out WHILE
		// under threat: the AI refuses to spend on economy with an enemy hero in reach.
		if(townUnderThreat(ctx, town))
			return -1;

		const CBuilding * data = buildingOf(town, building);

		if(data == nullptr)
			return 0;

		return 7 * ctx.player->resourceCost(data->produce);
	}

	// ---- 18 Horde 1, 24 Horde 2 - 0x42B790, and 19/25 their upgrades - 0x42B800 ---
	if(id == BuildingID::HORDE_1 || id == BuildingID::HORDE_2
		|| id == BuildingID::HORDE_1_UPGR || id == BuildingID::HORDE_2_UPGR)
	{
		const int hordeIndex = (id == BuildingID::HORDE_1 || id == BuildingID::HORDE_1_UPGR) ? 0 : 1;
		const int level = town->getTown()->hordeLvl.count(hordeIndex) > 0
			? town->getTown()->hordeLvl.at(hordeIndex)
			: -1;

		if(level < 0)
			return 0;

		const CreatureID creature = dwellingCreature(town, BuildingID(BuildingID::DWELL_LVL_1 + level));
		const CCreature * data = creature.toCreature();

		if(data == nullptr)
			return 0;

		if(ctx.player->creatureFlagged(creature))
			return -1;

		return static_cast<int>(static_cast<int64_t>(data->getAIValue()) * data->getHorde());
	}

	// ---- 30-36 Dwellings 1-7 - 0x42B520 ------------------------------------------
	if(id >= BuildingID::DWELL_LVL_1 && id <= BuildingID::DWELL_LVL_7)
	{
		const CreatureID creature = dwellingCreature(town, building);
		const CCreature * data = creature.toCreature();

		if(data == nullptr)
			return 0;

		if(ctx.player->creatureFlagged(creature))
			return -1;

		const int level = id - BuildingID::DWELL_LVL_1;

		return static_cast<int>(static_cast<int64_t>(data->getAIValue()) * town->creatureGrowth(level));
	}

	// ---- 37-43 Dwelling upgrades - 0x42B5B0 --------------------------------------
	if(id >= BuildingID::DWELL_LVL_1_UP && id <= BuildingID::DWELL_LVL_7_UP)
	{
		const CreatureID upgraded = dwellingCreature(town, building);
		const int level = id - BuildingID::DWELL_LVL_1_UP;
		const CreatureID base = dwellingCreature(town, BuildingID(BuildingID::DWELL_LVL_1 + level));

		const CCreature * upgradedData = upgraded.toCreature();
		const CCreature * baseData = base.toCreature();

		if(upgradedData == nullptr || baseData == nullptr)
			return 0;

		int available = 0;

		if(level < static_cast<int>(town->creatures.size()))
			available = static_cast<int>(town->creatures[level].first);

		return static_cast<int>(
			(static_cast<int64_t>(upgradedData->getAIValue()) - baseData->getAIValue()) * available);
	}

	// ---- everything else - town-type table 0x42B4FC ------------------------------
	// SS 4A.4 / SS 4G.5 - the per-faction special table (0x42B4FC), keyed on
	// town->b[4] - 1.  Castle (0) and Conflux (8) are not in the table at all and score 0
	// for every special:
	//   Rampart    b.22 Treasury:      0 unless the LAST DAY of the week, then
	//                                  ftol(pd->gold * goldValue / 10.0)
	//   Rampart    b.17 Mystic Pond:   2 * averageResourceValue   (pd + 0x160)
	//   Tower      b.21:               100
	//   Necropolis b.21 Necr. Amp.:    1000 * (our heroes whose class id is 9)
	//   Necropolis b.17 Cover of Dark: 10
	//   Stronghold b.17:               5000 if under threat and owned, else 0
	//   Fortress   b.21, b.22:         0 unless under threat, then garrison AI value / 20
	//   Inferno, Dungeon:              0
	// VCMI identifies only some of these; the rest are faction-mod configuration with no
	// portable handle, so their arms stay at the generic default.  The two Rampart
	// specials it does name are transcribed.
	const CBuilding * special = buildingOf(town, building);

	if(special != nullptr)
	{
		if(special->subId == BuildingSubID::TREASURY)
		{
			const Calendar calendar = ctx.cb->getCalendar();

			if(calendar.getDayOfWeek() != calendar.getDaysInWeek())
				return 0;

			return static_cast<int>(ctx.cb->getResourceAmount(GameResID::GOLD) * ctx.player->goldValue() / 10.0);
		}

		if(special->subId == BuildingSubID::MYSTIC_POND)
			return 2 * ctx.player->averageResourceValue();
	}

	return 0;
}

bool townUnderThreat(H3Context & ctx, const CGTownInstance * town)
{
	// SS 4G.4 - kingdom-goal pass A floods from every hero of every player NOT on our
	// team with (that hero's maximum movement + 800) of range, and counts our towns the
	// flood reaches.  That count is what the build planner reads as "under threat".
	if(town == nullptr)
		return false;

	const auto cached = ctx.cachedTownThreat.find(town->id);

	if(cached != ctx.cachedTownThreat.end())
		return cached->second;

	bool threatened = false;
	const int3 gates = town->visitablePos();

	for(PlayerColor p(0); p < PlayerColor::PLAYER_LIMIT && !threatened; ++p)
	{
		if(ctx.cb->getPlayerRelations(p, ctx.player->getColor()) != PlayerRelations::ENEMIES)
			continue;

		const PlayerState * state = ctx.cb->getPlayerState(p, false);

		if(state == nullptr)
			continue;

		for(const CGHeroInstance * enemy : state->getHeroes())
		{
			if(enemy == nullptr || enemy->isGarrisoned())
				continue;

			H3Search flood;

			flood.compute(ctx.cb, enemy, enemy->visitablePos(),
				enemy->movementPointsLimit() + KINGDOM_THREAT_RANGE_SLACK, -1, ctx.openMap);

			if(flood.at(gates).reachable)
			{
				threatened = true;
				break;
			}
		}
	}

	ctx.cachedTownThreat[town->id] = threatened;

	return threatened;
}

// ---------------------------------------------------------------------------------
// SS 4A.4 - AI_build_one_building
// ---------------------------------------------------------------------------------

bool buildOneBuilding(H3Context & ctx, std::set<std::pair<ObjectInstanceID, BuildingID>> & attempted)
{
	int64_t best = 0;
	const CGTownInstance * bestTown = nullptr;
	BuildingID bestBuilding = BuildingID::NONE;

	for(const CGTownInstance * town : ctx.cb->getTownsInfo(true))
	{
		// "if (T already built something this turn) continue"
		if(town->built > 0)
			continue;

		std::array<int64_t, 44> value = {};
		std::array<int64_t, 44> total = {};

		for(int b = 0; b < 44; ++b)
		{
			const BuildingID building(b);

			if(!townTypeCanHave(town, building) || town->hasBuilt(building) || b == BuildingID::GRAIL)
			{
				value[b] = -1;
				continue;
			}

			value[b] = evaluateBuilding(ctx, town, building);
		}

		for(int b = 0; b < 44; ++b)
		{
			if(value[b] <= 0)
				continue;

			// SS 4A.4 - "the prerequisite-closure step is what makes the AI build Forts
			// and Mage Guilds at all - those buildings have no direct value; they inherit
			// it from whatever they unlock."
			const std::set<BuildingID> closure = prerequisiteClosure(town, BuildingID(b));

			ResourceSet cost;

			for(const BuildingID & x : closure)
				cost += town->getBuildingCost(x);

			const int v = ctx.player->getTotalValue(static_cast<int>(value[b]), cost);

			if(v < 0)
				continue;

			for(const BuildingID & x : closure)
				if(x.getNum() >= 0 && x.getNum() < 44)
					total[x.getNum()] += v;
		}

		for(int b = 0; b < 44; ++b)
		{
			const BuildingID building(b);

			if(ctx.cb->canBuildStructure(town, building) != EBuildingState::ALLOWED)
				continue;

			if(attempted.count({ town->id, building }) > 0)
				continue;

			if(total[b] > best)
			{
				best = total[b];
				bestTown = town;
				bestBuilding = building;
			}
		}
	}

	if(bestTown == nullptr)
		return false;

	const ResourceSet cost = bestTown->getBuildingCost(bestBuilding);
	ctx.player->reserveFunds(cost, 1);

	// affordability check
	for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
		if(ctx.cb->getResourceAmount(GameResID(r)) < cost[GameResID(r)])
			return false;

	attempted.insert({ bestTown->id, bestBuilding });

	if(!ctx.cb->buildBuilding(bestTown, bestBuilding))
		return false;

	ctx.player->computeWants();

	return true;
}

// ---------------------------------------------------------------------------------
// SS 4B.6 - ally resource gifting
// ---------------------------------------------------------------------------------

void offerResourcesToAlly(H3Context & ctx, PlayerColor ally)
{
	// SS 4B.6 - type_AI_player::AI_offer_resources_to_ally @ 0x429110
	const PlayerState * us = ctx.cb->getPlayerState(ctx.player->getColor(), false);
	const PlayerState * them = ctx.cb->getPlayerState(ally, false);

	if(us == nullptr || them == nullptr)
		return;

	ResourceSet give;

	for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
	{
		const GameResID res(r);
		const int surplus = ctx.player->supply(res) - ctx.player->demand(res);

		if(surplus <= 0)
		{
			give[res] = surplus;
			continue;
		}

		const int mine = us->resources[res];
		const int theirs = them->resources[res];
		const int reserve = (r == GameResID::GOLD) ? ALLY_GOLD_RESERVE : ALLY_RESOURCE_RESERVE;
		const int minimum = (r == GameResID::GOLD) ? ALLY_MIN_GOLD_GIFT : ALLY_MIN_RESOURCE_GIFT;

		give[res] = std::min((mine - theirs) / 2, surplus);
		give[res] = std::min(give[res], mine - reserve);
		give[res] -= ctx.player->reserved(res);

		if(give[res] < minimum)
			give[res] = 0;

		// "only when the ally is genuinely broke, not to top them up"
		if(give[res] < ALLY_POVERTY_RATIO * theirs)
			give[res] = 0;
	}

	// The commit.  VCMI hands resources to another player through a marketplace
	// (EMarketMode::RESOURCE_PLAYER) - the same channel a human uses, and the same
	// Marketplace requirement H3 imposes - so the gift goes out through a market this
	// player owns.  With no such market the arithmetic above simply has nowhere to go.
	const CGTownInstance * market = nullptr;

	for(const CGTownInstance * town : ctx.cb->getTownsInfo(true))
	{
		const std::set<EMarketMode> modes = town->availableModes();

		if(modes.count(EMarketMode::RESOURCE_PLAYER) > 0)
		{
			market = town;
			break;
		}
	}

	if(market != nullptr)
	{
		for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
		{
			const GameResID res(r);

			if(give[res] <= 0)
				continue;

			// The server clamps the amount to what the sender actually holds, but a
			// request for more than that would still show up as a complaint.
			const int amount = std::min<int>(give[res], us->resources[res]);

			if(amount <= 0)
				continue;

			ctx.cb->trade(market->id, EMarketMode::RESOURCE_PLAYER, res, ally, amount, nullptr);
		}
	}

	// SS 4.10 step 9 / SS 4B.6 - the original logs a warning for any resource that ended
	// up negative.
	for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
		if(us->resources[GameResID(r)] < 0)
			logAi->warn("Warning!  AI player has %d of resource %d.", us->resources[GameResID(r)], r);
}

// ---------------------------------------------------------------------------------
// SS 4B.10a - town::AI_hero_arrival_value
// ---------------------------------------------------------------------------------

namespace
{
/// SS 4B.10a steps 1 and 2, played out on copies: what the candidate's army would be
/// worth once the town has handed over its garrison (SS 4.13 / SS 4B.4) and the treasury
/// has been spent on the town's dwellings.  Nothing here touches game state - the army
/// planner already works on ArmyGroup snapshots, and the recruitment pass is run with
/// fund reservation switched off so the player's own books are left alone.
int64_t townHandoverValue(H3Context & ctx, const CGTownInstance * town, const CGHeroInstance * candidate)
{
	ArmyPlanner planner(ctx.cb, ctx.player);

	planner.initFromTown(town);

	const bool bonus = ctx.player->anyHeroHasArtifact(ArtifactID::ANGELIC_ALLIANCE);
	const int morale = candidate->valOfBonuses(BonusType::MORALE);

	ArmyGroup destination(candidate);
	ArmyGroup source(town);

	const int64_t before = destination.aiValue();

	planner.destination = &destination;
	planner.source = &source;
	planner.mode = bonus;
	planner.morale = morale;

	// SS 4.13 - the same skill-difference gate the town visit applies: a newcomer no
	// stronger than the hero already sitting in the town is given nothing.
	int diff = primarySkillSum(candidate);
	const CGHeroInstance * townHero = town->getGarrisonHero();

	if(townHero != nullptr)
		diff -= primarySkillSum(townHero);

	planner.skillDiff = std::max(diff, 0);

	ArmyPlanner::mergeDuplicateStacks(destination);
	planner.normalise();

	while(planner.takeBestStack(source.slotCount() > 1) > 0)
		;

	// Step 2 - and then the treasury, against the town's dwellings.
	ResourceSet funds = ctx.cb->getResourceAmount();

	planner.recruit(destination, morale, &source, &funds, false, bonus);

	ArmyPlanner::writeback(destination);

	return std::max<int64_t>(0, destination.aiValue() - before);
}
}

int heroArrivalValue(H3Context & ctx, const CGTownInstance * town, const CGHeroInstance * candidate)
{
	// SS 4B.10a - the most elaborate piece of speculative execution in the whole
	// adventure AI: the original performs the hire on the live game state, measures what
	// the map looks like afterwards, and rolls everything back.
	//
	// An AI cannot mutate VCMI's game state, so steps 1 and 2 are run on copies instead:
	// the army planner already works on ArmyGroup snapshots, so "hand the town's garrison
	// over and spend the treasury on its dwellings" can be played out without touching
	// anything.  What still differs from the original is that the map scan below is made
	// with the army the candidate carries rather than with the army it would end up with,
	// so the hand-off enters as its own term rather than by making more of the map
	// winnable.  SS 4B.10a is explicit that this term is what makes "a rich AI with a
	// full Castle pay far more for a tavern hero than a poor one".
	if(town == nullptr || candidate == nullptr)
		return 0;

	const int64_t handover = townHandoverValue(ctx, town, candidate);

	// Step 3 - scan the map from the town
	H3Search scratch;
	std::vector<HeroDestination> objects = scanObjects(ctx, candidate, scratch, 0x7FFF, false);

	// Step 4 - fold the scan into a single number.
	int64_t direct = 0;
	std::vector<int3> touched;

	for(const HeroDestination & entry : objects)
	{
		SearchCell & cell = scratch.at(entry.coord);
		const CGObjectInstance * object = ctx.cb->isVisible(entry.coord)
			? ctx.cb->getTopObj(entry.coord)
			: nullptr;

		if(object == nullptr && ctx.openMap)
		{
			const TerrainTile * terrain = ctx.cb->getTileUnchecked(entry.coord);

			if(terrain != nullptr && !terrain->visitableObjects.empty())
				object = ctx.cb->getObjInstance(terrain->visitableObjects.back());
		}

		// (a) one of our own heroes stands here -> record, don't count
		if(object != nullptr && object->ID == Obj::HERO && object->getOwner() == town->getOwner())
		{
			cell.value = entry.value;
			continue;
		}

		// (b) credit the value to the branch root, if there is one
		if(cell.value < 0 && scratch.isInside(cell.predecessor))
		{
			SearchCell & parent = scratch.at(cell.predecessor);

			if(parent.reachable)
			{
				touched.push_back(cell.predecessor);
				parent.reachable = false;
			}

			parent.value += entry.value;
		}
		else
		{
			direct += entry.value; // (c) uncredited value
		}
	}

	for(const int3 & tile : touched)
		if(scratch.at(tile).value > 0)
			direct += scratch.at(tile).value;

	int64_t total = direct;

	// Step 5 - the crowding penalty
	int64_t overlap = 0;
	int n = 1;

	for(const CGHeroInstance * other : ctx.cb->getHeroesInfo())
	{
		if(other->visitablePos().z != candidate->visitablePos().z)
			continue;

		const SearchCell & cell = scratch.at(other->visitablePos());

		if(!cell.reachable)
			continue;

		++n;

		int64_t v = cell.value;

		if(scratch.isInside(cell.predecessor) && scratch.at(cell.predecessor).value < 0)
			v += scratch.at(cell.predecessor).value;

		overlap = std::max(overlap, v);
	}

	// SS 4B.10a - "a second hero bought into a region already covered by an existing hero
	// is worth roughly half, a third roughly a third."  The town's hand-off is not
	// subject to that division: it is worth the same whether or not another hero already
	// covers the region.
	return static_cast<int>((total + overlap) / n + handover);
}

// ---------------------------------------------------------------------------------
// SS 4B.10 - hire execution
// ---------------------------------------------------------------------------------

bool buyHero(H3Context & ctx, const CGHeroInstance * candidate)
{
	// SS 4B.10 - type_AI_player::AI_buy_hero @ 0x431800
	if(candidate == nullptr)
		return false;

	int64_t total = 0;

	// Step 1 - price the candidate's baggage.
	// (a) 64 backpack slots, priced by *transfer* value: what the artifact would be worth
	//     if handed to the best hero we already own.  Floor 10.
	for(const auto & slot : candidate->artifactsInBackpack)
	{
		const CArtifactInstance * artifact = slot.getArt();

		if(artifact == nullptr)
			continue;

		// SS 4.9a - hero::total_artifact_value @ 0x4339E0 is implemented as
		// H3ArtifactValue::totalArtifactValue: what this hero would GAIN by taking the
		// artifact, net of whatever it must give up to wear it.  The original's loop
		// writes the "loss" term unconditionally rather than accumulating it, so the LAST
		// occupied slot wins - that is the shipped behaviour, reproduced as such.
		// Here only the documented floor of 10 per backpack artifact is added, because a
		// backpack artifact is worn by nobody and so displaces nothing.
		total += ARTIFACT_MIN_VALUE;
	}

	// (b) 19 equipped slots, priced by the player-generic AI_get_value_of_artifact.
	//     Note the asymmetric floor: an equipped artifact can be worth 0.
	for(const auto & slot : candidate->artifactsWorn)
	{
		const CArtifactInstance * artifact = slot.second.getArt();

		if(artifact == nullptr)
			continue;

		total += artifactValue(ctx, artifact->getTypeId());
	}

	// Step 2 - add the army, valued through the AI's own resource prices.
	// "a tavern hero's army is worth exactly what it would cost to buy the same creatures"
	for(const auto & slot : candidate->Slots())
	{
		const CCreature * creature = candidate->getCreature(slot.first);

		if(creature == nullptr)
			continue;

		const int count = candidate->getStackCount(slot.first);

		for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
		{
			total = static_cast<int64_t>(
				static_cast<double>(creature->getRecruitCost(GameResID(r)))
					* ctx.player->resourceValue(GameResID(r))
					* static_cast<double>(count)
				+ static_cast<double>(total));
		}
	}

	// Step 3 - the affordability gate.
	const int heroCount = ctx.cb->howManyHeroes(true);
	const int64_t threshold = static_cast<int64_t>(
		static_cast<double>(heroCount) * ctx.player->goldValue() * TAVERN_HERO_COST);

	if(threshold > total && ctx.cb->getResourceAmount(GameResID::GOLD) < heroCount * TAVERN_HERO_COST)
		return false; // (1)

	// Step 4 - choose the town.
	const CGTownInstance * bestTown = nullptr;
	int64_t best = threshold;

	for(const CGTownInstance * town : ctx.cb->getTownsInfo(true))
	{
		// "a town with a hero standing in it is skipped outright"
		if(town->getVisitingHero() != nullptr)
			continue;

		int64_t score = total;

		if(!town->hasBuilt(BuildingID::TAVERN))
		{
			if(ctx.cb->canBuildStructure(town, BuildingID::TAVERN) != EBuildingState::ALLOWED)
				continue;

			score -= ctx.player->resourceCost(town->getBuildingCost(BuildingID::TAVERN));
		}

		score += heroArrivalValue(ctx, town, candidate);

		if(score > best)
		{
			best = score;
			bestTown = town;
		}
	}

	if(bestTown == nullptr)
		return false; // (2)

	// Step 5 - commit.
	if(!bestTown->hasBuilt(BuildingID::TAVERN))
	{
		// SS 4B.10 - "the Tavern is built first and unconditionally, and if that
		// construction drops the treasury below 2500 the hire is abandoned - but the
		// Tavern stays built."
		if(!ctx.cb->buildBuilding(bestTown, BuildingID::TAVERN))
			return false; // (3)

		if(ctx.cb->getResourceAmount(GameResID::GOLD) < TAVERN_HERO_COST)
			return false; // (4)
	}

	ctx.cb->recruitHero(bestTown, candidate);

	return true;
}

// ---------------------------------------------------------------------------------
// SS 4B.9 - hiring heroes
// ---------------------------------------------------------------------------------

bool hireHero(H3Context & ctx)
{
	// SS 4B.9 - type_AI_player::AI_hire_hero @ 0x431360
	const int heroCount = ctx.cb->howManyHeroes(true);

	if(heroCount >= ENGINE_HERO_CAP)
		return false;

	if(ctx.cb->getResourceAmount(GameResID::GOLD) < TAVERN_HERO_COST)
		return false;

	const int difficulty = std::clamp<int>(ctx.cb->getStartInfo()->difficulty, 0, 4);

	if(heroCount >= MAX_AI_HEROES[difficulty])
		return false;

	// how many heroes do the HUMAN players collectively own?
	int humanHeroes = 0;

	for(PlayerColor p(0); p < PlayerColor::PLAYER_LIMIT; ++p)
	{
		const PlayerState * state = ctx.cb->getPlayerState(p, false);

		if(state != nullptr && state->human)
			humanHeroes += static_cast<int>(state->getHeroes().size());
	}

	// SS 4B.9 - "a deliberate brake on AI hero spam that scales with how far ahead the
	// human is."
	if(heroCount > 0 && humanHeroes >= HUMAN_HERO_CAP[difficulty])
		return false;

	// score each of the heroes the tavern is offering
	const CGTownInstance * anyTown = nullptr;

	for(const CGTownInstance * town : ctx.cb->getTownsInfo(true))
	{
		if(town->hasBuilt(BuildingID::TAVERN))
		{
			anyTown = town;
			break;
		}
	}

	if(anyTown == nullptr)
		return false;

	const CGHeroInstance * bestCandidate = nullptr;
	int64_t bestScore = 0;

	for(const CGHeroInstance * candidate : ctx.cb->getAvailableHeroes(anyTown))
	{
		if(candidate == nullptr)
			continue;

		int64_t score = 0;

		// SS 4B.9 - "a tavern hero is priced almost entirely by what its artifacts would
		// be worth to the best hero we already own (floor 10 per artifact), not by its
		// own stats.  That is why AI players buy and immediately dismiss heroes carrying
		// good relics."
		for(const auto & slot : candidate->artifactsWorn)
			if(slot.second.getArt() != nullptr)
				score += std::max(ARTIFACT_MIN_VALUE, artifactValue(ctx, slot.second.getArt()->getTypeId()));

		for(const auto & slot : candidate->artifactsInBackpack)
			if(slot.getArt() != nullptr)
				score += std::max(ARTIFACT_MIN_VALUE, artifactValue(ctx, slot.getArt()->getTypeId()));

		// SS 4.9a - the weighting is armyValue * (get_primary_skill_sum + 40) / 40, the
		// same (sum + 40) / 40 factor every luck/morale object valuation uses.
		// get_primary_skill_sum (0x4E5960) adds the four primary skills at hero + 0x476,
		// each clamped to <= 99, counting a zero as 1 for spell power and knowledge only.
		score = score * (primarySkillSum(candidate) + 40) / 40;

		if(score > bestScore)
		{
			bestScore = score;
			bestCandidate = candidate;
		}
	}

	if(bestCandidate == nullptr)
		return false;

	return buyHero(ctx, bestCandidate);
}

// ---------------------------------------------------------------------------------
// SS 4.13 - hero visits one of our own towns
// ---------------------------------------------------------------------------------

void visitOwnTown(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town)
{
	// SS 4.13 - 0x42BA60: merge the garrison into the hero, then spend the treasury on
	// the town's dwellings.  It is a parameterisation of the SS 4B.4 planner.
	if(hero == nullptr || town == nullptr)
		return;

	ArmyPlanner planner(ctx.cb, ctx.player);

	planner.initFromTown(town);

	const CGHeroInstance * townHero = town->getGarrisonHero();

	// SS 4.13 / SS 4B.4a - artifact 0x81 is 129, the ANGELIC ALLIANCE.  It is passed as
	// the planner's "ignore alignment mixing" flag, and playerData::AnyHeroHasArtifact
	// (0x4BACB0) scans the player's field heroes - so one hero anywhere in the kingdom
	// carrying the assembled set switches the behaviour on for all of them.
	const bool bonus = ctx.player->anyHeroHasArtifact(ArtifactID::ANGELIC_ALLIANCE);

	ArmyGroup destination(hero);
	ArmyGroup source(town);
	const ArmyGroup garrisonBefore(town);

	planner.destination = &destination;
	planner.source = &source;
	planner.mode = bonus;
	planner.morale = hero->valOfBonuses(BonusType::MORALE);

	int diff = primarySkillSum(hero);

	if(townHero != nullptr)
		diff -= primarySkillSum(townHero);

	// SS 4.13 - "when the visiting hero is no stronger than the hero already sitting in
	// the town, skillDiff is 0 and not a single creature moves."
	planner.skillDiff = std::max(diff, 0);

	ArmyPlanner::mergeDuplicateStacks(destination);
	planner.normalise();

	while(planner.takeBestStack(source.slotCount() > 1) > 0)
		;

	ArmyPlanner::writeback(destination);

	// Commit the exchange.  take_best_stack consumes slots out of `source`, which is the
	// scratch copy of the town garrison, so the slots it emptied are exactly the ones to
	// hand over.  Note this indexes the *garrison*, not the planner's destination layout.
	//
	// VCMI moves troops through swap / merge / split callbacks on concrete slots, so the
	// planner's final layout is translated into that sequence here: a slot the planner
	// emptied moves whole, and a partial take (SS 4B.4's "also try leaving one behind"
	// branch) is split out at the count the planner actually wanted.
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
	{
		const SlotID garrisonSlot(slot);

		if(!garrisonBefore.type[slot].hasValue())
			continue;

		const int remaining = source.type[slot].hasValue() ? source.count[slot] : 0;
		const int taken = garrisonBefore.count[slot] - remaining;

		// nothing was taken from this slot
		if(taken <= 0)
			continue;

		// the town must really still hold creatures there, or the server rejects the pack
		if(!town->hasStackAtSlot(garrisonSlot))
			continue;

		if(remaining <= 0)
		{
			ctx.cb->bulkMoveArmy(town->id, hero->id, garrisonSlot);
			continue;
		}

		// getSlotFor answers with the hero's existing stack of that creature, or with a
		// free slot; without either there is nowhere to put the split.
		const SlotID target = hero->getSlotFor(garrisonBefore.type[slot]);

		if(!target.validSlot())
			continue;

		// ArrangeStacks' split arm reads `val` as the final count in the destination
		// slot when that slot already holds the same creature, and as the number to move
		// when it is empty.
		const int already = hero->hasStackAtSlot(target) ? hero->getStackCount(target) : 0;

		ctx.cb->splitStack(town, hero, garrisonSlot, target, already + taken);
	}

	// SS 4.13 - the dwelling pass, and the Easy-mode skip.
	const int difficulty = ctx.cb->getStartInfo()->difficulty;
	bool runDwellingPass = difficulty != 0;

	if(!runDwellingPass)
	{
		// "an AI whose team contains no other computer player skips the dwelling pass
		// altogether ... an AI with a computer ally plays the pass normally even on Easy."
		for(PlayerColor p(0); p < PlayerColor::PLAYER_LIMIT; ++p)
		{
			if(p == ctx.player->getColor())
				continue;

			const PlayerState * state = ctx.cb->getPlayerState(p, false);

			if(state != nullptr && !state->human
				&& ctx.cb->getPlayerRelations(p, ctx.player->getColor()) == PlayerRelations::ALLIES)
			{
				runDwellingPass = true;
				break;
			}
		}
	}

	if(!runDwellingPass)
		return;

	// SS 4.13 - re-reading the hero's morale for each purchase, so newly bought creatures
	// are priced with the army as it stands after each purchase.
	ResourceSet funds = ctx.cb->getResourceAmount();
	ArmyGroup recruited(hero);

	planner.recruit(recruited, hero->valOfBonuses(BonusType::MORALE), &source, &funds, true, bonus);

	// Commit the recruitment.
	for(size_t level = 0; level < town->creatures.size(); ++level)
	{
		const auto & tier = town->creatures[level];

		if(tier.second.empty())
			continue;

		for(const CreatureID & creature : tier.second)
		{
			int wanted = 0;

			for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
				if(recruited.type[slot] == creature)
					wanted += recruited.count[slot];

			int owned = 0;

			for(const auto & slot : hero->Slots())
				if(hero->getCreature(slot.first) != nullptr && hero->getCreature(slot.first)->getId() == creature)
					owned += hero->getStackCount(slot.first);

			const int toBuy = std::min(wanted - owned, static_cast<int>(tier.first));

			if(toBuy > 0)
				ctx.cb->recruitCreatures(town, hero, creature, toBuy, static_cast<si32>(level));
		}
	}
}

// ---------------------------------------------------------------------------------
// SS 4.10 - manage_kingdom
// ---------------------------------------------------------------------------------

void manageKingdom(H3Context & ctx)
{
	// SS 4.10 - type_AI_player::manage_kingdom @ 0x428DD0
	//
	// 1. reserved_funds[i] -= playerData->income[i]  (clamped at 0)
	ctx.player->decayReservedFunds();

	// 2. SS 4G.4 - the two "kingdom goal" evaluators 0x4280E0, with descriptors
	//    {0x63B67C, player} and {0x63B670, player}.  Each pass floods, from every hero of
	//    every player NOT on our team, with (maxMovement + 800) of range, and calls a
	//    visit method on each of OUR towns the flood reaches:
	//      0x63B670 - reset clears town + 0x03 on every town, visit increments it.
	//                 That count IS the "under threat" measure the build planner reads.
	//      0x63B67C - reset is a bare ret; visit is 0x428580.
	//    Pass A's product - "an enemy hero can reach this town" - is what the build
	//    planner reads, and it is computed on demand by townUnderThreat (cached on the
	//    context for the turn) rather than refilled into a town field here.  Pass B's
	//    visit method (0x428580) has no recorded body, so nothing else is run.

	// 3. compute_resource_values(player)
	ctx.player->computeResourceSupplyAndThreats();
	ctx.player->computeWants();

	// 4. the greedy purchase loop: one building / creature / market action per iteration,
	//    repeated until nothing more is worth buying.  The market action comes first, so
	//    the build loop below sees the treasury the trades produce: SS 4A.3's planner is
	//    what tells reserve_funds a purchase is affordable "after trading", and without
	//    the trade actually happening that promise would be empty.
	for(const CGTownInstance * town : ctx.cb->getTownsInfo(true))
	{
		if(town->availableModes().count(EMarketMode::RESOURCE_RESOURCE) > 0)
		{
			doTrades(ctx, town, nullptr);
			break;
		}
	}

	std::set<std::pair<ObjectInstanceID, BuildingID>> attempted;

	while(buildOneBuilding(ctx, attempted))
		;

	// 5. town build-order planning (0x431360) is, despite its position, the tavern
	//    decision (SS 4B.9).
	hireHero(ctx);

	// 6. the shared "value of a purchase" helper 0x428740 == compute_wants (SS 4A.1)
	ctx.player->computeWants();

	// 7. SS 8 - there are NO per-town AI build flags to recompute.  manage_kingdom
	//    (0x428DD0) writes no struct field at all; it is pure orchestration.  The build
	//    decision is recomputed from scratch on every call by AI_build_one_building, which
	//    picks the single best (town, building) pair across the whole kingdom.  Nothing is
	//    cached on the town, so there is nothing here to reproduce.

	// 8. for each other player on our team (and separately, each ally): cross-player
	//    resource trading / gifting.
	for(PlayerColor p(0); p < PlayerColor::PLAYER_LIMIT; ++p)
	{
		if(p == ctx.player->getColor())
			continue;

		if(ctx.cb->getPlayerRelations(p, ctx.player->getColor()) != PlayerRelations::ALLIES)
			continue;

		offerResourcesToAlly(ctx, p);
	}
}

// ---------------------------------------------------------------------------------
// SS 4A.3 / SS 4G.7 - the commit half of the trade machinery
// ---------------------------------------------------------------------------------

void doTrades(H3Context & ctx, const IMarket * market, const CGHeroInstance * hero)
{
	// SS 4G.7 names four routines and says the last one commits:
	//   0x42A2B0  can the deficit be covered?      (H3Player::planTrades)
	//   0x42A580  choose which resource to sell, and at what rate
	//   0x42AB40  re-validate the plan once built
	//   0x42AC20  EXECUTE the trades
	// Only the first is specified.  What is sold here is exactly what planTrades counts
	// as sellable - the surplus left after the hard floor of 20 and after the
	// supply / demand cap - and it is sold for whichever resource the kingdom is
	// shortest of, which is the only ordering the supply/threat model supplies.
	if(market == nullptr || !market->allowsTrade(EMarketMode::RESOURCE_RESOURCE))
		return;

	const PlayerState * state = ctx.cb->getPlayerState(ctx.player->getColor(), false);

	if(state == nullptr)
		return;

	const std::vector<TradeItemBuy> tradeable = market->availableItemsIds(EMarketMode::RESOURCE_RESOURCE);

	const auto isTradeable = [&tradeable](const GameResID & resource)
	{
		return std::find(tradeable.begin(), tradeable.end(), TradeItemBuy(resource)) != tradeable.end();
	};

	// The shortages, worst first.
	std::vector<GameResID> wanted;

	for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
	{
		const GameResID resource(r);

		if(ctx.player->demand(resource) > ctx.player->supply(resource) && isTradeable(resource))
			wanted.push_back(resource);
	}

	std::sort(wanted.begin(), wanted.end(), [&ctx](const GameResID & a, const GameResID & b)
	{
		const double left = (ctx.player->demand(a) - ctx.player->supply(a)) * ctx.player->resourceValue(a);
		const double right = (ctx.player->demand(b) - ctx.player->supply(b)) * ctx.player->resourceValue(b);

		return left > right;
	});

	// A local copy of the purse, so several trades in one visit stay consistent without
	// waiting for the server to answer each one.
	ResourceSet purse = state->resources;

	for(const GameResID & buy : wanted)
	{
		for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
		{
			const GameResID sell(r);

			if(sell == buy || !isTradeable(sell))
				continue;

			// SS 4A.3's sellable surplus, verbatim.
			const int reserve = std::max(ctx.player->reserved(sell), TRADE_RESERVE_FLOOR);
			int surplus = purse[sell] - reserve;

			surplus = std::min(surplus, ctx.player->supply(sell) - ctx.player->demand(sell));

			if(surplus <= 0)
				continue;

			int give = 0;
			int get = 0;

			if(!market->getOffer(sell.getNum(), buy.getNum(), give, get, EMarketMode::RESOURCE_RESOURCE) || give <= 0)
				continue;

			// tradeResources refuses a deal that does not use whole offer units.
			const int deals = surplus / give;

			if(deals <= 0)
				continue;

			// Never trade away more value than is bought back: the exchange rate at a
			// poor market can be bad enough that covering a shortage costs more than the
			// shortage is worth.
			const double sold = deals * give * ctx.player->resourceValue(sell);
			const double bought = deals * get * ctx.player->resourceValue(buy);

			if(sold > bought)
				continue;

			ctx.cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, sell, buy, deals * give, hero);

			purse[sell] -= deals * give;
			purse[buy] += deals * get;
		}
	}

	ctx.player->computeWants();
}

void buyUniversitySkill(H3Context & ctx, const IMarket * market, const CGHeroInstance * hero)
{
	if(market == nullptr || hero == nullptr || !hero->canLearnSkill())
		return;

	if(!market->allowsTrade(EMarketMode::RESOURCE_SKILL))
		return;

	const int cost = ctx.cb->getSettings().getInteger(EGameSettings::MARKETS_UNIVERSITY_GOLD_COST);

	if(ctx.cb->getResourceAmount(GameResID::GOLD) < cost)
		return;

	SecondarySkill best = SecondarySkill::NONE;
	int bestValue = 0;

	for(const TradeItemBuy & offered : market->availableItemsIds(EMarketMode::RESOURCE_SKILL))
	{
		const SecondarySkill skill = offered.as<SecondarySkill>();

		if(hero->getSecSkillLevel(skill) > 0 || !hero->canLearnSkill(skill))
			continue;

		// SS 4.12's valuer, with the army term on - the same reading heroGotLevel uses.
		const int value = secondarySkillValue(ctx, hero, skill, true);

		if(value > bestValue)
		{
			bestValue = value;
			best = skill;
		}
	}

	if(best == SecondarySkill::NONE)
		return;

	// The purchase has to be worth its price, measured the same way every other object
	// handler measures a gold cost.
	if(bestValue < static_cast<int>(cost * ctx.player->goldValue()))
		return;

	ctx.cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_SKILL, GameResID(GameResID::GOLD), best, 1, hero);
}

}
