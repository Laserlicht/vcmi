/*
 * H3Player.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3Player.h"

#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/IOwnableObject.h"

#include <algorithm>

namespace H3AI
{

namespace
{
/// SS 4A.1 - the original walks 14 dwelling slots per town: seven tiers, each with a
/// base and an upgraded variant (g_gDwellingType is indexed faction*14 + slot).
constexpr int DWELLING_SLOTS = 14;

/// Resolve dwelling slot -> creature for a town, mirroring g_gDwellingType[faction*14 + slot].
CreatureID dwellingCreature(const CGTownInstance * town, int slot)
{
	const CTown * townType = town->getTown();
	int level = slot % 7;
	int upgrade = slot / 7;

	if(level >= static_cast<int>(townType->creatures.size()))
		return CreatureID::NONE;

	const auto & tier = townType->creatures[level];

	if(upgrade >= static_cast<int>(tier.size()))
		return CreatureID::NONE;

	return tier[upgrade];
}

int clampMarketCount(int markets)
{
	return std::clamp(markets, 1, 10);
}
}

void H3Player::init(CCallback * callback, PlayerColor color)
{
	cb = callback;
	player = color;
}

ResourceSet H3Player::dailyIncome() const
{
	ResourceSet income;

	const PlayerState * state = cb->getPlayerState(player, false);

	if(state == nullptr)
		return income;

	for(const auto * object : state->getOwnedObjects())
	{
		const IOwnableObject * ownable = object->asOwnable();

		if(ownable != nullptr)
			income += ownable->dailyIncome();
	}

	return income;
}

void H3Player::decayReservedFunds()
{
	// SS 4.10 step 1 - reserved_funds[i] -= playerData->income[i], clamped at 0.
	ResourceSet income = dailyIncome();

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
		reservedFunds[i] = std::max(0, reservedFunds[i] - income[GameResID(i)]);
}

void H3Player::beginTurn()
{
	// SS 4.16 - type_AI_player::begin_turn @ 0x4297C0 is pure orchestration:
	//   0x4B8AF0 - player bookkeeping
	//   0x429910
	//   0x4280E0 - the [0x63B670] / [0x63B67C] kingdom-goal pair
	//   0x429AD0
	//   compute_wants                       (SS 4A.1)
	//   AI_update_grail_guess               (SS 4.14)
	//
	// SS 4G.3 - type_AI_player::begin_turn @ 0x4297C0, in full:
	//   1. clear hero + 0x43 ("destination reachable") and + 0x11C ("done this turn") on
	//      every field hero AND every garrisoned hero;
	//   2. recompute_all_player_income (0x4B8AF0) - rebuilds playerData->income[7] from
	//      scratch for every player, walking every town on the map;
	//   3. the Eye-of-the-Magi value (0x429910): the sum of scouting_value(radius 10)
	//      over every Eye of the Magi (object 27) on the map, skipped on Easy or while
	//      townless.  It is the Hut of the Magi (object 37) that consumes it;
	//   4. the two kingdom-goal passes (0x4280E0) - see manageKingdom;
	//   5. compute_weekly_recruitment_cost (0x429AD0): for every town, the cost of a full
	//      week of recruitment across all 14 dwelling slots.  That total is what the
	//      supply / threat model treats as already-committed spending;
	//   6. compute_wants, then the Grail estimate.
	// Steps 1 and 4 are engine-side bookkeeping VCMI has no equivalent for.
	computeResourceSupplyAndThreats();
	computeWants();

	// SS 4.14 - AI_update_grail_guess (0x4BAE50 -> 0x52C9B0).  The report explicitly
	// leaves the obelisk-area reduction unexpanded ("map-geometry bookkeeping shared
	// with the UI"), so the cached guess is not reproduced.
	// TODO: Grail dig-site estimate.
}

void H3Player::computeWants()
{
	const PlayerState * state = cb->getPlayerState(player, false);

	if(state == nullptr)
		return;

	const ResourceSet income = dailyIncome();

	// --- 1. supply = what we will have after two turns of income -------------------
	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
		resourceSupply[i] = state->resources[GameResID(i)] + 2 * income[GameResID(i)];

	// --- 2. demand = the largest single cost among all buildings we could build ----
	resourceDemand.fill(0);

	const auto towns = cb->getTownsInfo(true);

	for(const CGTownInstance * town : towns)
	{
		for(int b = 0; b < 44; ++b)
		{
			BuildingID building(b);

			// SS 4A.1 / SS 4A.4a - town::get_buildable_mask (0x5C0F20) contains NO
			// resource test at all: it filters on prerequisites, already-built, the map's
			// per-town allow-list, whether a Grail has been dug here, and one campaign
			// flag.  Unaffordable buildings ARE included, deliberately - the value the AI
			// computes for one it cannot yet pay for is what drives reserve_funds and,
			// through it, the trade planner.  Filtering by affordability would stop the AI
			// ever saving up, so NO_RESOURCES is accepted below.
			EBuildingState status = cb->canBuildStructure(town, building);

			if(status != EBuildingState::ALLOWED && status != EBuildingState::NO_RESOURCES)
				continue;

			ResourceSet cost = town->getBuildingCost(building);

			for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
				resourceDemand[i] = std::max(resourceDemand[i], cost[GameResID(i)]);
		}
	}

	// --- 3. creature wish-list ----------------------------------------------------
	// SS 4A.1: 145 records {value, count}; every town contributes the weekly growth of
	// each of its 14 dwelling slots, then the three highest-value entries add their
	// recruitment cost into the demand.
	std::map<CreatureID, int> wishlistCount;

	for(const CGTownInstance * town : towns)
	{
		for(int slot = 0; slot < DWELLING_SLOTS; ++slot)
		{
			CreatureID creature = dwellingCreature(town, slot);

			if(!creature.hasValue())
				continue;

			wishlistCount[creature] += town->creatureGrowth(slot % 7);
		}
	}

	std::vector<std::pair<int64_t, CreatureID>> wishlist;
	wishlist.reserve(wishlistCount.size());

	for(const auto & entry : wishlistCount)
	{
		const CCreature * creature = entry.first.toCreature();

		if(creature == nullptr)
			continue;

		wishlist.emplace_back(static_cast<int64_t>(creature->getAIValue()) * entry.second, entry.first);
	}

	std::sort(wishlist.begin(), wishlist.end(), [](const auto & a, const auto & b) { return a.first > b.first; });

	for(size_t i = 0; i < wishlist.size() && i < CREATURE_WISHLIST_TOP; ++i)
	{
		const CCreature * creature = wishlist[i].second.toCreature();

		for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
			resourceDemand[r] += creature->getRecruitCost(GameResID(r));
	}

	// --- 4. resource values -------------------------------------------------------
	int markets = 0;

	for(const CGTownInstance * town : towns)
		if(town->hasBuilt(BuildingID::MARKETPLACE))
			++markets;

	const double base = TRADE_RATE[clampMarketCount(markets)];

	double nonGoldSum = 0.0;

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
	{
		const int dem = resourceDemand[i];
		const int sup = resourceSupply[i];
		double v;

		if(dem == 0)
		{
			v = base;
		}
		else if(dem <= sup)
		{
			v = (static_cast<double>(dem) + static_cast<double>(sup - dem) * base) / static_cast<double>(sup);
		}
		else
		{
			v = static_cast<double>(dem);

			if(sup > 1)
				v /= static_cast<double>(sup);

			v = std::max(v, 1.0 / base);
		}

		v *= BASE_RESOURCE_VALUE[i];
		resourceValues[i] = v;

		if(i < 6)
			nonGoldSum += v;
	}

	// SS 4A.1 - playerData->d[0x160] = ftol(sum of the six non-gold values) / 5
	avgResourceValue = static_cast<int>(nonGoldSum) / 5;

	// SS 2 / SS 4G.1 - playerData + 0x164 is produced by advManager::AI_prepare
	// (0x527960), once per AI turn, as the MEAN of AI_get_value_of_artifact over every
	// artifact whose traits byte + 0x1C is zero - i.e. the average artifact on this map,
	// priced against this player's best-placed hero.  It therefore tracks the heroes as
	// they grow.  Consumers: Pandora's Box, creature banks, Corpse, Sea Chest, Treasure
	// Chest.
	// It is AI_prepare that writes it, once per turn, not compute_wants; the turn driver
	// calls setAverageArtifactValue because only it holds an H3Context.
	// Left untouched here so a turn's value is not clobbered mid-turn.

}

void H3Player::computeResourceSupplyAndThreats()
{
	// SS 4A.5 - AI_compute_resource_supply_and_threats @ 0x429D50
	const PlayerState * state = cb->getPlayerState(player, false);

	if(state == nullptr)
		return;

	const ResourceSet income = dailyIncome();
	std::array<int, GameConstants::RESOURCE_QUANTITY> supply = {};

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
		supply[i] = 7 * income[GameResID(i)];

	const auto towns = cb->getTownsInfo(true);

	for(const CGTownInstance * town : towns)
	{
		for(int slot = 0; slot < DWELLING_SLOTS; ++slot)
		{
			CreatureID id = dwellingCreature(town, slot);
			const CCreature * creature = id.toCreature();

			if(creature == nullptr)
				continue;

			const int growth = town->creatureGrowth(slot % 7);

			for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
				supply[r] -= creature->getRecruitCost(GameResID(r)) * growth;
		}
	}

	const int difficulty = cb->getStartInfo()->difficulty;

	creatureThreatFlags.clear();

	for(const auto & creature : LIBRARY->creh->objects)
	{
		bool flagged = false;

		// "when the player cannot afford its recruitment cost"
		for(int r = 0; r < 6; ++r)
		{
			if(creature->getRecruitCost(GameResID(r)) > 0 && supply[r] <= 0)
				flagged = true;
		}

		if(difficulty != 0)
		{
			// SS 4A.5 / SS 4G.6 - traits + 0x04 is 0-BASED: the original's test is
			// "== 6", and what it excludes is level-7 creatures.  VCMI's getLevel() is
			// 1-based, so the faithful comparison is against 7.
			if(creature->getLevel() == 7)
				flagged = true;

			// SS 4A.5 / SS 4G.6 - the two quantities are DWELLING STOCK, not army
			// strength:
			//   bestRivalStock = max, over every player NOT on our team, of
			//                    sum over their towns, over all 14 dwelling slots, of
			//                    (creatures available * traits[creature].AI_value)
			//   ourStock       = the same sum for us
			// and the test is
			//   traits[ct].weeklyGrowth * traits[ct].AI_value + ourStock > bestRivalStock
			// Both are computed ONLY when the difficulty word is 0 (Easy) and no human
			// shares our team; on every other setting tests 2 and 3 never run at all -
			// which is why this whole block sits under `difficulty != 0` being false.
		}

		creatureThreatFlags[creature->getId()] = flagged;
	}
}

bool H3Player::creatureFlagged(const CreatureID & creature) const
{
	auto it = creatureThreatFlags.find(creature);

	return it != creatureThreatFlags.end() && it->second;
}

/// SS 4B.4a - playerData::AnyHeroHasArtifact @ 0x4BACB0
bool H3Player::anyHeroHasArtifact(const ArtifactID & artifact) const
{
	for(const CGHeroInstance * hero : cb->getHeroesInfo())
		if(hero != nullptr && hero->hasArt(artifact))
			return true;

	return false;
}

int H3Player::resourceCost(const ResourceSet & resources) const
{
	// SS 4A.2 - AI_resource_cost @ 0x526C70
	//   int v = 0;
	//   for (i = 0; i < 7; ++i) v = ftol(res[i] * pd->resource_value[i] + (double)v);
	int v = 0;

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
		v = static_cast<int>(resources[GameResID(i)] * resourceValues[i] + static_cast<double>(v));

	return v;
}

bool H3Player::planTrades(const ResourceSet & cost) const
{
	// SS 4A.3 - AI_plan_trades @ 0x42A2B0 keeps a hard floor: reserved_funds[i] is
	// clamped up to 20, and the tradeable surplus of resource i is additionally capped
	// by resource_supply[i] - resource_demand[i].
	const PlayerState * state = cb->getPlayerState(player, false);

	if(state == nullptr)
		return false;

	int64_t deficitValue = 0;
	int64_t surplusValue = 0;

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
	{
		const int have = state->resources[GameResID(i)];
		const int need = cost[GameResID(i)];

		if(need > have)
		{
			deficitValue += static_cast<int64_t>((need - have) * resourceValues[i]);
			continue;
		}

		const int reserve = std::max(reservedFunds[i], TRADE_RESERVE_FLOOR);
		int surplus = have - need - reserve;

		surplus = std::min(surplus, resourceSupply[i] - resourceDemand[i]);

		if(surplus > 0)
			surplusValue += static_cast<int64_t>(surplus * resourceValues[i]);
	}

	// SS 4G.7 - the trade machinery is four routines, and the last one commits:
	//   0x42A2B0  can the deficit be covered?
	//   0x42A580  choose which resource to sell, and at what rate
	//   0x42AB40  re-validate the plan once built
	//   0x42AC20  EXECUTE the trades
	// The same entry point both prices a purchase and pays for it, which is why
	// AI_build_one_building reaches it through reserve_funds.  An AI cannot commit a
	// market trade in VCMI without a callback, so this answers only the first question.
	return surplusValue >= deficitValue;
}

int H3Player::getTotalValue(int base, const ResourceSet & cost) const
{
	// SS 4A.3 - type_AI_player::get_total_value @ 0x42A150
	const PlayerState * state = cb->getPlayerState(player, false);

	if(state == nullptr)
		return -1;

	const ResourceSet income = dailyIncome();
	bool needTrade = false;

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
	{
		if(cost[GameResID(i)] > state->resources[GameResID(i)] && income[GameResID(i)] == 0)
			needTrade = true;
	}

	if(needTrade && !planTrades(cost))
		return -1;

	int c = 0;

	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
		c = static_cast<int>(cost[GameResID(i)] * resourceValues[i] + static_cast<double>(c));

	if(c == 0)
	{
		// SS 4G.8 - there is no division to guard in the original.  get_total_value first
		// asks whether any component of the cost exceeds what we hold AND has zero income;
		// if not, it takes a plain path that never divides by the cost.  A zero cost
		// vector makes that test false, so the trap cannot arise.  The guard is kept only
		// because this reimplementation reaches the division by a shorter route.
		return base > 0 ? std::numeric_limits<int>::max() : 0;
	}

	return static_cast<int>(static_cast<int64_t>(base) * TOTAL_VALUE_SCALE / c);
}

void H3Player::reserveFunds(const ResourceSet & cost, int multiplier)
{
	// SS 4A.4 / SS 4B.4 / SS 4G.7 - type_AI_player::reserve_funds @ 0x42A470 is the trade
	// driver: feasibility (0x42A2B0), then plan (0x42A580), then re-validate (0x42AB40),
	// then commit (0x42AC20).  The `commit` flag is the fourth argument, threaded through
	// from AI_build_one_building; the two call shapes are reserve_funds(cost, true) when
	// building and reserve_funds(cost, offer.available) when recruiting.  Only the
	// accumulation into reserved_funds is reproducible here - see planTrades above.
	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
		reservedFunds[i] += cost[GameResID(i)] * multiplier;
}

float H3Player::getAttackBonus(PlayerColor targetOwner) const
{
	// SS 4.11 - type_AI_player::get_attack_bonus @ 0x428710:
	//   0.0 for an unowned target, else the human or computer bonus.
	//
	// SS 4G.1: those two globals are NOT the 0.5/0.5 pair sitting in the image - they
	// are recomputed from the difficulty by advManager::AI_prepare (0x527960) at the
	// start of every AI turn.  Reading them as constants made the AI behave as if every
	// game were on difficulty 1.
	if(!targetOwner.isValidPlayer())
		return 0.0f;

	const int difficulty = static_cast<int>(cb->getStartInfo()->difficulty);

	const PlayerState * state = cb->getPlayerState(targetOwner, false);

	if(state != nullptr && state->human)
		return attackHumanBonus(difficulty);

	return attackComputerBonus(difficulty);
}

}
