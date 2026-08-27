/*
 * H3CombatEstimate.h, part of VCMI engine
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

#include <vector>

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
class CGTownInstance;
class CCreatureSet;
VCMI_LIB_NAMESPACE_END

class CCallback;

namespace H3AI
{

class H3Player;

/// SS 5B.2 - type_monster_data (0x48 bytes), one per simulated stack.
struct MonsterData
{
	int index = 0;
	CreatureID type;
	int number = 0;
	int originalNumber = 0;
	int speed = 0;
	/// per-creature combat value
	int value = 0;
	int64_t totalValue = 0;
	double combatValuePerHit = 0.0;
	/// speed bucket, 0 = shooter .. 4
	int category = 0;
	double meleeModifier = 0.2;
	double finalMeleeModifier = 1.0;
	double rangedModifier = 0.0;
};

/// SS 5B - type_AI_combat_data (ctor 0x423EE0, body 0x424120).  Used both for the
/// adventure-map "should I attack this?" question and for quick-combat resolution.
class CombatData
{
public:
	CombatData(
		const CGHeroInstance * ourHero,
		const CCreatureSet * ourArmy,
		double baseModifier,
		const CGHeroInstance * enemyHero,
		const CGTownInstance * enemyTown);

	/// SS 5B.3 - simulate_combat @ 0x426BC0.
	void simulateCombat(CombatData & defender);

	/// SS 5B.3 - get_attack(speedLimit, shootersBlocked) @ 0x426390.
	int64_t getAttack(int speedLimit, bool shootersBlocked) const;

	/// SS 5B.3 - inflict_damage(damage, blockerSpeed) @ 0x426300.
	void inflictDamage(int64_t damage, int blockerSpeed);

	/// SS 5B.3 - get_final_melee_value @ 0x426450.
	double getFinalMeleeValue() const;

	int getFastestSpeed() const;

	int64_t totalCombatValue = 0;

	const std::vector<MonsterData> & getStacks() const { return stacks; }

	/// SS 4.11 - A.adjust_army(true): the AI value of the army that survived, including
	/// whatever the winner's Necromancy raised out of the loser.
	int64_t survivingArmyAIValue() const;

private:
	/// SS 5B.3 - the necromancy half of do_aftermath @ 0x426EE0.
	void applyNecromancy(const CombatData & loser);

	/// SS 5B.3 - inflict_melee_damage(damage, minCat, maxCat) @ 0x426170.
	int64_t inflictMeleeDamage(int64_t damage, int minCategory, int maxCategory);

	void kill();

	std::vector<MonsterData> stacks;
	/// The AI value of the undead do_aftermath raised for this side.
	int64_t necromancyValue = 0;
	const CGHeroInstance * hero = nullptr;
	const CGHeroInstance * enemyHero = nullptr;
	const CGTownInstance * town = nullptr;
};

/// SS 4.11 - AI_value_of_combat @ 0x427330.  The single most-called AI function:
/// it decides whether the AI attacks anything, and it feeds the danger map (SS 4.6).
///
/// @param defenderArmy the army that would be fought (may belong to a hero, a town
///        garrison or a wandering monster)
int valueOfCombat(
	CCallback * cb,
	const H3Player & player,
	const CGHeroInstance * attacker,
	const CGHeroInstance * defender,
	const CCreatureSet * defenderArmy,
	const CGTownInstance * defenderTown);

}
