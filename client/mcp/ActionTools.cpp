/*
 * ActionTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "ActionTools.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGDwelling.h"

#include "../GameInstance.h"
#include "../GameEngine.h"
#include "../CPlayerInterface.h"
#include "../PlayerLocalState.h"
#include "../ClientCommandManager.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	const CGHeroInstance * requireHero(CCallback & cb, int heroId)
	{
		auto obj = cb.getObj(ObjectInstanceID(heroId), false);
		auto hero = dynamic_cast<const CGHeroInstance *>(obj);
		if(!hero)
			throw std::runtime_error("No hero with id " + std::to_string(heroId));
		return hero;
	}

	const CGTownInstance * requireTown(CCallback & cb, int townId)
	{
		auto obj = cb.getObj(ObjectInstanceID(townId), false);
		auto town = dynamic_cast<const CGTownInstance *>(obj);
		if(!town)
			throw std::runtime_error("No town with id " + std::to_string(townId));
		return town;
	}

	mcp::json handleExecuteCommand(const mcp::json & params, const std::string &)
	{
		auto cmd = params["command"].get<std::string>();
		ENGINE->dispatchMainThread([cmd]() {
			if(GAME->interface())
			{
				ClientCommandManager commandController;
				commandController.processCommand(cmd, false);
			}
		});
		return mcptool::textContent("Command queued for execution");
	}

	mcp::json handleMoveHero(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		int3 dest(params["x"].get<int>(), params["y"].get<int>(), params["z"].get<int>());

		return mcptool::actionTool([heroId, dest]()
		{
			auto & cb = mcptool::activeCallback();
			auto hero = requireHero(cb, heroId);
			auto pi = GAME->interface();

			if(pi->localState->setPath(hero, dest, EPathfindingLayer::AUTO))
				pi->moveHero(hero, pi->localState->getPath(hero));
			else
				throw std::runtime_error("No path to destination");
		});
	}

	mcp::json handleEndTurn(const mcp::json &, const std::string &)
	{
		return mcptool::actionTool([]()
		{
			mcptool::activeCallback().endTurn();
		});
	}

	mcp::json handleRecruitCreatures(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();
		auto destId = params["destinationId"].get<int>();
		auto creatureId = params["creatureId"].get<int>();
		auto amount = params["amount"].get<int>();
		auto level = params.contains("level") ? params["level"].get<int>() : -1;

		return mcptool::actionTool([townId, destId, creatureId, amount, level]()
		{
			auto & cb = mcptool::activeCallback();
			auto dwelling = dynamic_cast<const CGDwelling *>(cb.getObj(ObjectInstanceID(townId), false));
			auto army = dynamic_cast<const CArmedInstance *>(cb.getObj(ObjectInstanceID(destId), false));
			if(!dwelling)
				throw std::runtime_error("No dwelling/town with id " + std::to_string(townId));
			if(!army)
				throw std::runtime_error("No garrison with id " + std::to_string(destId));
			cb.recruitCreatures(dwelling, army, CreatureID(creatureId), amount, level);
		});
	}

	mcp::json handleBuildBuilding(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();
		auto buildingId = params["buildingId"].get<int>();

		return mcptool::actionTool([townId, buildingId]()
		{
			auto & cb = mcptool::activeCallback();
			auto town = requireTown(cb, townId);
			if(!cb.buildBuilding(town, BuildingID(buildingId)))
				throw std::runtime_error("Building cannot be constructed (not owned, or requirements not met)");
		});
	}

	mcp::json handleDismissHero(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();

		return mcptool::actionTool([heroId]()
		{
			auto & cb = mcptool::activeCallback();
			auto hero = requireHero(cb, heroId);
			if(!cb.dismissHero(hero))
				throw std::runtime_error("Hero cannot be dismissed (not owned)");
		});
	}
}

void registerActionTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("execute_command")
			.with_description("Execute a VCMI console command (debug/cheat facility)")
			.with_string_param("command", "Console command to execute", true)
			.build(),
		handleExecuteCommand
	);

	srv->register_tool(
		mcp::tool_builder("move_hero")
			.with_description("Move a hero to a specific map tile (same as client-side click). Waits for the move to be realized and returns what happened (steps taken, blocking visit, battle started, ...).")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_number_param("x", "Target X coordinate", true)
			.with_number_param("y", "Target Y coordinate", true)
			.with_number_param("z", "Target Z coordinate (level)", true)
			.build(),
		handleMoveHero
	);

	srv->register_tool(
		mcp::tool_builder("end_turn")
			.with_description("End the current player's turn")
			.build(),
		handleEndTurn
	);

	srv->register_tool(
		mcp::tool_builder("recruit_creatures")
			.with_description("Recruit creatures in a town")
			.with_number_param("townId", "Town/dwelling instance ID", true)
			.with_number_param("destinationId", "Destination hero or garrison instance ID", true)
			.with_number_param("creatureId", "Creature type ID", true)
			.with_number_param("amount", "Number of creatures to recruit", true)
			.with_number_param("level", "Dwelling level to buy from (-1 for any)", false)
			.build(),
		handleRecruitCreatures
	);

	srv->register_tool(
		mcp::tool_builder("build_building")
			.with_description("Construct a building in a town")
			.with_number_param("townId", "Town instance ID", true)
			.with_number_param("buildingId", "Building ID to construct", true)
			.build(),
		handleBuildBuilding
	);

	srv->register_tool(
		mcp::tool_builder("dismiss_hero")
			.with_description("Dismiss a hero")
			.with_number_param("heroId", "Hero instance ID to dismiss", true)
			.build(),
		handleDismissHero
	);
}

#endif
