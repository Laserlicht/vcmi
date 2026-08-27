/*
 * H3AI.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Context.h"
#include "H3Movement.h"

#include "../../lib/callback/CAdventureAI.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

class AsyncRunner;

namespace H3AI
{

/// A reimplementation of the original Heroes III adventure-map AI, transcribed from a
/// reverse-engineering report of the shipped binary (Complete 4.0).  Section markers of
/// the form "SS 4.x" throughout this AI refer to that report.
///
/// Battles are ceded to the configured battle AI: the report's battle chapters (SS 5)
/// are deliberately out of scope here.
class H3AdventureAI : public CAdventureAI
{
public:
	/// Defined out of line: AsyncRunner is only forward-declared here, so the unique_ptr
	/// member needs the complete type at the point the destructor is instantiated.
	H3AdventureAI();
	~H3AdventureAI();

	std::string getBattleAIName() const override;

	void finish() override;

	void initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB) override;
	void yourTurn(QueryID queryID) override;

	void heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills, QueryID queryID) override;
	void commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID) override;
	void showBlockingDialog(const std::string & text, const std::vector<Component> & components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept) override;
	void showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle) override;
	void showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID) override;
	void showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects) override;
	void showRecruitmentDialog(const CGDwelling * dwelling, const CArmedInstance * dst, int level, QueryID queryID) override;
	void showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QueryID queryID) override;
	void showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID) override;
	void showUniversityWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID) override;
	void heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID queryID) override;
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState) override;

	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override;
	void battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID) override;
	void battleEnded() override;

private:
	/// SS 4.1 - advManager::AI_take_turn @ 0x525E80.
	void takeTurn();

	/// Every request this AI issues runs with CCallback::waitTillRealize set, which
	/// blocks the calling thread until the server confirms.  That must never happen on
	/// the network dispatch thread the interface callbacks arrive on, so both the turn
	/// itself and every query reply are handed to a worker.
	///
	/// @param beforeReply  optional work to run on that worker, with the game-state lock
	///        held, before the reply is sent - a window query that the AI wants to act on
	///        (recruitment) must issue its requests while the window is still open.
	void answerQueryAsync(QueryID queryID, int selection, std::function<void()> beforeReply = {});

	/// The server refuses every adventure-map action while any query sits on the player's
	/// query stack - a CBattleQuery for the battle itself, then a CHeroLevelUpDialogQuery
	/// for whoever levelled up in it.  The turn worker parks here, releasing the
	/// game-state lock, until the battle is over *and* every query has been answered.
	void waitWhileBusy();

	std::mutex busyMutex;
	std::condition_variable busyChanged;
	bool battleInProgress = false;
	/// Incremented on the network thread as each query is delivered, decremented once the
	/// reply has actually been sent.
	int pendingQueries = 0;

	std::unique_ptr<AsyncRunner> asyncTasks;

	std::shared_ptr<CCallback> cb;
	bool openMap = false;
	H3Player player;
	HeroStateMap heroStates;
	ValueMap dangerMap;
};

}

#define H3AI_NAME "H3AI 1.0"
