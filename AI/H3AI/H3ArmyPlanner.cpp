/*
 * H3ArmyPlanner.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3ArmyPlanner.h"

#include "H3Player.h"
#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

#include <algorithm>

namespace H3AI
{

namespace
{
/// SS 4B.4 / SS 4B.4 step 2 - gpGame->d[0x1F698], the "AI combat skill level" global,
/// gates the elemental alignment override.
/// TODO: VCMI has no equivalent setting, and the report only records that the AI reads
/// it here.  Zero is used, which selects the branch that treats the four base elementals
/// as alignment -1.
constexpr int AI_COMBAT_SKILL_LEVEL = 0;

/// SS 4B.4 - the four base elementals (SS 4E.3 confirms ids 112-115).
bool isBaseElemental(const CreatureID & creature)
{
	const int id = creature.getNum();

	return id >= 112 && id <= 115;
}

/// SS 4B.4 - traits + 0x00, the creature's town / alignment.
int alignmentOf(const CreatureID & creature)
{
	const CCreature * c = creature.toCreature();

	if(c == nullptr)
		return -1;

	if(AI_COMBAT_SKILL_LEVEL == 0 && isBaseElemental(creature))
		return -1;

	return c->getFactionID().getNum();
}

/// SS 4B.4 - armyGroup::get_alignments @ 0x44A460
uint32_t alignmentMask(const ArmyGroup & group)
{
	uint32_t mask = 0;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(!group.type[i].hasValue())
			continue;

		const int align = alignmentOf(group.type[i]);

		if(align >= 0 && align < 32)
			mask |= 1u << align;
	}

	return mask;
}

int lowestSetBit(uint32_t mask)
{
	for(int i = 0; i < 32; ++i)
		if(mask & (1u << i))
			return i;

	return -1;
}

/// SS 4B.4 - armyGroup::count_alignments @ 0x44AE60
/// TODO: the report names this routine and shows how its result is used
/// (count_alignments(dst, mode) + morale < maxAlign) but never gives its body.  It is
/// implemented here as "how many distinct alignments the group already holds", counting
/// only the creatures that are not alignment-free (SS 4E.2 bit 17).
int countAlignments(const ArmyGroup & group, bool /*mode*/)
{
	uint32_t mask = 0;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(!group.type[i].hasValue())
			continue;

		const CCreature * creature = group.type[i].toCreature();

		if(isAlignmentFree(creature))
			continue;

		const int align = alignmentOf(group.type[i]);

		if(align >= 0 && align < 32)
			mask |= 1u << align;
	}

	int count = 0;

	for(int i = 0; i < 32; ++i)
		if(mask & (1u << i))
			++count;

	return count;
}

int creatureSpeed(const CreatureID & creature)
{
	const CCreature * c = creature.toCreature();

	return c != nullptr ? c->getBaseSpeed() : 0;
}

int64_t creatureAIValue(const CreatureID & creature)
{
	const CCreature * c = creature.toCreature();

	return c != nullptr ? c->getAIValue() : 0;
}

/// SS 4.13 / SS 4B.4 - hero::get_morale(hero, 0, 0, 1) @ 0x4E39B0.
/// TODO: the report never expands get_morale; VCMI's own morale bonus total is used.
int heroMoraleOf(const CGHeroInstance * hero)
{
	return hero != nullptr ? hero->valOfBonuses(BonusType::MORALE) : 0;
}
}

int movementForSpeed(CCallback * cb, int speed)
{
	// SS 4B.4 - movementForSpeed[] is the runtime table at 0x698A98, "the adventure-map
	// movement points a hero gets for a given slowest-stack speed".  VCMI ships exactly
	// that table as HEROES_MOVEMENT_POINTS_LAND.
	const auto & table = cb->getSettings().getValue(EGameSettings::HEROES_MOVEMENT_POINTS_LAND).Vector();

	if(table.empty())
		return 1;

	if(speed < 0)
		speed = 0;

	if(static_cast<size_t>(speed) < table.size())
		return static_cast<int>(table[speed].Integer());

	return static_cast<int>(table.back().Integer());
}

