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

}
