/*
 * H3Valuations.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Constants.h"

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/int3.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
class CCreatureSet;
class CCreature;
class CGObjectInstance;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4.9 - the per-hero valuation block the AI recomputes from
/// hero::AI_update_valuations (0x527770).  In the original these live inside the
/// hero record at +0x109 / +0x47E / +0x482 / +0x486.
struct HeroValuations
{
	/// hero + 0x109 - the value of one experience point.
	float experienceValue = 0.0f;
	/// hero + 0x47E - the AI value of +1 spell power (floored at 10).
	int valueOfSpellPower = 0;
	/// hero + 0x482 - the AI value of +1 spell DURATION.  SS 4.9b: the probe that
	/// produces it increments the duration alone, and type_duration_artifact (the Ring
	/// of the Magi) is its only consumer.  An earlier draft labelled this "knowledge".
	int valueOfSpellDuration = 0;
	/// hero + 0x486 - the AI value of +1 knowledge (floored at 10).  This is the field
	/// Garden of Revelation, Library of Enlightenment, Mysticism and Intelligence read.
	int valueOfKnowledge = 0;
	/// hero + 0x48E - the value of refilling mana to the maximum (Magic Well).
	int valueOfFullMana = 0;
	/// hero + 0x48A - the value of mana up to twice the maximum (Magic Spring).
	int valueOfDoubleMana = 0;
};

/// SS 4.1 - hero::get_primary_skill_sum (0x4E5960).  Also the scale factor of every
/// luck/morale object valuation, as (sum + 40) / 40.
int primarySkillSum(const CGHeroInstance * hero);

/// SS 4.9 - the (sum + 40) / 40 factor itself, kept separate because every object
/// handler that touches luck or morale multiplies by it.
double priorityScale(const CGHeroInstance * hero);

/// SS 4.9 - value_of_luck_and_morale (0x435850), verbatim.
double valueOfLuckAndMorale(int current, int change, double good, double bad);

/// SS 4.9 - AI_value_of_morale (0x435830).
double valueOfMorale(int current, int change);

/// SS 4.9 - AI_value_of_luck (0x435960).
double valueOfLuck(int current, int change);

/// SS 4.9 - armyGroup::get_AI_value (0x44AC80): sum of AI_value * count.
int64_t armyAIValue(const CCreatureSet * army);

/// SS 4.9 - experience_for_level (0x4DA420).
/// The report gives one routine and refers to it both as experience_for_level(level)
/// (in the xpValue denominator) and as expForNextLevel(level) (in the object handlers).
int64_t experienceForLevel(int level);

/// SS 4.9 - hero::AI_update_valuations (0x527770).
HeroValuations computeHeroValuations(const CGHeroInstance * hero);

/// SS 4.9 - the standard "fraction of the army" conversion every luck/morale
/// handler performs: fraction * armyValue * (primarySkillSum + 40) / 40.
int64_t luckMoraleToAbsolute(const CGHeroInstance * hero, double fraction);

/// SS 4E.2 - traits + 0x10 bit 2 == SHOOTING, an exact match with "Shots > 0".
bool isShooter(const CCreature * creature);

/// SS 4E.2 - traits + 0x10 bit 17 (0x20000): "has no morale and belongs to no town",
/// the creatures the army planner may mix freely (SS 4B.4).
bool isAlignmentFree(const CCreature * creature);

/// SS 4E.2 - traits + 0x10 bit 1: flying.
bool isFlying(const CCreature * creature);

/// SS 4E.2 - traits + 0x10 bit 4: "living".  The bit the Elixir of Life filters on and
/// the one hero::creature_hp_bonus (0x4E5B80) tests before adding hp/4.
bool isLiving(const CCreature * creature);

/// SS 4.9 - hero::get_morale @ 0x4E39B0, clamped to [-3, +3].
int currentMorale(const CGHeroInstance * hero);

/// SS 4.9 - hero::get_luck @ 0x4E36C0, clamped to [-3, +3].
int currentLuck(const CGHeroInstance * hero);

/// SS 4.9a - hero::necromancy_fraction @ 0x4E3CD0: the share of a defeated army the
/// hero raises, as a fraction (0.10 / 0.20 / 0.30 plus artifacts and specialty).
double necromancyFraction(const CGHeroInstance * hero);

/// SS 4.9a - hero::first_aid_amount @ 0x4E4920, the multiplier the First Aid Tent arm
/// scales by 25.
double firstAidAmount(const CGHeroInstance * hero);

/// SS 4.9a - the school mask the Tomes and Orbs carry: 1 Air, 2 Fire, 4 Water, 8 Earth.
bool spellInSchoolMask(const SpellID & spell, int mask);

}
