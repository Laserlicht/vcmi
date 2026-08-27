/*
 * H3ArtifactValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3ArtifactData.h"
#include "H3Context.h"

#include "../../lib/constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
class CArtifactInstance;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4.9a - AI_get_value_of_artifact @ 0x4336C0.
///
/// @param equipped  the original's dl byte: "evaluate as if already worn".  Spell-granting
///        effects then skip the "the hero already has this from somewhere else" test, and
///        morale/luck effects subtract the artifact's own contribution before measuring.
/// @param cheap     the original's third stack argument: several effect classes return 0
///        rather than doing real work when it is set.
/// @param instance  the artifact instance, where the caller holds one.  Only the Spell
///        Scroll arm needs it: VCMI keeps the scroll's spell on the instance, not on the
///        ArtifactID, so without it a scroll cannot be priced.
int artifactValueForHero(H3Context & ctx, const CGHeroInstance * hero,
	const ArtifactID & artifact, bool equipped = false, bool cheap = false,
	const CArtifactInstance * instance = nullptr);

/// SS 4.9a - hero::total_artifact_value @ 0x4339E0.  What this hero would GAIN by taking
/// the artifact, net of whatever it would have to give up to wear it.
int totalArtifactValue(H3Context & ctx, const CGHeroInstance * hero,
	const ArtifactID & artifact, bool equipped = false,
	const CArtifactInstance * instance = nullptr);

/// SS 4.9a - AI_get_value_of_artifact @ 0x433AA0: what an artifact lying on the map is
/// worth to a player, namely the most any of its heroes would gain, floored at 10.
int artifactValueForPlayer(H3Context & ctx, const ArtifactID & artifact);

}
