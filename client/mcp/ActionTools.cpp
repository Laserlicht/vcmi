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
	mcp::json handleExecuteCommand(const mcp::json & params, const std::string &)
	{
		auto cmd = params["command"].get<std::string>();
		mcptool::dispatchMainThreadSafe([cmd]() {
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
			auto & hero = mcptool::requireHero(cb, heroId);
			auto pi = GAME->interface();

			if(pi->localState->setPath(&hero, dest, EPathfindingLayer::AUTO))
				pi->moveHero(&hero, pi->localState->getPath(&hero));
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
			if(!dwelling)
				throw std::runtime_error("No dwelling/town with id " + std::to_string(townId));
			auto & army = mcptool::requireArmedInstance(cb, destId);
			cb.recruitCreatures(dwelling, &army, CreatureID(creatureId), amount, level);
		});
	}

	mcp::json handleBuildBuilding(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();
		auto buildingId = params["buildingId"].get<int>();

		return mcptool::actionTool([townId, buildingId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & town = mcptool::requireTown(cb, townId);
			if(!cb.buildBuilding(&town, BuildingID(buildingId)))
				throw std::runtime_error("Building cannot be constructed (not owned, or requirements not met)");
		});
	}

	mcp::json handleDismissHero(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();

		return mcptool::actionTool([heroId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			if(!cb.dismissHero(&hero))
				throw std::runtime_error("Hero cannot be dismissed (not owned)");
		});
	}

	mcp::json handleCastAdventureSpell(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		auto spellId = params["spellId"].get<int>();
		int3 pos(
			params.contains("x") ? params["x"].get<int>() : -1,
			params.contains("y") ? params["y"].get<int>() : -1,
			params.contains("z") ? params["z"].get<int>() : -1);

		return mcptool::actionTool([heroId, spellId, pos]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			cb.castSpell(&hero, SpellID(spellId), pos);
		});
	}

	mcp::json handleDig(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();

		return mcptool::actionTool([heroId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			cb.dig(&hero);
		});
	}

	mcp::json handleCastleGateTeleport(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		auto townId = params["townId"].get<int>();

		return mcptool::actionTool([heroId, townId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			auto & town = mcptool::requireTown(cb, townId);
			if(!cb.teleportHero(&hero, &town))
				throw std::runtime_error("Teleport failed");
		});
	}

	mcp::json handleSetFormation(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		bool tight = params["tight"].get<bool>();

		return mcptool::actionTool([heroId, tight]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			cb.setFormation(&hero, tight ? EArmyFormation::TIGHT : EArmyFormation::LOOSE);
		});
	}

	mcp::json handleSetTactics(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		bool enabled = params["enabled"].get<bool>();

		return mcptool::actionTool([heroId, enabled]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			cb.setTactics(&hero, enabled);
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

	srv->register_tool(
		mcp::tool_builder("cast_adventure_spell")
			.with_description("Cast an adventure-map spell with a hero (Town Portal, Fly, Water Walk, Dimension Door, ...)")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_number_param("spellId", "Spell ID", true)
			.with_number_param("x", "Target X coordinate, if the spell needs one", false)
			.with_number_param("y", "Target Y coordinate, if the spell needs one", false)
			.with_number_param("z", "Target Z coordinate, if the spell needs one", false)
			.build(),
		handleCastAdventureSpell
	);

	srv->register_tool(
		mcp::tool_builder("dig")
			.with_description("Dig for the Grail with a hero standing on the correct tile")
			.with_number_param("heroId", "Hero instance ID", true)
			.build(),
		handleDig
	);

	srv->register_tool(
		mcp::tool_builder("castle_gate_teleport")
			.with_description("Teleport a hero through the Castle Gate to another owned town with a Castle Gate")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_number_param("townId", "Destination town instance ID", true)
			.build(),
		handleCastleGateTeleport
	);

	srv->register_tool(
		mcp::tool_builder("set_formation")
			.with_description("Set a hero's army formation")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_boolean_param("tight", "true for tight formation, false for loose", true)
			.build(),
		handleSetFormation
	);

	srv->register_tool(
		mcp::tool_builder("set_tactics")
			.with_description("Enable or disable the tactics phase for a hero's next battle")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_boolean_param("enabled", "true to enable tactics", true)
			.build(),
		handleSetTactics
	);
}

#endif
