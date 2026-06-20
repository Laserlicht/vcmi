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

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/json/JsonNode.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/networkPacks/PacksForServer.h"

#include "../GameInstance.h"
#include "../GameEngine.h"
#include "../CPlayerInterface.h"
#include "../PlayerLocalState.h"
#include "../ClientCommandManager.h"

#include <shared_mutex>

#ifdef ENABLE_MCP_SERVER

static mcp::json textContent(const std::string & text)
{
	mcp::json arr = mcp::json::array();
	arr.push_back({{"type", "text"}, {"text", text}});
	return arr;
}

static mcp::json handleExecuteCommand(const mcp::json & params, const std::string &)
{
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

static mcp::json handleMoveHero(const mcp::json & params, const std::string &)
{
	auto heroId = params["heroId"].get<int>();
	int3 dest(params["x"].get<int>(), params["y"].get<int>(), params["z"].get<int>());

	ENGINE->dispatchMainThread([heroId, dest]() {
		auto pi = GAME->interface();
		if(!pi || !pi->cb)
			return;
		auto obj = pi->cb->getObj(ObjectInstanceID(heroId), false);
		if(!obj)
			return;
		auto hero = dynamic_cast<const CGHeroInstance *>(obj);
		if(!hero)
			return;

		if(pi->localState->setPath(hero, dest, EPathfindingLayer::LAND))
			pi->moveHero(hero, pi->localState->getPath(hero));
	});
	return textContent("Move queued for execution");
}

static mcp::json handleEndTurn(const mcp::json &, const std::string &)
{
	ENGINE->dispatchMainThread([]() {
		auto pi = GAME->interface();
		if(!pi || !pi->cb)
			return;
		pi->cb->endTurn();
	});
	return textContent("End turn queued for execution");
}

static mcp::json handleRecruitCreatures(const mcp::json & params, const std::string &)
{
	auto townId = params["townId"].get<int>();
	auto destId = params["destinationId"].get<int>();
	auto creatureId = params["creatureId"].get<int>();
	auto amount = params["amount"].get<int>();
	auto level = params.contains("level") ? params["level"].get<int>() : -1;

	ENGINE->dispatchMainThread([townId, destId, creatureId, amount, level]() {
		auto pi = GAME->interface();
		if(!pi || !pi->cb)
			return;
		auto townObj = pi->cb->getObj(ObjectInstanceID(townId), false);
		auto destObj = pi->cb->getObj(ObjectInstanceID(destId), false);
		if(!townObj || !destObj)
			return;
		auto dwelling = dynamic_cast<const CGDwelling *>(townObj);
		auto army = dynamic_cast<const CArmedInstance *>(destObj);
		if(!dwelling || !army)
			return;
		pi->cb->recruitCreatures(dwelling, army, CreatureID(creatureId), amount, level);
	});
	return textContent("Recruit queued for execution");
}

static mcp::json handleBuildBuilding(const mcp::json & params, const std::string &)
{
	auto townId = params["townId"].get<int>();
	auto buildingId = params["buildingId"].get<int>();

	ENGINE->dispatchMainThread([townId, buildingId]() {
		auto pi = GAME->interface();
		if(!pi || !pi->cb)
			return;
		auto obj = pi->cb->getObj(ObjectInstanceID(townId), false);
		if(!obj)
			return;
		auto town = dynamic_cast<const CGTownInstance *>(obj);
		if(!town)
			return;
		pi->cb->buildBuilding(town, BuildingID(buildingId));
	});
	return textContent("Build queued for execution");
}

static mcp::json handleDismissHero(const mcp::json & params, const std::string &)
{
	auto heroId = params["heroId"].get<int>();

	ENGINE->dispatchMainThread([heroId]() {
		auto pi = GAME->interface();
		if(!pi || !pi->cb)
			return;
		auto obj = pi->cb->getObj(ObjectInstanceID(heroId), false);
		if(!obj)
			return;
		auto hero = dynamic_cast<const CGHeroInstance *>(obj);
		if(!hero)
			return;
		pi->cb->dismissHero(hero);
	});
	return textContent("Dismiss queued for execution");
}

void registerActionTools(mcp::server * srv)
{
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
		handleExecuteCommand
	);

	srv->register_tool(
		mcp::tool{ "move_hero", "Move a hero to a specific map tile (same as client-side click)", {
			{"type", "object"},
			{"properties", {
				{"heroId", {
					{"type", "number"},
					{"description", "Hero instance ID"}
				}},
				{"x", {
					{"type", "number"},
					{"description", "Target X coordinate"}
				}},
				{"y", {
					{"type", "number"},
					{"description", "Target Y coordinate"}
				}},
				{"z", {
					{"type", "number"},
					{"description", "Target Z coordinate (level)"}
				}}
			}},
			{"required", {"heroId", "x", "y", "z"}}
		}, mcp::json() },
		handleMoveHero
	);

	srv->register_tool(
		mcp::tool{ "end_turn", "End the current player's turn", {{"type", "object"}}, mcp::json() },
		handleEndTurn
	);

	srv->register_tool(
		mcp::tool{ "recruit_creatures", "Recruit creatures in a town", {
			{"type", "object"},
			{"properties", {
				{"townId", {
					{"type", "number"},
					{"description", "Town instance ID"}
				}},
				{"destinationId", {
					{"type", "number"},
					{"description", "Destination hero or garrison instance ID"}
				}},
				{"creatureId", {
					{"type", "number"},
					{"description", "Creature type ID"}
				}},
				{"amount", {
					{"type", "number"},
					{"description", "Number of creatures to recruit"}
				}},
				{"level", {
					{"type", "number"},
					{"description", "Dwelling level to buy from (-1 for any)"}
				}}
			}},
			{"required", {"townId", "destinationId", "creatureId", "amount"}}
		}, mcp::json() },
		handleRecruitCreatures
	);

	srv->register_tool(
		mcp::tool{ "build_building", "Construct a building in a town", {
			{"type", "object"},
			{"properties", {
				{"townId", {
					{"type", "number"},
					{"description", "Town instance ID"}
				}},
				{"buildingId", {
					{"type", "number"},
					{"description", "Building ID to construct"}
				}}
			}},
			{"required", {"townId", "buildingId"}}
		}, mcp::json() },
		handleBuildBuilding
	);

	srv->register_tool(
		mcp::tool{ "dismiss_hero", "Dismiss a hero", {
			{"type", "object"},
			{"properties", {
				{"heroId", {
					{"type", "number"},
					{"description", "Hero instance ID to dismiss"}
				}}
			}},
			{"required", {"heroId"}}
		}, mcp::json() },
		handleDismissHero
	);
}

#endif
