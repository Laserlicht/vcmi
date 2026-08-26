/*
 * H3Movement.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Context.h"
#include "H3Search.h"

#include "../../lib/int3.h"

#include <vector>

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 2 - HeroDestination (16 bytes), the record the object scan stores and the
/// destination chooser fills in.
struct HeroDestination
{
	int3 coord = int3(-1, -1, -1);   ///< +0x00
	int value = 0;                   ///< +0x04
	int moveCost = 0;                ///< +0x08 - move cost / limit
	bool reachable = false;          ///< +0x0C - reachable this turn
	bool chooserFlag = false;        ///< +0x0D - flag set by the destination chooser

	bool isValid() const { return coord.x >= 0; }
};

/// A map-sized grid of int values, the shape both the danger map (SS 4.6) and the
/// influence map (SS 4.5) use.
class ValueMap
{
public:
	void resize(const int3 & size);
	void clear();

	int64_t & at(const int3 & tile);
	int64_t at(const int3 & tile) const;

	bool isInside(const int3 & tile) const;

private:
	int3 mapSize = int3(0, 0, 0);
	std::vector<int64_t> values;
};

/// SS 4.4 - hero::AI_scan_objects @ 0x42EDD0.
std::vector<HeroDestination> scanObjects(
	H3Context & ctx,
	const CGHeroInstance * hero,
	H3Search & search,
	int range,
	bool ignoreCost);

/// SS 4.6 - hero::AI_add_enemy_threats @ 0x42DE50, the danger map.
void addEnemyThreats(H3Context & ctx, const CGHeroInstance * hero, ValueMap & dangerMap);

/// SS 4.5 - hero::AI_choose_destination @ 0x42E0B0, the influence map.
/// @return the value of the chosen destination
int chooseDestination(
	H3Context & ctx,
	const CGHeroInstance * hero,
	int radius,
	HeroDestination & out,
	bool & nearby,
	const ValueMap & dangerMap);

/// SS 4.7 / SS 4B.12 - hero::AI_move_to_destination @ 0x42FEE0.
void moveToDestination(H3Context & ctx, const CGHeroInstance * hero, HeroDestination & destination);

/// SS 4.3 - hero::AI_take_turn @ 0x5267B0.
void heroTakeTurn(H3Context & ctx, const CGHeroInstance * hero, bool unlimitedRange, bool & magusHutFlag);

/// SS 4.2 - AI_hero_turn @ 0x5261F0.
void heroTurn(H3Context & ctx, const CGHeroInstance * hero, ValueMap & dangerMap, bool aggressive, bool & magusHutFlag);

}
