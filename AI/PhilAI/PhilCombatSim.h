/*
 * PhilCombatSim.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PhilValuation.h"

#include <vector>

class CCreatureSet;
class CGHeroInstance;

namespace PhilAI
{

/// H3: type_speed_catagory (ai_combat.cpp) - how many rounds a stack needs to cross the field.
/// Recovered verbatim from type_AI_combat_data::get_catagory: shooters are their own category,
/// everyone else is (speed - 2*tacticsAdvantage + 14) / speed, clamped to SLOW.
enum class SpeedCategory : int
{
	RANGED = 0,
	VERY_FAST = 1,
	FAST = 2,
	AVERAGE = 3,
	SLOW = 4,
	MAX_CATEGORIES = 5
};

/// H3: type_monster_data (ai_combat.cpp) - one creature type inside a cloned battle side.
struct MonsterData
{
	int index = 0;
	const CCreature * creature = nullptr;
	int number = 0;
	int originalNumber = 0;
	int speed = 0;
	double meleeModifier = 1.0;
	double finalMeleeModifier = 1.0;
	double rangedModifier = 1.0;
	double combatValuePerHit = 1.0;
	SpeedCategory category = SpeedCategory::SLOW;
	int value = 0;       ///< the creature's fight value, not its map AI value
	int totalValue = 0;  ///< value * number
};

/// H3: type_AI_combat_data (ai_combat.cpp:172) - one cloned side of a predicted battle.
/// Stack-constructed per prediction and thrown away; never pooled, exactly as the original.
class CombatData
{
public:
	std::vector<MonsterData> creatures;
	int mana = 0;
	int totalCombatValue = 0;
	int tacticsAdvantage = 0;
	SpeedCategory wallSpeedLimit = SpeedCategory::RANGED;
	bool defendingSiege = false;

	CombatData() = default;

	/// H3: type_AI_combat_data::initialize_creatures ai_combat.cpp:221
	/// Hero skills are netted against the opposing hero's before the modifiers are derived.
	void initializeCreatures(const CCreatureSet * army, const CGHeroInstance * hero, const CGHeroInstance * enemyHero);

	/// H3: type_AI_combat_data::get_catagory ai_combat.cpp:381
	SpeedCategory getCategory(int speed, bool shooter, bool flying) const;

	/// H3: type_AI_combat_data::get_attack ai_combat.cpp:1184
	/// Sums value*number*modifier over every stack whose category is at or below speedLimit.
	int getAttack(SpeedCategory speedLimit, bool shootersBlocked) const;

	/// H3: type_AI_combat_data::get_final_melee_value ai_combat.cpp:1209
	int getFinalMeleeValue() const;

	/// H3: type_AI_combat_data::get_total - surviving combat value.
	int getTotal() const;

	/// H3: type_AI_combat_data::inflict_damage ai_combat.cpp:1166
	void inflictDamage(int damage, SpeedCategory fromCategory);

	/// H3: type_AI_combat_data::kill ai_combat.cpp:1150
	void kill();
};

/// H3: type_AI_combat_data::do_ranged_combat ai_combat.cpp:1224 - simultaneous exchange.
void doRangedCombat(CombatData & attacker, CombatData & defender);

/// H3: type_AI_combat_data::do_melee_combat ai_combat.cpp:1240
void doMeleeCombat(CombatData & attacker, SpeedCategory attackerSpeed, CombatData & defender);

/// H3: type_AI_combat_data::do_general_melee ai_combat.cpp:1270 - the closing exchange.
void doGeneralMelee(CombatData & attacker, CombatData & defender);

/// H3: type_AI_combat_data::choose_melee ai_combat.cpp:1301
/// Tries every "hold the line until round N" plan from SLOW downward and keeps the best
/// outcome. This is the whole go/no-go engine: shooting rounds first, then melee by speed
/// category, then one closing general melee.
int chooseMelee(const CombatData & us, const CombatData & them, SpeedCategory currentRound);

/// H3: AI_value_of_combat ai_combat.cpp:1565
/// The one pure (attacker, defender) -> number primitive. Positive means the AI expects to
/// come out ahead; negative is used unchanged as the danger-map contribution in II.6.
int valueOfCombat(
	const CGHeroInstance * attackingHero,
	const CCreatureSet * attackingArmy,
	const CGHeroInstance * defendingHero,
	const CCreatureSet * defendingArmy,
	int difficulty,
	double attackerBias = 1.0,
	double defenderBias = 1.0);

/// H3: AI_quick_combat ai_combat.cpp:1511 / AI_auto_combat ai_combat.cpp:1539
/// Each side's effective strength is independently scaled by a random 75-125% first.
bool quickCombatAttackerWins(
	const CGHeroInstance * attackingHero,
	const CCreatureSet * attackingArmy,
	const CGHeroInstance * defendingHero,
	const CCreatureSet * defendingArmy);

} // namespace PhilAI
