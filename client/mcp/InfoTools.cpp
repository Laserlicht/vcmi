/*
 * InfoTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "InfoTools.h"
#include "Helpers.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"

#include <vcmi/ArtifactService.h>
#include <vcmi/spells/Service.h>
#include <vcmi/SkillService.h>

#include "../GameInstance.h"
#include "../GameEngine.h"
#include "../CServerHandler.h"
#include "../Client.h"
#include "../CPlayerInterface.h"

#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/CBattleInfoEssentials.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"

#ifdef ENABLE_MCP_SERVER

static mcp::json textContent(const std::string & text)
{
	mcp::json arr = mcp::json::array();
	arr.push_back({{"type", "text"}, {"text", text}});
	return arr;
}

static mcp::json handleGetGameState(const mcp::json &, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	if(!GAME->interface() || !GAME->interface()->cb)
		return textContent("No active game");

	auto & gs = GAME->server().client->gameState();
	auto cal = gs.getCalendar();

	JsonNode result;
	result["day"] = JsonNode(cal.getCurrentDay());
	result["week"] = JsonNode(cal.getWeek());
	result["month"] = JsonNode(cal.getMonth());
	result["currentPlayer"] = JsonNode(GAME->interface()->cb->getPlayerID().value_or(PlayerColor::NEUTRAL).toString());

	auto si = gs.getStartInfo();
	if(si)
	{
		result["mapName"] = JsonNode(si->mapname);
		result["mode"] = JsonNode(static_cast<int>(si->mode));
	}

	JsonNode players;
	for(auto & [color, state] : gs.players)
		if(color.isValidPlayer())
			players.Vector().push_back(JsonNode(color.toString()));

	result["players"] = players;

	return textContent(result.toCompactString());
}

static mcp::json handleGetHeroes(const mcp::json &, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto heroes = pi->cb->getHeroesInfo();
	JsonNode arr;
	for(auto * h : heroes)
		arr.Vector().push_back(heroToJson(h));

	return textContent(arr.toCompactString());
}

static mcp::json handleGetTowns(const mcp::json &, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto towns = pi->cb->getTownsInfo(true);
	JsonNode arr;
	for(auto * t : towns)
		arr.Vector().push_back(townToJson(t));

	return textContent(arr.toCompactString());
}

static mcp::json handleGetPlayerInfo(const mcp::json & params, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto playerStr = params["player"].get<std::string>();
	auto color = parsePlayerColor(playerStr);
	if(!color.isValidPlayer())
		throw std::runtime_error("Invalid player color");

	auto state = pi->cb->getPlayerState(color, false);
	if(!state)
		throw std::runtime_error("Player not found");

	JsonNode result;
	result["color"] = JsonNode(color.toString());
	result["human"] = JsonNode(state->human);
	JsonNode resources;
	for(int i = 0; i < GameResID::COUNT; i++)
		resources[GameResID::encode(i)] = JsonNode(pi->cb->getResourceAmount(GameResID(i)));
	result["resources"] = resources;
	result["towns"] = JsonNode(pi->cb->howManyTowns());
	result["heroes"] = JsonNode(pi->cb->howManyHeroes(false));
	result["status"] = JsonNode(static_cast<int>(pi->cb->getPlayerStatus(color, false)));

	return textContent(result.toCompactString());
}

static mcp::json handleGetVisibleTiles(const mcp::json &, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto player = pi->cb->getPlayerID();
	if(!player)
		return textContent("No player");

	auto team = pi->cb->getPlayerTeam(*player);
	if(!team)
		return textContent("No team");

	auto & map = GAME->server().client->gameState().getMap();
	auto & fow = team->fogOfWarMap;
	JsonNode tiles;
	for(int z = 0; z < map.levels(); z++)
	{
		for(int x = 0; x < map.width; x++)
		{
			for(int y = 0; y < map.height; y++)
			{
				if(fow[int3(x, y, z)])
				{
					JsonNode tile;
					tile["x"] = JsonNode(x);
					tile["y"] = JsonNode(y);
					tile["z"] = JsonNode(z);
					tiles.Vector().push_back(tile);
				}
			}
		}
	}
	return textContent(tiles.toCompactString());
}

static mcp::json handleGetMapContent(const mcp::json &, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	JsonNode result;

	JsonNode heroes;
	for(auto * h : pi->cb->getHeroesInfo())
		heroes.Vector().push_back(heroToJson(h));
	result["heroes"] = heroes;

	JsonNode towns;
	for(auto * t : pi->cb->getTownsInfo(true))
		towns.Vector().push_back(townToJson(t));
	result["towns"] = towns;

	JsonNode objects;
	for(auto * obj : pi->cb->getAllVisitableObjs())
	{
		JsonNode entry;
		entry["id"] = JsonNode(obj->id.getNum());
		entry["type"] = JsonNode(obj->ID.getNum());
		entry["subtype"] = JsonNode(obj->subID.getNum());
		entry["name"] = JsonNode(obj->getObjectName());
		entry["x"] = JsonNode(obj->pos.x);
		entry["y"] = JsonNode(obj->pos.y);
		entry["z"] = JsonNode(obj->pos.z);
		objects.Vector().push_back(entry);
	}
	result["objects"] = objects;

	return textContent(result.toCompactString());
}

static mcp::json handleListCreatures(const mcp::json &, const std::string &)
{
	JsonNode arr;
	LIBRARY->creatures()->forEach([&](const Creature * creature, bool & stop) {
		JsonNode entry;
		entry["id"] = JsonNode(creature->getId().getNum());
		entry["name"] = JsonNode(creature->getNameSingularTranslated());
		entry["namePlural"] = JsonNode(creature->getNamePluralTranslated());
		entry["level"] = JsonNode(creature->getLevel());
		entry["attack"] = JsonNode(creature->getBaseAttack());
		entry["defense"] = JsonNode(creature->getBaseDefense());
		entry["minDamage"] = JsonNode(creature->getBaseDamageMin());
		entry["maxDamage"] = JsonNode(creature->getBaseDamageMax());
		entry["hitPoints"] = JsonNode(creature->getBaseHitPoints());
		entry["speed"] = JsonNode(creature->getBaseSpeed());
		entry["shots"] = JsonNode(creature->getBaseShots());
		entry["growth"] = JsonNode(creature->getGrowth());
		entry["aiValue"] = JsonNode(creature->getAIValue());
		entry["fightValue"] = JsonNode(creature->getFightValue());
		entry["doubleWide"] = JsonNode(creature->isDoubleWide());
		entry["hasUpgrades"] = JsonNode(creature->hasUpgrades());
		arr.Vector().push_back(entry);
	});
	return textContent(arr.toCompactString());
}

static mcp::json handleGetConfig(const mcp::json &, const std::string &)
{
	return textContent(getFullGameConfig().toCompactString());
}

static mcp::json handleListArtifacts(const mcp::json &, const std::string &)
{
	JsonNode arr;
	LIBRARY->artifacts()->forEach([&](const Artifact * a, bool & stop) {
		arr.Vector().push_back(artifactToJson(a));
	});
	return textContent(arr.toCompactString());
}

static mcp::json handleListSpells(const mcp::json &, const std::string &)
{
	JsonNode arr;
	LIBRARY->spells()->forEach([&](const spells::Spell * s, bool & stop) {
		arr.Vector().push_back(spellToJson(s));
	});
	return textContent(arr.toCompactString());
}

static mcp::json handleListSkills(const mcp::json &, const std::string &)
{
	JsonNode arr;
	LIBRARY->skills()->forEach([&](const Skill * s, bool & stop) {
		arr.Vector().push_back(skillToJson(s));
	});
	return textContent(arr.toCompactString());
}

static mcp::json handleGetHeroDetails(const mcp::json & params, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto heroId = params["heroId"].get<int>();
	auto obj = pi->cb->getObj(ObjectInstanceID(heroId), false);
	if(!obj)
		throw std::runtime_error("Hero not found");

	auto hero = dynamic_cast<const CGHeroInstance *>(obj);
	if(!hero)
		throw std::runtime_error("Object is not a hero");

	return textContent(heroDetailsToJson(hero).toCompactString());
}

static mcp::json handleGetTownDetails(const mcp::json & params, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto townId = params["townId"].get<int>();
	auto obj = pi->cb->getObj(ObjectInstanceID(townId), false);
	if(!obj)
		throw std::runtime_error("Town not found");

	auto town = dynamic_cast<const CGTownInstance *>(obj);
	if(!town)
		throw std::runtime_error("Object is not a town");

	return textContent(townDetailsToJson(town).toCompactString());
}

static mcp::json handleGetBattleState(const mcp::json &, const std::string &)
{
	std::shared_lock lock(CGameState::mutex);

	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		return textContent("No active game");

	auto battles = pi->cb->getActiveBattles();
	if(battles.empty())
		return textContent("No active battle");

	auto battleCB = battles.begin()->second;
	if(!battleCB)
		return textContent("No active battle");

	JsonNode result;
	result["round"] = JsonNode(battleCB->battleGetRound());

	auto * active = battleCB->battleActiveUnit();
	if(active)
		result["activeUnitId"] = JsonNode(static_cast<int>(active->unitId()));

	auto * battle = battleCB->getBattle();
	if(battle)
	{
		result["battlefieldType"] = JsonNode(static_cast<int>(battle->getBattlefieldType()));
		result["terrainType"] = JsonNode(static_cast<int>(battle->getTerrainType()));
		auto loc = battle->getLocation();
		JsonNode locJson;
		locJson["x"] = JsonNode(loc.x);
		locJson["y"] = JsonNode(loc.y);
		locJson["z"] = JsonNode(loc.z);
		result["location"] = locJson;
	}

	JsonNode units;
	auto allUnits = battleCB->battleGetAllUnits(false);
	for(auto * u : allUnits)
		units.Vector().push_back(battleUnitToJson(u));
	result["units"] = units;

	auto * town = battleCB->battleGetDefendedTown();
	if(town)
	{
		result["siege"] = JsonNode(true);
		result["defendedTownId"] = JsonNode(town->id.getNum());

		JsonNode walls;
		walls["keep"] = JsonNode(static_cast<int>(battleCB->battleGetWallState(EWallPart::KEEP)));
		walls["gate"] = JsonNode(static_cast<int>(battleCB->battleGetWallState(EWallPart::GATE)));
		walls["upperWall"] = JsonNode(static_cast<int>(battleCB->battleGetWallState(EWallPart::UPPER_WALL)));
		walls["bottomWall"] = JsonNode(static_cast<int>(battleCB->battleGetWallState(EWallPart::BOTTOM_WALL)));
		walls["upperTower"] = JsonNode(static_cast<int>(battleCB->battleGetWallState(EWallPart::UPPER_TOWER)));
		walls["bottomTower"] = JsonNode(static_cast<int>(battleCB->battleGetWallState(EWallPart::BOTTOM_TOWER)));
		result["walls"] = walls;
		result["gateState"] = JsonNode(static_cast<int>(battleCB->battleGetGateState()));
	}

	auto * attHero = battleCB->battleGetFightingHero(BattleSide::ATTACKER);
	auto * defHero = battleCB->battleGetFightingHero(BattleSide::DEFENDER);
	if(attHero)
		result["attackerHeroId"] = JsonNode(attHero->id.getNum());
	if(defHero)
		result["defenderHeroId"] = JsonNode(defHero->id.getNum());
	result["attackerPlayer"] = JsonNode(battleCB->getBattle()->getSidePlayer(BattleSide::ATTACKER).toString());
	result["defenderPlayer"] = JsonNode(battleCB->getBattle()->getSidePlayer(BattleSide::DEFENDER).toString());

	return textContent(result.toCompactString());
}

void registerInfoTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool{ "get_game_state", "Get current game state overview", {{"type", "object"}}, mcp::json() },
		handleGetGameState
	);

	srv->register_tool(
		mcp::tool{ "get_heroes", "List all heroes visible to current player", {{"type", "object"}}, mcp::json() },
		handleGetHeroes
	);

	srv->register_tool(
		mcp::tool{ "get_towns", "List all towns visible to current player", {{"type", "object"}}, mcp::json() },
		handleGetTowns
	);

	srv->register_tool(
		mcp::tool{ "get_player_info", "Get info about a specific player", {
			{"type", "object"},
			{"properties", {
				{"player", {
					{"type", "string"},
					{"description", "Player color name (red, blue, etc)"}
				}}
			}},
			{"required", {"player"}}
		}, mcp::json() },
		handleGetPlayerInfo
	);

	srv->register_tool(
		mcp::tool{ "get_visible_tiles", "Get all tiles visible to current player", {{"type", "object"}}, mcp::json() },
		handleGetVisibleTiles
	);

	srv->register_tool(
		mcp::tool{ "get_map_content", "Get all visible map content (heroes, towns, objects)", {{"type", "object"}}, mcp::json() },
		handleGetMapContent
	);

	srv->register_tool(
		mcp::tool{ "list_creatures", "List all creatures with their stats", {{"type", "object"}}, mcp::json() },
		handleListCreatures
	);

	srv->register_tool(
		mcp::tool{ "get_config", "Get full merged game configuration", {{"type", "object"}}, mcp::json() },
		handleGetConfig
	);

	srv->register_tool(
		mcp::tool{ "list_artifacts", "List all artifacts with their stats", {{"type", "object"}}, mcp::json() },
		handleListArtifacts
	);

	srv->register_tool(
		mcp::tool{ "list_spells", "List all spells with their stats", {{"type", "object"}}, mcp::json() },
		handleListSpells
	);

	srv->register_tool(
		mcp::tool{ "list_skills", "List all secondary skills", {{"type", "object"}}, mcp::json() },
		handleListSkills
	);

	srv->register_tool(
		mcp::tool{ "get_hero_details", "Get detailed info about a specific hero", {
			{"type", "object"},
			{"properties", {
				{"heroId", {
					{"type", "number"},
					{"description", "Hero instance ID"}
				}}
			}},
			{"required", {"heroId"}}
		}, mcp::json() },
		handleGetHeroDetails
	);

	srv->register_tool(
		mcp::tool{ "get_town_details", "Get detailed info about a specific town", {
			{"type", "object"},
			{"properties", {
				{"townId", {
					{"type", "number"},
					{"description", "Town instance ID"}
				}}
			}},
			{"required", {"townId"}}
		}, mcp::json() },
		handleGetTownDetails
	);

	srv->register_tool(
		mcp::tool{ "get_battle_state", "Get current battle state", {{"type", "object"}}, mcp::json() },
		handleGetBattleState
	);
}

#endif
