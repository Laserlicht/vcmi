/*
 * JournalVisitor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "JournalVisitor.h"

#include "../../lib/json/JsonNode.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/mapObjects/CGObjectInstance.h"
#include "../../lib/gameState/EVictoryLossCheckResult.h"

#include "EventJournal.h"
#include "RequestTracker.h"
#include "QueryRegistry.h"
#include "Serializers.h"

namespace
{
	JsonNode int3ToJson(const int3 & pos)
	{
		JsonNode entry;
		entry["x"] = JsonNode(pos.x);
		entry["y"] = JsonNode(pos.y);
		entry["z"] = JsonNode(pos.z);
		return entry;
	}
}

JournalVisitor::JournalVisitor(mcptool::EventJournal & journal, mcptool::RequestTracker & tracker, mcptool::QueryRegistry & queries)
	: journal(journal)
	, tracker(tracker)
	, queries(queries)
{
}

void JournalVisitor::openQuery(int32_t queryId, const std::string & kind, JsonNode description)
{
	description["queryId"] = JsonNode(queryId);
	description["kind"] = JsonNode(kind);
	queries.add(queryId, description);
	journal.push("queryOpened", description);
}

void JournalVisitor::visitPackageApplied(PackageApplied & pack)
{
	tracker.reportApplied(pack.result);
	if(!pack.result)
	{
		JsonNode data;
		data["player"] = JsonNode(pack.player.toString());
		journal.push("actionRejected", data);
	}
}

void JournalVisitor::visitHeroLevelUp(HeroLevelUp & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["heroId"] = JsonNode(pack.heroId.getNum());
	data["primarySkill"] = JsonNode(pack.primskill.getNum());
	JsonNode skills;
	for(auto & s : pack.skills)
		skills.Vector().push_back(JsonNode(s.getNum()));
	data["skillOptions"] = skills;
	openQuery(pack.queryID.getNum(), "heroLevelUp", data);
}

void JournalVisitor::visitCommanderLevelUp(CommanderLevelUp & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["heroId"] = JsonNode(pack.heroId.getNum());
	JsonNode skills;
	for(auto & s : pack.skills)
		skills.Vector().push_back(JsonNode(static_cast<int>(s)));
	data["skillOptions"] = skills;
	openQuery(pack.queryID.getNum(), "commanderLevelUp", data);
}

void JournalVisitor::visitBlockingDialog(BlockingDialog & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["text"] = JsonNode(pack.text.toString());
	data["components"] = componentsToJson(pack.components);
	data["allowCancel"] = JsonNode(pack.cancel());
	data["selection"] = JsonNode(pack.selection());
	openQuery(pack.queryID.getNum(), "blockingDialog", data);
}

void JournalVisitor::visitGarrisonDialog(GarrisonDialog & pack)
{
	JsonNode data;
	data["objectId"] = JsonNode(pack.objid.getNum());
	data["heroId"] = JsonNode(pack.hid.getNum());
	data["removableUnits"] = JsonNode(pack.removableUnits);
	openQuery(pack.queryID.getNum(), "garrisonDialog", data);
}

void JournalVisitor::visitExchangeDialog(ExchangeDialog & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["hero1Id"] = JsonNode(pack.hero1.getNum());
	data["hero2Id"] = JsonNode(pack.hero2.getNum());
	openQuery(pack.queryID.getNum(), "exchangeDialog", data);
}

void JournalVisitor::visitTeleportDialog(TeleportDialog & pack)
{
	JsonNode data;
	data["heroId"] = JsonNode(pack.hero.getNum());
	data["channel"] = JsonNode(pack.channel.getNum());
	data["impassable"] = JsonNode(pack.impassable);
	JsonNode exits;
	for(auto & [objId, pos] : pack.exits)
	{
		JsonNode exit;
		exit["objectId"] = JsonNode(objId.getNum());
		exit["position"] = int3ToJson(pos);
		exits.Vector().push_back(exit);
	}
	data["exits"] = exits;
	openQuery(pack.queryID.getNum(), "teleportDialog", data);
}

void JournalVisitor::visitMapObjectSelectDialog(MapObjectSelectDialog & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["icon"] = componentToJson(pack.icon);
	data["title"] = JsonNode(pack.title.toString());
	data["description"] = JsonNode(pack.description.toString());
	JsonNode objects;
	for(auto & id : pack.objects)
		objects.Vector().push_back(JsonNode(id.getNum()));
	data["objects"] = objects;
	openQuery(pack.queryID.getNum(), "mapObjectSelectDialog", data);
}

void JournalVisitor::visitOpenWindow(OpenWindow & pack)
{
	JsonNode data;
	data["window"] = JsonNode(static_cast<int>(pack.window));
	data["objectId"] = JsonNode(pack.object.getNum());
	data["visitorId"] = JsonNode(pack.visitor.getNum());
	openQuery(pack.queryID.getNum(), "openWindow", data);
}

void JournalVisitor::visitBattleResult(BattleResult & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["result"] = JsonNode(static_cast<int>(pack.result));
	data["winnerSide"] = JsonNode(static_cast<int>(pack.winner));
	journal.push("battleResult", data);
	openQuery(pack.queryID.getNum(), "battleResult", data);
}

void JournalVisitor::visitSystemMessage(SystemMessage & pack)
{
	JsonNode data;
	data["text"] = JsonNode(pack.text.toString());
	journal.push("systemMessage", data);
}

void JournalVisitor::visitPlayerBlocked(PlayerBlocked & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["reason"] = JsonNode(static_cast<int>(pack.reason));
	data["start"] = JsonNode(pack.startOrEnd == PlayerBlocked::BLOCKADE_STARTED);
	journal.push("playerBlocked", data);
}

void JournalVisitor::visitPlayerStartsTurn(PlayerStartsTurn & pack)
{
	// Auto-answered by CPlayerInterface::acceptTurn - not a query an MCP client needs to answer.
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	journal.push("playerStartsTurn", data);
}

void JournalVisitor::visitPlayerEndsTurn(PlayerEndsTurn & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	journal.push("playerEndsTurn", data);
}

void JournalVisitor::visitPlayerEndsGame(PlayerEndsGame & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["victory"] = JsonNode(pack.victoryLossCheckResult.victory());
	data["message"] = JsonNode(pack.victoryLossCheckResult.messageToSelf.toString());
	journal.push("playerEndsGame", data);
}

void JournalVisitor::visitDaysWithoutTown(DaysWithoutTown & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["daysWithoutCastle"] = pack.daysWithoutCastle.has_value() ? JsonNode(*pack.daysWithoutCastle) : JsonNode();
	journal.push("daysWithoutTown", data);
}

void JournalVisitor::visitSetPrimarySkill(SetPrimarySkill & pack)
{
	JsonNode data;
	data["heroId"] = JsonNode(pack.id.getNum());
	data["skill"] = JsonNode(pack.which.getNum());
	data["value"] = JsonNode(static_cast<si64>(pack.val));
	data["mode"] = JsonNode(static_cast<int>(pack.mode));
	journal.push("heroPrimarySkillChanged", data);
}

void JournalVisitor::visitSetHeroExperience(SetHeroExperience & pack)
{
	JsonNode data;
	data["heroId"] = JsonNode(pack.id.getNum());
	data["value"] = JsonNode(static_cast<si64>(pack.val));
	data["mode"] = JsonNode(static_cast<int>(pack.mode));
	journal.push("heroExperienceChanged", data);
}

void JournalVisitor::visitHeroVisitCastle(HeroVisitCastle & pack)
{
	JsonNode data;
	data["townId"] = JsonNode(pack.tid.getNum());
	data["heroId"] = JsonNode(pack.hid.getNum());
	data["start"] = JsonNode(pack.start());
	journal.push("heroVisitCastle", data);
}

void JournalVisitor::visitGiveBonus(GiveBonus & pack)
{
	JsonNode data;
	data["target"] = JsonNode(static_cast<int>(pack.who));
	data["id"] = JsonNode(pack.id.getNum());
	data["bonusType"] = JsonNode(static_cast<int>(pack.bonus.type));
	data["value"] = JsonNode(pack.bonus.val);
	journal.push("bonusGiven", data);
}

void JournalVisitor::visitRemoveBonus(RemoveBonus & pack)
{
	JsonNode data;
	data["target"] = JsonNode(static_cast<int>(pack.who));
	data["id"] = JsonNode(pack.whoID.getNum());
	data["bonusType"] = JsonNode(static_cast<int>(pack.bonus.type));
	journal.push("bonusRemoved", data);
}

void JournalVisitor::visitAddQuest(AddQuest & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["objectId"] = JsonNode(pack.quest.obj.getNum());
	journal.push("questAdded", data);
}

void JournalVisitor::visitRemoveObject(RemoveObject & pack)
{
	JsonNode data;
	data["objectId"] = JsonNode(pack.objectID.getNum());
	data["initiator"] = JsonNode(pack.initiator.toString());
	journal.push("objectRemoved", data);
}

void JournalVisitor::visitTryMoveHero(TryMoveHero & pack)
{
	JsonNode data;
	data["heroId"] = JsonNode(pack.id.getNum());
	data["result"] = JsonNode(static_cast<int>(pack.result));
	data["from"] = int3ToJson(pack.start);
	data["to"] = int3ToJson(pack.end);
	data["movePoints"] = JsonNode(static_cast<int>(pack.movePoints));
	if(pack.attackedFrom != int3(0, 0, 0))
		data["attackedFrom"] = int3ToJson(pack.attackedFrom);
	journal.push("heroMoved", data);
}

void JournalVisitor::visitNewStructures(NewStructures & pack)
{
	JsonNode data;
	data["townId"] = JsonNode(pack.tid.getNum());
	JsonNode ids;
	for(auto & b : pack.bid)
		ids.Vector().push_back(JsonNode(b.getNum()));
	data["buildingIds"] = ids;
	journal.push("buildingBuilt", data);
}

void JournalVisitor::visitRazeStructures(RazeStructures & pack)
{
	JsonNode data;
	data["townId"] = JsonNode(pack.tid.getNum());
	JsonNode ids;
	for(auto & b : pack.bid)
		ids.Vector().push_back(JsonNode(b.getNum()));
	data["buildingIds"] = ids;
	journal.push("buildingRazed", data);
}

void JournalVisitor::visitHeroRecruited(HeroRecruited & pack)
{
	JsonNode data;
	data["heroTypeId"] = JsonNode(pack.hid.getNum());
	data["townId"] = JsonNode(pack.tid.getNum());
	data["player"] = JsonNode(pack.player.toString());
	data["position"] = int3ToJson(pack.tile);
	journal.push("heroRecruited", data);
}

void JournalVisitor::visitGiveHero(GiveHero & pack)
{
	JsonNode data;
	data["objectId"] = JsonNode(pack.id.getNum());
	data["player"] = JsonNode(pack.player.toString());
	journal.push("heroGiven", data);
}

void JournalVisitor::visitNewObject(NewObject & pack)
{
	JsonNode data;
	if(pack.newObject)
	{
		data["objectId"] = JsonNode(pack.newObject->id.getNum());
		data["typeId"] = JsonNode(pack.newObject->ID.getNum());
		data["name"] = JsonNode(pack.newObject->getObjectName());
		data["position"] = int3ToJson(pack.newObject->visitablePos());
	}
	data["initiator"] = JsonNode(pack.initiator.toString());
	journal.push("objectCreated", data);
}

void JournalVisitor::visitHeroVisit(HeroVisit & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["heroId"] = JsonNode(pack.heroId.getNum());
	data["objectId"] = JsonNode(pack.objId.getNum());
	data["starting"] = JsonNode(pack.starting);
	journal.push("heroVisit", data);
}

void JournalVisitor::visitInfoWindow(InfoWindow & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["text"] = JsonNode(pack.text.toString());
	data["components"] = componentsToJson(pack.components);
	journal.push("infoWindow", data);
}

void JournalVisitor::visitNewTurn(NewTurn & pack)
{
	JsonNode data;
	data["day"] = JsonNode(static_cast<int>(pack.day));
	data["specialWeek"] = JsonNode(static_cast<int>(pack.specialWeek));
	journal.push("newTurn", data);
}

void JournalVisitor::visitSetObjectProperty(SetObjectProperty & pack)
{
	JsonNode data;
	data["objectId"] = JsonNode(pack.id.getNum());
	data["property"] = JsonNode(static_cast<int>(pack.what));
	data["value"] = JsonNode(pack.identifier.getNum());
	journal.push("objectPropertyChanged", data);
}

void JournalVisitor::visitAdvmapSpellCast(AdvmapSpellCast & pack)
{
	JsonNode data;
	data["casterId"] = JsonNode(pack.casterID.getNum());
	data["spellId"] = JsonNode(pack.spellID.getNum());
	journal.push("adventureSpellCast", data);
}

void JournalVisitor::visitPlayerMessageClient(PlayerMessageClient & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	data["text"] = JsonNode(pack.text);
	journal.push("chatMessage", data);
}

void JournalVisitor::visitResponseStatistic(ResponseStatistic & pack)
{
	JsonNode data;
	data["player"] = JsonNode(pack.player.toString());
	journal.push("statisticsReady", data);
}

void JournalVisitor::visitBattleStart(BattleStart & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	journal.push("battleStart", data);
}

void JournalVisitor::visitBattleNextRound(BattleNextRound & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	journal.push("battleNextRound", data);
}

void JournalVisitor::visitBattleSetActiveStack(BattleSetActiveStack & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["unitId"] = JsonNode(static_cast<int>(pack.stack));
	journal.push("battleUnitActive", data);
}

void JournalVisitor::visitBattleCancelled(BattleCancelled & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	journal.push("battleCancelled", data);
}

void JournalVisitor::visitBattleLogMessage(BattleLogMessage & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	JsonNode lines;
	for(auto & line : pack.lines)
		lines.Vector().push_back(JsonNode(line.toString()));
	data["lines"] = lines;
	journal.push("battleLog", data);
}

void JournalVisitor::visitBattleAttack(BattleAttack & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["attackerId"] = JsonNode(static_cast<int>(pack.stackAttacking));
	data["shot"] = JsonNode(pack.shot());
	data["counter"] = JsonNode(pack.counter());
	data["deathBlow"] = JsonNode(pack.deathBlow());
	JsonNode targets;
	for(auto & bsa : pack.bsa)
	{
		JsonNode target;
		target["unitId"] = JsonNode(static_cast<int>(bsa.stackAttacked));
		target["damage"] = JsonNode(static_cast<si64>(bsa.damageAmount));
		target["killed"] = JsonNode(static_cast<int>(bsa.killedAmount));
		target["fatal"] = JsonNode(bsa.killed());
		targets.Vector().push_back(target);
	}
	data["targets"] = targets;
	journal.push("battleAttack", data);
}

void JournalVisitor::visitBattleSpellCast(BattleSpellCast & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["side"] = JsonNode(static_cast<int>(pack.side));
	data["spellId"] = JsonNode(pack.spellID.getNum());
	data["casterStack"] = JsonNode(pack.casterStack);
	journal.push("battleSpellCast", data);
}

void JournalVisitor::visitStacksInjured(StacksInjured & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	JsonNode stacks;
	for(auto & bsa : pack.stacks)
	{
		JsonNode entry;
		entry["unitId"] = JsonNode(static_cast<int>(bsa.stackAttacked));
		entry["damage"] = JsonNode(static_cast<si64>(bsa.damageAmount));
		entry["killed"] = JsonNode(static_cast<int>(bsa.killedAmount));
		stacks.Vector().push_back(entry);
	}
	data["stacks"] = stacks;
	journal.push("stacksInjured", data);
}

void JournalVisitor::visitBattleResultsApplied(BattleResultsApplied & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["victor"] = JsonNode(pack.victor.toString());
	data["loser"] = JsonNode(pack.loser.toString());
	journal.push("battleEnded", data);
}

void JournalVisitor::visitCatapultAttack(CatapultAttack & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["wallPart"] = JsonNode(static_cast<int>(pack.attackedPart));
	data["damage"] = JsonNode(static_cast<int>(pack.damageDealt));
	journal.push("catapultAttack", data);
}

void JournalVisitor::visitBattleTriggerEffect(BattleTriggerEffect & pack)
{
	JsonNode data;
	data["battleId"] = JsonNode(pack.battleID.getNum());
	data["unitId"] = JsonNode(pack.stackID);
	data["effect"] = JsonNode(static_cast<int>(pack.effect));
	data["value"] = JsonNode(pack.val);
	journal.push("battleTriggerEffect", data);
}
