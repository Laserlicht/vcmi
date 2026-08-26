/*
 * H3TownValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Context.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
class CGTownInstance;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4B.1 - hero::AI_town_value @ 0x52AB80, the TOWN branch of AI_object_value.
int townValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town, int & moveLimit);

/// SS 4B.2 - hero::AI_town_capture_value @ 0x529CB0.
int townCaptureValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town, int moveLimit);

/// SS 4B.3 - hero::AI_town_visit_value @ 0x52B1E0.
int townVisitValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town);

/// SS 4B.5 - AI_town_recruit_value @ 0x52B090.
int townRecruitValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town, int moveLimit);

/// SS 4B.1 / SS 4B.2 - numTowns is the number of town records on the map, not the
/// number the player owns: "with eight towns on the map that is 625 000 per town".
int townsOnMap(H3Context & ctx);

}