// ---------------------------------------------------------------------------------
// ArmyGroup
// ---------------------------------------------------------------------------------

ArmyGroup::ArmyGroup()
{
	type.fill(CreatureID::NONE);
	count.fill(0);
}

ArmyGroup::ArmyGroup(const CCreatureSet * army)
	: ArmyGroup()
{
	if(army == nullptr)
		return;

	for(const auto & slot : army->Slots())
	{
		const int index = slot.first.getNum();

		if(index < 0 || index >= GameConstants::ARMY_SIZE)
			continue;

		const CCreature * creature = army->getCreature(slot.first);

		if(creature == nullptr)
			continue;

		type[index] = creature->getId();
		count[index] = army->getStackCount(slot.first);
	}
}

int ArmyGroup::slotCount() const
{
	// SS 4B.4 - armyGroup::slot_count @ 0x44ACC0, the whole function.
	int n = 0;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		if(type[i].hasValue())
			++n;

	return n;
}

int64_t ArmyGroup::aiValue() const
{
	int64_t value = 0;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		if(type[i].hasValue())
			value += creatureAIValue(type[i]) * count[i];

	return value;
}

void ArmyGroup::removeSlot(int slot)
{
	if(slot < 0 || slot >= GameConstants::ARMY_SIZE)
		return;

	type[slot] = CreatureID::NONE;
	count[slot] = 0;
}

void ArmyGroup::setSlot(const CreatureID & creature, int amount, int slot)
{
	if(slot < 0 || slot >= GameConstants::ARMY_SIZE)
		return;

	type[slot] = creature;
	count[slot] = amount;
}

// ---------------------------------------------------------------------------------
// ArmyPlanner
// ---------------------------------------------------------------------------------

ArmyPlanner::ArmyPlanner(CCallback * callback, H3Player * owner)
	: cb(callback)
	, player(owner)
{
}

bool ArmyPlanner::alignSeen(int alignment) const
{
	const size_t index = static_cast<size_t>(alignment + 1);

	return index < alignSeenFlags.size() && alignSeenFlags[index];
}

void ArmyPlanner::setAlignSeen(int alignment, bool value)
{
	const size_t index = static_cast<size_t>(alignment + 1);

	if(index < alignSeenFlags.size())
		alignSeenFlags[index] = value;
}

void ArmyPlanner::mergeDuplicateStacks(ArmyGroup & group)
{
	// SS 4B.4 - merge_duplicate_stacks @ 0x42D870, lowest slot wins.
	for(int i = 1; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(!group.type[i].hasValue())
			continue;

		for(int j = 0; j < i; ++j)
		{
			if(group.type[j] == group.type[i])
			{
				group.count[j] += group.count[i];
				group.removeSlot(i);
				break;
			}
		}
	}
}

