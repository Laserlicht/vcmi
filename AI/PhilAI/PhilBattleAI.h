/*
 * PhilBattleAI.h, part of VCMI engine
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
#include "../../lib/battle/BattleHexArray.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/callback/CBattleGameInterface.h"

#include <array>
#include <vector>

class BattleStateInfoForRetreat;
class CStack;

namespace PhilAI
{

/// H3: type_AI_combat_parameters (ai_tactical.cpp:393) - the shared "current combat-value
/// estimate" record every hex- and target-scoring routine borrows from.
struct CombatParameters
{
	int lowestAttack = 0;
	int lowestDefense = 0;
	bool killsOnly = false;
	int friendlyCombatValue = 0;
	int enemyCombatValue = 0;
	int roundsLeft = Const::SIMULATION_ROUNDS;
	BattleSide group = BattleSide::ATTACKER;

	BattleSide enemyGroup() const;
};

/// H3: combatManager's AI half (ai.cpp) - the live battle turn loop.
///
/// Structure follows the original exactly: DoCompAI is a flat triage that routes each acting
/// stack to a specialist, and every hex-safety judgement those specialists make reads the
/// threat array mark_enemy_attacks builds once per turn.
class PhilBattleAI : public CBattleGameInterface
{
	BattleSide side = BattleSide::NONE;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;
	bool wasWaitingForRealize = false;
	bool placementDone = false;

	/// H3: combatManager::mark_enemy_attacks ai.cpp:1241 - which hexes each living enemy
	/// stack could actually threaten, given its real speed and reach. Rebuilt once per turn.
	std::vector<int> enemyThreat;

	/// H3: combatManager::mark_firewalls ai.cpp:1866 - live obstacle damage, subtracted from
	/// a hex's attack value so the AI is measurably discouraged from routing through one.
	std::vector<int> hexHazard;

	CombatParameters buildEstimate(const BattleID & battleID) const;

	void markEnemyAttacks(const BattleID & battleID);
	void markFirewalls(const BattleID & battleID);

	/// H3: combatManager::DoCompAI ai.cpp:2272 - flat triage, no weighing of alternatives.
	void doCompAI(const BattleID & battleID, const CStack * stack);

	/// H3: combatManager::choose_shooter_action ai.cpp:558
	bool chooseShooterAction(const BattleID & battleID, const CStack * stack);
	/// H3: combatManager::choose_shooter_target ai.cpp:397
	const CStack * chooseShooterTarget(const BattleID & battleID, const CStack * stack) const;

	/// H3: combatManager::choose_melee_action ai.cpp:2159
	bool chooseMeleeAction(const BattleID & battleID, const CStack * stack);
	/// H3: combatManager::choose_melee_target ai.cpp:1896
	bool chooseMeleeTarget(const BattleID & battleID, const CStack * stack);
	/// H3: combatManager::move_toward ai.cpp:779
	bool moveToward(const BattleID & battleID, const CStack * stack);

	/// H3: combatManager::attempt_shooter_defense ai.cpp:1445
	bool attemptShooterDefense(const BattleID & battleID, const CStack * stack);
	/// H3: combatManager::choose_defense_hex ai.cpp:1357
	BattleHex chooseDefenseHex(const BattleID & battleID, const CStack * stack, const CStack * shooter, int & travelTime) const;

	/// H3: combatManager::place_shooter ai.cpp:2187 - initial deployment.
	void placeShooters(const BattleID & battleID);

	/// H3: combatManager::ChooseBallistaTarget ai.cpp:43
	const CStack * chooseBallistaTarget(const BattleID & battleID, const CStack * stack) const;
	/// H3: combatManager::choose_cyclops_action ai.cpp:475
	bool chooseCyclopsAction(const BattleID & battleID, const CStack * stack);
	/// H3: combatManager::choose_spell_action ai.cpp:1794 - four hard-coded creature types.
	bool chooseCreatureSpellAction(const BattleID & battleID, const CStack * stack);

	/// H3: combatManager::has_ranged_advantage ai.cpp:1572
	bool hasRangedAdvantage(const BattleID & battleID, const CombatParameters & estimate) const;
	/// H3: combatManager::should_stay_in_castle ai.cpp:1822
	bool shouldStayInCastle(const BattleID & battleID, const CombatParameters & estimate) const;

	/// H3: combatManager::DoSpellAI ai.cpp:2699 - the side's combat spell phase.
	bool doSpellAI(const BattleID & battleID, const CStack * activeStack, bool retreating);

	/// H3: get_attack_value ai.cpp:696 - one attack's worth in combat-value currency.
	int getAttackValue(const BattleID & battleID, const CStack * attacker, const CStack * defender, const BattleHex & from) const;

public:
	PhilBattleAI();
	~PhilBattleAI() override;

	void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB) override;
	void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences) override;

	void activeStack(const BattleID & battleID, const CStack * stack) override;
	void yourTacticPhase(const BattleID & battleID, int distance) override;

	void actionFinished(const BattleID & battleID, const BattleAction & action) override;
	void actionStarted(const BattleID & battleID, const BattleAction & action) override;
	void battleAttack(const BattleID & battleID, const BattleAttack * ba) override;
	void battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & bsa, bool ranged) override;
	void battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID) override;
	void battleNewRoundFirst(const BattleID & battleID) override;
	void battleNewRound(const BattleID & battleID) override;
	void battleStackMoved(const BattleID & battleID, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport) override;
	void battleSpellCast(const BattleID & battleID, const BattleSpellCast * sc) override;
	void battleStacksEffectsSet(const BattleID & battleID, const SetStackEffect & sse) override;
	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override;
	void battleCatapultAttacked(const BattleID & battleID, const CatapultAttack & ca) override;

	/// H3: combatManager::AICheckRetreat ai.cpp:162
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState);
};

/// H3: get_move_order ai.cpp:610 - creature speed with a few hard exceptions.
int getMoveOrder(const CStack * stack, bool secondPhase);

/// H3: combatManager::AICheckRetreat ai.cpp:162, available separately so the adventure AI can
/// reuse the identical gauntlet when deciding a pre-battle go/no-go.
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
	int surrenderCost);

} // namespace PhilAI
