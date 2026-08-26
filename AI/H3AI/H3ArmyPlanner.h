/*
 * H3ArmyPlanner.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Constants.h"

#include "../../lib/ResourceSet.h"
#include "../../lib/constants/EntityIdentifiers.h"

#include <array>
#include <vector>

VCMI_LIB_NAMESPACE_BEGIN
class CCreatureSet;
class CGHeroInstance;
class CGTownInstance;
class CCreature;
VCMI_LIB_NAMESPACE_END

class CCallback;

namespace H3AI
{

class H3Player;

/// SS 2 - hero + 0x91: 7 x int32 type then 7 x int32 count.  A plain, mutable copy of
/// an army, which is what the planner works on.
struct ArmyGroup
{
	std::array<CreatureID, GameConstants::ARMY_SIZE> type;
	std::array<int, GameConstants::ARMY_SIZE> count;

	ArmyGroup();
	explicit ArmyGroup(const CCreatureSet * army);

	/// SS 4B.4 - armyGroup::slot_count @ 0x44ACC0
	int slotCount() const;
	/// SS 4.9 - armyGroup::get_AI_value @ 0x44AC80
	int64_t aiValue() const;
	/// SS 4B.4 - armyGroup::remove_slot @ 0x44AB60
	void removeSlot(int slot);
	/// SS 4B.4 - armyGroup::set_slot @ 0x44ACE0
	void setSlot(const CreatureID & creature, int amount, int slot);
};

/// SS 4B.4 - one entry of the 12-byte offer vector built by init_from_town.
struct CreatureOffer
{
	CreatureID type;
	/// +0x04 - a pointer back into town + 0x16 in the original; the slot index here
	int townSlot = -1;
	/// +0x08
	int available = 0;
	/// +0x0A - "free", i.e. costs nothing
	bool free = false;
};

/// SS 4B.4 - type_AI_army_planner, the ~0x54-byte scratch object behind every troop
/// decision (hero <-> hero exchange, garrison pickup, recruitment).
class ArmyPlanner
{
public:
	ArmyPlanner(CCallback * cb, H3Player * player);

	/// SS 4B.4 - AI_evaluate_troop_exchange @ 0x42C4A0
	int64_t evaluateTroopExchange(
		const CGHeroInstance * receiver,
		const CCreatureSet * offered,
		const CGHeroInstance * giver,
		bool mode);

	/// SS 4B.4 - AI_evaluate_purchase (0x42D780 / 0x42D690)
	int64_t evaluatePurchase(const CGTownInstance * town, const CGHeroInstance * receiver, bool mode);

	/// SS 4B.4 - AI_army_planner::init_from_town @ 0x42D1B0
	void initFromTown(const CGTownInstance * town);

	/// SS 4B.4 - AI_army_planner::recruit @ 0x42D690
	void recruit(ArmyGroup & destination, int morale, ArmyGroup * source, ResourceSet * funds, bool reserveFunds, bool flag);

	/// SS 4B.4 - take_best_stack @ 0x42C280
	int64_t takeBestStack(bool allowPartial);

	/// SS 4B.4 - buy_one_stack @ 0x42D420
	int64_t buyOneStack(bool reserveFunds);

	/// SS 4B.4 - value_of_adding @ 0x42C830
	int64_t valueOfAdding(const CreatureID & creature, int amount, int & outSlot, bool mustFit);

	/// SS 4B.4 - pick_slot_to_displace @ 0x42C690
	int pickSlotToDisplace(bool newcomerIsShooter, bool checkAlign) const;

	/// SS 4B.4 - normalise @ 0x42C5B0
	void normalise();

	/// SS 4B.4 - prepare @ 0x42C060
	void prepare();

	/// SS 4B.4 - merge_duplicate_stacks @ 0x42D870
	static void mergeDuplicateStacks(ArmyGroup & group);

	/// SS 4B.4 - writeback @ 0x42D8E0: the army slot-layout rule.
	static void writeback(ArmyGroup & group);

	ArmyGroup * destination = nullptr;   ///< +0x00
	ArmyGroup * source = nullptr;        ///< +0x04
	bool mode = false;                   ///< +0x08
	int morale = 0;                      ///< +0x0A
	int skillDiff = 0;                   ///< +0x1C
	ResourceSet * funds = nullptr;       ///< +0x24
	bool flag = false;                   ///< +0x28
	std::vector<CreatureOffer> offers;   ///< +0x30 / +0x34

private:
	/// planner + 0x0F - alignSeen[], indexed from alignment -1 upwards
	bool alignSeen(int alignment) const;
	void setAlignSeen(int alignment, bool value);

	int chosenSlot = -1;                 ///< +0x0C

	std::array<bool, 32> alignSeenFlags = {};

	CCallback * cb = nullptr;
	H3Player * player = nullptr;
};

/// SS 4B.4 - the runtime table at 0x698A98, the adventure-map movement points a hero
/// gets for a given slowest-stack speed.  VCMI ships the same table as the game setting
/// HEROES_MOVEMENT_POINTS_LAND.
int movementForSpeed(CCallback * cb, int speed);

}
