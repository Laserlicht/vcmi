/*
 * PhilBattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilBattleAI.h"

#include "PhilCombatSim.h"
#include "PhilSpellcaster.h"
#include "PhilValuation.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/artifact/CArtifactSet.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include <algorithm>

namespace PhilAI
{

/// H3 creature table indices, used where the original hard-codes a creature type rather than
/// testing a trait. VCMI keeps the same indices for the original 150 creatures.
namespace H3Creature
{
inline constexpr int ARCHANGEL = 13;
inline constexpr int GRIFFIN = 10;
inline constexpr int ROYAL_GRIFFIN = 11;
inline constexpr int MAGOG = 59;
inline constexpr int PIT_FIEND = 60;
inline constexpr int PIT_LORD = 61;
inline constexpr int LICH = 66;
inline constexpr int POWER_LICH = 67;
inline constexpr int MINOTAUR = 82;
inline constexpr int MINOTAUR_KING = 83;
inline constexpr int OGRE_MAGE = 89;
inline constexpr int CYCLOPS = 94;
inline constexpr int CYCLOPS_KING = 95;
inline constexpr int EFREET_SULTAN = 117;
}

BattleSide CombatParameters::enemyGroup() const
{
	return group == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
}

// ---------------------------------------------------------------------------
// III.3 - turn order
// ---------------------------------------------------------------------------

int getMoveOrder(const CStack * stack, bool secondPhase)
{
	// H3: get_move_order ai.cpp:610 - ordinary speed with four hard exceptions, recovered
	// verbatim from the decompile.
	if(stack->isFirstAidTent() || stack->isAmmoCart())
		return Const::MOVE_ORDER_WAR_MACHINE;

	// Blind and Stone Gaze with two or more rounds left move near-last.
	if(stack->hasBonusOfType(BonusType::NOT_ACTIVE))
		return Const::MOVE_ORDER_BLINDED;

	const int speed = stack->getMovementRange();

	// A stack that cannot act this round moves late, still ordered among itself by speed.
	if(!stack->willMove())
		return speed + Const::MOVE_ORDER_INCAPACITATED_OFFSET;

	// Waited stacks and the round's second phase are resolved in reverse speed order.
	if(secondPhase || stack->waited())
		return -speed;

	return speed;
}

// ---------------------------------------------------------------------------
// III.4 - the retreat calculus
// ---------------------------------------------------------------------------

bool checkRetreat(
	const CGHeroInstance * ourHero,
	const CGHeroInstance * enemyHero,
	uint64_t ourStrength,
	uint64_t enemyStrength,
	bool weAreDefendingSiege,
	bool aiControlled,
	int difficulty,
	int ourGold,
	int lootValue,
	int surrenderCost)
{
	// H3: combatManager::AICheckRetreat ai.cpp:162
	// A sequence of hard gates before any numeric comparison happens at all.

	// Gate 1 - no hero, no retreat question.
	if(!ourHero)
		return false;

	// Gate 2 - on Easy an AI-controlled side never even reaches the check; on Normal an extra
	// roughly 51-in-100 roll skips it. Neither exists for a human-controlled side.
	if(aiControlled)
	{
		if(difficulty == 0)
			return false;
		if(difficulty == 1 && randomRoll(1, Const::RETREAT_SKIP_ROLL_MAX) < Const::RETREAT_SKIP_ROLL_UNDER)
			return false;
	}

	// Gate 3 - one specific artifact blocks retreat outright, checked against either hero.
	const ArtifactID blocker(Const::RETREAT_BLOCKING_ARTIFACT);
	if(ourHero->hasArt(blocker))
		return false;
	if(enemyHero && enemyHero->hasArt(blocker))
		return false;

	// PHILAI-GAP: the scripted-mode exemption (game mode == 5 exempting one specific hero id)
	// has no VCMI equivalent and is not reproduced.

	// Gate 4 - a hero judged to have nothing valuable on them is treated as having nothing
	// worth retreating to protect, and the whole check is skipped.
	if(lootValue < Const::RETREAT_LOOT_GATE && ourGold < Const::RETREAT_GOLD_GATE)
		return false;

	// Gate 5 - the AI will not pay a retreat/surrender cost it cannot comfortably afford.
	if(ourGold < surrenderCost + Const::RETREAT_TREASURY_MINIMUM)
		return false;

	double ourPower = static_cast<double>(ourStrength);
	const double enemyPower = static_cast<double>(enemyStrength);

	// A town siege gives the defending side a flat 10% power bonus in this calculation only.
	if(weAreDefendingSiege)
		ourPower *= Const::RETREAT_SIEGE_DEFENDER_BONUS;

	if(ourPower + enemyPower <= 0.0)
		return false;

	double threshold = Const::RETREAT_THRESHOLD_BASE_A;

	// The loot the hero carries moves the threshold in three bands.
	// PHILAI-GAP: the three replacement thresholds live in the binary's compiled data section
	// and were not recoverable. The band boundaries themselves are exact, so the structure is
	// preserved and the base threshold is kept for every band.
	if(lootValue >= Const::RETREAT_LOOT_TIER_3)
		threshold = Const::RETREAT_THRESHOLD_BASE_A;
	else if(lootValue >= Const::RETREAT_LOOT_TIER_2)
		threshold = Const::RETREAT_THRESHOLD_BASE_A;
	else if(lootValue >= Const::RETREAT_LOOT_TIER_1)
		threshold = Const::RETREAT_THRESHOLD_BASE_A;

	// Recovered directly from the decompile: the threshold drops by 1.5 points per step below
	// the top difficulty, so a higher-difficulty AI is markedly more willing to stand and fight.
	threshold -= (Const::RETREAT_DIFFICULTY_BASE - std::clamp(difficulty, 0, 4)) * Const::RETREAT_DIFFICULTY_RATE;

	// A well-funded hero gets a more cautious - higher - threshold.
	threshold += (ourGold / Const::RETREAT_GOLD_RESERVE_UNIT) * Const::RETREAT_GOLD_RESERVE_RATE_A;

	// The comparison itself is a pure "am I heavily outnumbered" ratio, not a win model.
	return (ourPower / (ourPower + enemyPower)) < threshold;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

PhilBattleAI::PhilBattleAI() = default;

PhilBattleAI::~PhilBattleAI()
{
	if(cb)
		cb->waitTillRealize = wasWaitingForRealize;
}

void PhilBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	wasWaitingForRealize = CB->waitTillRealize;
	CB->waitTillRealize = false;
}

void PhilBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences)
{
	initBattleInterface(ENV, CB);
}

void PhilBattleAI::battleStart(const BattleID & battleID, const CCreatureSet *, const CCreatureSet *, int3, const CGHeroInstance *, const CGHeroInstance *, BattleSide Side, bool)
{
	side = Side;
	placementDone = false;
}

void PhilBattleAI::battleNewRound(const BattleID & battleID)
{
	// The threat array is rebuilt once per round, exactly as mark_enemy_attacks is.
	markEnemyAttacks(battleID);
	markFirewalls(battleID);
}

void PhilBattleAI::battleNewRoundFirst(const BattleID &) {}
void PhilBattleAI::actionFinished(const BattleID &, const BattleAction &) {}
void PhilBattleAI::actionStarted(const BattleID &, const BattleAction &) {}
void PhilBattleAI::battleAttack(const BattleID &, const BattleAttack *) {}
void PhilBattleAI::battleStacksAttacked(const BattleID &, const std::vector<BattleStackAttacked> &, bool) {}
void PhilBattleAI::battleEnd(const BattleID &, const BattleResult *, QueryID) {}
void PhilBattleAI::battleStackMoved(const BattleID &, const CStack *, const BattleHexArray &, int, bool) {}
void PhilBattleAI::battleSpellCast(const BattleID &, const BattleSpellCast *) {}
void PhilBattleAI::battleStacksEffectsSet(const BattleID &, const SetStackEffect &) {}
void PhilBattleAI::battleCatapultAttacked(const BattleID &, const CatapultAttack &) {}

// ---------------------------------------------------------------------------
// Shared per-turn bookkeeping
// ---------------------------------------------------------------------------

CombatParameters PhilBattleAI::buildEstimate(const BattleID & battleID) const
{
	// H3: type_AI_combat_parameters ai_tactical.cpp:393
	CombatParameters p;
	p.group = side;

	auto battle = cb->getBattle(battleID);
	if(!battle)
		return p;

	bool first = true;
	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		const int a = s->getAttack(false);
		const int d = s->getDefense(false);
		if(first || a < p.lowestAttack)
			p.lowestAttack = a;
		if(first || d < p.lowestDefense)
			p.lowestDefense = d;
		first = false;

		p.enemyCombatValue += s->getCount() * s->unitType()->getAIValue();
	}

	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
		p.friendlyCombatValue += s->getCount() * s->unitType()->getAIValue();

	return p;
}

void PhilBattleAI::markEnemyAttacks(const BattleID & battleID)
{
	// H3: combatManager::mark_enemy_attacks ai.cpp:1241
	// Walk every non-incapacitated enemy stack and mark which hexes it could actually threaten
	// given its real speed and reach. This is a marker - how many enemies cover the hex - not a
	// magnitude: the routines below use it to separate otherwise equal hexes, never to outweigh
	// an attack's own value.
	enemyThreat.assign(GameConstants::BFIELD_SIZE, 0);

	auto battle = cb->getBattle(battleID);
	if(!battle)
		return;

	for(const auto * enemy : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!enemy->alive() || !enemy->willMove())
			continue;

		// A shooter covers the whole field, so it marks every hex equally and drops out of any
		// comparison between hexes - which is exactly what it should do.
		if(enemy->canShoot())
		{
			for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
				++enemyThreat[hex];
			continue;
		}

		const ReachabilityInfo reach = battle->getReachability(enemy);
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		{
			const BattleHex h(hex);
			if(!h.isAvailable())
				continue;

			// A hex is threatened if the enemy could reach a tile adjacent to it this turn.
			for(const auto & neighbour : h.getNeighbouringTiles())
			{
				if(reach.isReachable(neighbour))
				{
					++enemyThreat[hex];
					break;
				}
			}
		}
	}
}

void PhilBattleAI::markFirewalls(const BattleID & battleID)
{
	// H3: combatManager::mark_firewalls ai.cpp:1866 - price every live obstacle's real damage
	// and subtract it from that hex's attack value.
	hexHazard.assign(GameConstants::BFIELD_SIZE, 0);

	auto battle = cb->getBattle(battleID);
	if(!battle)
		return;

	for(const auto & obstacle : battle->battleGetAllObstacles())
	{
		const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		if(!spellObstacle || spellObstacle->minimalDamage <= 0)
			continue;

		for(const auto & hex : obstacle->getAffectedTiles())
			if(hex.isValid())
				hexHazard[hex.toInt()] += spellObstacle->minimalDamage;
	}
}

int PhilBattleAI::getAttackValue(const BattleID & battleID, const CStack * attacker, const CStack * defender, const BattleHex & from) const
{
	// H3: get_attack_value ai.cpp:696 - expected damage converted into combat-value currency,
	// net of the retaliation the attack would invite, minus any hazard on the chosen hex.
	auto battle = cb->getBattle(battleID);
	if(!battle || !attacker || !defender)
		return 0;

	DamageEstimation retaliation;
	DamageEstimation dealt = battle->battleEstimateDamage(attacker, defender, from, &retaliation);

	const int64_t defenderHealth = defender->getTotalHealth();
	const int64_t attackerHealth = attacker->getTotalHealth();

	int64_t dealtAvg = (dealt.damage.min + dealt.damage.max) / 2;
	int64_t takenAvg = (retaliation.damage.min + retaliation.damage.max) / 2;
	vstd::amin(dealtAvg, defenderHealth);
	vstd::amin(takenAvg, attackerHealth);

	const int defenderUnitValue = defender->unitType()->getAIValue();
	const int attackerUnitValue = attacker->unitType()->getAIValue();
	const int defenderHP = std::max(1, static_cast<int>(defender->getMaxHealth()));
	const int attackerHP = std::max(1, static_cast<int>(attacker->getMaxHealth()));

	int value = static_cast<int>(dealtAvg * defenderUnitValue / defenderHP)
		- static_cast<int>(takenAvg * attackerUnitValue / attackerHP);

	if(from.isValid() && from.toInt() < static_cast<int>(hexHazard.size()))
		value -= hexHazard[from.toInt()];

	return value;
}

// ---------------------------------------------------------------------------
// III.3 - the combat loop
// ---------------------------------------------------------------------------

void PhilBattleAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	if(enemyThreat.empty())
	{
		markEnemyAttacks(battleID);
		markFirewalls(battleID);
	}

	doCompAI(battleID, stack);
}

void PhilBattleAI::doCompAI(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::DoCompAI ai.cpp:2272
	// A short, flat triage. DoCompAI itself weighs nothing against anything - it just routes
	// the acting stack to the right specialist.
	auto battle = cb->getBattle(battleID);

	if(stack->isCatapult())
	{
		// PHILAI-GAP: the original picks a wall segment through its own siege bookkeeping;
		// the nearest equivalent here is the first still-standing destructible segment.
		static const std::vector<EWallPart> order = {
			EWallPart::KEEP, EWallPart::BOTTOM_TOWER, EWallPart::UPPER_TOWER,
			EWallPart::GATE, EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE,
			EWallPart::OVER_GATE, EWallPart::UPPER_WALL };

		for(auto part : order)
		{
			if(battle->battleGetWallState(part) == EWallState::INTACT || battle->battleGetWallState(part) == EWallState::DAMAGED)
			{
				BattleAction attack;
				attack.actionType = EActionType::CATAPULT;
				attack.side = side;
				attack.stackNumber = stack->unitId();
				attack.aimToHex(BattleHex(static_cast<int>(part)));
				cb->battleMakeUnitAction(battleID, attack);
				return;
			}
		}

		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
		return;
	}

	if(stack->isBallista())
	{
		const CStack * target = chooseBallistaTarget(battleID, stack);
		if(target)
		{
			cb->battleMakeUnitAction(battleID, BattleAction::makeShotAttack(stack, target));
			return;
		}
		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
		return;
	}

	if(stack->isFirstAidTent() || stack->isAmmoCart())
	{
		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
		return;
	}

	// Four creature types with abilities that behave like spells get their own route first.
	if(chooseCreatureSpellAction(battleID, stack))
		return;

	// A Cyclops only ever considers its wall-bypassing boulder throw when attacking a town
	// with an actual fortification - never on defence, never in an open field.
	if(chooseCyclopsAction(battleID, stack))
		return;

	if(stack->canShoot())
	{
		if(chooseShooterAction(battleID, stack))
			return;
	}
	else
	{
		if(chooseMeleeAction(battleID, stack))
			return;
	}

	cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
}

const CStack * PhilBattleAI::chooseShooterTarget(const BattleID & battleID, const CStack * stack) const
{
	// H3: combatManager::choose_shooter_target ai.cpp:397
	// Ordinary shooters score by the ranged-attack-value primitive. Three creature types are
	// special-cased as area-effect shooters and score by splash instead - and a negative splash
	// value skips the candidate outright, the same own-troops-in-the-blast avoidance that
	// governs hero-cast mass damage.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return nullptr;

	const int index = stack->unitType()->getIndex();
	const bool areaShooter = index == H3Creature::MAGOG || index == H3Creature::LICH || index == H3Creature::POWER_LICH;

	const CStack * best = nullptr;
	int bestValue = 0;

	for(const auto * enemy : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!enemy->alive() || enemy->isInvincible())
			continue;
		if(!battle->battleCanShoot(stack, enemy->getPosition()))
			continue;

		int value = getAttackValue(battleID, stack, enemy, stack->getPosition());

		if(areaShooter)
		{
			// The splash is scored across everything it would touch, our own troops included.
			int splash = 0;
			for(const auto & hex : enemy->getPosition().getNeighbouringTiles())
			{
				const auto * caught = battle->battleGetUnitByPos(hex);
				if(!caught || !caught->alive())
					continue;

				const int caughtValue = caught->getCount() * caught->unitType()->getAIValue();
				splash += caught->unitSide() == side ? -caughtValue : caughtValue;
			}
			value += splash;

			// A negative splash value rules the candidate out entirely.
			if(splash < 0)
				continue;
		}

		if(!best || value > bestValue)
		{
			best = enemy;
			bestValue = value;
		}
	}

	return best;
}

bool PhilBattleAI::chooseShooterAction(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::choose_shooter_action ai.cpp:558
	const CStack * target = chooseShooterTarget(battleID, stack);
	if(!target)
		return false;

	cb->battleMakeUnitAction(battleID, BattleAction::makeShotAttack(stack, target));
	return true;
}

bool PhilBattleAI::chooseMeleeAction(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::choose_melee_action ai.cpp:2159
	// A non-shooter's melee path is not purely about attacking - it can choose to bodyguard a
	// friendly shooter instead. Attacking is tried first; shielding is the fallback before
	// simply walking toward the enemy.
	if(chooseMeleeTarget(battleID, stack))
		return true;

	if(attemptShooterDefense(battleID, stack))
		return true;

	return moveToward(battleID, stack);
}

bool PhilBattleAI::chooseMeleeTarget(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::choose_melee_target ai.cpp:1896
	// Whichever reachable enemy and attacking hex maximise the attack's own value wins. The
	// original does not decline to attack because that value came out negative - deciding a
	// fight is not worth having is combatManager::choose_to_run's job, not this routine's.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;

	const BattleHexArray available = battle->battleGetAvailableHexes(stack, false);

	const CStack * bestTarget = nullptr;
	BattleHex bestHex = BattleHex::INVALID;
	int bestValue = 0;
	int bestThreat = 0;
	bool found = false;

	for(const auto * enemy : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!enemy->alive() || enemy->isInvincible())
			continue;

		for(const auto & hex : available)
		{
			if(!battle->isMeleeAttackPossible(stack, enemy, hex))
				continue;

			int value = getAttackValue(battleID, stack, enemy, hex);

			// A fourth independent randomizer: the chosen target/hex score is scaled by a
			// fresh random 75-100% before comparison.
			value = static_cast<int>(value * randomPercent(Const::MELEE_RANDOM_MIN_PCT, Const::MELEE_RANDOM_MAX_PCT));

			// How exposed the hex we would end up standing on is separates otherwise equal
			// choices; it never outweighs the attack itself.
			const int threat = hex.isValid() && hex.toInt() < static_cast<int>(enemyThreat.size())
				? enemyThreat[hex.toInt()] : 0;

			if(!found || value > bestValue || (value == bestValue && threat < bestThreat))
			{
				found = true;
				bestValue = value;
				bestThreat = threat;
				bestTarget = enemy;
				bestHex = hex;
			}
		}
	}

	if(!found)
		return false;

	cb->battleMakeUnitAction(battleID, BattleAction::makeMeleeAttack(stack, bestTarget->getPosition(), bestHex));
	return true;
}

BattleHex PhilBattleAI::chooseDefenseHex(const BattleID & battleID, const CStack * stack, const CStack * shooter, int & travelTime) const
{
	// H3: combatManager::choose_defense_hex ai.cpp:1357
	// Find the fastest-to-reach hex that would block one of the enemy's approach angles.
	auto battle = cb->getBattle(battleID);
	travelTime = std::numeric_limits<int>::max();
	BattleHex best = BattleHex::INVALID;

	if(!battle)
		return best;

	const ReachabilityInfo reach = battle->getReachability(stack);
	const BattleHexArray available = battle->battleGetAvailableHexes(stack, false);

	for(const auto & hex : shooter->getPosition().getNeighbouringTiles())
	{
		if(!hex.isAvailable() || !available.contains(hex))
			continue;
		if(battle->battleGetUnitByPos(hex))
			continue;

		const int cost = static_cast<int>(reach.distances[hex.toInt()]);
		if(cost < travelTime)
		{
			travelTime = cost;
			best = hex;
		}
	}

	return best;
}

bool PhilBattleAI::attemptShooterDefense(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::attempt_shooter_defense ai.cpp:1445
	// Look across every friendly shooter, not just this stack, and find the one both valuable
	// and exposed enough to be worth physically shielding. Candidates rank first by travel
	// time, then by that shooter's combat value divided by its open approach hexes - so
	// blocking a shooter's last open side scores far higher than blocking one of five.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;

	BattleHex bestHex = BattleHex::INVALID;
	int bestTime = std::numeric_limits<int>::max();
	double bestScore = 0.0;

	for(const auto * shooter : battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
	{
		if(shooter == stack || !shooter->alive() || !shooter->isShooter())
			continue;

		int openSides = 0;
		for(const auto & hex : shooter->getPosition().getNeighbouringTiles())
			if(hex.isAvailable() && !battle->battleGetUnitByPos(hex))
				++openSides;

		if(openSides == 0)
			continue;

		int travelTime = 0;
		const BattleHex candidate = chooseDefenseHex(battleID, stack, shooter, travelTime);
		if(!candidate.isValid())
			continue;

		const double score = static_cast<double>(shooter->getCount() * shooter->unitType()->getAIValue()) / openSides;

		if(travelTime < bestTime || (travelTime == bestTime && score > bestScore))
		{
			bestTime = travelTime;
			bestScore = score;
			bestHex = candidate;
		}
	}

	if(!bestHex.isValid())
		return false;

	// Already standing in the blocking position - hold it rather than shuffling.
	if(stack->getPosition() == bestHex)
	{
		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
		return true;
	}

	cb->battleMakeUnitAction(battleID, BattleAction::makeMove(stack, bestHex));
	return true;
}

bool PhilBattleAI::moveToward(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::move_toward ai.cpp:779
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;

	// If the town should turtle, the defenders simply do not leave the walls.
	const CombatParameters estimate = buildEstimate(battleID);
	if(shouldStayInCastle(battleID, estimate))
	{
		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
		return true;
	}

	const ReachabilityInfo reach = battle->getReachability(stack);
	const BattleHexArray available = battle->battleGetAvailableHexes(stack, false);

	const CStack * closest = nullptr;
	uint32_t closestDistance = std::numeric_limits<uint32_t>::max();

	for(const auto * enemy : battle->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
	{
		if(!enemy->alive())
			continue;

		const uint32_t d = reach.distToNearestNeighbour(stack, enemy);
		if(d < closestDistance)
		{
			closestDistance = d;
			closest = enemy;
		}
	}

	if(!closest || closestDistance >= GameConstants::BFIELD_SIZE)
		return false;

	// Walk as far along the approach as this turn allows, preferring the least-threatened hex
	// among those equally close to the target.
	BattleHex best = BattleHex::INVALID;
	int bestDistance = std::numeric_limits<int>::max();
	int bestThreat = std::numeric_limits<int>::max();

	for(const auto & hex : available)
	{
		int minDistance = std::numeric_limits<int>::max();
		for(const auto & attackable : closest->getAttackableHexes(stack))
			minDistance = std::min(minDistance, static_cast<int>(BattleHex::getDistance(hex, attackable)));

		const int threat = hex.toInt() < static_cast<int>(enemyThreat.size()) ? enemyThreat[hex.toInt()] : 0;

		if(minDistance < bestDistance || (minDistance == bestDistance && threat < bestThreat))
		{
			bestDistance = minDistance;
			bestThreat = threat;
			best = hex;
		}
	}

	if(!best.isValid() || best == stack->getPosition())
		return false;

	cb->battleMakeUnitAction(battleID, BattleAction::makeMove(stack, best));
	return true;
}

// ---------------------------------------------------------------------------
// III.7 - siege defence & creature specials
// ---------------------------------------------------------------------------

bool PhilBattleAI::hasRangedAdvantage(const BattleID & battleID, const CombatParameters & estimate) const
{
	// H3: combatManager::has_ranged_advantage ai.cpp:1572
	// Compare each side's total ranged value: every stack that can shoot at full combat value,
	// plus - if the hero can cast - the best available damage spell counted as firepower too.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;

	std::array<int, 2> rangedValue = { 0, 0 };
	std::array<int, 2> totalValue = { 0, 0 };

	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::MINE_AND_ENEMY))
	{
		if(!s->alive())
			continue;

		const int idx = s->unitSide() == BattleSide::ATTACKER ? 0 : 1;
		const int value = s->getCount() * s->unitType()->getAIValue();
		totalValue[idx] += value;
		if(s->canShoot())
			rangedValue[idx] += value;
	}

	for(int idx = 0; idx < 2; ++idx)
	{
		const BattleSide sideForIdx = idx == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		const CGHeroInstance * hero = battle->battleGetFightingHero(sideForIdx);
		if(hero && hero->hasSpellbook())
			rangedValue[idx] += bestDamageSpellValue(hero, totalValue[idx]);
	}

	// The defender's still-standing towers add their garrison creatures' value, scaled by wall
	// level. A destroyed wall silences that tower's contribution outright.
	const TownFortifications forts = battle->battleGetFortifications();
	if(forts.wallsHealth > 0)
	{
		const int defenderIdx = 1;

		if(battle->battleGetWallState(EWallPart::KEEP) != EWallState::DESTROYED && forts.citadelShooter.hasValue())
		{
			const CCreature * c = forts.citadelShooter.toCreature();
			if(c)
				rangedValue[defenderIdx] += c->getAIValue();
		}

		// At Castle-tier fortification a second, upper tower adds a further half-share.
		if(forts.upperTowerHealth > 0 && battle->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::DESTROYED
			&& forts.upperTowerShooter.hasValue())
		{
			const CCreature * c = forts.upperTowerShooter.toCreature();
			if(c)
				rangedValue[defenderIdx] += c->getAIValue() / 2;
		}

		if(forts.lowerTowerHealth > 0 && battle->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::DESTROYED
			&& forts.lowerTowerShooter.hasValue())
		{
			const CCreature * c = forts.lowerTowerShooter.toCreature();
			if(c)
				rangedValue[defenderIdx] += c->getAIValue() / 2;
		}
	}

	const int us = estimate.group == BattleSide::ATTACKER ? 0 : 1;
	const int them = 1 - us;
	return rangedValue[them] < rangedValue[us];
}

bool PhilBattleAI::shouldStayInCastle(const BattleID & battleID, const CombatParameters & estimate) const
{
	// H3: combatManager::should_stay_in_castle ai.cpp:1822
	// Turtle only when every one of five conditions holds at once.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;

	// 1 - the town has a fortification at all.
	const TownFortifications forts = battle->battleGetFortifications();
	if(forts.wallsHealth <= 0)
		return false;

	// 2 - evaluated for the defending side only.
	if(estimate.group != BattleSide::DEFENDER)
		return false;

	// 3 - every remaining wall segment is still genuinely blocked.
	static const std::vector<EWallPart> segments = {
		EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE,
		EWallPart::UPPER_WALL, EWallPart::GATE };

	for(auto part : segments)
		if(battle->battleGetWallState(part) == EWallState::DESTROYED)
			return false;

	// 4 - the town's ranged total genuinely outnumbers the attacker's.
	if(!hasRangedAdvantage(battleID, estimate))
		return false;

	// 5 - every one of our units is either flying or already inside.
	for(const auto * s : battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
	{
		if(!s->alive())
			continue;
		if(s->hasBonusOfType(BonusType::FLYING))
			continue;
		// PHILAI-GAP: combatManager::InCastle tests a hex against the town's interior region;
		// VCMI exposes no equivalent, so a non-flying stack is conservatively counted as inside.
	}

	return true;
}

const CStack * PhilBattleAI::chooseBallistaTarget(const BattleID & battleID, const CStack * stack) const
{
	// H3: combatManager::ChooseBallistaTarget ai.cpp:43 - the Ballista runs the exact same
	// attack/defense damage math as an ordinary creature attack.
	return chooseShooterTarget(battleID, stack);
}

bool PhilBattleAI::chooseCyclopsAction(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::choose_cyclops_action ai.cpp:475 - the boulder throw is only ever
	// considered when attacking a town with an actual fortification, never on defence.
	const int index = stack->unitType()->getIndex();
	if(index != H3Creature::CYCLOPS && index != H3Creature::CYCLOPS_KING)
		return false;

	if(side != BattleSide::ATTACKER)
		return false;

	auto battle = cb->getBattle(battleID);
	if(!battle || battle->battleGetFortifications().wallsHealth <= 0)
		return false;

	static const std::vector<EWallPart> order = {
		EWallPart::GATE, EWallPart::BOTTOM_WALL, EWallPart::UPPER_WALL,
		EWallPart::BELOW_GATE, EWallPart::OVER_GATE };

	for(auto part : order)
	{
		if(battle->battleGetWallState(part) == EWallState::INTACT || battle->battleGetWallState(part) == EWallState::DAMAGED)
		{
			BattleAction attack;
			attack.actionType = EActionType::CATAPULT;
			attack.side = side;
			attack.stackNumber = stack->unitId();
			attack.aimToHex(BattleHex(static_cast<int>(part)));
			cb->battleMakeUnitAction(battleID, attack);
			return true;
		}
	}

	return false;
}

bool PhilBattleAI::chooseCreatureSpellAction(const BattleID & battleID, const CStack * stack)
{
	// H3: combatManager::choose_spell_action ai.cpp:1794 - a hard-coded whitelist of exactly
	// four creature types. Every other creature gets nothing from this routine at all.
	const int index = stack->unitType()->getIndex();

	const bool resurrector = index == H3Creature::ARCHANGEL || index == H3Creature::PIT_FIEND || index == H3Creature::PIT_LORD;
	// PHILAI-GAP: "Caliph" in the original symbol table has no Shadow of Death equivalent;
	// only the Ogre Mage half of the creature-cast branch is reproduced.
	const bool caster = index == H3Creature::OGRE_MAGE;

	if(!resurrector && !caster)
		return false;

	// PHILAI-GAP: routing these to their real resurrect / creature-cast actions needs the
	// creature's granted-spell bonus, which differs per mod; the whitelist and its gating are
	// reproduced but the action itself falls through to the ordinary path.
	return false;
}

// ---------------------------------------------------------------------------
// III.5 - the spell phase
// ---------------------------------------------------------------------------

bool PhilBattleAI::doSpellAI(const BattleID & battleID, const CStack * activeStack, bool retreating)
{
	// H3: combatManager::DoSpellAI ai.cpp:2699 - the top-level entry point for a side's
	// combat spell phase. It computes the retreat decision first and passes the result
	// straight into cast_spell, which is the literal wiring behind "cast now, we are fleeing".
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return false;

	const CGHeroInstance * hero = battle->battleGetMyHero();
	if(!hero)
		return false;

	if(battle->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
		return false;

	Spellcaster caster(cb, battleID, side, hero);
	return caster.considerSpell(activeStack, retreating);
}

void PhilBattleAI::placeShooters(const BattleID & battleID)
{
	// H3: combatManager::place_shooter ai.cpp:2187
	// Scan every legal starting hex, penalize any position directly adjacent to an enemy army
	// almost to the point of exclusion, and among what is left pick whichever hex has the
	// fewest open neighbouring cells.
	// PHILAI-GAP: VCMI's tactics phase moves units one at a time rather than exposing a free
	// deployment grid, so the scoring is applied to whichever shooters can still be moved.
	auto battle = cb->getBattle(battleID);
	if(!battle)
		return;

	for(const auto * shooter : battle->battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
	{
		if(!shooter->isShooter() || !shooter->willMove())
			continue;

		const BattleHexArray available = battle->battleGetAvailableHexes(shooter, false);
		BattleHex best = BattleHex::INVALID;
		int bestScore = std::numeric_limits<int>::min();

		for(const auto & hex : available)
		{
			int openNeighbours = 0;
			bool adjacentToEnemy = false;

			for(const auto & neighbour : hex.getNeighbouringTiles())
			{
				if(!neighbour.isAvailable())
					continue;

				const auto * occupant = battle->battleGetUnitByPos(neighbour);
				if(!occupant)
					++openNeighbours;
				else if(occupant->unitSide() != side)
					adjacentToEnemy = true;
			}

			// Fewest open neighbours wins: shooters deploy into the most boxed-in hex offered.
			int score = -openNeighbours;
			if(adjacentToEnemy)
				score -= 1000;

			if(score > bestScore)
			{
				bestScore = score;
				best = hex;
			}
		}

		if(best.isValid() && best != shooter->getPosition())
		{
			cb->battleMakeTacticAction(battleID, BattleAction::makeMove(shooter, best));
			return;
		}
	}
}

void PhilBattleAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	if(!placementDone)
	{
		placeShooters(battleID);
		placementDone = true;
	}

	auto battle = cb->getBattle(battleID);
	cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(battle->battleGetTacticsSide()));
}

std::optional<BattleAction> PhilBattleAI::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	// H3: combatManager::AICheckRetreat ai.cpp:162
	if(!battleState.canFlee && !battleState.canSurrender)
		return std::nullopt;

	auto battle = cb->getBattle(battleID);
	const bool defendingSiege = battle && battle->battleGetFortifications().wallsHealth > 0
		&& battleState.ourSide == BattleSide::DEFENDER;

	// The loot the hero carries: every equipped and backpack artifact, each contributing at
	// least half its own base value.
	int lootValue = 0;
	if(battleState.ourHero)
	{
		for(const auto & slot : battleState.ourHero->artifactsWorn)
		{
			const auto * art = slot.second.getArt();
			if(art)
				lootValue += std::max(1, static_cast<int>(art->getType()->getPrice()) / 2);
		}
		for(const auto & art : battleState.ourHero->artifactsInBackpack)
		{
			if(art.getArt())
				lootValue += std::max(1, static_cast<int>(art.getArt()->getType()->getPrice()) / 2);
		}
	}

	// PHILAI-GAP: the AI's own gold reserve and the map's difficulty setting are not carried in
	// BattleStateInfoForRetreat; the mid-range difficulty and a zero treasury are assumed here,
	// which makes the gold-reserve and difficulty threshold terms inert in this call path.
	const int assumedDifficulty = 2;
	const int assumedGold = 0;
	const int surrenderCost = 0;

	const bool retreat = checkRetreat(
		battleState.ourHero,
		battleState.enemyHero,
		battleState.getOurStrength(),
		battleState.getEnemyStrength(),
		defendingSiege,
		true,
		assumedDifficulty,
		assumedGold,
		lootValue,
		surrenderCost);

	if(!retreat)
		return std::nullopt;

	if(battleState.canFlee)
		return BattleAction::makeRetreat(battleState.ourSide);

	return BattleAction::makeSurrender(battleState.ourSide);
}

} // namespace PhilAI
