/*
 * PhilEconomy.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilEconomy.h"

#include "PhilCombatSim.h"
#include "PhilValuation.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

#include <algorithm>
#include <set>

namespace PhilAI
{

/// H3: calculate_demand ai_player.cpp:258 - the marketplace-count trading-efficiency table,
/// indexed by the clamped count of towns with (or able to build) a Marketplace.
/// PHILAI-GAP: the ten-entry table lives in the binary's data section. The shape below is a
/// reconstruction of the documented behaviour - efficiency improves with market count - not a
/// recovered set of figures.
static const std::array<double, Const::MARKET_COUNT_MAX + 1> TRADE_EFFICIENCY = {
	1.00, 1.00, 0.90, 0.82, 0.76, 0.71, 0.67, 0.64, 0.61, 0.59, 0.57 };

PhilEconomy::PhilEconomy(std::shared_ptr<CCallback> callback)
	: cb(std::move(callback))
{
	resourceValue.fill(Const::DEFAULT_DEMAND);
}

double PhilEconomy::getAttackBonus() const
{
	// H3: type_AI_player::get_attack_bonus ai_player.cpp:245
	return Const::ATTACK_BONUS_BY_DIFFICULTY[std::clamp(difficulty, 0, 4)];
}

int PhilEconomy::getResourceValue(GameResID res) const
{
	// H3: type_AI_player::get_resource_value ai_player.cpp:230
	const int index = res.getNum();
	if(index < 0 || index >= GameResID::COUNT)
		return Const::DEFAULT_DEMAND;
	return std::max(1, resourceValue[index]);
}

int PhilEconomy::resourceCost(const ResourceSet & cost) const
{
	// H3: AI_resource_cost philai.cpp:1319 - a nominal cost priced through the player's own
	// subjective values, so the same bill reads cheap or dear from turn to turn.
	int total = 0;
	for(int i = 0; i < GameResID::COUNT; ++i)
		total += cost[i] * getResourceValue(GameResID(i));
	return total;
}

void PhilEconomy::calculateDemand()
{
	// H3: type_AI_player::calculate_demand ai_player.cpp:258
	resourceSupply.fill(0);
	resourceDemand.fill(0);

	const auto towns = cb->getTownsInfo(true);

	// Supply: this turn's production income, with a second producing source of the same
	// resource counting double rather than merely additively.
	std::array<int, GameResID::COUNT> sources = {};
	for(const auto * town : towns)
	{
		const ResourceSet income = town->dailyIncome();
		for(int i = 0; i < GameResID::COUNT; ++i)
		{
			if(income[i] <= 0)
				continue;

			const int weight = sources[i] > 0 ? Const::SECOND_RESOURCE_SOURCE_WEIGHT : 1;
			resourceSupply[i] += static_cast<int>(income[i]) * weight;
			++sources[i];
		}
	}

	// Demand: for each resource, the single most expensive unbuilt legal building requirement
	// found in any owned town - town by town, not a sum across the empire.
	marketCount = 0;
	for(const auto * town : towns)
	{
		if(town->hasBuilt(BuildingID::MARKETPLACE))
			++marketCount;

		for(const auto & pair : town->getTown()->buildings)
		{
			const BuildingID id = pair.first;
			if(town->hasBuilt(id))
				continue;

			const ResourceSet cost = town->getBuildingCost(id);
			for(int i = 0; i < GameResID::COUNT; ++i)
				resourceDemand[i] = std::max(resourceDemand[i], static_cast<int>(cost[i]));
		}
	}

	// The three most valuable creature types recruitable this week fold their recruitment cost
	// straight into the demand model.
	std::vector<std::pair<int, const CCreature *>> growing;
	for(const auto * town : towns)
	{
		for(int level = 0; level < GameConstants::CREATURES_PER_TOWN; ++level)
		{
			const int growth = town->creatureGrowth(level);
			if(growth <= 0)
				continue;

			const auto & options = town->creatures.at(level).second;
			if(options.empty())
				continue;

			const CCreature * c = options.back().toCreature();
			if(c)
				growing.emplace_back(c->getAIValue() * growth, c);
		}
	}

	std::sort(growing.begin(), growing.end(),
		[](const auto & a, const auto & b) { return a.first > b.first; });

	for(size_t i = 0; i < growing.size() && i < Const::DEMAND_CREATURE_TYPES; ++i)
	{
		const ResourceSet cost = growing[i].second->getFullRecruitCost();
		for(int r = 0; r < GameResID::COUNT; ++r)
			resourceDemand[r] += static_cast<int>(cost[r]);
	}

	// The marketplace count is clamped to 1..10 before it indexes the efficiency table.
	marketCount = std::clamp(marketCount, Const::MARKET_COUNT_MIN, Const::MARKET_COUNT_MAX);
	const double efficiency = TRADE_EFFICIENCY[marketCount];

	// Final pricing: a resource the AI is short of, relative to what it wants, is subjectively
	// more expensive to it. A resource with no recorded demand falls back to a flat baseline.
	int runningTotal = 0;
	for(int i = 0; i < GameResID::COUNT; ++i)
	{
		const int demand = resourceDemand[i] > 0 ? resourceDemand[i] : Const::DEFAULT_DEMAND;
		const int supply = std::max(1, resourceSupply[i]);
		resourceValue[i] = std::max(1, static_cast<int>(demand * efficiency / supply));

		if(i != GameResID::GOLD)
			runningTotal += resourceValue[i];
	}

	// The six non-gold resources are divided by five, not six, for the average figure.
	averageResourceValue = runningTotal / Const::AVERAGE_RESOURCE_DIVISOR;
}

void PhilEconomy::calculateReserve()
{
	// H3: type_AI_player::calculate_reserve ai_player.cpp:752
	// Each turn the AI reserves the cost of the two most valuable currently-growing creature
	// types per town. It is planning a week ahead for its own best recruits even while
	// spending greedily on buildings in the meantime.
	reservedFunds.fill(0);

	for(const auto * town : cb->getTownsInfo(true))
	{
		std::vector<std::pair<int, const CCreature *>> candidates;

		for(int level = 0; level < GameConstants::CREATURES_PER_TOWN; ++level)
		{
			const int growth = town->creatureGrowth(level);
			if(growth <= 0)
				continue;

			const auto & options = town->creatures.at(level).second;
			if(options.empty())
				continue;

			const CCreature * c = options.back().toCreature();
			if(c)
				candidates.emplace_back(c->getAIValue() * growth, c);
		}

		std::sort(candidates.begin(), candidates.end(),
			[](const auto & a, const auto & b) { return a.first > b.first; });

		for(size_t i = 0; i < candidates.size() && i < Const::RESERVE_CREATURE_TYPES; ++i)
		{
			const int growth = std::max(1, town->creatureGrowth(static_cast<int>(i)));
			const ResourceSet cost = candidates[i].second->getFullRecruitCost();
			for(int r = 0; r < GameResID::COUNT; ++r)
				reservedFunds[r] += static_cast<int>(cost[r]) * growth;
		}
	}
}

void PhilEconomy::drawDownReserve(const ResourceSet & spent)
{
	// H3: type_AI_player::end_turn ai_player.cpp:414 - the reserve decays across the turn as
	// purchases consume it, and never goes negative.
	for(int i = 0; i < GameResID::COUNT; ++i)
		reservedFunds[i] = std::max(0, reservedFunds[i] - static_cast<int>(spent[i]));
}

// ---------------------------------------------------------------------------
// II.2 - building priorities
// ---------------------------------------------------------------------------

int PhilEconomy::valueOfHall(const CGTownInstance * town, BuildingID building) const
{
	// H3: value_of_hall ai_player.cpp:1109
	// Completely uniform across all nine factions - there is no per-faction hall pricing
	// anywhere in the original, confirmed by control flow rather than inferred from a sample.
	switch(building.toEnum())
	{
		case BuildingID::VILLAGE_HALL: return Const::HALL_VALUE_VILLAGE * averageResourceValue;
		case BuildingID::TOWN_HALL:    return Const::HALL_VALUE_TOWN * averageResourceValue;
		case BuildingID::CITY_HALL:    return Const::HALL_VALUE_CITY * averageResourceValue;
		case BuildingID::CAPITOL:      return Const::HALL_VALUE_CAPITOL * averageResourceValue;
		default: return 0;
	}
}

int PhilEconomy::valueOfDwelling(const CGTownInstance * town, BuildingID building) const
{
	// H3: value_of_dwelling ai_player.cpp:834
	const int level = building.getNum() - BuildingID::DWELL_LVL_1;
	if(level < 0 || level >= GameConstants::CREATURES_PER_TOWN)
		return 0;

	const auto & options = town->creatures.at(level).second;
	if(options.empty())
		return 0;

	const CCreature * c = options.front().toCreature();
	if(!c)
		return 0;

	int growth = std::max(1, town->creatureGrowth(level));

	// Above difficulty 4 the projected growth is doubled outright, and a castle-level bonus
	// added - the AI's own forward-looking model plans more aggressively, not just its stock.
	if(difficulty > Const::DIFFICULTY_GROWTH_DOUBLING_THRESHOLD)
	{
		growth *= 2;
		if(town->hasBuilt(BuildingID::CASTLE))
			growth += 1;
	}

	return c->getAIValue() * growth;
}

int PhilEconomy::valueOfSilo(const CGTownInstance * town) const
{
	// H3: value_of_silo ai_player.cpp:1045 - priced income times seven.
	const ResourceSet income = town->dailyIncome();
	return resourceCost(income) * Const::SILO_INCOME_MULTIPLIER;
}

int PhilEconomy::valueOfBuilding(const CGTownInstance * town, BuildingID building, bool townThreatened) const
{
	// H3: value_of_building ai_player.cpp:1147
	//
	// The instant a town is flagged as threatened, its Resource Silo, Horde buildings,
	// Dwellings and Dwelling upgrades are suppressed entirely - not scaled down, switched off.
	if(townThreatened)
	{
		const int id = building.getNum();
		const bool isDwelling = id >= BuildingID::DWELL_LVL_1 && id <= BuildingID::DWELL_LVL_7_UP5;
		const bool isHorde = building == BuildingID::HORDE_1 || building == BuildingID::HORDE_1_UPGR
			|| building == BuildingID::HORDE_2 || building == BuildingID::HORDE_2_UPGR;

		if(isDwelling || isHorde || building == BuildingID::RESOURCE_SILO)
			return Const::VALUE_BLOCKED;
	}

	switch(building.toEnum())
	{
		case BuildingID::VILLAGE_HALL:
		case BuildingID::TOWN_HALL:
		case BuildingID::CITY_HALL:
		case BuildingID::CAPITOL:
			return valueOfHall(town, building);

		case BuildingID::RESOURCE_SILO:
			return valueOfSilo(town);

		case BuildingID::SPECIAL_1:
		case BuildingID::SPECIAL_2:
		case BuildingID::SPECIAL_3:
		case BuildingID::SPECIAL_4:
		{
			// Five of the nine factions get bespoke logic here; the other four fall straight
			// through to zero and are priced only by the generic ROI path.
			const FactionID faction = town->getFactionID();
			const int factionIndex = faction.getNum();

			switch(factionIndex)
			{
				case 1: // Rampart - Castle Gate slot, then Artifact Merchants
					// PHILAI-GAP: the Castle Gate branch is gated on an unconfirmed game-state
					// field being exactly 7; only the Artifact Merchants branch is reproduced.
					return Const::SPECIAL_RAMPART_ARTIFACT_MERCHANT_AVG_MULT * averageResourceValue;

				case 2: // Tower - flat 100
					return Const::SPECIAL_TOWER_FLAT;

				case 4: // Necropolis - 1,000 per Necromancer-class hero owned
				{
					int necromancers = 0;
					for(const auto * h : cb->getHeroesInfo())
						if(h->getHeroType()->heroClass && h->getHeroType()->heroClass->faction == faction)
							++necromancers;
					return Const::SPECIAL_NECRO_PER_NECROMANCER_HERO * necromancers;
				}

				case 6: // Stronghold - flat 5,000, but only while threatened with a garrison hero
					if(townThreatened && town->getGarrisonHero())
						return Const::SPECIAL_STRONGHOLD_ARTIFACT_MERCHANT;
					return 0;

				case 7: // Fortress - garrison army value / 20, only while threatened
					if(townThreatened)
						return getArmyAIValue(town) / Const::SPECIAL_FORTRESS_GARRISON_DIVISOR;
					return 0;

				// Castle, Inferno, Dungeon and Conflux get no AI opinion at all on their unique
				// building beyond the generic ROI path.
				default:
					return 0;
			}
		}

		default:
			break;
	}

	const int id = building.getNum();
	if(id >= BuildingID::DWELL_LVL_1 && id <= BuildingID::DWELL_LVL_7)
		return valueOfDwelling(town, building);

	// H3: value_of_dwelling_upgrade ai_player.cpp:865 - the same difficulty gate applies.
	if(id >= BuildingID::DWELL_LVL_1_UP && id <= BuildingID::DWELL_LVL_7_UP5)
		return valueOfDwelling(town, BuildingID(BuildingID::DWELL_LVL_1 + (id - BuildingID::DWELL_LVL_1_UP) % 7));

	// PHILAI-GAP: the remaining generic building types (Fort tiers, Mage Guild tiers, Tavern,
	// Blacksmith, Shipyard, Marketplace) are priced only through the ROI path below in the
	// original as well, so no bespoke value is missing here.
	return 0;
}

int PhilEconomy::getTotalValue(const CGTownInstance * town, BuildingID building, bool townThreatened) const
{
	// H3: get_full_cost ai_player.cpp:1313 then type_AI_player::get_total_value ai_player.cpp:1337
	const int value = valueOfBuilding(town, building, townThreatened);
	if(value <= 0)
		return Const::VALUE_BLOCKED;

	const ResourceSet cost = town->getBuildingCost(building);
	const int pricedCost = resourceCost(cost);
	if(pricedCost <= 0)
		return value * Const::ROI_NUMERATOR;

	// A building is blocked outright if its cost exceeds stock, income is zero, and no trade
	// could cover the gap.
	const ResourceSet available = cb->getResourceAmount();
	bool unreachable = false;
	for(int i = 0; i < GameResID::COUNT; ++i)
		if(cost[i] > available[i] && resourceSupply[i] == 0 && marketCount <= 1)
			unreachable = true;

	if(unreachable)
		return Const::VALUE_BLOCKED;

	return value * Const::ROI_NUMERATOR / pricedCost;
}

void PhilEconomy::purchaseBuildings(bool anyTownThreatened)
{
	// H3: type_AI_player::purchase_buildings ai_player.cpp:1838
	// Greedy with no lookahead: score everything, buy the single best, re-derive from the
	// post-purchase state, repeat until nothing scores positively.
	//
	// The original could re-derive from live state because its own purchase took effect
	// immediately; VCMI only sends a request, so the two ledgers below stand in for that. A
	// town may raise only one building per day, and its "built today" flag is not visible here
	// until the request comes back, so a town is retired from the search once it has bought.
	std::set<ObjectInstanceID> builtInTown;
	ResourceSet budget = cb->getResourceAmount();

	while(true)
	{
		const CGTownInstance * bestTown = nullptr;
		BuildingID bestBuilding = BuildingID::NONE;
		int bestScore = 0;

		for(const auto * town : cb->getTownsInfo(true))
		{
			for(const auto & pair : town->getTown()->buildings)
			{
				const BuildingID id = pair.first;
				if(town->hasBuilt(id))
					continue;
				if(builtInTown.count(town->id))
					continue;
				if(cb->canBuildStructure(town, id) != EBuildingState::ALLOWED)
					continue;
				if(!budget.canAfford(town->getBuildingCost(id)))
					continue;

				const int score = getTotalValue(town, id, anyTownThreatened);
				if(score > bestScore)
				{
					bestScore = score;
					bestTown = town;
					bestBuilding = id;
				}
			}
		}

		if(!bestTown || bestScore <= 0)
			return;

		const ResourceSet cost = bestTown->getBuildingCost(bestBuilding);
		builtInTown.insert(bestTown->id);

		if(!cb->buildBuilding(bestTown, bestBuilding))
			continue;

		budget -= cost;
		drawDownReserve(cost);
	}
}

// ---------------------------------------------------------------------------
// II.3 - recruiting the army
// ---------------------------------------------------------------------------

int PhilEconomy::maxBuyableCreatures(const CCreature * creature, const ResourceSet & funds, int limit) const
{
	// H3: MaxBuyableCreatures ai_player.cpp:1808
	if(!creature)
		return 0;

	int result = limit;
	const ResourceSet cost = creature->getFullRecruitCost();
	for(int i = 0; i < GameResID::COUNT; ++i)
	{
		if(cost[i] <= 0)
			continue;
		result = std::min<int>(result, static_cast<int>(funds[i] / cost[i]));
	}

	return std::max(0, result);
}

bool PhilEconomy::doBestPurchase(const CGDwelling * dwelling, const CArmedInstance * destination,
	bool subtractCost, ResourceSet & budget, std::set<int> & alreadyBought)
{
	// H3: type_AI_creature_purchaser::do_best_purchase ai_player.cpp:2535
	//
	// For every affordable creature type, the net increase in total army value from buying one
	// more stack, discounted by the resources actually spent - and that discount is capped at
	// three quarters of the value gained (value * 3 >> 2, confirmed at the bit level).
	// Emergency garrison buying turns the discount off entirely and just buys the strongest
	// army it can afford.
	if(!dwelling || !destination)
		return false;

	ResourceSet funds = budget;
	for(int i = 0; i < GameResID::COUNT; ++i)
		funds[i] = std::max<TResource>(0, funds[i] - reservedFunds[i]);

	int bestScore = 0;
	int bestAmount = 0;
	CreatureID bestCreature = CreatureID::NONE;
	int bestLevel = -1;
	ResourceSet bestSpend;

	for(size_t level = 0; level < dwelling->creatures.size(); ++level)
	{
		// A dwelling level already recruited from this pass has no stock left to re-offer,
		// even though the request has not been applied yet.
		if(alreadyBought.count(static_cast<int>(level)))
			continue;

		const int available = dwelling->creatures[level].first;
		if(available <= 0)
			continue;

		for(const auto & creatureID : dwelling->creatures[level].second)
		{
			const CCreature * c = creatureID.toCreature();
			if(!c)
				continue;

			const int amount = maxBuyableCreatures(c, funds, available);
			if(amount <= 0)
				continue;

			// The army has to have somewhere to put them.
			if(!destination->getSlotFor(c).validSlot())
				continue;

			const int gain = amount * c->getAIValue();

			ResourceSet spend = c->getFullRecruitCost();
			for(int i = 0; i < GameResID::COUNT; ++i)
				spend[i] *= amount;

			int score = gain;
			if(subtractCost)
			{
				const int discountCap = gain * Const::PURCHASE_DISCOUNT_NUM / Const::PURCHASE_DISCOUNT_DEN;
				score = gain - std::min(resourceCost(spend), discountCap);
			}

			if(score > bestScore)
			{
				bestScore = score;
				bestAmount = amount;
				bestCreature = creatureID;
				bestLevel = static_cast<int>(level);
				bestSpend = spend;
			}
		}
	}

	if(bestScore <= 0 || bestAmount <= 0)
		return false;

	cb->recruitCreatures(dwelling, destination, bestCreature, bestAmount, bestLevel);
	alreadyBought.insert(bestLevel);
	budget -= bestSpend;
	return true;
}

void PhilEconomy::buyCreatures(const CGTownInstance * town, const CArmedInstance * destination, bool subtractCost)
{
	// H3: type_AI_player::buy_creatures ai_player.cpp:1850
	// The outer loop re-evaluates from scratch each pass, since buying troops changes both the
	// army's value and its available slots. There is no shopping list - only a running ledger
	// standing in for the immediate state change the original got for free.
	if(!town || !destination)
		return;

	ResourceSet budget = cb->getResourceAmount();
	std::set<int> alreadyBought;

	while(doBestPurchase(town, destination, subtractCost, budget, alreadyBought))
	{
		// Legal options and the post-purchase army value are re-derived every iteration, which
		// is what keeps this correct as slots fill and funds shrink.
	}
}

// ---------------------------------------------------------------------------
// II.1 - trade
// ---------------------------------------------------------------------------

bool PhilEconomy::buildMarkets()
{
	// H3: type_AI_player::build_markets ai_player.cpp:1587
	// Look for a town that could legally afford a Marketplace right now and build it on the
	// spot purely to unlock the trade, before retrying the whole check.
	for(const auto * town : cb->getTownsInfo(true))
	{
		if(town->hasBuilt(BuildingID::MARKETPLACE))
			continue;
		if(cb->canBuildStructure(town, BuildingID::MARKETPLACE) != EBuildingState::ALLOWED)
			continue;

		if(cb->buildBuilding(town, BuildingID::MARKETPLACE))
			return true;
	}
	return false;
}

void PhilEconomy::tradeResources(const ResourceSet & pendingPurchase)
{
	// H3: type_AI_player::trade_resources ai_player.cpp:1446, chaining check_trade_supply
	// (ai_player.cpp:1383), can_trade_resources (ai_player.cpp:1474), build_markets
	// (ai_player.cpp:1587) and do_resource_trade (ai_player.cpp:1620).
	const ResourceSet available = cb->getResourceAmount();

	// Walk all seven resources against what the pending purchase actually needs; queue a
	// deficit for anything that would go short.
	std::vector<int> deficits;
	std::vector<int> surpluses;

	for(int i = 0; i < GameResID::COUNT; ++i)
	{
		const int need = static_cast<int>(pendingPurchase[i]) - static_cast<int>(available[i]);
		if(need > 0)
			deficits.push_back(i);
		else
		{
			// A surplus never offers more than the reserved-funds floor allows, and never more
			// than calculate_demand says is genuinely spare.
			const int spare = static_cast<int>(available[i]) - reservedFunds[i] - resourceDemand[i];
			if(spare > 0)
				surpluses.push_back(i);
		}
	}

	if(deficits.empty() || surpluses.empty())
		return;

	// If no owned town can trade at all yet, build a Marketplace on the spot and retry.
	const CGTownInstance * market = nullptr;
	for(const auto * town : cb->getTownsInfo(true))
		if(town->hasBuilt(BuildingID::MARKETPLACE))
			market = town;

	if(!market)
	{
		if(!buildMarkets())
			return;

		for(const auto * town : cb->getTownsInfo(true))
			if(town->hasBuilt(BuildingID::MARKETPLACE))
				market = town;

		if(!market)
			return;
	}

	// Choose the cheapest combination of conversions that closes the gap.
	for(int want : deficits)
	{
		const int need = static_cast<int>(pendingPurchase[want]) - static_cast<int>(available[want]);
		if(need <= 0)
			continue;

		int bestSource = -1;
		int bestPrice = std::numeric_limits<int>::max();
		for(int have : surpluses)
		{
			const int price = getResourceValue(GameResID(have));
			if(price < bestPrice)
			{
				bestPrice = price;
				bestSource = have;
			}
		}

		if(bestSource < 0)
			continue;

		const int spare = static_cast<int>(available[bestSource]) - reservedFunds[bestSource] - resourceDemand[bestSource];
		if(spare <= 0)
			continue;

		cb->trade(market->id, EMarketMode::RESOURCE_RESOURCE, GameResID(bestSource), GameResID(want), spare, nullptr);
	}
}

// ---------------------------------------------------------------------------
// II.3 - hero hiring
// ---------------------------------------------------------------------------

int PhilEconomy::valueOfHiring(const CGTownInstance * town, const CGHeroInstance * candidate) const
{
	// H3: value_of_hiring ai_player.cpp:4320
	// The original merges the candidate's starting army into the town garrison, tops it up
	// with the leftover purchase budget, relocates the candidate to the town, and then runs
	// the real pathfinding search to measure how much not-already-claimed map value this new
	// hero could actually go and get - discounting for overlap with existing heroes.
	if(!town || !candidate)
		return 0;

	int value = getArmyAIValue(candidate);

	// PHILAI-GAP: the "not already claimed by one of my heroes" reach simulation needs the
	// destination set from find_all_destinations, which is owned by the map-scoring layer.
	// The candidate's own army and primary skills are priced here instead.
	for(auto skill : { PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE })
		value += candidate->getPrimSkillLevel(skill) * Const::MIN_PRIMARY_POINT_VALUE;

	return value;
}

void PhilEconomy::hireHeroes()
{
	// H3: type_AI_player::hire_heroes ai_player.cpp:4670
	const int ownHeroes = cb->howManyHeroes(true);

	// A hard cap of eight before hiring is even considered, plus a difficulty-indexed
	// per-player cap.
	if(ownHeroes >= Const::MAX_HEROES)
		return;
	if(ownHeroes >= Const::HERO_LIMIT_BY_DIFFICULTY[std::clamp(difficulty, 0, 4)])
		return;

	// PHILAI-GAP: the empire-wide cap summed across every non-human, non-dead AI player needs
	// visibility into other players' hero counts, which VCMI's player callback does not grant.
	// Its documented exception - skipped entirely when this player holds zero heroes - would
	// apply here.

	for(const auto * town : cb->getTownsInfo(true))
	{
		if(!town->hasBuilt(BuildingID::TAVERN))
			continue;
		if(town->getVisitingHero())
			continue;

		const auto available = cb->getAvailableHeroes(town);
		const CGHeroInstance * best = nullptr;
		int bestScore = 0;

		for(const auto * candidate : available)
		{
			if(!candidate)
				continue;

			const int score = valueOfHiring(town, candidate);
			if(score > bestScore)
			{
				bestScore = score;
				best = candidate;
			}
		}

		if(best)
		{
			cb->recruitHero(town, best);
			return;
		}
	}
}

void PhilEconomy::makeGifts()
{
	// H3: type_AI_player::make_gift ai_player.cpp:504
	// PHILAI-GAP: VCMI has no player-to-player resource transfer callback exposed to an AI, so
	// the end-of-turn allied gifting economy (and its mirror, requesting help from a human
	// ally when a surplus comes out negative) cannot be reproduced. The gating it would use -
	// supply minus demand, capped against the giver's production and the recipient's stock,
	// reduced by the reserve, and zeroed below 5 units / 1,000 gold - is documented here so
	// the omission is visible rather than silent.
}

} // namespace PhilAI