void ArmyPlanner::writeback(ArmyGroup & group)
{
	// SS 4B.4 - writeback @ 0x42D8E0, the army slot-layout rule.  Battlefield starting
	// positions follow slot order, so this directly shapes every AI battle line.
	struct Record
	{
		CreatureID type;
		int speed;
		int count;
	};

	std::vector<Record> sorted;

	// 1. drain the group into a vector of 12-byte records
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(!group.type[i].hasValue())
			continue;

		sorted.push_back(Record{ group.type[i], creatureSpeed(group.type[i]), group.count[i] });
	}

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		group.removeSlot(i);

	// 2. sort.  The report records only that the key record carries traits[type].speed;
	// the comparator itself is the inlined MSVC std::sort idiom.
	// TODO: the report does not state the comparison direction.  Ascending by speed is
	// used, which is what makes the two opposite-direction passes of step 3 place fast
	// shooters and slow walkers at opposite ends.
	std::sort(sorted.begin(), sorted.end(), [](const Record & a, const Record & b) { return a.speed < b.speed; });

	const int n = static_cast<int>(sorted.size());

	// 3a. shooters first, walking the sorted list BACKWARDS, into alternating slots
	int slot = 0;

	for(int k = n - 1; k >= 0; --k)
	{
		if(!isShooter(sorted[k].type.toCreature()))
			continue;

		group.setSlot(sorted[k].type, sorted[k].count, slot);
		slot += 2;

		if(slot >= GameConstants::ARMY_SIZE)
			slot = 1;
	}

	// 3b. everyone else, walking FORWARDS, into the first free slot
	int i = 0;

	for(int k = 0; k < n; ++k)
	{
		if(isShooter(sorted[k].type.toCreature()))
			continue;

		while(i < GameConstants::ARMY_SIZE && group.type[i].hasValue())
			++i;

		if(i >= GameConstants::ARMY_SIZE)
			break;

		group.setSlot(sorted[k].type, sorted[k].count, i);
	}
}

void ArmyPlanner::prepare()
{
	// SS 4B.4 - prepare @ 0x42C060, run at the head of take_best_stack and buy_one_stack.
	//
	//   this->w[0x0C] = 0x44ABB0(this->dst, &this->b[0x0E]);   // free / weakest slot
	//   if (this->mode)
	//       for each alignment i with alignSeen[i] set ... refresh from get_alignments(dst)
	if(destination == nullptr)
		return;

	chosenSlot = -1;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(!destination->type[i].hasValue())
		{
			chosenSlot = i;
			break;
		}
	}

	if(mode)
	{
		const uint32_t mask = alignmentMask(*destination);

		for(int align = 0; align < 32; ++align)
			setAlignSeen(align, (mask & (1u << align)) != 0);
	}
}

void ArmyPlanner::normalise()
{
	// SS 4B.4 - normalise @ 0x42C5B0: push back into the source the stacks the
	// destination should not keep, freeing destination slots.
	if(source == nullptr)
		return;

	if(source->slotCount() == GameConstants::ARMY_SIZE)
		return;

	if(destination == nullptr || destination->slotCount() == 1)
		return;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(!destination->type[i].hasValue())
			continue;

		// The two guards are re-evaluated every iteration in the original.
		if(source->slotCount() == GameConstants::ARMY_SIZE)
			return;

		if(destination->slotCount() == 1)
			return;

		// SS 4B.4: "value_of_adding (0x42C830) decides; armyGroup::remove_slot commits".
		// TODO: the report does not give the exact predicate normalise applies to the
		// value_of_adding result.  A stack whose re-addition would be worthless (<= 0)
		// is the only reading consistent with "stacks the destination should not keep".
		int outSlot = -1;
		const CreatureID creature = destination->type[i];
		const int amount = destination->count[i];

		destination->removeSlot(i);

		if(valueOfAdding(creature, amount, outSlot, false) > 0)
		{
			destination->setSlot(creature, amount, i);
			continue;
		}

		const int freeSlot = source->slotCount() < GameConstants::ARMY_SIZE
			? [this]() { for(int s = 0; s < GameConstants::ARMY_SIZE; ++s) if(!source->type[s].hasValue()) return s; return -1; }()
			: -1;

		if(freeSlot < 0)
		{
			destination->setSlot(creature, amount, i);
			continue;
		}

		source->setSlot(creature, amount, freeSlot);
	}
}

