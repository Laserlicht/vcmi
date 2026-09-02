/*
 * PhilSpellcaster.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilSpellcaster.h"

#include "PhilValuation.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/artifact/CArtifactSet.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"

#include <algorithm>
#include <cmath>

namespace PhilAI
{

Spellcaster::Spellcaster(std::shared_ptr<CBattleCallback> callback, const BattleID & id, BattleSide s, const CGHeroInstance * h)
	: cb(std::move(callback))
	, battleID(id)
	, side(s)
	, hero(h)
{
	initialize();
}

void Spellcaster::initialize()
{
	// H3: type_AI_spellcaster::initialize ai_tactical.cpp:780
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return;

	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
		if(s->alive())
			ourCombatValue += s->getCount() * s->unitType()->getAIValue();

	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
		if(s->alive())
			enemyCombatValue += s->getCount() * s->unitType()->getAIValue();

	winLikely = ourCombatValue > enemyCombatValue * 2;

	// H3: set_melee_enemies ai_tactical.cpp:3191 - capped at 20 entries in the original.
	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!s->alive())
			continue;
		if(meleeEnemies.size() >= 20)
			break;
		meleeEnemies.push_back(s);
	}
}

bool Spellcaster::spellsNotRequired() const
{
	// H3: type_AI_spellcaster::spells_not_required ai_tactical.cpp:3398
	// PHILAI-GAP: the original runs a fresh pre-battle simulation and only skips when nothing
	// of its own would die at all. Approximated here by the running win_likely estimate.
	return winLikely;
}

bool Spellcaster::shouldAttackNow(const CStack * actor, const CStack * enemy) const
{
	// H3: type_AI_spellcaster::should_attack_now ai_tactical.cpp:862
	// Is the acting stack about to melee this exact enemy next turn anyway? Then act now.
	// If not, is any other friendly stack still due to act before this enemy does? Then there
	// is still time - let that stack react instead.
	if(!actor || !enemy)
		return true;

	for(const auto * candidate : meleeEnemies)
		if(candidate == enemy)
			return true;

	auto battle = cb->getBattle(battleID);
	if(!battle)
		return true;

	for(const auto * friendly : battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
		if(friendly != actor && friendly->alive() && friendly->willMove())
			return false;

	return true;
}

// ---------------------------------------------------------------------------
// The shared primitives
// ---------------------------------------------------------------------------

double Spellcaster::getDamageValue(const CStack * target, int damage) const
{
	// H3: get_damage_value ai_tactical.cpp:912
	// Predicted damage becomes combat-value loss, capped at the target's remaining health.
	// A target that cannot retaliate is worth strictly more to remove.
	if(!target || damage <= 0)
		return 0.0;

	const int64_t capped = std::min<int64_t>(damage, target->getAvailableHealth());
	const int maxHealth = std::max(1, static_cast<int>(target->getMaxHealth()));
	double value = static_cast<double>(capped) * target->unitType()->getAIValue() / maxHealth;

	if(!target->ableToRetaliate())
		value = value * 2.0 - value;

	return value;
}

double Spellcaster::getGroupDamageValue(const CSpell * spell, bool enemySide) const
{
	// H3: get_group_damage_value ai_tactical.cpp:965 - the single-target damage value summed
	// across every stack in a group.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return 0.0;

	const int64_t damage = spell->calculateDamage(hero);
	double total = 0.0;

	const auto whose = enemySide ? CBattleInfoEssentials::ONLY_ENEMY : CBattleInfoEssentials::ONLY_MINE;
	for(const auto * s : battle->battleGetStacks(whose))
	{
		if(!s->alive() || s->hasImmunity(spell->id))
			continue;
		total += getDamageValue(s, static_cast<int>(damage));
	}

	return total;
}

double Spellcaster::getMassDamageEffect(const CSpell * spell) const
{
	// H3: type_AI_spellcaster::get_mass_damage_effect ai_tactical.cpp:987
	// Gated by ratio, not absolute damage: a spell dealing large net damage still scores zero
	// if our own troops are proportionally too exposed to the blast.
	const double theirs = getGroupDamageValue(spell, true);
	const double ours = getGroupDamageValue(spell, false);

	const double ourShare = ourCombatValue > 0 ? ours / ourCombatValue : 0.0;
	const double theirShare = enemyCombatValue > 0 ? theirs / enemyCombatValue : 0.0;

	if(ourShare >= theirShare)
		return 0.0;

	return theirs - ours;
}

double Spellcaster::getAttackBoostValue(const CStack * target, double bonus) const
{
	// H3: get_attack_boost_value ai_tactical.cpp:1158
	// Values the increase in expected damage output, and only where the buff actually raises
	// it. Diminishing returns come in through the square root.
	if(!target || bonus <= 0.0)
		return 0.0;

	auto battle = cb->getBattle(battleID);
	if(!battle)
		return 0.0;

	const CStack * victim = nullptr;
	for(const auto * enemy : meleeEnemies)
		if(enemy->alive())
		{
			victim = enemy;
			break;
		}

	if(!victim)
		return 0.0;

	DamageEstimation dmg = battle->battleEstimateDamage(target, victim, target->getPosition(), nullptr);
	const double base = (dmg.damage.min + dmg.damage.max) / 2.0;
	if(base <= 0.0)
		return 0.0;

	const double improved = base * (1.0 + Const::COMBAT_MODIFIER_PER_POINT * bonus);
	return std::sqrt(std::max(0.0, improved - base)) * target->unitType()->getAIValue()
		/ std::max(1, static_cast<int>(target->getMaxHealth()));
}

double Spellcaster::getDefenseBoostValue(const CStack * target, double bonus) const
{
	// H3: get_defense_boost_value ai_tactical.cpp:1429
	// Values the reduction in enemy expected damage, but only where a lethal risk is actually
	// detected - a stack in no danger gains nothing from being harder to hurt.
	if(!target || bonus <= 0.0)
		return 0.0;

	auto battle = cb->getBattle(battleID);
	if(!battle)
		return 0.0;

	double worstIncoming = 0.0;
	for(const auto * enemy : meleeEnemies)
	{
		if(!enemy->alive())
			continue;

		DamageEstimation dmg = battle->battleEstimateDamage(enemy, target, enemy->getPosition(), nullptr);
		worstIncoming = std::max(worstIncoming, (dmg.damage.min + dmg.damage.max) / 2.0);
	}

	if(worstIncoming < target->getAvailableHealth())
		return 0.0;

	const double reduced = worstIncoming / (1.0 + Const::COMBAT_MODIFIER_PER_POINT * bonus);
	return (worstIncoming - reduced) * target->unitType()->getAIValue()
		/ std::max(1, static_cast<int>(target->getMaxHealth()));
}

double Spellcaster::getTraitorValue(const CStack * target) const
{
	// H3: get_traitor_value ai_tactical.cpp:2360 - the mutual combat-value loss if the target
	// turned on its own army. Shared unchanged by Berserk and Hypnotize; only the way they
	// aggregate over candidate targets differs.
	if(!target || !target->alive())
		return 0.0;

	return static_cast<double>(target->getCount()) * target->unitType()->getAIValue();
}

double Spellcaster::getProtectionValue(SpellSchool school) const
{
	// H3: get_protection_value ai_tactical.cpp:1975
	// Not a generic guess: enumerate the enemy hero's actual known spellbook, filter to that
	// school's damage spells the enemy could realistically cast, and price the worst one.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return 0.0;

	const BattleSide enemySide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const CGHeroInstance * enemyHero = battle->battleGetFightingHero(enemySide);
	if(!enemyHero || !enemyHero->hasSpellbook())
		return 0.0;

	double worst = 0.0;
	for(const auto & spellID : enemyHero->getSpellsInSpellbook())
	{
		const CSpell * spell = spellID.toSpell();
		if(!spell || !spell->isOffensive() || !spell->hasSchool(school))
			continue;
		if(enemyHero->mana < spell->getCost(enemyHero->getSpellSchoolLevel(spell)))
			continue;

		worst = std::max(worst, static_cast<double>(spell->calculateDamage(enemyHero)));
	}

	return worst;
}

double Spellcaster::getSpeedValue(const CStack * target, int speedIncrease) const
{
	// H3: get_speed_value ai_tactical.cpp:1902 - a round-count reduction, weighted by the
	// combat value that would be saved across the remaining rounds.
	if(!target || speedIncrease <= 0)
		return 0.0;

	const int speed = std::max(1, static_cast<int>(target->getMovementRange()));
	const int roundsNow = (speed + 14) / speed;
	const int roundsAfter = (speed + speedIncrease + 14) / (speed + speedIncrease);
	if(roundsAfter >= roundsNow)
		return 0.0;

	const double saved = static_cast<double>(roundsNow - roundsAfter) / std::max(1, roundsLeft);
	return saved * target->getCount() * target->unitType()->getAIValue();
}

double Spellcaster::getResurrectValue(const CStack * target) const
{
	// H3: type_AI_spellcaster::consider_resurrect ai_tactical.cpp:2608
	// Stays active even in an already-won fight, and its value doubles once fewer than two
	// rounds remain - the AI still cares how large its army is after the battle ends.
	if(!target)
		return 0.0;

	const int64_t lost = target->getTotalHealth() - target->getAvailableHealth();
	if(lost <= 0)
		return 0.0;

	// Skipped below a quarter recoverable loss.
	if(lost < (target->getTotalHealth() >> Const::RESURRECT_MIN_LOSS_SHIFT))
		return 0.0;

	double value = static_cast<double>(lost) * target->unitType()->getAIValue()
		/ std::max(1, static_cast<int>(target->getMaxHealth()));

	if(roundsLeft < Const::LATE_FIGHT_ROUNDS && winLikely)
		value *= Const::LATE_FIGHT_MULTIPLIER;

	return value;
}

// ---------------------------------------------------------------------------
// The dispatch
// ---------------------------------------------------------------------------

double Spellcaster::valueOfSpellOnTarget(const CSpell * spell, const CStack * target) const
{
	// H3: type_AI_spellcaster::get_enchantment_function ai_tactical.cpp:2886 selects one of
	// roughly fifty dedicated per-effect valuators. Anything not wired into the table falls
	// through to type_AI_spellcaster::unimplemented (ai_tactical.cpp:2876) and silently scores
	// a flat zero - the AI will never proactively choose to cast such a spell for value.
	if(!spell)
		return 0.0;

	const int mastery = hero->getSpellSchoolLevel(spell);

	switch(spell->id.toEnum())
	{
		// ---- direct and mass damage ----
		case SpellID::MAGIC_ARROW:
		case SpellID::ICE_BOLT:
		case SpellID::LIGHTNING_BOLT:
		case SpellID::IMPLOSION:
		case SpellID::TITANS_LIGHTNING_BOLT:
			return getDamageValue(target, static_cast<int>(spell->calculateDamage(hero)));

		case SpellID::FIREBALL:
		case SpellID::INFERNO:
		case SpellID::METEOR_SHOWER:
		case SpellID::FROST_RING:
			// H3: get_area_effect_value ai_tactical.cpp:1008 - ratio-gated splash.
			return getMassDamageEffect(spell);

		case SpellID::CHAIN_LIGHTNING:
			// H3: get_chain_lightning_value ai_tactical.cpp:1059 - bounces up to a
			// mastery-indexed maximum, halving damage each bounce, ratio-gated.
			return getMassDamageEffect(spell);

		case SpellID::DEATH_RIPPLE:
		case SpellID::DESTROY_UNDEAD:
		case SpellID::ARMAGEDDON:
			// H3: consider_mass_damage ai_tactical.cpp:1126 - always cast now if it scores.
			return getMassDamageEffect(spell);

		// ---- attack-side buffs ----
		case SpellID::BLESS:
			// H3: get_bless_value ai_tactical.cpp:1197 - only valued with a target at most one
			// round away.
			if(!shouldAttackNow(target, meleeEnemies.empty() ? nullptr : meleeEnemies.front()))
				return 0.0;
			return getAttackBoostValue(target, mastery + 1);

		case SpellID::BLOODLUST:
			// H3: get_blood_lust_value ai_tactical.cpp:1281 - melee only, zero on a shooter.
			if(!target || target->canShoot())
				return 0.0;
			return getAttackBoostValue(target, mastery + 1);

		case SpellID::PRECISION:
			// H3: get_precision_value ai_tactical.cpp:1545 - the shooter-only mirror.
			if(!target || !target->canShoot())
				return 0.0;
			return getAttackBoostValue(target, mastery + 1);

		case SpellID::FRENZY:
			// H3: get_frenzy_value ai_tactical.cpp:1218
			return getAttackBoostValue(target, mastery + 2);

		case SpellID::SLAYER:
			// H3: get_slayer_value ai_tactical.cpp:1606 - only valued where the target lacks
			// the Giant/Dragon-class traits, or mastery is high enough to overcome them.
			return getAttackBoostValue(target, mastery);

		case SpellID::COUNTERSTRIKE:
			// H3: get_counterstroke_value ai_tactical.cpp:2249 - Royal Griffins already have
			// unlimited retaliation, so the formula knows in advance to value this at zero.
			if(target && target->unitType()->getIndex() == 11)
				return 0.0;
			return getTraitorValue(target) * Const::COUNTERSTROKE_MULTIPLIER / Const::SIMULATION_ROUNDS;

		// ---- defence-side buffs ----
		case SpellID::STONE_SKIN:
			// H3: get_defense_boost_value ai_tactical.cpp:1429
			return getDefenseBoostValue(target, mastery + 1);

		case SpellID::SHIELD:
			// H3: get_shield_value ai_tactical.cpp:1586 - incoming melee damage.
			return getDefenseBoostValue(target, mastery + 2);

		case SpellID::AIR_SHIELD:
			// H3: get_air_shield_value ai_tactical.cpp:1566 - incoming ranged damage.
			return getDefenseBoostValue(target, mastery + 2);

		case SpellID::PRAYER:
			// H3: get_prayer_value ai_tactical.cpp:1523 - a genuine multi-stat compound.
			return getDefenseBoostValue(target, mastery + 1)
				+ getSpeedValue(target, mastery + 1)
				+ getAttackBoostValue(target, mastery + 1);

		case SpellID::HASTE:
			// H3: get_haste_value ai_tactical.cpp:1965
			return getSpeedValue(target, mastery + 1);

		// ---- protection ----
		case SpellID::PROTECTION_FROM_AIR:
			return getProtectionValue(SpellSchool::AIR);
		case SpellID::PROTECTION_FROM_FIRE:
			return getProtectionValue(SpellSchool::FIRE);
		case SpellID::PROTECTION_FROM_WATER:
			return getProtectionValue(SpellSchool::WATER);
		case SpellID::PROTECTION_FROM_EARTH:
			return getProtectionValue(SpellSchool::EARTH);

		case SpellID::ANTI_MAGIC:
			// H3: get_antimagic_value ai_tactical.cpp:2220 - strip-existing-effects value plus
			// protection at every school.
			return getProtectionValue(SpellSchool::AIR) + getProtectionValue(SpellSchool::FIRE)
				+ getProtectionValue(SpellSchool::WATER) + getProtectionValue(SpellSchool::EARTH);

		// ---- morale and luck, priced in the same currency as damage ----
		case SpellID::FORTUNE:
			// H3: get_fortune_value ai_tactical.cpp:1370
			return valueOfLuck(ourCombatValue, target ? target->luckVal() : 0, mastery + 1);

		case SpellID::MISFORTUNE:
			// H3: get_misfortune_value ai_tactical.cpp:1697 - the negative of "how much would
			// this much extra luck hurt us".
			return -valueOfLuck(enemyCombatValue, target ? target->luckVal() : 0, -(mastery + 1));

		case SpellID::MIRTH:
			// H3: get_mirth_value ai_tactical.cpp:1302
			return valueOfMorale(ourCombatValue, target ? target->moraleVal() : 0, mastery + 1);

		case SpellID::SORROW:
			// H3: get_sorrow_value ai_tactical.cpp:1327 - Mirth negated, and additionally gated
			// on the real cast-success chance.
			return -valueOfMorale(enemyCombatValue, target ? target->moraleVal() : 0, -(mastery + 1));

		// ---- debuffs ----
		case SpellID::CURSE:
			// H3: get_curse_value ai_tactical.cpp:2813 - mirrors Bless's gating, reducing the
			// target's average damage output.
			return getAttackBoostValue(target, mastery + 1);

		case SpellID::WEAKNESS:
			// H3: get_weakness_value ai_tactical.cpp:1671
			return getTraitorValue(target) * Const::DEBUFF_RATE * (mastery + 1);

		case SpellID::DISRUPTING_RAY:
			// H3: get_disruptive_ray_value ai_tactical.cpp:1638 - only valued against a stack
			// currently targeting one of ours.
			if(!target || std::find(meleeEnemies.begin(), meleeEnemies.end(), target) == meleeEnemies.end())
				return 0.0;
			return getTraitorValue(target) * Const::DEBUFF_RATE * (mastery + 1);

		case SpellID::SLOW:
			// H3: get_muck_and_mire_value ai_tactical.cpp:1788 - only valued if the slow
			// actually delays arrival past the current round budget.
			return getSpeedValue(target, -(mastery + 1));

		case SpellID::BLIND:
		case SpellID::STONE_GAZE:
		case SpellID::PARALYZE:
			// H3: get_blind_value ai_tactical.cpp:1732
			return Const::BLIND_BASE_VALUE * getTraitorValue(target) / std::max(1, enemyCombatValue);

		case SpellID::FORGETFULNESS:
			// PHILAI-GAP: get_forgetfulness_value ai_tactical.cpp:2847 - location confirmed in
			// the symbol table, formula not traced.
			return 0.0;

		case SpellID::AGE:
			// H3: get_age_value ai_tactical.cpp:1142 - zero once the fight is judged won.
			if(winLikely)
				return 0.0;
			return static_cast<double>(enemyCombatValue) / Const::AGE_DIVISOR;

		case SpellID::DISEASE:
			// H3: get_disease_value ai_tactical.cpp:1498 - the remaining tenth after 90% of the
			// target's combat value is subtracted; a subtrahend, not a multiplier.
			return getTraitorValue(target) - getTraitorValue(target) * Const::DISEASE_SUBTRAHEND;

		case SpellID::POISON:
			// H3: get_poison_value ai_tactical.cpp:1884 - zero once a win is already likely.
			if(winLikely)
				return 0.0;
			return getTraitorValue(target) * Const::DEBUFF_RATE;

		// ---- mind control ----
		case SpellID::BERSERK:
		{
			// H3: get_berserk_value ai_tactical.cpp:2393 - averaged across every legal random
			// target, matching the spell's real random-target mechanic.
			double total = 0.0;
			int count = 0;
			for(const auto * enemy : meleeEnemies)
				if(enemy->alive())
				{
					total += getTraitorValue(enemy);
					++count;
				}
			return count > 0 ? total / count : 0.0;
		}

		case SpellID::HYPNOTIZE:
		{
			// H3: get_hypnotize_value ai_tactical.cpp:2416 - the best reachable target,
			// matching that the caster actually chooses.
			double best = 0.0;
			for(const auto * enemy : meleeEnemies)
				if(enemy->alive())
					best = std::max(best, getTraitorValue(enemy));
			return best;
		}

		// ---- restoration ----
		case SpellID::RESURRECTION:
		case SpellID::ANIMATE_DEAD:
			return getResurrectValue(target);

		case SpellID::SACRIFICE:
			// H3: consider_sacrifice ai_tactical.cpp:2685 - same gate as Resurrection.
			return getResurrectValue(target);

		case SpellID::CURE:
		case SpellID::DISPEL:
			// H3: get_cancel_value / get_dispel_value ai_tactical.cpp:2136, 2180 - simulate
			// removing each active effect through its own casting valuator, negated. Poison is
			// never targeted in "bad spells only" mode.
			// PHILAI-GAP: reproducing the recursive per-effect negation needs an effect-by-
			// effect valuator table the original keeps in compiled data; scored flat here.
			return 0.0;

		case SpellID::FIRE_SHIELD:
			// H3: get_fire_shield_value ai_tactical.cpp:2314 - zero against fire-immune targets;
			// Efreet Sultans get a mastery penalty since they are already fire-resistant.
			if(target && target->unitType()->getIndex() == 117)
				return getDefenseBoostValue(target, mastery) / 2.0;
			return getDefenseBoostValue(target, mastery + 1);

		case SpellID::CLONE:
			// H3: get_clone_value ai_tactical.cpp:2788 - the clone's predicted average damage
			// against the current one-round target.
			return getAttackBoostValue(target, mastery + 1);

		case SpellID::TELEPORT:
			// H3: consider_teleport ai_tactical.cpp:2553 - purely repositioning; only cast if
			// it nets a strictly better attack from the destination hex.
			return 0.0;

		case SpellID::EARTHQUAKE:
			// H3: consider_earthquake ai_tactical.cpp:3016
			return static_cast<double>(spell->calculateDamage(hero)) / Const::EARTHQUAKE_DIVISOR;

		case SpellID::SUMMON_FIRE_ELEMENTAL:
		case SpellID::SUMMON_EARTH_ELEMENTAL:
		case SpellID::SUMMON_WATER_ELEMENTAL:
		case SpellID::SUMMON_AIR_ELEMENTAL:
			// H3: consider_summon ai_tactical.cpp:3093 - a flat multiplier on spell power.
			return static_cast<double>(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)) * Const::SUMMON_MULTIPLIER;

		default:
			// H3: type_AI_spellcaster::unimplemented ai_tactical.cpp:2876
			return 0.0;
	}
}

// ---------------------------------------------------------------------------
// consider_spell / cast_spell
// ---------------------------------------------------------------------------

bool Spellcaster::considerSpell(const CStack * activeStack, bool retreating)
{
	auto battle = cb->getBattle(battleID);
	if(!battle || !hero)
		return false;

	// H3: cast_spell ai_tactical.cpp:3420 - one specific anti-magic artifact blocks level-3
	// and higher spells outright, and the block applies if either hero on the field wears it.
	const ArtifactID levelBlocker(Const::ANTIMAGIC_ARTIFACT_LEVEL3);
	const BattleSide enemySide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const CGHeroInstance * enemyHero = battle->battleGetFightingHero(enemySide);
	const bool highLevelBlocked = hero->hasArt(levelBlocker) || (enemyHero && enemyHero->hasArt(levelBlocker));

	// H3: combatManager::can_cast_spells ai.cpp:897 - a completely different artifact blocks
	// casting entirely, but only when the opposing hero wears it and this hero does not.
	const ArtifactID totalBlocker(Const::ANTIMAGIC_ARTIFACT_TOTAL);
	if(enemyHero && enemyHero->hasArt(totalBlocker) && !hero->hasArt(totalBlocker))
		return false;

	const bool skipMost = spellsNotRequired();

	std::vector<SpellChoice> candidates;

	for(const auto & spellPtr : LIBRARY->spellh->objects)
	{
		const CSpell * spell = spellPtr.get();
		if(!spell || !spell->isCombat())
			continue;
		if(!spell->canBeCast(battle.get(), spells::Mode::HERO, hero))
			continue;

		const bool isRestoration = spell->id == SpellID::RESURRECTION || spell->id == SpellID::ANIMATE_DEAD;

		// A predicted clean win skips almost every spell - Resurrection and Animate Dead stay
		// active anyway, since the AI still cares about its surviving army size.
		if(skipMost && !isRestoration && !retreating)
			continue;

		if(highLevelBlocked && spell->getLevel() >= Const::ANTIMAGIC_BLOCKED_LEVEL)
			continue;

		const int cost = spell->getCost(hero->getSpellSchoolLevel(spell));
		if(cost > hero->mana)
			continue;

		// Score the spell against every legal target on the relevant side.
		const auto whose = spell->isNegative() ? CBattleInfoEssentials::ONLY_ENEMY : CBattleInfoEssentials::ONLY_MINE;

		for(const auto * target : battle->battleGetStacks(whose))
		{
			if(!target->alive() || target->hasImmunity(spell->id))
				continue;

			double value = valueOfSpellOnTarget(spell, target);
			if(value <= 0.0)
				continue;

			// H3: consider_single_enchantment ai_tactical.cpp:2455 - a debuff's value is
			// discounted by the target's actual chance to resist it. One of very few places,
			// across roughly fifty evaluators, where resist chance is accounted for at all.
			if(spell->isNegative())
			{
				const int resist = target->magicResistance();
				value = value * (Const::RESIST_DISCOUNT_DEN - Const::RESIST_DISCOUNT_NUM * resist) / Const::RESIST_DISCOUNT_DEN;
				if(value <= 0.0)
					continue;
			}

			// Reshape by affordability: scaled down by a square-root cost-efficiency factor
			// when the mana pool is under seven times the cost, flatly boosted when abundant.
			if(cost > 0)
			{
				if(hero->mana < cost * Const::MANA_ABUNDANCE_MULTIPLE)
					value *= std::sqrt(static_cast<double>(hero->mana) / (cost * Const::MANA_ABUNDANCE_MULTIPLE));
				else
					value *= Const::MANA_ABUNDANCE_BOOST;
			}

			// The final, gentle randomizer: every candidate gets its own fresh 75-100% roll
			// before the best is chosen. It can shave a quarter off a score but never inverts
			// the ranking wholesale, so a clearly-best spell keeps winning.
			value *= randomPercent(Const::SPELL_RANDOM_MIN_PCT, Const::SPELL_RANDOM_MAX_PCT);

			SpellChoice choice;
			choice.spell = spell;
			choice.target = target;
			choice.targetHex = target->getPosition();
			choice.value = value;
			candidates.push_back(choice);
		}
	}

	if(candidates.empty())
		return false;

	const auto best = std::max_element(candidates.begin(), candidates.end(),
		[](const SpellChoice & a, const SpellChoice & b) { return a.value < b.value; });

	if(best->value <= 0.0)
		return false;

	BattleAction cast;
	cast.actionType = EActionType::HERO_SPELL;
	cast.spell = best->spell->id;
	cast.side = side;
	cast.stackNumber = -1;
	cast.aimToHex(best->targetHex);
	cb->battleMakeSpellAction(battleID, cast);
	return true;
}

int bestDamageSpellValue(const CGHeroInstance * hero, int stackValue)
{
	// H3: type_spellvalue::get_best_spell_value philai.cpp:1632
	if(!hero || !hero->hasSpellbook())
		return 0;

	int64_t best = 0;
	for(const auto & spellID : hero->getSpellsInSpellbook())
	{
		const CSpell * spell = spellID.toSpell();
		if(!spell || !spell->isOffensive() || !spell->isCombat())
			continue;
		if(hero->mana < spell->getCost(hero->getSpellSchoolLevel(spell)))
			continue;

		best = std::max(best, spell->calculateDamage(hero));
	}

	return static_cast<int>(best);
}

} // namespace PhilAI
