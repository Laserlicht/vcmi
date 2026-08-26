/*
 * H3SecondarySkills.h, part of VCMI engine
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
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4.12 - hero::AI_secondary_skill_value @ 0x524690, the 28-arm switch.
int secondarySkillValue(H3Context & ctx, const CGHeroInstance * hero, const SecondarySkill & skill, bool useArmy);

/// SS 4.12 - hero::AI_choose_secondary_skill @ 0x52BBD0.
SecondarySkill chooseSecondarySkill(
	H3Context & ctx,
	const CGHeroInstance * hero,
	const SecondarySkill & skillA,
	const SecondarySkill & skillB,
	bool useArmy);

}
