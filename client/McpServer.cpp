/*
 * McpServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "McpServer.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../lib/CConfigHandler.h"
#include "../lib/GameConstants.h"
#include "../lib/json/JsonNode.h"
#include "../lib/json/JsonUtils.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/CPlayerState.h"
#include "../lib/mapping/CMap.h"
#include "../lib/StartInfo.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/callback/CCallback.h"
#include "../lib/GameLibrary.h"

#include "GameInstance.h"
#include "GameEngine.h"
#include "CServerHandler.h"
#include "Client.h"
#include "CPlayerInterface.h"
#include "ClientCommandManager.h"

#include <shared_mutex>

#ifdef ENABLE_MCP_SERVER

static JsonNode getFullGameConfig()
{
	JsonNode result;
	try
	{
		result = JsonUtils::assembleFromFiles("config/gameConfig.json");
		if(!result.isStruct())
			return result;
		std::vector<std::string> keys;
		for(auto & kv : result.Struct())
			keys.push_back(kv.first);
		for(auto & name : keys)
		{
			auto & value = result[name];
			if(name == "settings" || !value.isVector())
				continue;
			JsonNode merged;
			for(auto & entry : value.Vector())
			{
				JsonNode fileData = JsonUtils::assembleFromFiles(entry.String());
				JsonUtils::mergeCopy(merged, fileData);
			}
			result[name] = merged;
		}
	}
	catch(const std::exception & e)
	{
		logGlobal->error("Failed to assemble game config: %s", e.what());
	}
	return result;
}

static JsonNode heroToJson(const CGHeroInstance * h)
{
	JsonNode entry;
	entry["name"] = JsonNode(h->getNameTranslated());
	entry["id"] = JsonNode(h->id.getNum());
	entry["typeId"] = JsonNode(h->getHeroTypeID().getNum());
	entry["level"] = JsonNode(h->level);
	entry["experience"] = JsonNode(h->exp);
	entry["mana"] = JsonNode(h->mana);
	entry["maxMana"] = JsonNode(h->manaLimit());
	entry["attack"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::ATTACK));
	entry["defense"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::DEFENSE));
	entry["spellPower"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::SPELL_POWER));
	entry["knowledge"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::KNOWLEDGE));
	entry["movement"] = JsonNode(h->movementPointsRemaining());
	entry["maxMovement"] = JsonNode(h->movementPointsLimit());
	JsonNode army;
	for(auto & [slot, stack] : h->stacks)
	{
		JsonNode stackEntry;
		stackEntry["slot"] = JsonNode(slot.getNum());
		stackEntry["id"] = JsonNode(stack->getId().getNum());
		stackEntry["count"] = JsonNode(stack->getCount());
		auto creature = stack->getCreature();
		if(creature)
		{
			stackEntry["name"] = JsonNode(creature->getNameSingularTranslated());
			stackEntry["level"] = JsonNode(creature->getLevel());
		}
		army.Vector().push_back(stackEntry);
	}
	entry["army"] = army;
	return entry;
}

static JsonNode townToJson(const CGTownInstance * t)
{
	JsonNode entry;
	entry["name"] = JsonNode(t->getNameTranslated());
	entry["id"] = JsonNode(t->id.getNum());
	entry["faction"] = JsonNode(t->getFactionID());
	entry["hasFort"] = JsonNode(t->hasFort());
	JsonNode built;
	for(auto & bid : t->getBuildings())
		built.Vector().push_back(JsonNode(bid.getNum()));
	entry["built"] = built;
	return entry;
}

static mcp::json textContent(const std::string & text)
{
	mcp::json arr = mcp::json::array();
	arr.push_back({{"type", "text"}, {"text", text}});
	return arr;
}

static PlayerColor parsePlayerColor(const std::string & name)
{
	for(int i = 0; i < PlayerColor::PLAYER_LIMIT_I; i++)
	{
		auto c = PlayerColor(i);
		if(c.toString() == name)
			return c;
	}
	return PlayerColor::NEUTRAL;
}

#endif

McpServer::McpServer() :
	enabled(settings["mcp"]["enabled"].Bool())
{
#ifdef ENABLE_MCP_SERVER
	if(!enabled)
		return;

	auto config = mcp::server::configuration{};
	config.host = settings["mcp"]["host"].String();
	config.port = static_cast<int>(settings["mcp"]["port"].Integer());
	config.name = "VCMI MCP Server";
	config.version = GameConstants::VCMI_VERSION;
	config.threadpool_size = 2;
	config.max_sessions = 4;
	config.session_timeout = 0;

	if(config.host.empty())
		config.host = "localhost";
	if(config.port == 0)
		config.port = 9100;

	auto srv = std::make_unique<mcp::server>(config);

	srv->set_server_info(config.name, config.version);
	srv->set_capabilities(mcp::json{
		{"tools", mcp::json::object()},
		{"resources", mcp::json::object()}
	});

	srv->register_tool(
		mcp::tool{ "get_game_state", "Get current game state overview", {{"type", "object"}}, mcp::json() },
		[](const mcp::json &, const std::string &) -> mcp::json {
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
	);

	srv->register_tool(
		mcp::tool{ "get_heroes", "List all heroes visible to current player", {{"type", "object"}}, mcp::json() },
		[](const mcp::json &, const std::string &) -> mcp::json {
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
	);

	srv->register_tool(
		mcp::tool{ "get_towns", "List all towns visible to current player", {{"type", "object"}}, mcp::json() },
		[](const mcp::json &, const std::string &) -> mcp::json {
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
		[](const mcp::json & params, const std::string &) -> mcp::json {
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
	);

	srv->register_tool(
		mcp::tool{ "execute_command", "Execute a VCMI console command", {
			{"type", "object"},
			{"properties", {
				{"command", {
					{"type", "string"},
					{"description", "Console command to execute"}
				}}
			}},
			{"required", {"command"}}
		}, mcp::json() },
		[](const mcp::json & params, const std::string &) -> mcp::json {
			auto cmd = params["command"].get<std::string>();
			ENGINE->dispatchMainThread([cmd]() {
				if(GAME->interface())
				{
					ClientCommandManager commandController;
					commandController.processCommand(cmd, false);
				}
			});
			return textContent("Command queued for execution");
		}
	);

	srv->register_tool(
		mcp::tool{ "list_creatures", "List all creatures with their stats", {{"type", "object"}}, mcp::json() },
		[](const mcp::json &, const std::string &) -> mcp::json {
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
	);

	srv->register_tool(
		mcp::tool{ "get_config", "Get full merged game configuration", {{"type", "object"}}, mcp::json() },
		[](const mcp::json &, const std::string &) -> mcp::json {
			return textContent(getFullGameConfig().toCompactString());
		}
	);

	srv->register_resource_template(
		"vcmi://gamestate",
		"Game State",
		"application/json",
		"Current VCMI game state snapshot",
		[](const std::string & uri, const std::map<std::string, std::string> &, const std::string &) -> mcp::json {
			std::shared_lock lock(CGameState::mutex);

			if(!GAME->interface() || !GAME->interface()->cb)
				return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{}"}}}}};

			auto & gs = GAME->server().client->gameState();
			JsonNode data;
			data["day"] = JsonNode(gs.day);
			JsonNode mapSize;
			mapSize.Vector().push_back(JsonNode(gs.getMap().width));
			mapSize.Vector().push_back(JsonNode(gs.getMap().height));
			data["mapSize"] = mapSize;

			return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", data.toCompactString()}}}}};
		}
	);

	srv->register_resource_template(
		"vcmi://hero/{heroId}",
		"Hero Info",
		"application/json",
		"Get info about a specific hero by ID",
		[](const std::string & uri, const std::map<std::string, std::string> & uri_params, const std::string &) -> mcp::json {
			std::shared_lock lock(CGameState::mutex);

			auto pi = GAME->interface();
			if(!pi || !pi->cb)
				return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{}"}}}}};

			ObjectInstanceID heroId(std::stoi(uri_params.at("heroId")));
			auto obj = pi->cb->getObj(heroId, false);
			if(!obj)
				return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{}"}}}}};

			JsonNode data;
			data["id"] = JsonNode(heroId.getNum());
			data["name"] = JsonNode(obj->getObjectName());

			return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", data.toCompactString()}}}}};
		}
	);

	srv->register_resource_template(
		"vcmi://player/{color}",
		"Player Info",
		"application/json",
		"Get info about a player by color",
		[](const std::string & uri, const std::map<std::string, std::string> & uri_params, const std::string &) -> mcp::json {
			std::shared_lock lock(CGameState::mutex);

			auto pi = GAME->interface();
			if(!pi || !pi->cb)
				return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{}"}}}}};

			auto color = parsePlayerColor(uri_params.at("color"));
			if(!color.isValidPlayer())
				return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{}"}}}}};

			auto state = pi->cb->getPlayerState(color, false);
			if(!state)
				return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", "{}"}}}}};

			JsonNode data;
			data["color"] = JsonNode(color.toString());
			data["human"] = JsonNode(state->human);
			data["towns"] = JsonNode(static_cast<int>(state->getTowns().size()));
			data["heroes"] = JsonNode(static_cast<int>(state->getHeroes().size()));

			return mcp::json{{"contents", {{{"uri", uri}, {"mimeType", "application/json"}, {"text", data.toCompactString()}}}}};
		}
	);

	if(srv->start(false))
	{
		logGlobal->info("MCP server started on %s:%d", config.host, config.port);
		server = std::move(srv);
	}
	else
	{
		logGlobal->warn("MCP server failed to start on %s:%d - port already in use?", config.host, config.port);
	}
#endif
}

McpServer::~McpServer()
{
#ifdef ENABLE_MCP_SERVER
	if(!enabled || !server)
		return;

	server->stop();
#endif
}
