/*
 * H3Context.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3HeroState.h"
#include "H3Player.h"
#include "H3VictoryConditions.h"

class CCallback;

namespace H3AI
{

/// The globals the original AI reaches through gpGame / gpCurPlayer / AI_player[],
/// bundled so every valuation can be a free function like it is in the binary.
struct H3Context
{
	CCallback * cb = nullptr;
	H3Player * player = nullptr;
	HeroStateMap * heroStates = nullptr;
	VictoryConditionInfo victory;

	/// Whether this AI may read the map through the fog.  The original AI reads gpGame's
	/// map memory directly and its searchArray floods the whole map regardless of what
	/// the player has explored, so an open map is the faithful configuration - but it is
	/// a cheat against a human, and is gated accordingly (see H3AdventureAI::initGameInterface).
	/// SS 4.4's exploration reward still keys on what the *player* has explored.
	bool openMap = false;

	/// SS 4B.1 / SS 4B.2 - the number of town records on the map.  The original reads it
	/// as a pointer difference; here it is counted once and cached for the turn.
	mutable int cachedTownsOnMap = -1;
};

}
