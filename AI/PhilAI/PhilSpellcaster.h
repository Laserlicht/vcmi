/*
 * PhilSpellcaster.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PhilConstants.h"

#include "../../lib/battle/BattleHex.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <vcmi/spells/Magic.h>

#include <memory>
#include <vector>

class CBattleCallback;
class CGHeroInstance;
class CSpell;
class CStack;

namespace PhilAI
{

/// One scored spell/target pair, the shape of H3's type_spell_choice (ai_tactical.cpp:758).
struct SpellChoice
{
	const CSpell * spell = nullptr;
	const CStack * target = nullptr;
	BattleHex targetHex = BattleHex::INVALID;
	double value = 0.0;
};

/// H3: type_AI_spellcaster (ai_tactical.cpp:793) - the live-combat spell decision object.
///
/// Built once per side when a combat turn's spell phase begins and thrown away afterwards,
/// carrying running state (win_likely, the melee-enemy list) that would be wasteful to
/// recompute per spell. Its ~50 per-effect valuators are almost all thin wrappers around six
/// or seven shared primitives, each following the same three-tier shape: an immunity or
/// relevance gate, a per-spell constant from a data table keyed by mastery, then delegation
/// into one shared primitive.
class Spellcaster
{
	std::shared_ptr<CBattleCallback> cb;
	BattleID battleID;
	BattleSide side;
	const CGHeroInstance * hero;

	bool winLikely = false;
	int roundsLeft = Const::SIMULATION_ROUNDS;
	int ourCombatValue = 0;
	int enemyCombatValue = 0;

	/// H3: type_AI_spellcaster::set_melee_enemies ai_tactical.cpp:3191 - the enemies this side
	/// is about to trade blows with, capped at 20 entries in the original.
	std::vector<const CStack *> meleeEnemies;

	void initialize();

	/// H3: type_AI_spellcaster::spells_not_required ai_tactical.cpp:3398
	/// Skips almost every spell if a fresh pre-battle simulation predicts a clean win where
	/// nothing of ours dies - Resurrection and Animate Dead excepted.
	bool spellsNotRequired() const;

	/// H3: type_AI_spellcaster::should_attack_now ai_tactical.cpp:862
	bool shouldAttackNow(const CStack * actor, const CStack * enemy) const;

	// ---- the shared primitives every valuator below delegates into ----

	/// H3: get_damage_value ai_tactical.cpp:912 - damage converted into combat-value loss,
	/// capped at the target's health, with a bonus when the target cannot retaliate.
	double getDamageValue(const CStack * target, int damage) const;

	/// H3: get_group_damage_value ai_tactical.cpp:965
	double getGroupDamageValue(const CSpell * spell, bool enemySide) const;

	/// H3: get_mass_damage_effect ai_tactical.cpp:987 - ratio gate, not absolute damage: the
	/// spell only scores positively if our own proportional share of the splash is smaller
	/// than the enemy's.
	double getMassDamageEffect(const CSpell * spell) const;

	/// H3: get_attack_boost_value ai_tactical.cpp:1158 - values increased expected damage
	/// output only where the buff actually raises it, with sqrt diminishing returns.
	double getAttackBoostValue(const CStack * target, double bonus) const;

	/// H3: get_defense_boost_value ai_tactical.cpp:1429 - values the reduction in enemy
	/// expected damage, only where a lethal risk is actually detected.
	double getDefenseBoostValue(const CStack * target, double bonus) const;

	/// H3: get_traitor_value ai_tactical.cpp:2360 - shared by Berserk and Hypnotize.
	double getTraitorValue(const CStack * target) const;

	/// H3: get_protection_value ai_tactical.cpp:1975 - enumerates the enemy hero's actual
	/// spellbook and prices the single worst spell this protection would block.
	double getProtectionValue(SpellSchool school) const;

	/// H3: get_speed_value ai_tactical.cpp:1902
	double getSpeedValue(const CStack * target, int speedIncrease) const;

	/// H3: consider_resurrect ai_tactical.cpp:2608 - stays active even in an already-won fight.
	double getResurrectValue(const CStack * target) const;

	/// The per-effect dispatch. Anything not wired in falls through to a flat zero, exactly as
	/// type_AI_spellcaster::unimplemented (ai_tactical.cpp:2876) does.
	double valueOfSpellOnTarget(const CSpell * spell, const CStack * target) const;

public:
	Spellcaster(std::shared_ptr<CBattleCallback> callback, const BattleID & id, BattleSide s, const CGHeroInstance * h);

	/// H3: type_AI_spellcaster::consider_spell ai_tactical.cpp:3114 followed by
	/// type_AI_spellcaster::cast_spell ai_tactical.cpp:3420 - score every castable spell, then
	/// reshape by mana affordability and apply the final random 75-100% re-roll.
	bool considerSpell(const CStack * activeStack, bool retreating);
};

/// H3: type_spellvalue::get_best_spell_value philai.cpp:1632, reached from has_ranged_advantage
/// to count a caster's best damage spell as ranged firepower.
int bestDamageSpellValue(const CGHeroInstance * hero, int stackValue);

} // namespace PhilAI