int64_t ArmyPlanner::valueOfAdding(const CreatureID & creature, int amount, int & outSlot, bool mustFit)
{
	// SS 4B.4 - value_of_adding @ 0x42C830, the 832-byte core behind every troop decision.
	outSlot = -1;

	const CCreature * tr = creature.toCreature();

	if(tr == nullptr || destination == nullptr)
		return 0;

	int64_t value = static_cast<int64_t>(tr->getAIValue()) * amount;
	bool ok = false;

	// ---- 1. alignment ----
	int align = alignmentOf(creature);

	if(mode)
	{
		const uint32_t allowed = alignmentMask(*destination);

		if(align < 0 || !(allowed & (1u << align)))
			align = lowestSetBit(allowed);
	}

	// ---- 2. may we mix this alignment in at all? ----
	if(!alignSeen(align) && destination->slotCount() > 0)
	{
		const int maxAlign = (AI_COMBAT_SKILL_LEVEL != 0 || !isBaseElemental(creature))
			? (tr->getFactionID().getNum() == 4 /*Necropolis*/ ? 2 : 1)
			: 1;

		if(countAlignments(*destination, mode) + morale < maxAlign)
		{
			int64_t keep = 0;

			for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
			{
				if(!destination->type[slot].hasValue())
					continue;

				if(isAlignmentFree(destination->type[slot].toCreature()))
					continue;

				keep += creatureAIValue(destination->type[slot]) * destination->count[slot];
			}

			if(!isAlignmentFree(tr))
				keep += value;

			// The mix must be worth at least ten times the newcomer.
			ok = (keep >= ALIGNMENT_MIX_RATIO * value);
		}
	}

	// ---- 3. movement penalty ----
	int slowest = 20;

	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		if(destination->type[slot].hasValue())
			slowest = std::min(slowest, creatureSpeed(destination->type[slot]));

	if(slowest > tr->getBaseSpeed())
	{
		const int64_t armyVal = destination->aiValue() + MOVEMENT_PENALTY_ARMY_SLACK;
		const int newSpeedPoints = movementForSpeed(cb, tr->getBaseSpeed());
		const int oldSpeedPoints = movementForSpeed(cb, slowest);

		if(oldSpeedPoints > 0)
		{
			value += static_cast<int64_t>(
				static_cast<double>(newSpeedPoints) * static_cast<double>(armyVal)
					/ static_cast<double>(oldSpeedPoints)
				- static_cast<double>(armyVal));
		}
	}

	// ---- 4. pick the destination slot ----
	// (a) already have this creature type? -> merge, free of charge
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(destination->type[i] == creature)
		{
			outSlot = i;
			// Note: the original returns -1, not 0, in the mustFit case.
			return mustFit ? -1 : value;
		}
	}

	// (b) new type. If the alignment gate fired, or the caller demands a guaranteed fit,
	//     skip the free-slot search entirely and go straight to displacement.
	if(!ok && !mustFit)
	{
		if(destination->slotCount() < 6 || source == nullptr)
		{
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			{
				if(!destination->type[i].hasValue())
				{
					outSlot = i;
					return value;
				}
			}
		}
	}

	// (c) displace something
	const int slot = pickSlotToDisplace(isShooter(tr), ok);
	outSlot = slot;

	if(slot < 0)
		return 0;

	return value - creatureAIValue(destination->type[slot]) * destination->count[slot];
}

int ArmyPlanner::pickSlotToDisplace(bool newcomerIsShooter, bool checkAlign) const
{
	// SS 4B.4 - pick_slot_to_displace @ 0x42C690
	if(destination == nullptr)
		return -1;

	int flagged = 0;

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		if(destination->type[i].hasValue() && isShooter(destination->type[i].toCreature()))
			++flagged;

	const bool protectClass = newcomerIsShooter && flagged > 3;
	const bool keepLastOfClass = !newcomerIsShooter && flagged == 1;

	int best = -1;
	int64_t bestVal = 0;

	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
	{
		if(!destination->type[slot].hasValue())
			continue;

		const CCreature * tr = destination->type[slot].toCreature();

		if(checkAlign)
		{
			int align = alignmentOf(destination->type[slot]);

			if(mode)
			{
				const uint32_t allowed = alignmentMask(*destination);

				if(align < 0 || !(allowed & (1u << align)))
					align = lowestSetBit(allowed);
			}

			if(!alignSeen(align))
				continue;
		}

		if(protectClass && !isShooter(tr))
			continue;

		if(keepLastOfClass && isShooter(tr))
			continue;

		const int64_t score = static_cast<int64_t>(destination->count[slot]) * creatureAIValue(destination->type[slot]);

		if(best < 0 || score < bestVal)
		{
			bestVal = score;
			best = slot;
		}
	}

	return best;
}

