/*
 * JournalVisitor.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include "../../lib/networkPacks/NetPackVisitor.h"

namespace mcptool
{
class EventJournal;
class RequestTracker;
class QueryRegistry;
}

/// Observes every CPackForClient applied by this client (hooked from CClient::handlePack) and
/// turns it into MCP-visible state: completed action results (RequestTracker), open dialogs
/// that need an answer (QueryRegistry), and a general activity feed (EventJournal) an LLM
/// client can poll or long-poll to see the consequences of its actions or of AI/other players.
///
/// Deliberately does not read back into CGameState/CCallback for extra context (e.g. resolving
/// a hero id into a name) - it runs on the network thread for every single applied pack, so it
/// only uses fields already present on the pack itself. Callers resolve ids via the normal read
/// tools (get_hero_details, get_town_details, ...).
class JournalVisitor : public VCMI_LIB_WRAP_NAMESPACE(ICPackVisitor)
{
public:
	JournalVisitor(mcptool::EventJournal & journal, mcptool::RequestTracker & tracker, mcptool::QueryRegistry & queries);

	void visitPackageApplied(PackageApplied & pack) override;

	void visitHeroLevelUp(HeroLevelUp & pack) override;
	void visitCommanderLevelUp(CommanderLevelUp & pack) override;
	void visitBlockingDialog(BlockingDialog & pack) override;
	void visitGarrisonDialog(GarrisonDialog & pack) override;
	void visitExchangeDialog(ExchangeDialog & pack) override;
	void visitTeleportDialog(TeleportDialog & pack) override;
	void visitMapObjectSelectDialog(MapObjectSelectDialog & pack) override;
	void visitOpenWindow(OpenWindow & pack) override;
	void visitBattleResult(BattleResult & pack) override;

	void visitSystemMessage(SystemMessage & pack) override;
	void visitPlayerBlocked(PlayerBlocked & pack) override;
	void visitPlayerStartsTurn(PlayerStartsTurn & pack) override;
	void visitPlayerEndsTurn(PlayerEndsTurn & pack) override;
	void visitPlayerEndsGame(PlayerEndsGame & pack) override;
	void visitDaysWithoutTown(DaysWithoutTown & pack) override;
	void visitSetPrimarySkill(SetPrimarySkill & pack) override;
	void visitSetHeroExperience(SetHeroExperience & pack) override;
	void visitHeroVisitCastle(HeroVisitCastle & pack) override;
	void visitGiveBonus(GiveBonus & pack) override;
	void visitRemoveBonus(RemoveBonus & pack) override;
	void visitAddQuest(AddQuest & pack) override;
	void visitRemoveObject(RemoveObject & pack) override;
	void visitTryMoveHero(TryMoveHero & pack) override;
	void visitNewStructures(NewStructures & pack) override;
	void visitRazeStructures(RazeStructures & pack) override;
	void visitHeroRecruited(HeroRecruited & pack) override;
	void visitGiveHero(GiveHero & pack) override;
	void visitNewObject(NewObject & pack) override;
	void visitHeroVisit(HeroVisit & pack) override;
	void visitInfoWindow(InfoWindow & pack) override;
	void visitNewTurn(NewTurn & pack) override;
	void visitSetObjectProperty(SetObjectProperty & pack) override;
	void visitAdvmapSpellCast(AdvmapSpellCast & pack) override;
	void visitPlayerMessageClient(PlayerMessageClient & pack) override;
	void visitResponseStatistic(ResponseStatistic & pack) override;

	void visitBattleStart(BattleStart & pack) override;
	void visitBattleNextRound(BattleNextRound & pack) override;
	void visitBattleSetActiveStack(BattleSetActiveStack & pack) override;
	void visitBattleCancelled(BattleCancelled & pack) override;
	void visitBattleLogMessage(BattleLogMessage & pack) override;
	void visitBattleAttack(BattleAttack & pack) override;
	void visitBattleSpellCast(BattleSpellCast & pack) override;
	void visitStacksInjured(StacksInjured & pack) override;
	void visitBattleResultsApplied(BattleResultsApplied & pack) override;
	void visitCatapultAttack(CatapultAttack & pack) override;
	void visitBattleTriggerEffect(BattleTriggerEffect & pack) override;

private:
	mcptool::EventJournal & journal;
	mcptool::RequestTracker & tracker;
	mcptool::QueryRegistry & queries;

	/// Registers a query in the registry and journals its arrival with the same description.
	void openQuery(int32_t queryId, const std::string & kind, JsonNode description);
};
