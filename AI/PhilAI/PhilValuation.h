/*
 * PhilValuation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PhilConstants.h"

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/GameConstants.h"

#include <array>
#include <vector>

class CCreature;
class CCreatureSet;
class CGHeroInstance;
class CStackInstance;

namespace PhilAI
{

/// One army side reduced to plain numbers, matching the shape of type_AI_combat_data's
/// per-monster-type vector. All simulation below runs on this, never on live game state.
struct SimStack
{
	const CCreature * creature = nullptr;
	int count = 0;
	int hitPointsRemaining = 0;
	int attack = 0;
	int defense = 0;
	int damageMin = 0;
	int damageMax = 0;
	int speed = 0;
	int hitPoints = 0;
	int aiValue = 0;
	bool shooter = false;
	bool flying = false;
	bool alive = true;
};

struct SimArmy
{
	std::vector<SimStack> stacks;
	int attackSkill = 0;   ///< hero Attack, 0 without a hero
	int defenseSkill = 0;  ///< hero Defense, 0 without a hero
	int spellPower = 0;
	int mana = 0;
	bool hasHero = false;
	double biasMultiplier = 1.0; ///< can_take_town's 1.25 / 0.75, or 1.0 elsewhere

	int totalAIValue() const;
	bool anyAlive() const;
};

// ---------------------------------------------------------------------------
// III.1 - Measuring strength
// ---------------------------------------------------------------------------

/// armyGroup::get_AI_value armygrp.cpp:785
/// A flat additive sum: troop count times the creature type's AI value constant, across
/// up to seven slots. No synergy, no counter-unit effects of any kind.
int getArmyAIValue(const CCreatureSet * army);
int getArmyAIValue(const SimArmy & army);

/// hero::get_combat_value_modifier hero.cpp:6171
/// sqrt((1 + 0.05*Attack) * (1 + 0.05*Defense)) - 5% per point, combined geometrically.
double getCombatValueModifier(int attack, int defense);
double getCombatValueModifier(const CGHeroInstance * hero);

/// The quick read the AI uses whenever it needs a fast threat estimate for a garrison or
/// guard: raw army value scaled by the accompanying hero's modifier (1.0 with no hero).
int getApproximateStrength(const CCreatureSet * army, const CGHeroInstance * hero);

// ---------------------------------------------------------------------------
// III.6 - Morale & luck as currency
// ---------------------------------------------------------------------------

/// value_of_luck_and_morale ai_tactical.cpp:34
/// Converts a stat change into the same "value" currency the spell scoring uses.
/// `current` is the stat before the change, `change` the delta. The result is clamped so a
/// single change is never evaluated below -3 or above +3 of final stat.
double valueOfLuckAndMorale(double armyValue, double positiveRate, double negativeRate, int current, int change);

/// AI_value_of_morale ai_tactical.cpp:83 - positive +0.0173/pt, negative -0.0833/pt.
double valueOfMorale(double armyValue, int current, int change);

/// AI_value_of_luck ai_tactical.cpp:97 - positive +0.0173/pt, negative -0.0122/pt.
double valueOfLuck(double armyValue, int current, int change);

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// Builds the plain-number snapshot the simulator works on, from live game state.
SimArmy buildSimArmy(const CCreatureSet * army, const CGHeroInstance * hero);

/// Random(lo, hi) / 100, the exact shape of all four independent randomizers in this AI.
double randomPercent(int lowPercent, int highPercent);

/// Random(1, high) inclusive.
int randomRoll(int low, int high);

} // namespace PhilAI
