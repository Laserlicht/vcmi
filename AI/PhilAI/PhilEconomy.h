/*
 * PhilEconomy.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PhilConstants.h"

#include "../../lib/ResourceSet.h"
#include "../../lib/constants/EntityIdentifiers.h"

#include <array>
#include <memory>
#include <set>
#include <vector>

class CArmedInstance;
class CCallback;
class CCreature;
class CGDwelling;
class CGHeroInstance;
class CGTownInstance;

namespace PhilAI
{

/// H3: type_AI_player (ai_player.cpp) - the per-player economic scratch record.
///
/// One of these exists per player slot for the whole process, human slots included, and its
/// fields are overwritten each turn rather than reconstructed. The turn-scoped feel of the
/// economy comes from that overwriting, not from anything being built fresh.
class PhilEconomy
{
	std::shared_ptr<CCallback> cb;
	int difficulty = 2;

public:
	/// The genuine subjective price the player puts on one unit of each resource. Everything
	/// - build costs, purchases, gifts, bribes - is converted through this before comparison.
	std::array<int, GameResID::COUNT> resourceValue = {};
	std::array<int, GameResID::COUNT> resourceSupply = {};
	std::array<int, GameResID::COUNT> resourceDemand = {};
	std::array<int, GameResID::COUNT> reservedFunds = {};
	int averageResourceValue = 0;
	int averageArtifactValue = 0;
	int marketCount = 1;

	/// Value of one more point of Power / Knowledge to this player's heroes, refreshed once
	/// per turn by AI_set_hero_bonuses (philai.cpp:1725).
	int valueOfPower = Const::MIN_PRIMARY_POINT_VALUE;
	int valueOfKnowledge = Const::MIN_PRIMARY_POINT_VALUE;

	explicit PhilEconomy(std::shared_ptr<CCallback> callback);

	void setDifficulty(int value) { difficulty = value; }
	int getDifficulty() const { return difficulty; }

	/// H3: type_AI_player::get_attack_bonus ai_player.cpp:245 - a one-line accessor returning
	/// the computer figure for AI-controlled players and the human figure otherwise.
	double getAttackBonus() const;

	/// H3: AI_resource_cost philai.cpp:1319 - prices a cost through the subjective values.
	int resourceCost(const ResourceSet & cost) const;

	/// H3: type_AI_player::get_resource_value ai_player.cpp:230
	int getResourceValue(GameResID res) const;

	/// H3: type_AI_player::calculate_demand ai_player.cpp:258
	/// Supply is this turn's production, weighted so a second producing source of the same
	/// resource counts double. Demand is not a sum: for each resource it is the single most
	/// expensive unbuilt legal building requirement found in any owned town, plus the
	/// recruitment cost of the three most valuable creature types growing this week.
	void calculateDemand();

	/// H3: type_AI_player::calculate_reserve ai_player.cpp:752
	/// Holds back gold for the two most valuable currently-growing creature types per town.
	/// The reserve is spent down across the turn, never replenished mid-turn.
	void calculateReserve();

	/// H3: type_AI_player::end_turn ai_player.cpp:414 - draws the reserve down by what was
	/// actually spent, floored at zero per resource.
	void drawDownReserve(const ResourceSet & spent);

	// -----------------------------------------------------------------------
	// II.2 - building priorities
	// -----------------------------------------------------------------------

	/// H3: value_of_building ai_player.cpp:1147
	int valueOfBuilding(const CGTownInstance * town, BuildingID building, bool townThreatened) const;

	/// H3: value_of_hall ai_player.cpp:1109 - uniform across all nine factions.
	int valueOfHall(const CGTownInstance * town, BuildingID building) const;

	/// H3: value_of_dwelling ai_player.cpp:834 - growth is doubled above difficulty 4.
	int valueOfDwelling(const CGTownInstance * town, BuildingID building) const;

	/// H3: value_of_silo ai_player.cpp:1045 - priced income times seven.
	int valueOfSilo(const CGTownInstance * town) const;

	/// H3: get_full_cost ai_player.cpp:1313 then type_AI_player::get_total_value
	/// ai_player.cpp:1337 - the ROI figure, value * 1000 / priced cost.
	int getTotalValue(const CGTownInstance * town, BuildingID building, bool townThreatened) const;

	/// H3: type_AI_player::purchase_buildings ai_player.cpp:1838
	/// Greedy: score every legal unbuilt building across the whole empire, buy the single best,
	/// repeat until nothing scores positively. No lookahead of any kind.
	void purchaseBuildings(bool anyTownThreatened);

	// -----------------------------------------------------------------------
	// II.3 - recruiting the army
	// -----------------------------------------------------------------------

	/// H3: MaxBuyableCreatures ai_player.cpp:1808 - the minimum, across every resource the
	/// type costs, of funds available divided by that resource's cost, capped by a limit.
	int maxBuyableCreatures(const CCreature * creature, const ResourceSet & funds, int limit) const;

	/// H3: type_AI_creature_purchaser::do_best_purchase ai_player.cpp:2535
	/// The general-purpose "which single move raises total army value the most" primitive,
	/// reused unchanged for creature purchasing, garrison swaps and hero-to-hero exchanges.
	/// The gain is discounted by the resource cost, capped at three quarters of the gain -
	/// unless subtractCost is off, which is what emergency garrison buying does.
	/// `budget` is the caller's running ledger. VCMI applies a recruit request asynchronously,
	/// so re-reading the treasury each pass would re-pick the same purchase forever; the
	/// original could re-derive from live state because its own purchase was immediate.
	bool doBestPurchase(const CGDwelling * dwelling, const CArmedInstance * destination,
		bool subtractCost, ResourceSet & budget, std::set<int> & alreadyBought);

	/// H3: type_AI_player::buy_creatures ai_player.cpp:1850
	/// Construction and recruitment are one combined decision within a single town visit: an
	/// unbuilt dwelling is funded first if it would unlock a better purchase than anything on
	/// offer right now.
	void buyCreatures(const CGTownInstance * town, const CArmedInstance * destination, bool subtractCost);

	// -----------------------------------------------------------------------
	// II.1 - trade
	// -----------------------------------------------------------------------

	/// H3: type_AI_player::trade_resources ai_player.cpp:1446 - chains the shortfall check,
	/// the affordability re-derivation, the build-a-market escape hatch, and the conversion.
	void tradeResources(const ResourceSet & pendingPurchase);

	/// H3: type_AI_player::build_markets ai_player.cpp:1587 - builds a Marketplace mid-turn
	/// purely to unlock a trade the AI needs right now.
	bool buildMarkets();

	// -----------------------------------------------------------------------
	// II.3 - hero hiring
	// -----------------------------------------------------------------------

	/// H3: type_AI_player::hire_heroes ai_player.cpp:4670
	void hireHeroes();

	/// H3: value_of_hiring ai_player.cpp:4320 - simulates the hire rather than scoring the
	/// candidate's class or starting skills.
	int valueOfHiring(const CGTownInstance * town, const CGHeroInstance * candidate) const;

	// -----------------------------------------------------------------------
	// II.13 - allied gifting
	// -----------------------------------------------------------------------

	/// H3: type_AI_player::make_gift ai_player.cpp:504 - runs against every living teammate at
	/// end of turn, in two passes, and asks for help wherever a surplus comes out negative.
	void makeGifts();
};

} // namespace PhilAI
