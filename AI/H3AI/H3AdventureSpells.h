/*
 * H3AdventureSpells.h, part of VCMI engine
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
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4F - the AI's complete adventure-spell behaviour, which is four spells: Summon
/// Boat, Fly, Water Walk and Dimension Door.  Nothing else is ever cast outside battle.
///
/// The report fires all four from inside the move driver, at the step where the search
/// plane flips (cell flag 0x400) or where a Dimension Door transition is marked (flag
/// 0x800).  This reimplementation has no second search plane, but it does not need one:
/// Fly and Water Walk last the whole day, so a hero is in one plane or the other for a
/// whole turn, and H3Search reads which from the hero's own bonuses.  What is left of
/// the flag test is therefore the condition it stood for - "the route the hero wants
/// does not exist on foot" - and that is where this runs.
///
/// The spells are identified by their mechanics rather than by spell id, so a mod that
/// ships its own Fly is used like the original.
///
/// @return true when a spell was cast, i.e. the caller must recompute the route.
bool castAdventureSpells(H3Context & ctx, const CGHeroInstance * hero, const int3 & destination);

}
