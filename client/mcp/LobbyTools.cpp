/*
 * LobbyTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "LobbyTools.h"
#include "ToolContext.h"
#include "Serializers.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include "../../lib/json/JsonNode.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/ISimpleResourceLoader.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/networkPacks/PacksForLobby.h"

#include "../GameInstance.h"
#include "../GameEngine.h"
#include "../CServerHandler.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	/// Lobby state lives on CServerHandler, not CGameState, so it needs neither the
	/// CGameState::mutex lock nor the in-game action-wait envelope that ToolContext provides
	/// for gameplay tools. Lobby setup is a one-shot, infrequent flow, so actions here simply
	/// dispatch and let the caller confirm the result via a follow-up lobby_get_state.
	mcp::json lobbyReadTool(const std::function<JsonNode()> & fn)
	{
		return mcptool::textContent(fn());
	}

	mcp::json lobbyActionTool(const std::function<void()> & fn)
	{
		ENGINE->dispatchMainThread(fn);
		return mcptool::textContent("Queued for execution - use lobby_get_state to confirm the result");
	}

	JsonNode playerSettingsToJson(const PlayerSettings & ps)
	{
		JsonNode entry;
		entry["color"] = JsonNode(ps.color.toString());
		entry["name"] = JsonNode(ps.name);
		entry["factionId"] = JsonNode(ps.castle.getNum());
		entry["heroTypeId"] = JsonNode(ps.hero.getNum());
		entry["isHuman"] = JsonNode(ps.isControlledByHuman());
		entry["compOnly"] = JsonNode(ps.compOnly);
		return entry;
	}

	JsonNode getLobbyState()
	{
		auto & server = GAME->server();

		JsonNode result;
		result["state"] = JsonNode(static_cast<int>(server.getState()));
		result["isHost"] = JsonNode(server.isHost());

		if(server.mi)
		{
			JsonNode map;
			map["fileUri"] = JsonNode(server.mi->fileURI);
			map["name"] = JsonNode(server.mi->getNameTranslated());
			map["description"] = JsonNode(server.mi->getDescriptionTranslated());
			map["playerCount"] = JsonNode(server.mi->amountOfPlayersOnMap);
			map["humanPlayerCount"] = JsonNode(server.mi->amountOfHumanControllablePlayers);
			result["map"] = map;
		}

		if(server.si)
		{
			result["mode"] = JsonNode(static_cast<int>(server.si->mode));
			result["difficulty"] = JsonNode(server.si->difficulty);
			JsonNode players;
			for(auto & [color, ps] : server.si->playerInfos)
				players.Vector().push_back(playerSettingsToJson(ps));
			result["players"] = players;
		}

		return result;
	}

	JsonNode listMaps()
	{
		std::string dirURI = "MAPS/";
		CResourceHandler::get()->updateFilteredFiles([&](const std::string & mount)
		{
			return boost::algorithm::starts_with(mount, dirURI);
		});

		auto files = CResourceHandler::get()->getFilteredFiles([&](const ResourcePath & ident)
		{
			return ident.getType() == EResType::MAP && boost::algorithm::starts_with(ident.getName(), dirURI);
		});

		JsonNode result;
		for(auto & file : files)
			result.Vector().push_back(JsonNode(file.getName()));
		return result;
	}

	mcp::json handleLobbyGetState(const mcp::json &, const std::string &)
	{
		return lobbyReadTool(getLobbyState);
	}

	mcp::json handleLobbyListMaps(const mcp::json &, const std::string &)
	{
		return lobbyReadTool(listMaps);
	}

	mcp::json handleLobbySelectMap(const mcp::json & params, const std::string &)
	{
		auto fileUri = params["fileUri"].get<std::string>();

		return lobbyActionTool([fileUri]()
		{
			auto mapInfo = std::make_shared<CMapInfo>();
			mapInfo->mapInit(fileUri);
			GAME->server().setMapInfo(mapInfo);
		});
	}

	mcp::json handleLobbyClaimPlayer(const mcp::json & params, const std::string &)
	{
		auto color = parsePlayerColor(params["player"].get<std::string>());
		if(!color.isValidPlayer())
			throw std::runtime_error("Invalid player color");

		return lobbyActionTool([color]()
		{
			GAME->server().setPlayer(color);
		});
	}

	mcp::json handleLobbySetPlayerOption(const mcp::json & params, const std::string &)
	{
		auto color = parsePlayerColor(params["player"].get<std::string>());
		if(!color.isValidPlayer())
			throw std::runtime_error("Invalid player color");
		auto what = params["what"].get<std::string>();
		auto value = params["value"].get<int32_t>();

		return lobbyActionTool([color, what, value]()
		{
			LobbyChangePlayerOption::EWhat code;
			if(what == "town")
				code = LobbyChangePlayerOption::TOWN;
			else if(what == "hero")
				code = LobbyChangePlayerOption::HERO;
			else if(what == "bonus")
				code = LobbyChangePlayerOption::BONUS;
			else if(what == "townId")
				code = LobbyChangePlayerOption::TOWN_ID;
			else if(what == "heroId")
				code = LobbyChangePlayerOption::HERO_ID;
			else if(what == "bonusId")
				code = LobbyChangePlayerOption::BONUS_ID;
			else
				throw std::runtime_error("Unknown option: " + what);

			GAME->server().setPlayerOption(code, value, color);
		});
	}

	mcp::json handleLobbySetDifficulty(const mcp::json & params, const std::string &)
	{
		auto value = params["difficulty"].get<int>();
		return lobbyActionTool([value]()
		{
			GAME->server().setDifficulty(value);
		});
	}

	mcp::json handleLobbyStartGame(const mcp::json &, const std::string &)
	{
		return lobbyActionTool([]()
		{
			GAME->server().sendStartGame();
		});
	}

	mcp::json handleLoadGame(const mcp::json & params, const std::string &)
	{
		auto path = params["path"].get<std::string>();
		return lobbyActionTool([path]()
		{
			GAME->server().quickLoadGame(path);
		});
	}

	mcp::json handleRestartGame(const mcp::json &, const std::string &)
	{
		return lobbyActionTool([]()
		{
			GAME->server().sendRestartGame();
		});
	}

	mcp::json handleReturnToMenu(const mcp::json &, const std::string &)
	{
		return lobbyActionTool([]()
		{
			GAME->server().endGameplay();
		});
	}
}

void registerLobbyTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("lobby_get_state")
			.with_description("Get the current lobby/pre-game state: connection state, selected map, mode, difficulty, per-player settings")
			.build(),
		handleLobbyGetState
	);

	srv->register_tool(
		mcp::tool_builder("lobby_list_maps")
			.with_description("List available map files (resource URIs usable with lobby_select_map)")
			.build(),
		handleLobbyListMaps
	);

	srv->register_tool(
		mcp::tool_builder("lobby_select_map")
			.with_description("Select a map for the game about to start (host only)")
			.with_string_param("fileUri", "Map resource URI, from lobby_list_maps (e.g. 'Maps/Arrogance')", true)
			.build(),
		handleLobbySelectMap
	);

	srv->register_tool(
		mcp::tool_builder("lobby_claim_player")
			.with_description("Claim a player color slot for this client (host only)")
			.with_string_param("player", "Player color name", true)
			.build(),
		handleLobbyClaimPlayer
	);

	srv->register_tool(
		mcp::tool_builder("lobby_set_player_option")
			.with_description("Change a player's lobby setting (host only)")
			.with_string_param("player", "Player color name", true)
			.with_string_param("what", "One of: town, hero, bonus, townId, heroId, bonusId", true)
			.with_number_param("value", "New value: -1/+1 to cycle for town/hero/bonus, or a specific id for the *Id variants", true)
			.build(),
		handleLobbySetPlayerOption
	);

	srv->register_tool(
		mcp::tool_builder("lobby_set_difficulty")
			.with_description("Set game difficulty (host only)")
			.with_number_param("difficulty", "Difficulty level (0=easy .. 4=impossible)", true)
			.build(),
		handleLobbySetDifficulty
	);

	srv->register_tool(
		mcp::tool_builder("lobby_start_game")
			.with_description("Start the game with the current lobby setup (host only). Once started, use the in-game tools (get_game_state, wait_for_event, ...).")
			.build(),
		handleLobbyStartGame
	);

	srv->register_tool(
		mcp::tool_builder("load_game")
			.with_description("Load a save file, replacing any current game. Destructive: unsaved progress in the current game is lost.")
			.with_string_param("path", "Save file path/resource name", true)
			.build(),
		handleLoadGame
	);

	srv->register_tool(
		mcp::tool_builder("restart_game")
			.with_description("Restart the current scenario from the beginning. Destructive: all progress in the current game is lost.")
			.build(),
		handleRestartGame
	);

	srv->register_tool(
		mcp::tool_builder("return_to_menu")
			.with_description("Leave the current game and return to the main menu. Destructive: unsaved progress in the current game is lost.")
			.build(),
		handleReturnToMenu
	);
}

#endif
