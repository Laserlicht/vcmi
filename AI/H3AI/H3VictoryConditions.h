/*
 * H3VictoryConditions.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/int3.h"

class CCallback;

namespace H3AI
{

/// SS 4C.2 - the special-victory record at gpGame + 0x1F89C.  The numbering is the
/// original's, cross-checked in SS 4C.2 against the AI's own comparisons.
enum class H3VictoryCondition
{
	NONE = -1,
	ACQUIRE_ARTIFACT = 0,     ///< AI aware (SS 4C.3)
	ACCUMULATE_CREATURES = 1, ///< AI blind
	ACCUMULATE_RESOURCES = 2, ///< AI blind
	UPGRADE_TOWN = 3,         ///< AI aware
	BUILD_GRAIL = 4,          ///< AI aware
	DEFEAT_HERO = 5,          ///< AI aware
	CAPTURE_TOWN = 6,         ///< AI aware
	DEFEAT_MONSTER = 7,       ///< AI aware
	FLAG_DWELLINGS = 8,       ///< AI aware
	FLAG_MINES = 9,           ///< AI aware
	TRANSPORT_ARTIFACT = 10   ///< AI aware, but SS 4C.3 proves it is handled as condition 0
};

/// SS 4C.2 - the fields the AI reads out of that record.
struct VictoryConditionInfo
{
	H3VictoryCondition condition = H3VictoryCondition::NONE;

	/// +0x1F8A0 - artifact id (conditions 0 and 10)
	ArtifactID artifact;
	/// +0x1F8D0 target hero id, +0x1F8B4.. target town, +0x1F8D4.. target monster
	ObjectInstanceID targetObject;
	/// +0x1F8B4 / +0x1F8B8 / +0x1F8BC town position, +0x1F8D4.. monster position
	int3 position = int3(-1, -1, -1);
	/// +0x1F8C0 - required hall level, building id 11 + level
	int hallLevel = -1;
	/// +0x1F8C1 - required fort level, building id 7 + level
	int fortLevel = -1;

	bool isAIAware() const
	{
		return condition != H3VictoryCondition::NONE
			&& condition != H3VictoryCondition::ACCUMULATE_CREATURES
			&& condition != H3VictoryCondition::ACCUMULATE_RESOURCES;
	}
};

/// SS 4C - reads the scenario's special victory condition and maps it onto the
/// original's numbering.  There is no caching in the original either: the AI reads
/// gpGame + 0x1F89C directly at each of the nine sites.
VictoryConditionInfo getVictoryConditionInfo(CCallback * cb);

}
