/*
 * InfoTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "InfoTools.h"
#include "ToolContext.h"
#include "Serializers.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

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
#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../Client.h"

#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/CBattleInfoEssentials.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	JsonNode getGameState()
	{
		auto & cb = mcptool::activeCallback();
		auto & gs = GAME->server().client->gameState();
		auto cal = gs.getCalendar();

		JsonNode result;
		result["day"] = JsonNode(cal.getCurrentDay());
		result["week"] = JsonNode(cal.getWeek());
		result["month"] = JsonNode(cal.getMonth());
		result["currentPlayer"] = JsonNode(cb.getPlayerID().value_or(PlayerColor::NEUTRAL).toString());

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
		return result;
	}

	JsonNode getHeroes()
	{
		auto & cb = mcptool::activeCallback();
		JsonNode arr;
		for(auto * h : cb.getHeroesInfo())
			arr.Vector().push_back(heroToJson(h));
		return arr;
	}

	JsonNode getTowns()
	{
		auto & cb = mcptool::activeCallback();
		JsonNode arr;
		for(auto * t : cb.getTownsInfo(true))
			arr.Vector().push_back(townToJson(t));
		return arr;
	}

	JsonNode getPlayerInfo(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto playerStr = params["player"].get<std::string>();
		auto color = parsePlayerColor(playerStr);
		if(!color.isValidPlayer())
			throw std::runtime_error("Invalid player color");

		auto state = cb.getPlayerState(color, false);
		if(!state)
			throw std::runtime_error("Player not found");

		JsonNode result;
		result["color"] = JsonNode(color.toString());
		result["human"] = JsonNode(state->human);
		JsonNode resources;
		for(int i = 0; i < GameResID::COUNT; i++)
			resources[GameResID::encode(i)] = JsonNode(cb.getResourceAmount(GameResID(i)));
		result["resources"] = resources;
		result["towns"] = JsonNode(cb.howManyTowns());
		result["heroes"] = JsonNode(cb.howManyHeroes(false));
		result["status"] = JsonNode(static_cast<int>(cb.getPlayerStatus(color, false)));
		return result;
	}

	JsonNode getVisibleTiles()
	{
		auto & cb = mcptool::activeCallback();
		auto player = cb.getPlayerID();
		if(!player)
			throw std::runtime_error("No player");

		auto team = cb.getPlayerTeam(*player);
		if(!team)
			throw std::runtime_error("No team");

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
		return tiles;
	}

	JsonNode getMapContent()
	{
		auto & cb = mcptool::activeCallback();

		JsonNode result;

		JsonNode heroes;
		for(auto * h : cb.getHeroesInfo())
			heroes.Vector().push_back(heroToJson(h));
		result["heroes"] = heroes;

		JsonNode towns;
		for(auto * t : cb.getTownsInfo(true))
			towns.Vector().push_back(townToJson(t));
		result["towns"] = towns;

		JsonNode objects;
		for(auto * obj : cb.getAllVisitableObjs())
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
		return result;
	}

	JsonNode listCreatures()
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
		return arr;
	}

	JsonNode listArtifacts()
	{
		JsonNode arr;
		LIBRARY->artifacts()->forEach([&](const Artifact * a, bool & stop) {
			arr.Vector().push_back(artifactToJson(a));
		});
		return arr;
	}

	JsonNode listSpells()
	{
		JsonNode arr;
		LIBRARY->spells()->forEach([&](const spells::Spell * s, bool & stop) {
			arr.Vector().push_back(spellToJson(s));
		});
		return arr;
	}

	JsonNode listSkills()
	{
		JsonNode arr;
		LIBRARY->skills()->forEach([&](const Skill * s, bool & stop) {
			arr.Vector().push_back(skillToJson(s));
		});
		return arr;
	}

	JsonNode getHeroDetails(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto heroId = params["heroId"].get<int>();
		auto obj = cb.getObj(ObjectInstanceID(heroId), false);
		auto hero = dynamic_cast<const CGHeroInstance *>(obj);
		if(!hero)
			throw std::runtime_error("No hero with id " + std::to_string(heroId));
		return heroDetailsToJson(hero);
	}

	JsonNode getTownDetails(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto townId = params["townId"].get<int>();
		auto obj = cb.getObj(ObjectInstanceID(townId), false);
		auto town = dynamic_cast<const CGTownInstance *>(obj);
		if(!town)
			throw std::runtime_error("No town with id " + std::to_string(townId));
		return townDetailsToJson(town);
	}

	JsonNode getBattleState()
	{
		auto & cb = mcptool::activeCallback();
		auto battles = cb.getActiveBattles();
		if(battles.empty())
			throw std::runtime_error("No active battle");

		auto battleCB = battles.begin()->second;
		if(!battleCB)
			throw std::runtime_error("No active battle");

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
		return result;
	}
}

void registerInfoTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("get_game_state")
			.with_description("Get current game state overview")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getGameState); }
	);

	srv->register_tool(
		mcp::tool_builder("get_heroes")
			.with_description("List all heroes visible to current player")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getHeroes); }
	);

	srv->register_tool(
		mcp::tool_builder("get_towns")
			.with_description("List all towns visible to current player")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getTowns); }
	);

	srv->register_tool(
		mcp::tool_builder("get_player_info")
			.with_description("Get info about a specific player")
			.with_string_param("player", "Player color name (red, blue, etc)", true)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getPlayerInfo(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_visible_tiles")
			.with_description("Get all tiles visible to current player")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getVisibleTiles); }
	);

	srv->register_tool(
		mcp::tool_builder("get_map_content")
			.with_description("Get all visible map content (heroes, towns, objects)")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getMapContent); }
	);

	srv->register_tool(
		mcp::tool_builder("list_creatures")
			.with_description("List all creatures with their stats")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(listCreatures); }
	);

	srv->register_tool(
		mcp::tool_builder("get_config")
			.with_description("Get full merged game configuration")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getFullGameConfig); }
	);

	srv->register_tool(
		mcp::tool_builder("list_artifacts")
			.with_description("List all artifacts with their stats")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(listArtifacts); }
	);

	srv->register_tool(
		mcp::tool_builder("list_spells")
			.with_description("List all spells with their stats")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(listSpells); }
	);

	srv->register_tool(
		mcp::tool_builder("list_skills")
			.with_description("List all secondary skills")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(listSkills); }
	);

	srv->register_tool(
		mcp::tool_builder("get_hero_details")
			.with_description("Get detailed info about a specific hero")
			.with_number_param("heroId", "Hero instance ID", true)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getHeroDetails(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_town_details")
			.with_description("Get detailed info about a specific town")
			.with_number_param("townId", "Town instance ID", true)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getTownDetails(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_battle_state")
			.with_description("Get current battle state")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getBattleState); }
	);
}

#endif
