/*
 * PhilCombatSim.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilCombatSim.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

#include <algorithm>
#include <cmath>

namespace PhilAI
{

/// The original derives the crossing-time category from a fixed battlefield span of 14.
static constexpr int BATTLEFIELD_SPAN = 14;

/// H3: initialize_creatures ai_combat.cpp:221 - default ranged modifier with no hero present.
static constexpr double RANGED_MODIFIER_NO_HERO = 0.2;
/// H3: initialize_creatures ai_combat.cpp:221 - the two Archery-scaling constants.
static constexpr double ARCHERY_SCALE_HIGH = 5.0;
static constexpr double ARCHERY_SCALE_LOW = 0.7;

SpeedCategory CombatData::getCategory(int speed, bool shooter, bool flying) const
{
	// H3: type_AI_combat_data::get_catagory ai_combat.cpp:381 - recovered verbatim.
	if(shooter)
		return SpeedCategory::RANGED;

	if(speed <= 0)
		return SpeedCategory::SLOW;

	int category = (speed - 2 * tacticsAdvantage + BATTLEFIELD_SPAN) / speed;
	if(category > static_cast<int>(SpeedCategory::SLOW))
		category = static_cast<int>(SpeedCategory::SLOW);

	// A wall keeps ground troops out for a minimum number of rounds; fliers ignore it.
	if(category < static_cast<int>(wallSpeedLimit) && !flying)
		category = static_cast<int>(wallSpeedLimit);

	return static_cast<SpeedCategory>(category);
}

void CombatData::initializeCreatures(const CCreatureSet * army, const CGHeroInstance * hero, const CGHeroInstance * enemyHero)
{
	creatures.clear();
	totalCombatValue = 0;

	// H3: the acting hero's Attack is netted against the enemy hero's Defense and vice versa,
	// so only the surplus over the opponent feeds the geometric modifier.
	int effectiveAttack = 0;
	int effectiveDefense = 0;
	double archeryFactor = 1.0;
	int hitPointBonus = 0;
	int speedBonus = 0;

	if(hero)
	{
		effectiveAttack = hero->getPrimSkillLevel(PrimarySkill::ATTACK);
		effectiveDefense = hero->getPrimSkillLevel(PrimarySkill::DEFENSE);

		if(enemyHero)
		{
			effectiveAttack -= std::min(effectiveAttack, enemyHero->getPrimSkillLevel(PrimarySkill::DEFENSE));
			effectiveDefense -= std::min(effectiveDefense, enemyHero->getPrimSkillLevel(PrimarySkill::ATTACK));
		}

		mana = hero->mana;

		// PHILAI-GAP: hero::GetArcheryFactor's per-mastery table and the exact way the 5.0 / 0.7
		// constants combine with it were not recoverable from the FPU-mangled decompile. The
		// structure (Archery scales only the ranged modifier) is exact.
		const int archery = hero->getSecSkillLevel(SecondarySkill::ARCHERY);
		archeryFactor = 1.0 + ARCHERY_SCALE_LOW * archery / ARCHERY_SCALE_HIGH;
	}

	const double attackModifier = std::sqrt(1.0 + Const::COMBAT_MODIFIER_PER_POINT * effectiveAttack);
	const double defenseModifier = std::sqrt(1.0 + Const::COMBAT_MODIFIER_PER_POINT * effectiveDefense);

	if(!army)
		return;

	int index = 0;
	for(const auto & slot : army->Slots())
	{
		const CCreature * c = army->getCreature(slot.first);
		if(!c)
			continue;

		MonsterData m;
		m.index = index++;
		m.creature = c;
		m.number = army->getStackCount(slot.first);
		m.originalNumber = m.number;
		m.speed = c->getBaseSpeed() + speedBonus;
		// The simulation prices a creature by its fight value, not by the map-facing AI value.
		m.value = c->getFightValue();
		m.totalValue = m.value * m.number;

		const bool shooter = c->hasBonusOfType(BonusType::SHOOTER);
		const bool flying = c->hasBonusOfType(BonusType::FLYING);
		m.category = getCategory(m.speed, shooter, flying);

		m.meleeModifier = attackModifier;
		m.finalMeleeModifier = attackModifier * defenseModifier;
		m.rangedModifier = shooter ? attackModifier * archeryFactor : RANGED_MODIFIER_NO_HERO;

		const int hp = c->getBaseHitPoints() + hitPointBonus;
		m.combatValuePerHit = hp > 0 ? static_cast<double>(m.value) / hp : m.value;

		totalCombatValue += m.totalValue;
		creatures.push_back(m);
	}
}

int CombatData::getAttack(SpeedCategory speedLimit, bool shootersBlocked) const
{
	// H3: type_AI_combat_data::get_attack ai_combat.cpp:1184 - recovered verbatim.
	double total = 0.0;
	for(const auto & m : creatures)
	{
		if(static_cast<int>(m.category) > static_cast<int>(speedLimit))
			continue;

		const bool shootingNow = !shootersBlocked && m.category == SpeedCategory::RANGED;
		total += static_cast<double>(m.value) * m.number * (shootingNow ? m.rangedModifier : m.meleeModifier);
	}
	return static_cast<int>(total);
}

int CombatData::getFinalMeleeValue() const
{
	// H3: type_AI_combat_data::get_final_melee_value ai_combat.cpp:1209
	double total = 0.0;
	for(const auto & m : creatures)
		total += static_cast<double>(m.totalValue) * m.finalMeleeModifier;
	return static_cast<int>(total);
}

int CombatData::getTotal() const
{
	int total = 0;
	for(const auto & m : creatures)
		total += m.value * m.number;
	return total;
}

void CombatData::kill()
{
	// H3: type_AI_combat_data::kill ai_combat.cpp:1150
	for(auto & m : creatures)
	{
		m.number = 0;
		m.totalValue = 0;
	}
}

void CombatData::inflictDamage(int damage, SpeedCategory fromCategory)
{
	// H3: type_AI_combat_data::inflict_damage ai_combat.cpp:1166 - damage is spent against
	// engaged stacks, converted into losses through each stack's combat value per hit point.
	// PHILAI-GAP: the exact order the original walks its stacks in (and whether partial
	// casualties round up or down) was not recoverable; losses are taken slowest-first here.
	if(damage <= 0)
		return;

	for(auto it = creatures.rbegin(); it != creatures.rend() && damage > 0; ++it)
	{
		if(static_cast<int>(it->category) < static_cast<int>(fromCategory))
			continue;
		if(it->number <= 0)
			continue;

		const double perCreature = it->combatValuePerHit > 0.0 ? it->combatValuePerHit : 1.0;
		int killable = static_cast<int>(damage / perCreature);
		if(killable <= 0)
			break;

		const int killed = std::min(killable, it->number);
		it->number -= killed;
		it->totalValue = it->value * it->number;
		damage -= static_cast<int>(killed * perCreature);
	}

	if(getTotal() <= 0)
		kill();
}

void doRangedCombat(CombatData & attacker, CombatData & defender)
{
	// H3: type_AI_combat_data::do_ranged_combat ai_combat.cpp:1224 - both sides shoot at once,
	// so the damage figures are computed before either side takes losses.
	const int attackerDamage = attacker.getAttack(SpeedCategory::RANGED, false);
	const int defenderDamage = defender.getAttack(SpeedCategory::RANGED, false);

	attacker.inflictDamage(defenderDamage, SpeedCategory::RANGED);
	defender.inflictDamage(attackerDamage, SpeedCategory::RANGED);
}

void doMeleeCombat(CombatData & attacker, SpeedCategory attackerSpeed, CombatData & defender)
{
	// H3: type_AI_combat_data::do_melee_combat ai_combat.cpp:1240
	// The attacker commits everything up to attackerSpeed; the defender answers with its whole
	// force but with its shooters blocked, since melee has arrived on top of them.
	const int attackerDamage = attacker.getAttack(attackerSpeed, false);
	const int defenderDamage = defender.getAttack(SpeedCategory::SLOW, true);

	attacker.inflictDamage(defenderDamage, attackerSpeed);
	defender.inflictDamage(attackerDamage, SpeedCategory::RANGED);
}

void doGeneralMelee(CombatData & attacker, CombatData & defender)
{
	// H3: type_AI_combat_data::do_general_melee ai_combat.cpp:1270
	// The closing exchange: whichever side has the larger final melee value survives, and the
	// loser is wiped out while the winner absorbs the difference.
	const int attackerValue = attacker.getFinalMeleeValue();
	const int defenderValue = defender.getFinalMeleeValue();

	if(attackerValue == 0 && defenderValue == 0)
		return;

	if(attackerValue >= defenderValue)
	{
		defender.kill();
		attacker.inflictDamage(defenderValue, SpeedCategory::RANGED);
	}
	else
	{
		attacker.kill();
		defender.inflictDamage(attackerValue, SpeedCategory::RANGED);
	}
}

int chooseMelee(const CombatData & us, const CombatData & them, SpeedCategory currentRound)
{
	// H3: type_AI_combat_data::choose_melee ai_combat.cpp:1301
	// Every "keep shooting until round N, then commit" plan is simulated from SLOW downward
	// and the best surviving value wins. This is the whole go/no-go engine.
	bool haveMeleeStack = false;
	for(const auto & m : us.creatures)
		if(m.category != SpeedCategory::RANGED && m.number > 0 && static_cast<int>(m.category) <= static_cast<int>(currentRound))
			haveMeleeStack = true;

	int best = 0;
	bool first = true;

	for(int plan = static_cast<int>(SpeedCategory::SLOW); plan >= static_cast<int>(currentRound); --plan)
	{
		CombatData a = us;
		CombatData b = them;

		int round = static_cast<int>(currentRound);
		while(round < plan && a.getTotal() > 0 && b.getTotal() > 0)
		{
			doRangedCombat(a, b);
			++round;
		}

		while(round < static_cast<int>(SpeedCategory::SLOW) && a.getTotal() > 0 && b.getTotal() > 0)
		{
			doMeleeCombat(a, static_cast<SpeedCategory>(round), b);
			++round;
		}

		doGeneralMelee(a, b);

		int score = a.getTotal();
		if(score == 0)
			score = -b.getTotal();

		if(first || score > best)
		{
			best = score;
			first = false;
		}

		if(!haveMeleeStack)
			break;
	}

	return best;
}

int valueOfCombat(
	const CGHeroInstance * attackingHero,
	const CCreatureSet * attackingArmy,
	const CGHeroInstance * defendingHero,
	const CCreatureSet * defendingArmy,
	int difficulty,
	double attackerBias,
	double defenderBias)
{
	// H3: AI_value_of_combat ai_combat.cpp:1565
	CombatData us;
	CombatData them;

	us.initializeCreatures(attackingArmy, attackingHero, defendingHero);
	them.initializeCreatures(defendingArmy, defendingHero, attackingHero);

	const int ourValueBefore = us.getTotal();
	const int theirValueBefore = them.getTotal();

	// The bias multipliers are can_take_town's 1.25 / 0.75, and 1.0 for every other caller.
	for(auto & m : us.creatures)
	{
		m.meleeModifier *= attackerBias;
		m.rangedModifier *= attackerBias;
		m.finalMeleeModifier *= attackerBias;
	}
	for(auto & m : them.creatures)
	{
		m.meleeModifier *= defenderBias;
		m.rangedModifier *= defenderBias;
		m.finalMeleeModifier *= defenderBias;
	}

	// The defender is judged more or less threatening purely as a function of difficulty -
	// the single most-consulted difficulty lever in the whole AI.
	const int clamped = std::clamp(difficulty, 0, 4);
	const double defenseEstimate = Const::DEFENSE_ESTIMATE_BY_DIFFICULTY[clamped];
	for(auto & m : them.creatures)
		m.finalMeleeModifier *= defenseEstimate;

	CombatData usCopy = us;
	CombatData themCopy = them;

	int round = static_cast<int>(SpeedCategory::RANGED);
	while(round < static_cast<int>(SpeedCategory::SLOW) && usCopy.getTotal() > 0 && themCopy.getTotal() > 0)
	{
		if(round == 0)
			doRangedCombat(usCopy, themCopy);
		else
			doMeleeCombat(usCopy, static_cast<SpeedCategory>(round), themCopy);
		++round;
	}
	doGeneralMelee(usCopy, themCopy);

	const int ourLoss = ourValueBefore - usCopy.getTotal();
	const int theirLoss = theirValueBefore - themCopy.getTotal();

	// PHILAI-GAP: hero::get_aggression (hero.h:961) is a per-hero trait with no VCMI
	// equivalent; the original folds it into this result before returning.
	return theirLoss - ourLoss;
}

bool quickCombatAttackerWins(
	const CGHeroInstance * attackingHero,
	const CCreatureSet * attackingArmy,
	const CGHeroInstance * defendingHero,
	const CCreatureSet * defendingArmy)
{
	// H3: AI_quick_combat ai_combat.cpp:1511 - each side rolls its own 75-125% independently,
	// so the same two armies auto-resolved twice can genuinely produce different outcomes.
	const double attackerRoll = randomPercent(Const::AUTORESOLVE_RANDOM_MIN_PCT, Const::AUTORESOLVE_RANDOM_MAX_PCT);
	const double defenderRoll = randomPercent(Const::AUTORESOLVE_RANDOM_MIN_PCT, Const::AUTORESOLVE_RANDOM_MAX_PCT);

	const double attackerStrength = getApproximateStrength(attackingArmy, attackingHero) * attackerRoll;
	const double defenderStrength = getApproximateStrength(defendingArmy, defendingHero) * defenderRoll;

	return attackerStrength > defenderStrength;
}

} // namespace PhilAI
