/*
 * PhilValuation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilValuation.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"
#include "../../lib/mapObjects/army/CStackInstance.h"

#include <cmath>

namespace PhilAI
{

int SimArmy::totalAIValue() const
{
	int total = 0;
	for(const auto & s : stacks)
		if(s.alive && s.count > 0)
			total += s.count * s.aiValue;
	return total;
}

bool SimArmy::anyAlive() const
{
	for(const auto & s : stacks)
		if(s.alive && s.count > 0)
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// III.1 - Measuring strength
// ---------------------------------------------------------------------------

int getArmyAIValue(const CCreatureSet * army)
{
	// H3: armyGroup::get_AI_value armygrp.cpp:785
	// Flat additive sum over up to seven slots, count * per-type AI value. Nothing else.
	if(!army)
		return 0;

	int total = 0;
	for(const auto & slot : army->Slots())
	{
		const CCreature * c = army->getCreature(slot.first);
		if(c)
			total += army->getStackCount(slot.first) * c->getAIValue();
	}
	return total;
}

int getArmyAIValue(const SimArmy & army)
{
	return army.totalAIValue();
}

double getCombatValueModifier(int attack, int defense)
{
	// H3: hero::get_combat_value_modifier hero.cpp:6171
	const double a = 1.0 + Const::COMBAT_MODIFIER_PER_POINT * attack;
	const double d = 1.0 + Const::COMBAT_MODIFIER_PER_POINT * defense;
	return std::sqrt(a * d);
}

double getCombatValueModifier(const CGHeroInstance * hero)
{
	if(!hero)
		return 1.0;

	return getCombatValueModifier(
		hero->getPrimSkillLevel(PrimarySkill::ATTACK),
		hero->getPrimSkillLevel(PrimarySkill::DEFENSE));
}

int getApproximateStrength(const CCreatureSet * army, const CGHeroInstance * hero)
{
	// H3: AI_approximate_strength ai_combat.cpp:1666 - the static read, not the simulation.
	return static_cast<int>(getArmyAIValue(army) * getCombatValueModifier(hero));
}

// ---------------------------------------------------------------------------
// III.6 - Morale & luck as currency
// ---------------------------------------------------------------------------

double valueOfLuckAndMorale(double armyValue, double positiveRate, double negativeRate, int current, int change)
{
	// H3: value_of_luck_and_morale ai_tactical.cpp:34
	// The change is first clamped into the real -3..+3 stat range, then the portion of it
	// that lands on the positive side is priced at positiveRate and the portion on the
	// negative side at negativeRate. Positive and negative are deliberately asymmetric.
	if(change == 0)
		return 0.0;

	if(change > 0)
	{
		if(current >= Const::LUCK_MORALE_CEILING)
			return 0.0;
		if(current + change > Const::LUCK_MORALE_CEILING)
			change = Const::LUCK_MORALE_CEILING - current;

		if(current >= 0)
			return armyValue * positiveRate * change;

		// Crossing zero: the negative part unwinds at the negative rate, the rest accrues.
		if(current + change <= 0)
			return armyValue * negativeRate * -change;

		return armyValue * negativeRate * -current + armyValue * positiveRate * (current + change);
	}

	if(current <= Const::LUCK_MORALE_FLOOR)
		return 0.0;
	if(current + change < Const::LUCK_MORALE_FLOOR)
		change = Const::LUCK_MORALE_FLOOR - current;

	if(current <= 0)
		return armyValue * negativeRate * -change;

	if(current + change >= 0)
		return armyValue * positiveRate * change;

	return armyValue * positiveRate * -current + armyValue * negativeRate * (current + change);
}

double valueOfMorale(double armyValue, int current, int change)
{
	// H3: AI_value_of_morale ai_tactical.cpp:83
	return valueOfLuckAndMorale(armyValue, Const::VALUE_PER_POSITIVE_POINT, Const::VALUE_PER_NEGATIVE_MORALE, current, change);
}

double valueOfLuck(double armyValue, int current, int change)
{
	// H3: AI_value_of_luck ai_tactical.cpp:97
	return valueOfLuckAndMorale(armyValue, Const::VALUE_PER_POSITIVE_POINT, Const::VALUE_PER_NEGATIVE_LUCK, current, change);
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

SimArmy buildSimArmy(const CCreatureSet * army, const CGHeroInstance * hero)
{
	// H3: type_AI_combat_data::initialize_creatures ai_combat.cpp:221
	SimArmy out;

	if(army)
	{
		for(const auto & slot : army->Slots())
		{
			const CCreature * c = army->getCreature(slot.first);
			if(!c)
				continue;

			SimStack s;
			s.creature = c;
			s.count = army->getStackCount(slot.first);
			s.attack = c->getBaseAttack();
			s.defense = c->getBaseDefense();
			s.damageMin = c->getBaseDamageMin();
			s.damageMax = c->getBaseDamageMax();
			s.speed = c->getBaseSpeed();
			s.hitPoints = c->getBaseHitPoints();
			s.hitPointsRemaining = s.hitPoints;
			s.aiValue = c->getAIValue();
			s.shooter = c->hasBonusOfType(BonusType::SHOOTER);
			s.flying = c->hasBonusOfType(BonusType::FLYING);
			s.alive = s.count > 0;
			out.stacks.push_back(s);
		}
	}

	if(hero)
	{
		out.hasHero = true;
		out.attackSkill = hero->getPrimSkillLevel(PrimarySkill::ATTACK);
		out.defenseSkill = hero->getPrimSkillLevel(PrimarySkill::DEFENSE);
		out.spellPower = hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
		out.mana = hero->mana;
	}

	return out;
}

double randomPercent(int lowPercent, int highPercent)
{
	return CRandomGenerator::getDefault().nextInt(lowPercent, highPercent) / 100.0;
}

int randomRoll(int low, int high)
{
	return CRandomGenerator::getDefault().nextInt(low, high);
}

} // namespace PhilAI
