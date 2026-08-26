/*
 * H3Valuations.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

namespace H3AI
{

int primarySkillSum(const CGHeroInstance * hero)
{
	// SS 4.1 - hero::get_primary_skill_sum @ 0x4E5960
	//
	//   for (i = 0; i < 4; ++i) {
	//       v = hero->primary[i];
	//       if      (v > 99) score += 99;
	//       else if (v > 0)  score += v;
	//       else             score += (i >= 2) ? 1 : 0;
	//   }
	//
	// i.e. spell power and knowledge count as at least 1, attack and defence as 0.
	int score = 0;

	for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
	{
		int value = hero->getPrimSkillLevel(static_cast<PrimarySkill>(i));

		if(value > PRIMARY_SKILL_CLAMP)
			score += PRIMARY_SKILL_CLAMP;
		else if(value > 0)
			score += value;
		else
			score += (i >= 2) ? 1 : 0;
	}

	return score;
}

double priorityScale(const CGHeroInstance * hero)
{
	return (primarySkillSum(hero) + PRIORITY_SCALE_OFFSET) / static_cast<double>(PRIORITY_SCALE_DIVISOR);
}

double valueOfLuckAndMorale(int current, int change, double good, double bad)
{
	// SS 4.9 - value_of_luck_and_morale @ 0x435850, transcribed verbatim.
	if(change > 0)
	{
		if(current >= 3)
			return 0.0;
		if(current + change > 3)
			change = 3 - current;
		if(current >= 0)
			return change * good;
		if(current + change <= 0)
			return -(change * bad);
		return (current + change) * good + current * bad;
	}
	else
	{
		if(current <= -3)
			return 0.0;
		if(current + change < -3)
			change = -3 - current;
		if(current <= 0)
			return -(change * bad);
		if(-change <= current)
			return change * good;
		return -(current * good) - (current + change) * bad;
	}
}

double valueOfMorale(int current, int change)
{
	// SS 4.9 - AI_value_of_morale @ 0x435830
	return valueOfLuckAndMorale(current, change, MORALE_GOOD, MORALE_BAD);
}

double valueOfLuck(int current, int change)
{
	// SS 4.9 - AI_value_of_luck @ 0x435960
	return valueOfLuckAndMorale(current, change, LUCK_GOOD, LUCK_BAD);
}

int64_t armyAIValue(const CCreatureSet * army)
{
	// SS 4.9 - armyGroup::get_AI_value @ 0x44AC80:
	//   sum over the 7 slots of traits[type].AI_value * count
	if(army == nullptr)
		return 0;

	int64_t value = 0;

	for(const auto & slot : army->Slots())
	{
		const CCreature * creature = army->getCreature(slot.first);

		if(creature != nullptr)
			value += static_cast<int64_t>(creature->getAIValue()) * army->getStackCount(slot.first);
	}

	return value;
}

int64_t experienceForLevel(int level)
{
	// SS 4.9 - experience_for_level @ 0x4DA420: a static int16 table for levels <= 12,
	// then geometric with ratio 1.2.  VCMI ships the identical H3 progression in
	// CHeroHandler::reqExp, so it is used here rather than duplicating the table.
	if(level <= 0)
		return 0;

	return LIBRARY->heroh->reqExp(static_cast<ui32>(level));
}

HeroValuations computeHeroValuations(const CGHeroInstance * hero)
{
	HeroValuations out;

	// SS 4.9 - hero::AI_update_valuations @ 0x527770
	//
	//   hero->xpValue = (2500.0f + armyGroup::get_AI_value(&hero->army))
	//                 / (float)(40 * experience_for_level(hero->level));
	const int64_t denominator = XP_VALUATION_LEVEL_DIVISOR * experienceForLevel(hero->level);

	if(denominator > 0)
	{
		out.experienceValue = static_cast<float>(XP_VALUATION_BASE + armyAIValue(hero))
			/ static_cast<float>(denominator);
	}
	else
	{
		// TODO: the report does not say what the original does at level 0, where
		// experience_for_level returns 0 and the division would trap.  Guarded here.
		out.experienceValue = 0.0f;
	}

	// SS 4.9 - the same function computes the three "value of +1 stat" fields through
	// type_spellvalue::get_best_spell_value, evaluated at the current stats and again
	// at +1.  The only fact the report states about the result is that hero + 0x47E is
	// floored at 10.
	//
	// TODO: get_best_spell_value / type_spellvalue is not specified anywhere in the
	// report, so the counterfactual spellbook re-pricing cannot be reproduced.  Until
	// it is, all three fields collapse onto the documented floor, which makes the AI
	// undervalue Star Axis, Garden of Revelation, Library of Enlightenment, School of
	// Magic and the Tower/Inferno town specials.
	out.valueOfSpellPower = 10;
	out.valueOfKnowledge = 10;
	out.valueOfOther = 10;

	return out;
}

int64_t luckMoraleToAbsolute(const CGHeroInstance * hero, double fraction)
{
	// SS 4.9 - value = value_of_morale(...) * armyGroup::get_AI_value(&hero->army)
	//                * (AI_priority(hero) + 40) / 40
	return static_cast<int64_t>(fraction * static_cast<double>(armyAIValue(hero)) * priorityScale(hero));
}

bool isShooter(const CCreature * creature)
{
	// SS 4E.2 - traits + 0x10 bit 2 is SHOOTING; the report proves it matches
	// crtraits.txt's "Shots > 0" column exactly across all 150 creatures.
	return creature != nullptr && creature->hasBonusOfType(BonusType::SHOOTER);
}

bool isAlignmentFree(const CCreature * creature)
{
	// SS 4E.2 - traits + 0x10 bit 17 (0x20000): the creatures with no morale and no
	// town, i.e. all elementals, all golems, all undead and all five war machines.
	// SS 4B.4 additionally exempts creature types 78 and 79 (Minotaur / Minotaur King)
	// by hard-coded id.
	if(creature == nullptr)
		return false;

	// SS 4E.3 - creature ids 78 / 79 are Minotaur / Minotaur King; the original tests
	// the raw ids here, not a flag bit, so the numbers are reproduced literally.
	if(creature->getId().getNum() == 78 || creature->getId().getNum() == 79)
		return true;

	return creature->hasBonusOfType(BonusType::UNDEAD)
		|| creature->hasBonusOfType(BonusType::NON_LIVING)
		|| creature->hasBonusOfType(BonusType::MECHANICAL)
		|| creature->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool isFlying(const CCreature * creature)
{
	// SS 4E.2 - traits + 0x10 bit 1: flying.
	return creature != nullptr && creature->hasBonusOfType(BonusType::FLYING);
}

}
