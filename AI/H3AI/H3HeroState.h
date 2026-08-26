/*
 * H3HeroState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Valuations.h"

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/int3.h"

#include <map>

namespace H3AI
{

/// SS 2 - the AI-private fields the original keeps inside the hero record.  VCMI's
/// CGHeroInstance is read-only from an AI's point of view, so they live in a side table.
struct HeroAIState
{
	/// hero + 0x35 / +0x39 / +0x3D - the AI destination (-1 = none)
	int3 destination = int3(-1, -1, -1);
	/// hero + 0x44 / +0x45 / +0x46 - the previous destination (0xFF = none)
	int3 previousDestination = int3(-1, -1, -1);
	/// hero + 0x41 - added to the search radius; also the stored cost-to-goal estimate (SS 4B.7)
	int destinationCostEstimate = 0;
	/// hero + 0x43 - cleared when the destination is invalidated
	bool destinationValid = false;
	/// hero + 0x11C - "hero is done for this turn / asleep"
	bool done = false;
	/// hero + 0x109 / +0x47E / +0x482 / +0x486
	HeroValuations valuations;

	bool hasDestination() const { return destination.x >= 0; }
	bool hasPreviousDestination() const { return previousDestination.x >= 0; }
};

using HeroStateMap = std::map<ObjectInstanceID, HeroAIState>;

}
