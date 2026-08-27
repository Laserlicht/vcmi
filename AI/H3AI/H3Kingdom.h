/*
 * H3Kingdom.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Context.h"

#include <set>

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
class CGTownInstance;
class IMarket;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4.10 - type_AI_player::manage_kingdom @ 0x428DD0.
void manageKingdom(H3Context & ctx);

/// SS 4A.4 - type_AI_player::AI_build_one_building @ 0x42AE00.
/// @param attempted  buildings already committed to this turn.  The original relies on
///        town + "built something this turn" alone; this reimplementation also refuses
///        to pick the same building twice so a rejected request cannot spin the loop.
bool buildOneBuilding(H3Context & ctx, std::set<std::pair<ObjectInstanceID, BuildingID>> & attempted);

/// SS 4A.4 - the per-building evaluator table.
int evaluateBuilding(H3Context & ctx, const CGTownInstance * town, const BuildingID & building);

/// SS 4G.4 - town + 0x03 != 0: "an enemy hero can reach this town in about 1.5 turns".
/// The original refills the count from kingdom-goal pass A (vftable 0x63B670), which
/// floods from every enemy hero with (max movement + 800) of range; the same flood is
/// run here, cached per turn on the context.
bool townUnderThreat(H3Context & ctx, const CGTownInstance * town);

/// SS 4B.6 - type_AI_player::AI_offer_resources_to_ally @ 0x429110.
void offerResourcesToAlly(H3Context & ctx, PlayerColor ally);

/// SS 4B.9 - type_AI_player::AI_hire_hero @ 0x431360.
bool hireHero(H3Context & ctx);

/// SS 4B.10 - type_AI_player::AI_buy_hero @ 0x431800.
bool buyHero(H3Context & ctx, const CGHeroInstance * candidate);

/// SS 4B.10a - town::AI_hero_arrival_value @ 0x431BD0.
int heroArrivalValue(H3Context & ctx, const CGTownInstance * town, const CGHeroInstance * candidate);

/// SS 4.13 - the driver that runs when an AI hero is standing in one of its own towns
/// (0x42BA60).
void visitOwnTown(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town);

/// SS 4A.3 / SS 4G.7 - AI_do_trades (0x42A580 choose, 0x42AB40 revalidate, 0x42AC20
/// commit): the half of the trade machinery that actually spends.  Sells the surplus
/// AI_plan_trades measures for the resources the kingdom is short of.
void doTrades(H3Context & ctx, const IMarket * market, const CGHeroInstance * hero);

/// Buying a secondary skill at a University.
/// NOT IN REPORT: the original AI has no University handler at all
/// (SS 4.8 prices the object as an experience purchase, and nothing buys skills there).
/// The obvious reading is used: the best skill the SS 4.12 valuer ranks, taken when it
/// is worth more than the gold it costs.
void buyUniversitySkill(H3Context & ctx, const IMarket * market, const CGHeroInstance * hero);

}
