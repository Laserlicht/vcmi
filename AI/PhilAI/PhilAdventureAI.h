/*
 * PhilAdventureAI.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PhilConstants.h"
#include "PhilEconomy.h"
#include "PhilMapScoring.h"

#include "../../lib/callback/CAdventureAI.h"

#include <map>
#include <memory>
#include <thread>

namespace PhilAI
{

/// H3: philAI (philai.cpp) - the adventure dispatcher.
///
/// The original holds no state of its own (a single opaque byte); everything real lives in the
/// per-player type_AI_player record and the persistent AI struct inside playerData. The same
/// split is kept here: PhilAdventureAI is the dispatcher, PhilEconomy the per-player record.
///
/// The turn is a fixed four-beat script, run identically every turn on every difficulty:
/// setup, a full pass of hero movement, a teardown pass that reacts to what movement produced,
/// and a second movement pass so any hero freed up or hired during teardown still gets to act.
/// move_all_heroes is called exactly twice, hard-coded - this is not a loop to a fixed point.
class PhilAdventureAI : public CAdventureAI
{
	std::shared_ptr<CCallback> cb;
	std::unique_ptr<PhilEconomy> economy;
	std::unique_ptr<PhilMapScoring> mapScoring;

	/// The turn runs on its own thread. VCMI delivers every interface callback - dialogs
	/// included - on the game-events thread, so a turn executed there would block the very
	/// query it is waiting to have answered. The original had no such split: its turn ran
	/// straight through with every action taking effect immediately, which is what the
	/// AI-thread plus waitTillRealize pairing below restores.
	std::thread turnThread;

	void runTurn();

	/// Answers a query without blocking on its round trip. Dialog callbacks arrive on the
	/// game-events thread; waiting there for the reply to come back would deadlock, since that
	/// same thread is what applies it.
	void answerQuery(QueryID queryID, int selection);

	/// Releases the gamestate lock briefly so the game-events thread can drain anything it has
	/// queued - in particular a dialog callback that arrived after the acting request was
	/// already reported as applied. No counterpart in the original, which had no such split.
	void settle();

	/// H3: the per-hero committed target that net_value_of_location's x1.5 stickiness bonus
	/// and move_hero's radius widening both key off.
	std::map<const CGHeroInstance *, int3> committedTargets;

	/// H3: philAI::DoAI clears these per-hero flags in start_turn.
	std::map<const CGHeroInstance *, bool> heroMoved;

	/// H3: type_AI_player::start_turn ai_player.cpp:695
	void startTurn();
	/// H3: move_all_heroes philai.cpp:1239 - a plain work queue, no strategy of its own.
	void moveAllHeroes();
	/// H3: type_AI_player::end_turn ai_player.cpp:414
	void endTurn();

	/// H3: DetermineHeroToMove philai.cpp:1156
	const CGHeroInstance * determineHeroToMove() const;

	/// H3: move_hero philai.cpp:934 - decides how far to search before any scoring runs, then
	/// escalates the radius if nothing is found.
	bool moveHero(const CGHeroInstance * hero, bool isLastHero);

	/// H3: AI_AttemptMove ai_player.cpp:4179 - walks the path one step at a time, re-choosing
	/// the destination mid-move when a step is flagged and enough mobility remains.
	bool attemptMove(const CGHeroInstance * hero, const int3 & destination);

	/// H3: AI_enter_town philai.cpp:732 - an unconditional eight-step sequence, fixed order.
	void enterTown(const CGTownInstance * town, const CGHeroInstance * hero);

	/// H3: consider_garrisoning philai.cpp:687 and should_garrison_town philai.cpp:662
	void considerGarrisoning(const CGTownInstance * town, const CGHeroInstance * hero);

	/// H3: get_best_hero philai.cpp:636 - the summed-primary-skills metric used everywhere
	/// heroes are compared in this AI.
	const CGHeroInstance * getBestHero() const;

	/// H3: type_AI_player::buy_mage_guild ai_player.cpp:1954
	void buyMageGuild(const CGTownInstance * town, const CGHeroInstance * hero);

	/// H3: AI_visit_hill_fort philai.cpp:2513 - unconditional "buy every affordable upgrade".
	void visitHillFort(const CGTownInstance * town, const CGHeroInstance * hero);

	/// H3: AI_friendly_hero_meeting philai.cpp:811
	void friendlyHeroMeeting(const CGHeroInstance * a, const CGHeroInstance * b);

	/// H3: AI_arrange_army ai_player.cpp:2718 - ranged stacks into alternating slots so a
	/// single area attack cannot catch them all.
	void arrangeArmy(const CGHeroInstance * hero);

public:
	PhilAdventureAI() = default;
	~PhilAdventureAI() override;

	std::string getBattleAIName() const override;
	void finish() override;

	void initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB) override;
	void yourTurn(QueryID queryID) override;

	/// H3: AI_choose_secondary_skill philai.cpp:4180 and wants_skill philai.cpp:3645
	void heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills, QueryID queryID) override;
	void commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID) override;

	void showBlockingDialog(const std::string & text, const std::vector<Component> & components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept) override;
	void showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle) override;
	void showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID) override;
	void showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects) override;

	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState) override;
};

/// H3: get_skill_value philai.cpp:3469
/// Every skill's score starts from one shared base - 1,000 plus the AI's assessed value of the
/// hero's whole army, with a separate running subtotal for stacks carrying the shooter trait -
/// which each skill then divides, multiplies or gates in its own way.
int getSkillValue(const CGHeroInstance * hero, SecondarySkill skill, const PhilEconomy & economy);

/// H3: wants_skill philai.cpp:3645 - scores every skill the hero does not yet hold, sorts them,
/// and accepts a new one only if it lands within the remaining skill-slot budget.
bool wantsSkill(const CGHeroInstance * hero, SecondarySkill skill, const PhilEconomy & economy, int slotBudget);

} // namespace PhilAI
