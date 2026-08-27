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

#include "H3SpellValue.h"
#include "H3SpellData.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>

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
	//
	// SS 4.9: experience_for_level (0x4DA420) computes n = level + 1 and indexes from
	// there, so it returns what the NEXT level costs and is positive even at level 0.
	// The original needs no guard and neither does this.
	const int64_t denominator = XP_VALUATION_LEVEL_DIVISOR * experienceForLevel(hero->level);

	if(denominator > 0)
	{
		out.experienceValue = static_cast<float>(XP_VALUATION_BASE + armyAIValue(hero))
			/ static_cast<float>(denominator);
	}

	// SS 4.9b - the five counterfactual probes of type_spellvalue.  Each one mutates a
	// field, re-runs get_best_spell_value over all six categories, and takes the
	// difference against the unmodified baseline.
	H3SpellValue sv(hero);

	if(!sv.valid())
	{
		// No spell book, or the Orb of Inhibition: every probe collapses onto the floor
		// the original applies to two of the five fields.
		out.valueOfSpellPower = 10;
		out.valueOfKnowledge = 10;
		return out;
	}

	const int base = sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES);

	// +1 spell power also advances duration, exactly as in the original.
	sv.spellPower += 1;
	sv.duration += 1;
	out.valueOfSpellPower = std::max(10, sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES) - base);
	sv.spellPower -= 1;
	sv.duration -= 1;

	// +1 duration alone.  Not floored.
	sv.duration += 1;
	out.valueOfSpellDuration = sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES) - base;
	sv.duration -= 1;

	// +30 mana is three points of knowledge, so the result is divided by three.
	sv.mana += 30;
	out.valueOfKnowledge = std::max(10, (sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES) - base) / 3);
	sv.mana -= 30;

	// The two mana-refill numbers the Magic Well and Magic Spring handlers return.
	const int maxMana = sv.mana;
	const int currentMana = hero->mana;

	sv.mana = currentMana;
	const int atCurrent = sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES);

	if(currentMana < maxMana)
	{
		sv.mana = maxMana;
		out.valueOfFullMana = sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES) - atCurrent;
	}

	if(currentMana < 2 * maxMana)
	{
		sv.mana = 2 * maxMana;
		out.valueOfDoubleMana = sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES) - atCurrent;
	}

	sv.mana = maxMana;

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


bool isLiving(const CCreature * creature)
{
	// SS 4E.2 - traits + 0x10 bit 4.  VCMI expresses the same set as "not undead, not a
	// golem-class construct, not a siege engine".
	return creature != nullptr
		&& !creature->hasBonusOfType(BonusType::UNDEAD)
		&& !creature->hasBonusOfType(BonusType::NON_LIVING)
		&& !creature->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

int currentMorale(const CGHeroInstance * hero)
{
	// SS 4.9 - hero::get_morale @ 0x4E39B0.  The original sums the Leadership table
	// {0,1,2,3}, the specialty scaling, five artifacts, a Castle-faction Grail, the
	// accumulated terrain/event modifier at hero + 0x11A, and clamps to [-3, +3].
	// VCMI keeps every one of those as a MORALE bonus, so the total is already correct.
	if(hero == nullptr)
		return 0;

	return std::clamp(hero->valOfBonuses(BonusType::MORALE), -3, 3);
}

int currentLuck(const CGHeroInstance * hero)
{
	// SS 4.9 - hero::get_luck @ 0x4E36C0, the same shape with the Luck table and its
	// own artifact list, no Grail term, and hero + 0x11B as the accumulator.
	if(hero == nullptr)
		return 0;

	return std::clamp(hero->valOfBonuses(BonusType::LUCK), -3, 3);
}

double necromancyFraction(const CGHeroInstance * hero)
{
	// SS 4.9a - hero::necromancy_fraction @ 0x4E3CD0: g_SSNecromancyFactor[skill]
	// {0.00, 0.10, 0.20, 0.30}, scaled by 1 + 0.05*level for a Necromancy specialist,
	// plus the artifact contributions.  VCMI folds all of that into one bonus.
	if(hero == nullptr)
		return 0.0;

	return static_cast<double>(hero->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE)) / 100.0;
}

double firstAidAmount(const CGHeroInstance * hero)
{
	// SS 4.9a - hero::first_aid_amount @ 0x4E4920: g_firstaid_factor[skill]
	// {0.0, 1.0, 2.0, 3.0}, scaled by 1 + 0.05*level for a First Aid specialist, +1.0.
	if(hero == nullptr)
		return 1.0;

	static const double FACTOR[4] = { 0.0, 1.0, 2.0, 3.0 };
	const int skill = std::clamp(static_cast<int>(hero->getSecSkillLevel(SecondarySkill::FIRST_AID)), 0, 3);

	// The original also scales by (1 + 0.05 * level) when the hero is a First Aid
	// specialist (g_heroSpecialty[hero].type == 0 && param == 27).  That branch is left
	// out rather than guessed at: VCMI expresses specialties as bonuses and the exact
	// equivalent is unconfirmed.  Omitting a refinement that applies to a handful of
	// heroes is better than shipping a wrong one.
	return FACTOR[skill] + 1.0;
}

bool spellInSchoolMask(const SpellID & spell, int mask)
{
	// SS 4.9a - 1 Air, 2 Fire, 4 Water, 8 Earth, fixed by Tome of Fire -> TOME(2) and
	// Orb of Tempestuous Fire -> SCHOOL(2, 100).
	const CSpell * data = spell.toSpell();

	if(data == nullptr)
		return false;

	bool hit = false;

	data->forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		int bit = 0;

		if(school == SpellSchool::AIR)        bit = 1;
		else if(school == SpellSchool::FIRE)  bit = 2;
		else if(school == SpellSchool::WATER) bit = 4;
		else if(school == SpellSchool::EARTH) bit = 8;

		if((bit & mask) != 0)
		{
			hit = true;
			stop = true;
		}
	});

	return hit;
}

}