int64_t ArmyPlanner::takeBestStack(bool allowPartial)
{
	// SS 4B.4 - take_best_stack @ 0x42C280
	prepare();

	if(source == nullptr || destination == nullptr)
		return 0;

	int64_t best = 0;
	int bestSourceSlot = -1;
	CreatureID bestType;
	int bestCount = 0;
	int bestDstSlot = -1;

	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
	{
		if(!source->type[slot].hasValue())
			continue;

		const CreatureID creature = source->type[slot];
		int n = source->count[slot];
		int dstSlot = -1;

		int64_t v = valueOfAdding(creature, n, dstSlot, allowPartial ? false : true);

		if(!allowPartial && n > 1)
		{
			int dstSlot2 = -1;
			const int64_t v2 = valueOfAdding(creature, n - 1, dstSlot2, false);

			if(v2 > v)
			{
				v = v2;
				n = n - 1;
				dstSlot = dstSlot2;
			}
		}

		const int64_t score = (static_cast<int64_t>(skillDiff) * v) / TROOP_EXCHANGE_DIVISOR;

		if(score > best)
		{
			best = score;
			bestSourceSlot = slot;
			bestType = creature;
			bestCount = n;
			bestDstSlot = dstSlot;
		}
	}

	if(best > 0 && bestDstSlot >= 0)
	{
		// perform the move
		if(destination->type[bestDstSlot] == bestType)
			destination->count[bestDstSlot] += bestCount;
		else
			destination->setSlot(bestType, bestCount, bestDstSlot);

		source->count[bestSourceSlot] -= bestCount;

		if(source->count[bestSourceSlot] <= 0)
			source->removeSlot(bestSourceSlot);
	}

	return best;
}

int64_t ArmyPlanner::buyOneStack(bool reserveFunds)
{
	// SS 4B.4 - buy_one_stack @ 0x42D420
	prepare();

	if(destination == nullptr)
		return 0;

	int64_t best = 0;
	int bestOffer = -1;
	int bestCount = 0;
	int bestSlot = -1;

	for(size_t index = 0; index < offers.size(); ++index)
	{
		CreatureOffer & offer = offers[index];

		if(offer.available <= 0)
			continue;

		const CCreature * creature = offer.type.toCreature();

		if(creature == nullptr)
			continue;

		int n;

		if(offer.free)
		{
			n = offer.available;
		}
		else
		{
			const TResources & cost = creature->getFullRecruitCost();

			if(reserveFunds && player != nullptr)
				player->reserveFunds(cost, offer.available);

			n = offer.available;

			if(funds != nullptr)
			{
				for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
					if(cost[GameResID(r)] > 0)
						n = std::min(n, (*funds)[GameResID(r)] / cost[GameResID(r)]);
			}
		}

		if(n <= 0)
			continue;

		int slot = -1;
		const int64_t v = valueOfAdding(offer.type, n, slot, false);

		if(v > best && slot >= 0)
		{
			best = v;
			bestOffer = static_cast<int>(index);
			bestCount = n;
			bestSlot = slot;
		}
	}

	if(bestOffer < 0)
		return 0;

	CreatureOffer & offer = offers[bestOffer];
	const CCreature * creature = offer.type.toCreature();

	if(!offer.free && funds != nullptr)
	{
		const TResources & cost = creature->getFullRecruitCost();

		for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
			(*funds)[GameResID(r)] -= cost[GameResID(r)] * bestCount;
	}

	// the tier's stock is shared between the base creature and its upgrades
	for(CreatureOffer & sibling : offers)
		if(sibling.townSlot == offer.townSlot)
			sibling.available -= bestCount;

	if(destination->type[bestSlot] == offer.type)
		destination->count[bestSlot] += bestCount;
	else
		destination->setSlot(offer.type, bestCount, bestSlot);

	return best;
}

