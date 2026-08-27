/*
 * H3AI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3AI.h"

#include "H3ArtifactValue.h"
#include "H3Kingdom.h"
#include "H3SecondarySkills.h"
#include "H3Valuations.h"

#include "../../lib/AsyncRunner.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"

#include <algorithm>
#include <shared_mutex>

namespace H3AI
{

H3AdventureAI::H3AdventureAI() = default;

H3AdventureAI::~H3AdventureAI()
{
	if(asyncTasks)
		asyncTasks->wait();
}

std::string H3AdventureAI::getBattleAIName() const
{
	// SS 5 (the battle chapters) is out of scope for this adventure-map reimplementation,
	// so battles are ceded to the engine's configured battle AI.
	return "BattleAI";
}

void H3AdventureAI::initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB)
{
	cb = CB;
	cbc = CB;
	env = ENV;
	human = false;
	playerID = *cb->getPlayerID();

	// Every request this AI issues (build, recruit, hire, move) must be realized by the
	// server before the next decision is taken: the whole adventure AI is a greedy loop
	// that re-reads game state after each action (SS 4.10 step 4, SS 4A.4).  Without
	// this the loop would re-pick an action it has already committed to.
	cb->waitTillRealize = true;

	asyncTasks = std::make_unique<AsyncRunner>();

	// Map-open cheat, on the same terms the other adventure AI uses it: only when the
	// scenario allows cheats at all, and never when a human shares this AI's team.
	openMap = false;

	if(cb->getStartInfo()->extraOptionsInfo.cheatsAllowed)
	{
		const TeamState * team = cb->getPlayerTeam(playerID);
		bool humanInTeam = false;

		if(team != nullptr)
		{
			for(const PlayerColor & mate : team->players)
			{
				const PlayerState * mateState = cb->getPlayerState(mate, false);

				if(mateState != nullptr && mateState->human)
					humanInTeam = true;
			}
		}

		openMap = !humanInTeam;
	}

	logAi->info("H3AI for player %s: open map is %s", playerID.toString(), openMap ? "on" : "off");

	player.init(cb.get(), playerID);
}

void H3AdventureAI::finish()
{
	if(asyncTasks)
	{
		asyncTasks->wait();
		asyncTasks.reset();
	}
}

void H3AdventureAI::answerQueryAsync(QueryID queryID, int selection)
{
	// A query the server does not expect an answer to arrives as QueryID::NONE; replying
	// to it is the "Cannot answer the query -1!" error in CCallback::sendQueryReply.
	if(queryID == QueryID::NONE)
		return;

	if(!asyncTasks)
	{
		cb->selectionMade(selection, queryID);
		return;
	}

	// Registered here, on the thread the query was delivered on, so the turn worker can
	// never slip past a query that has already arrived.
	{
		std::lock_guard lock(busyMutex);
		++pendingQueries;
	}

	asyncTasks->run([this, queryID, selection]()
	{
		ScopedThreadName guard("H3AI::answerQuery");

		{
			// The shared lock is mandatory, not merely protective: CClient::sendRequest's
			// waitTillRealize path releases CGameState::mutex through
			// makeUnlockSharedGuard, which calls unlock_shared() unconditionally.
			// Replying without holding the lock would unlock a mutex this thread never
			// owned and wedge every other thread.
			std::shared_lock gsLock(CGameState::mutex);

			cb->selectionMade(selection, queryID);
		}

		{
			std::lock_guard lock(busyMutex);
			--pendingQueries;
		}

		busyChanged.notify_all();
	});
}

void H3AdventureAI::yourTurn(QueryID queryID)
{
	answerQueryAsync(queryID, 0);

	// SS 4.1's turn driver is a long synchronous loop that issues blocking requests; it
	// cannot run on the thread the interface callbacks are dispatched on.
	asyncTasks->run([this]()
	{
		ScopedThreadName guard("H3AI::makingTurn");
		std::shared_lock gsLock(CGameState::mutex);

		takeTurn();

		cb->endTurn();
	});
}

void H3AdventureAI::takeTurn()
{
	// SS 4.1 - advManager::AI_take_turn @ 0x525E80
	H3Context ctx;
	ctx.cb = cb.get();
	ctx.player = &player;
	ctx.heroStates = &heroStates;
	ctx.victory = getVictoryConditionInfo(cb.get());
	ctx.openMap = openMap;

	// 2. value_map = calloc((levels) * MAP_W * MAP_H * sizeof(int32))
	dangerMap.resize(cb->getMapSize());

	// 3. AI_player[player].begin_turn()
	player.beginTurn();

	// 4. advManager::AI_prepare(player)  -> 0x527960
	// SS 4G.1 - advManager::AI_prepare @ 0x527960 does three things, in order:
	//   1. hero::AI_update_valuations on every hero we own (SS 4.9b);
	//   2. playerData + 0x164 = the MEAN of AI_get_value_of_artifact over every artifact
	//      whose traits byte + 0x1C is zero, priced against our best-placed hero;
	//   3. the two difficulty-derived combat bonuses, which are NOT constants - see
	//      H3Player::getAttackBonus.
	// Step 1 is the per-hero valuation refresh below.  Step 2:
	{
		int64_t sum = 0;
		int n = 0;

		for(int a = AI_PREPARE_FIRST_ARTIFACT; a <= AI_PREPARE_LAST_ARTIFACT; ++a)
		{
			const ArtifactID id(a);
			const CArtifact * art = id.toArtifact();

			// traits + 0x1C != 0 means "not AI-tradable" in the original
			if(art == nullptr || !art->isTradable())
				continue;

			++n;
			sum += artifactValueForPlayer(ctx, id);
		}

		player.setAverageArtifactValue(n > 0 ? static_cast<int>(sum / n) : 0);
	}

	// 5. the "computer is thinking" progress counter is pure UI.

	// Per-turn hero bookkeeping: wake every hero up and refresh the valuations that
	// SS 4.9's hero::AI_update_valuations recomputes.
	for(const CGHeroInstance * hero : cb->getHeroesInfo())
	{
		HeroAIState & state = heroStates[hero->id];
		state.done = false;
		state.valuations = computeHeroValuations(hero);
	}

	bool magusHutFlag = true;

	// 6. PASS 1 - special heroes.
	// SS 4G.2 - AI_pick_special_hero @ 0x526A90.  "Special" means the LEAST developed
	// hero that is not already committed to a destination - the scout:
	//   skip heroes with no movement left or already done this turn;
	//   prefer one with no previous destination (hero + 0x44 == 0xFF);
	//   among equals, prefer the LOWER get_primary_skill_sum.
	// If nobody can move it falls back to waking a garrisoned hero out of a town, which
	// is why AI towns so often empty themselves late in a turn.

	// 7. KINGDOM PHASE
	manageKingdom(ctx);

	// 8. PASS 2 - the main hero loop.
	while(true)
	{
		if(cb->getPlayerStatus(playerID, false) != EPlayerStatus::INGAME)
			break;

		// SS 4.1 - "pick the town-visiting / best hero by the primary-skill priority
		// score; among equals it prefers a hero whose previous destination is set."
		const CGHeroInstance * chosen = nullptr;
		int bestScore = -1;
		bool bestHasPrevious = false;

		for(const CGHeroInstance * hero : cb->getHeroesInfo())
		{
			if(hero->isGarrisoned())
				continue;

			HeroAIState & state = heroStates[hero->id];

			if(state.done)
				continue;

			const int score = primarySkillSum(hero);
			const bool hasPrevious = state.hasPreviousDestination();

			if(score > bestScore || (score == bestScore && hasPrevious && !bestHasPrevious))
			{
				bestScore = score;
				bestHasPrevious = hasPrevious;
				chosen = hero;
			}
		}

		if(chosen == nullptr)
		{
			// SS 4.1 - "if none, pick any garrisoned hero in a town with a free slot and
			// mobilise it".
			for(const CGTownInstance * town : cb->getTownsInfo(true))
			{
				const CGHeroInstance * garrisoned = town->getGarrisonHero();

				if(garrisoned == nullptr || town->getVisitingHero() != nullptr)
					continue;

				HeroAIState & state = heroStates[garrisoned->id];

				if(state.done)
					continue;

				cb->swapGarrisonHero(town);
				chosen = garrisoned;
				break;
			}
		}

		if(chosen == nullptr)
			break;

		// Anything below may block and let the server run a battle in which this hero
		// dies, so the pointer is not held across those calls - only its id is.
		const ObjectInstanceID heroId = chosen->id;

		(void)heroStates[heroId];

		// SS 4.2 step 1 - a hero standing in one of our own towns visits it first.
		const CGTownInstance * visitedTown = chosen->getVisitedTown();

		if(visitedTown != nullptr && visitedTown->getOwner() == playerID)
		{
			visitOwnTown(ctx, chosen, visitedTown);

			chosen = cb->getHero(heroId);

			if(chosen == nullptr || chosen->getOwner() != playerID)
			{
				heroStates.erase(heroId);
				continue;
			}
		}

		const int movementBefore = chosen->movementPointsRemaining();

		heroTurn(ctx, chosen, dangerMap, false, magusHutFlag);

		// Moving onto an enemy starts a battle; its CBattleQuery, and the level-up query
		// that may follow it, block every further adventure-map action until answered.
		waitWhileBusy();

		// The hero may have lost that battle: a removed object keeps pos = (-1,-1,-1) and
		// an invalid owner, so every later use of it would be garbage.
		chosen = cb->getHero(heroId);

		if(chosen == nullptr || chosen->getOwner() != playerID)
		{
			heroStates.erase(heroId);
			continue;
		}

		// Guard against a hero that neither moved nor gave up: the original relies on
		// hero->mp being zeroed on every dead end (SS 4B.11 names two independent paths
		// to that outcome), so a hero that still has all its movement is done.
		HeroAIState & state = heroStates[heroId];

		if(!state.done && chosen->movementPointsRemaining() == movementBefore)
			state.done = true;
	}
}

void H3AdventureAI::heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills, QueryID queryID)
{
	// SS 4.12 - the level-up secondary-skill choice.
	if(skills.empty())
	{
		answerQueryAsync(queryID, 0);
		return;
	}

	H3Context ctx;
	ctx.cb = cb.get();
	ctx.player = &player;
	ctx.heroStates = &heroStates;
	ctx.victory = getVictoryConditionInfo(cb.get());
	ctx.openMap = openMap;

	// The original is offered exactly two skills; VCMI may offer one or two.
	SecondarySkill chosen = skills.front();

	if(skills.size() >= 2)
	{
		// SS 4.12 - hero::LevelUp passes useArmy = TRUE.  The flag does two things: it
		// turns on the army / shooter accumulation, AND it enables the four prerequisite
		// gates (Eagle Eye and Scholar need Wisdom, Artillery a Ballista, First Aid a
		// Tent).  No AI call site in the original passes false.
		chosen = chooseSecondarySkill(ctx, hero, skills[0], skills[1], true);
	}

	const auto it = std::find(skills.begin(), skills.end(), chosen);
	const int index = it != skills.end() ? static_cast<int>(std::distance(skills.begin(), it)) : 0;

	answerQueryAsync(queryID, index);
}

void H3AdventureAI::commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID)
{
	// Commanders do not exist in the original game, so the report says nothing about them.
	// TODO: no documented behaviour to reproduce.
	answerQueryAsync(queryID, 0);
}

void H3AdventureAI::showBlockingDialog(const std::string & text, const std::vector<Component> & components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept)
{
	// TODO: the report does not cover the AI's dialog answers.  Accepting is the choice
	// that lets a march continue, which is what every documented decision assumes.
	answerQueryAsync(askID, 1);
}

void H3AdventureAI::showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle)
{
	// SS 4B.4's planner is what decides troop movement; the dialog itself is just closed.
	answerQueryAsync(queryID, 0);
}

void H3AdventureAI::showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID)
{
	// SS 4.8 / SS 6 - "the SoD AI has no notion of using Monoliths, Subterranean Gates,
	// Whirlpools ... as goals.  It only ever walks through a portal if the path happens to
	// run over it."  There is therefore no documented exit-choice rule.
	answerQueryAsync(askID, 0);
}

void H3AdventureAI::showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects)
{
	// TODO: not covered by the report.
	answerQueryAsync(askID, 0);
}

void H3AdventureAI::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed)
{
	{
		std::lock_guard lock(busyMutex);
		battleInProgress = true;
	}

	CAdventureAI::battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void H3AdventureAI::battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID)
{
	// Only tears down the battle AI.  The flag stays set until battleEnded(), which the
	// server sends once the results have been applied - the level-up query that follows a
	// battle is delivered in that window, and resuming before it arrives is what makes
	// the server reject the next action.
	CAdventureAI::battleEnd(battleID, br, queryID);
}

void H3AdventureAI::battleEnded()
{
	{
		std::lock_guard lock(busyMutex);
		battleInProgress = false;
	}

	busyChanged.notify_all();
}

void H3AdventureAI::waitWhileBusy()
{
	std::unique_lock lock(busyMutex);

	if(!battleInProgress && pendingQueries == 0)
		return;

	// The game-state lock must be dropped while parked here, or the network thread can
	// never apply the packs it needs the unique lock for - and the battle would never
	// end.  This thread does hold the shared lock, so releasing it is legitimate.
	auto gsUnlocker = vstd::makeUnlockSharedGuard(CGameState::mutex);

	busyChanged.wait(lock, [this]() { return !battleInProgress && pendingQueries == 0; });
}

std::optional<BattleAction> H3AdventureAI::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	// SS 5.9 covers combatManager::AI_should_flee, which is battle AI and out of scope
	// for this adventure-map reimplementation.
	return std::nullopt;
}

}
