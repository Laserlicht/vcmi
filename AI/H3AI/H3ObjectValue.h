/*
 * H3ObjectValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Context.h"

#include "../../lib/int3.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
class CGObjectInstance;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4.8 - hero::AI_object_value @ 0x528040.
///
/// @param moveLimit  the original's `int* limit` out-parameter: a handful of handlers
///        rewrite it so the caller knows the object costs the rest of the turn.
int objectValue(H3Context & ctx, const CGHeroInstance * hero, const int3 & tile, int & moveLimit);

/// SS 4.8 - AI_pay_for_object @ 0x529810.
int payForObject(H3Context & ctx, int value, int goldCost, int amount, GameResID resource);

/// SS 4.9 - AI_get_value_of_artifact (0x433AA0 / 0x4336C0), including the
/// SS 4C.3 victory-condition override for conditions 0 and 10.
int artifactValue(H3Context & ctx, const ArtifactID & artifact);

/// SS 4.17 - hero::AI_get_spell_value @ 0x5298D0.
int spellValue(H3Context & ctx, const CGHeroInstance * hero, const SpellID & spell);

}