void ArmyPlanner::initFromTown(const CGTownInstance * town)
{
	// SS 4B.4 - AI_army_planner::init_from_town @ 0x42D1B0.
	// There is no filtering and no ordering here: every dwelling slot with stock is
	// offered, in dwelling order.  All the selection pressure lives downstream.
	offers.clear();

	if(town == nullptr)
		return;

	// The original walks 14 slots (7 tiers x base/upgraded), each with its own int16
	// stock at town + 0x16.  VCMI keeps one pool per tier with the base creature and
	// every built upgrade listed as alternatives, so each alternative is offered here
	// and the shared stock is decremented across the whole tier when one is bought.
	for(size_t level = 0; level < town->creatures.size(); ++level)
	{
		const auto & available = town->creatures[level];

		if(available.second.empty())
			continue;

		const int amount = static_cast<int>(available.first);

		if(amount <= 0)
			continue;

		for(const CreatureID & creature : available.second)
		{
			CreatureOffer offer;
			offer.type = creature;
			offer.townSlot = static_cast<int>(level);
			offer.available = amount;
			offer.free = false;
			offers.push_back(offer);
		}
	}
}

void ArmyPlanner::recruit(ArmyGroup & dst, int heroMorale, ArmyGroup * src, ResourceSet * money, bool reserveFunds, bool modeFlag)
{
	// SS 4B.4 - AI_army_planner::recruit @ 0x42D690
	destination = &dst;
	source = src;
	flag = modeFlag;
	morale = heroMorale;
	funds = money;

	// 1. fold duplicate creature types in the destination together
	mergeDuplicateStacks(dst);

	// 2. spend
	normalise();

	while(buyOneStack(reserveFunds) > 0)
		;

	// 3. write the leftovers back into the town
	// (in the original the offer record holds a pointer straight into town + 0x16, so
	// this step really drains the town's dwellings; here the caller owns that write.)
}

int64_t ArmyPlanner::evaluateTroopExchange(
	const CGHeroInstance * receiver,
	const CCreatureSet * offered,
	const CGHeroInstance * giver,
	bool modeFlag)
{
	// SS 4B.4 - AI_evaluate_troop_exchange @ 0x42C4A0
	if(receiver == nullptr)
		return 0;

	ArmyGroup dst(receiver);
	ArmyGroup src(offered);

	destination = &dst;
	source = &src;
	mode = modeFlag;
	morale = heroMoraleOf(receiver);
	skillDiff = std::max(primarySkillSum(receiver) - (giver != nullptr ? primarySkillSum(giver) : 0), 0);

	mergeDuplicateStacks(dst);
	normalise();

	int64_t total = 0;
	int64_t gain = 0;

	do
	{
		gain = takeBestStack(src.slotCount() > 1);
		total += gain;
	}
	while(gain > 0);

	return total;
}

int64_t ArmyPlanner::evaluatePurchase(const CGTownInstance * town, const CGHeroInstance * receiver, bool modeFlag)
{
	// SS 4B.4 - AI_evaluate_purchase (0x42D780 / 0x42D690) wraps buy_one_stack in the
	// same do { ... } while (gain > 0) loop.
	if(town == nullptr || receiver == nullptr)
		return 0;

	initFromTown(town);

	ArmyGroup dst(receiver);
	ResourceSet money = cb->getResourceAmount();

	destination = &dst;
	source = nullptr;
	mode = modeFlag;
	morale = heroMoraleOf(receiver);
	funds = &money;

	int64_t total = 0;
	int64_t gain = 0;

	do
	{
		gain = buyOneStack(false);
		total += gain;
	}
	while(gain > 0);

	return total;
}

}
