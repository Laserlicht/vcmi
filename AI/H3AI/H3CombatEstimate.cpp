/*
 * H3CombatEstimate.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3CombatEstimate.h"

#include "H3Player.h"
#include "H3Valuations.h"
#include "H3VictoryConditions.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

#include <algorithm>
#include <cmath>

namespace H3AI
{

namespace
{
int clamp99(int value)
{
	return std::clamp(value, 0, 99);
}
}

CombatData::CombatData(
	const CGHeroInstance * ourHero,
	const CCreatureSet * ourArmy,
	double baseModifier,
	const CGHeroInstance * theirHero,
	const CGTownInstance * theirTown)
	: hero(ourHero)
	, enemyHero(theirHero)
	, town(theirTown)
{
	// ---- SS 5B.1 Setup ----------------------------------------------------------
	int tacticsAdvantage = ourHero ? ourHero->getSecSkillLevel(SecondarySkill::TACTICS) : 0;

	if(enemyHero != nullptr)
		tacticsAdvantage = std::max(tacticsAdvantage - enemyHero->getSecSkillLevel(SecondarySkill::TACTICS), 0);

	int atk = 0;
	int def = 0;

	if(ourHero != nullptr)
	{
		atk = clamp99(ourHero->getPrimSkillLevel(PrimarySkill::ATTACK));
		def = clamp99(ourHero->getPrimSkillLevel(PrimarySkill::DEFENSE));
	}

	if(enemyHero != nullptr)
	{
		atk -= std::min(atk, clamp99(enemyHero->getPrimSkillLevel(PrimarySkill::DEFENSE)));
		def -= std::min(def, clamp99(enemyHero->getPrimSkillLevel(PrimarySkill::ATTACK)));
	}

	const double modifier = std::sqrt(def * 0.05 + 1.0) * std::sqrt(atk * 0.05 + 1.0) * baseModifier;

	const double manaFactor = ourHero
		? ourHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER) / 5.0
		: 0.2;

	// SS 5D.3 - hero::creature_speed_bonus @ 0x4E5AA0:
	//   +1 Necklace of Swiftness (97), +1 Ring of the Wayfarer (69),
	//   +2 Cape of Velocity (99), +2 for a speed-specialist hero,
	// each tested as "worn, or the assembled set containing it".
	int speedBonus = 0;

	if(ourHero != nullptr)
	{
		if(ourHero->hasArt(ArtifactID(ART_NECKLACE_OF_SWIFTNESS), false, true)) speedBonus += 1;
		if(ourHero->hasArt(ArtifactID(ART_RING_OF_THE_WAYFARER), false, true))  speedBonus += 1;
		if(ourHero->hasArt(ArtifactID(ART_CAPE_OF_VELOCITY), false, true))      speedBonus += 2;
		// The +2 speed specialty is a hero-specialty lookup VCMI models as a bonus;
		// left out rather than guessed (same reasoning as firstAidAmount, SS 4.9a).
	}

	// SS 5B.2 - the two siege parameters, set by 0x424790 from the defending town:
	//   no fortification -> 0, Fort -> 4, Citadel -> 5, Castle -> 6,
	// and the archery penalty is simply "the town has any of the three".
	//
	// Every adventure-side caller of the original's constructor passes NO town, so for
	// the adventure AI both are always neutral and a siege estimate is an open-field
	// estimate.  That is the shipped behaviour, not a simplification.
	int wallSpeedLimit = 0;
	bool wallArcheryPenalty = false;

	if(theirTown != nullptr)
	{
		if(theirTown->hasBuilt(BuildingID::CASTLE))       wallSpeedLimit = 6;
		else if(theirTown->hasBuilt(BuildingID::CITADEL)) wallSpeedLimit = 5;
		else if(theirTown->hasBuilt(BuildingID::FORT))    wallSpeedLimit = 4;

		wallArcheryPenalty = wallSpeedLimit > 0;
	}

	// ---- SS 5B.2 per-stack type_monster_data -------------------------------------
	int index = 0;

	if(ourArmy != nullptr)
	{
		for(const auto & slot : ourArmy->Slots())
		{
			const CCreature * creature = ourArmy->getCreature(slot.first);

			if(creature == nullptr)
				continue;

			MonsterData md;
			md.index = index++;
			md.type = creature->getId();
			md.number = md.originalNumber = ourArmy->getStackCount(slot.first);

			const double baseHp = creature->getBaseHitPoints();
			double hp = baseHp;

			// SS 5D.3 - hero::creature_hp_bonus @ 0x4E5B80:
			//   +1 Ring of Vitality (94), +1 Ring of Life (95),
			//   +2 Vial of Lifeblood (96), and for a LIVING creature
			//   + maxHealth / 4 with the Elixir of Life (131).
			if(ourHero != nullptr)
			{
				if(ourHero->hasArt(ArtifactID(ART_RING_OF_VITALITY), false, true))  hp += 1.0;
				if(ourHero->hasArt(ArtifactID(ART_RING_OF_LIFE), false, true))      hp += 1.0;
				if(ourHero->hasArt(ArtifactID(ART_VIAL_OF_LIFEBLOOD), false, true)) hp += 2.0;

				if(isLiving(creature) && ourHero->hasArt(ArtifactID(ART_ELIXIR_OF_LIFE), false, true))
					hp += static_cast<double>(creature->getMaxHealth()) / 4.0;
			}

			md.speed = creature->getBaseSpeed() + speedBonus;
			md.value = static_cast<int>(std::sqrt(hp / baseHp) * creature->getFightValue() * modifier);
			md.totalValue = static_cast<int64_t>(md.value) * md.number;
			md.combatValuePerHit = hp > 0.0 ? static_cast<double>(md.value) / hp : 0.0;

			if(isShooter(creature))
			{
				md.category = 0;
			}
			else
			{
				md.category = md.speed > 0 ? (14 - 2 * tacticsAdvantage + md.speed) / md.speed : 4;

				if(md.category > 4)
					md.category = 4;

				if(wallSpeedLimit > md.category && !isFlying(creature))
					md.category = wallSpeedLimit;
			}

			md.meleeModifier = 0.2;
			md.finalMeleeModifier = 1.0;

			// SS 5B.2 - a shooter without the "no melee penalty" flag (traits bit 12).
			if(md.category == 0 && !creature->hasBonusOfType(BonusType::NO_MELEE_PENALTY))
			{
				md.meleeModifier = 0.1;
				md.finalMeleeModifier = 0.7;
			}

			md.rangedModifier = manaFactor;

			// SS 5B.2 / SS 4E.2 bit 15 - "attacks twice".
			if(creature->hasBonusOfType(BonusType::ADDITIONAL_ATTACK))
				md.rangedModifier *= 2;

			// SS 5B.2 - the Arch Mage (creature 35) is exempt from the wall penalty.
			if(wallArcheryPenalty && creature->getId().getNum() != 35)
				md.rangedModifier /= 2;

			totalCombatValue += md.totalValue;
			stacks.push_back(md);
		}
	}

	// SS 5B.2 - "The vector is then sorted by value".
	std::sort(stacks.begin(), stacks.end(), [](const MonsterData & a, const MonsterData & b) { return a.value < b.value; });
}

int CombatData::getFastestSpeed() const
{
	int fastest = 0;

	for(const MonsterData & md : stacks)
		if(md.number > 0)
			fastest = std::max(fastest, md.speed);

	return fastest;
}

int64_t CombatData::getAttack(int speedLimit, bool shootersBlocked) const
{
	// SS 5B.3 - get_attack @ 0x426390, walked backwards over the value-sorted vector.
	int64_t total = 0;

	for(int i = static_cast<int>(stacks.size()) - 1; i >= 0; --i)
	{
		const MonsterData & md = stacks[i];

		if(md.category > speedLimit)
			continue;

		const double m = (!shootersBlocked && md.category == 0) ? md.rangedModifier : md.meleeModifier;

		total = static_cast<int64_t>(static_cast<double>(md.number) * md.value * m + static_cast<double>(total));
	}

	return total;
}

int64_t CombatData::inflictMeleeDamage(int64_t damage, int minCategory, int maxCategory)
{
	// SS 5B.3 - inflict_melee_damage @ 0x426170: damage is distributed proportionally
	// to total_value among the stacks whose category is in [minCat, maxCat].
	int64_t pool = 0;

	for(const MonsterData & md : stacks)
		if(md.category >= minCategory && md.category <= maxCategory)
			pool += md.totalValue;

	if(pool <= 0)
		return damage;

	for(MonsterData & md : stacks)
	{
		if(md.category < minCategory || md.category > maxCategory || md.totalValue <= 0)
			continue;

		const int64_t share = static_cast<int64_t>(static_cast<double>(md.totalValue) * damage / pool);
		pool -= md.totalValue;

		int64_t destroyed;

		if(md.totalValue < share)
		{
			destroyed = md.totalValue;
			md.totalValue = 0;
			md.number = 0;
		}
		else
		{
			destroyed = share;
			md.totalValue -= share;
			md.number = md.value > 0 ? static_cast<int>((md.totalValue + md.value - 1) / md.value) : 0;
		}

		damage -= destroyed;

		if(damage <= 0 || pool <= 0)
			break;
	}

	return damage;
}

void CombatData::kill()
{
	totalCombatValue = 0;

	for(MonsterData & md : stacks)
	{
		md.number = 0;
		md.totalValue = 0;
	}
}

void CombatData::inflictDamage(int64_t damage, int blockerSpeed)
{
	// SS 5B.3 - inflict_damage @ 0x426300
	totalCombatValue -= damage;

	if(totalCombatValue <= 0)
	{
		kill();
		return;
	}

	const int64_t left = inflictMeleeDamage(damage, 1, blockerSpeed);

	if(left > 0)
		inflictMeleeDamage(left, 0, 4);
}

double CombatData::getFinalMeleeValue() const
{
	// SS 5B.3 - get_final_melee_value @ 0x426450
	double total = 0.0;

	for(const MonsterData & md : stacks)
		total += static_cast<double>(md.totalValue) * md.finalMeleeModifier;

	return total;
}

int64_t CombatData::survivingArmyAIValue() const
{
	int64_t value = 0;

	for(const MonsterData & md : stacks)
	{
		const CCreature * creature = md.type.toCreature();

		if(creature != nullptr)
			value += static_cast<int64_t>(creature->getAIValue()) * md.number;
	}

	return value;
}

void CombatData::simulateCombat(CombatData & defender)
{
	// SS 5B.3 - simulate_combat @ 0x426BC0
	for(int round = 1; ; ++round)
	{
		if(totalCombatValue <= 0 || defender.totalCombatValue <= 0)
			break;

		// SS 5B.3 - bool aMelee = this->choose_melee(def, round);   // 0x4267C0
		//           bool bMelee = def.choose_melee(*this, round);
		// TODO: choose_melee (0x4267C0) and do_general_melee (0x4264D0) are named in
		// SS 4.11 and referenced in SS 5B.3, but neither body is given in the report.
		// Without them the "is this side forced into melee this round" decision cannot
		// be reproduced; both sides are treated as melee, which is what the original
		// converges to once shooters are engaged.
		const bool aMelee = true;
		const bool bMelee = true;

		// SS 5B.3 - the side with the faster surviving stack casts first.
		// TODO: cast_spell (0x425BD0) is the quick-combat spell AI (SS 5B.4).  Its
		// seven helpers are explicitly left unexpanded by the report, so no spell is
		// cast here and the simulation is purely a troop exchange.
		(void)getFastestSpeed();
		(void)defender.getFastestSpeed();

		// SS 5B.3 - the exchange itself.  SS 5D.4 settles the ownership question the
		// comment below used to raise: X.get_attack(...) is the damage X DEALS, and it
		// is subtracted from the OTHER side.
		// The report states the two cases as:
		//   both melee:      each side takes get_attack(4, true) from the other
		//   one side ranged: the ranged side takes get_attack(round, false),
		//                    the other get_attack(4, true)
		// TODO: "takes ... from the other" is ambiguous about which side owns the
		// get_attack call.  It is read here as "each side *deals* the named attack",
		// which is the only reading consistent across both cases.
		int64_t damageFromUs;
		int64_t damageFromThem;

		if(aMelee && bMelee)
		{
			damageFromUs = getAttack(4, true);
			damageFromThem = defender.getAttack(4, true);
		}
		else
		{
			damageFromUs = aMelee ? getAttack(4, true) : getAttack(round, false);
			damageFromThem = bMelee ? defender.getAttack(4, true) : defender.getAttack(round, false);
		}

		// SS 5B.3 - "after every hit, if total_combat_value <= 0 -> kill() and stop".
		defender.inflictDamage(damageFromUs, 4);

		if(defender.totalCombatValue <= 0)
			break;

		inflictDamage(damageFromThem, 4);

		if(totalCombatValue <= 0)
			break;
	}

	// SS 5B.3 - do_aftermath @ 0x426EE0 applies necromancy, town-tower fire and the
	// surviving-army bookkeeping.
	// TODO: the report names do_aftermath but does not give its arithmetic.
}

int valueOfCombat(
	CCallback * cb,
	const H3Player & player,
	const CGHeroInstance * attacker,
	const CGHeroInstance * defender,
	const CCreatureSet * defenderArmy,
	const CGTownInstance * defenderTown)
{
	// SS 4.11 - AI_value_of_combat @ 0x427330
	if(attacker == nullptr)
		return 0;

	const VictoryConditionInfo victory = getVictoryConditionInfo(cb);

	// SS 4.11 / SS 5D.1 - hero + 0x47A is a RANDOM per-hero multiplier, drawn once when
	// the hero record is created:
	//   base = rand(75, 100) * heroClass.f[0x08];   modifier = base / rand(100, 125)
	// i.e. roughly classFactor * U[0.6, 1.0], fixed for that hero's lifetime.  Two heroes
	// of the same class therefore price the same fight slightly differently, and always
	// will.  VCMI has no equivalent field and cannot recover the draw, so 1.0 is used;
	// the consequence is that every AI hero here rates a given fight identically, where
	// the original spreads them over roughly a 0.6..1.0 band.
	double atkMod = 1.0;

	// SS 4C, condition 5 - the hero we must defeat fights us at half our strength.
	if(victory.condition == H3VictoryCondition::DEFEAT_HERO && victory.targetObject == attacker->id)
		atkMod *= 0.5;

	const auto isComputer = [cb](PlayerColor color) -> bool
	{
		if(!color.isValidPlayer())
			return false;

		const PlayerState * state = cb->getPlayerState(color, false);

		return state != nullptr && !state->human;
	};

	bool anyComputer = isComputer(attacker->getOwner());

	if(defender != nullptr)
		anyComputer = anyComputer || isComputer(defender->getOwner());

	double defMod = 1.25;

	if(anyComputer || (defenderTown != nullptr && isComputer(defenderTown->getOwner())))
	{
		const int difficulty = std::clamp<int>(cb->getStartInfo()->difficulty, 0, 4);
		defMod = AI_ENEMY_STRENGTH_MULTIPLIER[difficulty];
	}

	CombatData a(attacker, attacker, atkMod, defender, defenderTown);
	CombatData d(defender, defenderArmy, defMod, attacker, nullptr);

	const int64_t armyBefore = armyAIValue(attacker);

	a.simulateCombat(d);

	if(a.totalCombatValue == 0)
		return ABSOLUTE_NO_GO;

	const int64_t survivingArmyValue = a.survivingArmyAIValue();
	const int64_t defenderArmyValue = armyAIValue(defenderArmy);

	// SS 4.11 / SS 5D.3 - hero::xp_reward_factor @ 0x4E4840:
	//   f = g_learning_factor[Learning skill]        // {0.00, 0.05, 0.10, 0.15}
	//   if the hero is a Learning SPECIALIST, f *= (1 + 0.05 * level)
	//   return f + 1.0
	// so the multiplier is 1.00 / 1.05 / 1.10 / 1.15 by Learning level.  The specialist
	// term is not applied here: VCMI expresses hero specialties as bonuses rather than a
	// class id, so there is no faithful equivalent of the original's specialist test.
	const float f1 = 1.0f + LEARNING_XP_FACTOR[std::clamp(
		static_cast<int>(attacker->getSecSkillLevel(SecondarySkill::LEARNING)), 0, 3)];

	const int64_t expForNext = experienceForLevel(attacker->level);
	int64_t xpReward = 0;

	if(expForNext > 0)
	{
		xpReward = static_cast<int64_t>(
			(static_cast<double>(XP_VALUATION_BASE + survivingArmyValue)
				/ static_cast<double>(XP_VALUATION_LEVEL_DIVISOR * expForNext))
			* static_cast<double>(static_cast<int64_t>(f1 * defenderArmyValue)));
	}

	const int64_t losses = armyBefore - survivingArmyValue;
	int64_t value = xpReward - losses;

	// SS 4.11 - small losses are treated as neutral rather than negative.
	if(value < 0 && losses * 4 < armyBefore)
		value = 0;

	if(defender != nullptr || defenderTown != nullptr)
	{
		const PlayerColor color = defender != nullptr ? defender->getOwner() : defenderTown->getOwner();

		value = static_cast<int64_t>(
			player.getAttackBonus(color) * static_cast<float>(defenderArmyValue) + static_cast<float>(value));

		// SS 4C, condition 5 - defeat a specific hero.
		if(defender != nullptr
			&& victory.condition == H3VictoryCondition::DEFEAT_HERO
			&& victory.targetObject == defender->id)
		{
			value += VICTORY_CONDITION_OVERRIDE;
		}

		// SS 5D.2 - the apparent contradiction with SS 4C.4 is resolved: there is NO
		// loss-condition term.  The term that looked like one reads the VICTORY record
		// (gpGame + 0x1F89C) and fires on condition 5, "defeat a specific hero":
		//   if (victory.condition == DEFEAT_HERO && victory.hero == this hero)
		//       modifier *= 0.5;
		// A hero that is itself the enemy's victory target halves its own appetite for
		// fights.  That term is applied as the atkMod halving above, so nothing is
		// missing here.  SS 4C.4 was right: the loss condition is never read.
	}

	return static_cast<int>(std::clamp<int64_t>(value, ABSOLUTE_NO_GO, std::numeric_limits<int>::max()));
}

}
