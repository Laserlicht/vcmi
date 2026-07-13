/*
 * AdventureInfoTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "AdventureInfoTools.h"
#include "ToolContext.h"
#include "Serializers.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/json/JsonNode.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGMarket.h"
#include "../../lib/mapObjects/IMarket.h"
#include "../../lib/mapObjects/CQuest.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/pathfinder/CGPathNode.h"

#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../Client.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	JsonNode dwellingCreaturesToJson(const CGDwelling::TCreaturesSet & creatures)
	{
		JsonNode result;
		for(size_t level = 0; level < creatures.size(); level++)
		{
			auto & [available, types] = creatures[level];
			if(available == 0 && types.empty())
				continue;
			JsonNode clevel;
			clevel["level"] = JsonNode(static_cast<int>(level));
			clevel["available"] = JsonNode(static_cast<int>(available));
			JsonNode typesJson;
			for(auto & cid : types)
			{
				JsonNode ce;
				ce["id"] = JsonNode(cid.getNum());
				auto * cre = cid.toCreature();
				if(cre)
					ce["name"] = JsonNode(cre->getNameSingularTranslated());
				typesJson.Vector().push_back(ce);
			}
			clevel["types"] = typesJson;
			result.Vector().push_back(clevel);
		}
		return result;
	}

	JsonNode marketToJson(const IMarket * market)
	{
		JsonNode entry;
		entry["objectId"] = JsonNode(market->getObjInstanceID().getNum());
		entry["efficiency"] = JsonNode(market->getMarketEfficiency());
		JsonNode modes;
		for(auto mode : market->availableModes())
			modes.Vector().push_back(JsonNode(static_cast<int>(mode)));
		entry["modes"] = modes;
		return entry;
	}

	JsonNode getTiles(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		int x1 = params["x1"].get<int>();
		int y1 = params["y1"].get<int>();
		int x2 = params["x2"].get<int>();
		int y2 = params["y2"].get<int>();
		int z = params.contains("z") ? params["z"].get<int>() : 0;

		auto player = cb.getPlayerID();
		if(!player)
			throw std::runtime_error("No player");
		auto team = cb.getPlayerTeam(*player);
		if(!team)
			throw std::runtime_error("No team");

		auto & map = GAME->server().client->gameState().getMap();
		int xLo = std::max(0, std::min(x1, x2));
		int xHi = std::min(map.width - 1, std::max(x1, x2));
		int yLo = std::max(0, std::min(y1, y2));
		int yHi = std::min(map.height - 1, std::max(y1, y2));

		JsonNode tiles;
		for(int x = xLo; x <= xHi; x++)
		{
			for(int y = yLo; y <= yHi; y++)
			{
				int3 pos(x, y, z);
				if(!map.isInTheMap(pos) || !team->fogOfWarMap[pos])
					continue;
				tiles.Vector().push_back(terrainTileToJson(map.getTile(pos), pos));
			}
		}
		return tiles;
	}

	JsonNode getObjectDetails(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto objectId = params["objectId"].get<int>();
		auto obj = cb.getObj(ObjectInstanceID(objectId), false);
		if(!obj)
			throw std::runtime_error("No object with id " + std::to_string(objectId));

		if(auto * hero = dynamic_cast<const CGHeroInstance *>(obj))
			return heroDetailsToJson(hero);
		if(auto * town = dynamic_cast<const CGTownInstance *>(obj))
			return townDetailsToJson(town);

		JsonNode entry;
		entry["id"] = JsonNode(obj->id.getNum());
		entry["typeId"] = JsonNode(obj->ID.getNum());
		entry["subtypeId"] = JsonNode(obj->subID.getNum());
		entry["name"] = JsonNode(obj->getObjectName());
		entry["position"]["x"] = JsonNode(obj->visitablePos().x);
		entry["position"]["y"] = JsonNode(obj->visitablePos().y);
		entry["position"]["z"] = JsonNode(obj->visitablePos().z);
		entry["owner"] = JsonNode(obj->tempOwner.toString());
		entry["blockVisit"] = JsonNode(obj->blockVisit);

		if(auto * dwelling = dynamic_cast<const CGDwelling *>(obj))
			entry["creatures"] = dwellingCreaturesToJson(dwelling->creatures);

		if(auto * market = dynamic_cast<const IMarket *>(obj))
			entry["market"] = marketToJson(market);

		return entry;
	}

	JsonNode getHeroPath(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto heroId = params["heroId"].get<int>();
		auto obj = cb.getObj(ObjectInstanceID(heroId), false);
		auto hero = dynamic_cast<const CGHeroInstance *>(obj);
		if(!hero)
			throw std::runtime_error("No hero with id " + std::to_string(heroId));

		int3 dest(params["x"].get<int>(), params["y"].get<int>(), params["z"].get<int>());

		auto pi = GAME->interface();
		if(!pi)
			throw std::runtime_error("No active game");

		auto pathsInfo = pi->getPathsInfo(hero);
		if(!pathsInfo)
			throw std::runtime_error("Path info not available");

		CGPath path;
		JsonNode result;
		result["reachable"] = JsonNode(pathsInfo->getPath(path, dest));

		JsonNode nodes;
		for(auto & node : path.nodes)
		{
			JsonNode n;
			n["x"] = JsonNode(node.coord.x);
			n["y"] = JsonNode(node.coord.y);
			n["z"] = JsonNode(node.coord.z);
			n["turns"] = JsonNode(static_cast<int>(node.turns));
			n["moveRemains"] = JsonNode(node.moveRemains);
			n["accessibility"] = JsonNode(static_cast<int>(node.accessible));
			n["action"] = JsonNode(static_cast<int>(node.action));
			nodes.Vector().push_back(n);
		}
		result["nodes"] = nodes;
		return result;
	}

	JsonNode getMarketInfo(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto marketId = params["marketId"].get<int>();
		auto obj = cb.getObj(ObjectInstanceID(marketId), false);
		auto market = dynamic_cast<const IMarket *>(obj);
		if(!market)
			throw std::runtime_error("No market with id " + std::to_string(marketId));

		JsonNode result = marketToJson(market);

		if(params.contains("mode") && params.contains("itemId"))
		{
			auto mode = static_cast<EMarketMode>(params["mode"].get<int>());
			int itemId = params["itemId"].get<int>();
			JsonNode rates;
			for(auto & buyId : market->availableItemsIds(mode))
			{
				int val1 = 1, val2 = 0;
				if(market->getOffer(itemId, buyId.getNum(), val1, val2, mode))
				{
					JsonNode rate;
					rate["buyItemId"] = JsonNode(buyId.getNum());
					rate["giveUnits"] = JsonNode(val1);
					rate["receiveUnits"] = JsonNode(val2);
					rates.Vector().push_back(rate);
				}
			}
			result["rates"] = rates;
		}
		return result;
	}

	JsonNode getTavernHeroes(const mcp::json & params)
	{
		auto & cb = mcptool::activeCallback();
		auto color = params.contains("player") ? parsePlayerColor(params["player"].get<std::string>()) : cb.getPlayerID().value_or(PlayerColor::NEUTRAL);
		if(!color.isValidPlayer())
			throw std::runtime_error("Invalid player color");

		auto & gs = GAME->server().client->gameState();
		JsonNode arr;
		for(auto * h : gs.heroesPool->getHeroesFor(color))
			arr.Vector().push_back(heroToJson(h));
		return arr;
	}

	JsonNode getQuests()
	{
		auto & cb = mcptool::activeCallback();
		JsonNode arr;
		for(auto & questInfo : cb.getMyQuests())
		{
			auto * quest = questInfo.getQuest(&cb);
			if(!quest)
				continue;
			JsonNode entry;
			entry["objectId"] = JsonNode(questInfo.obj.getNum());
			entry["name"] = JsonNode(quest->questName);
			entry["isCompleted"] = JsonNode(quest->isCompleted);
			entry["lastDay"] = JsonNode(quest->lastDay);
			MetaString rollover;
			quest->getRolloverText(&cb, rollover, false);
			entry["description"] = JsonNode(rollover.toString());
			arr.Vector().push_back(entry);
		}
		return arr;
	}

	JsonNode listBuildings()
	{
		JsonNode result;
		LIBRARY->townh->forEach([&](const Faction * factionBase, bool & stop)
		{
			auto * faction = dynamic_cast<const CFaction *>(factionBase);
			if(!faction || !faction->town)
				return;
			JsonNode buildings;
			for(auto & [id, building] : faction->town->buildings)
				buildings.Vector().push_back(buildingToJson(building.get()));
			result[std::to_string(faction->getId().getNum())] = buildings;
		});
		return result;
	}

	JsonNode listHeroTypes()
	{
		JsonNode arr;
		LIBRARY->heroh->forEach([&](const HeroType * hero, bool & stop)
		{
			arr.Vector().push_back(heroTypeToJson(hero));
		});
		return arr;
	}
}

void registerAdventureInfoTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("get_tiles")
			.with_description("Get terrain info for a rectangular region of tiles visible to the current player (replaces dumping the whole map)")
			.with_number_param("x1", "First corner X", true)
			.with_number_param("y1", "First corner Y", true)
			.with_number_param("x2", "Second corner X", true)
			.with_number_param("y2", "Second corner Y", true)
			.with_number_param("z", "Map level (0 = surface, 1 = underground, default 0)", false)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getTiles(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_object_details")
			.with_description("Get detailed info about any map object by id (dispatches to hero/town details, or a generic description with dwelling/market info where applicable)")
			.with_number_param("objectId", "Object instance ID", true)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getObjectDetails(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_hero_path")
			.with_description("Get the pathfinding route and reachability/cost info for a hero to a destination tile")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_number_param("x", "Destination X", true)
			.with_number_param("y", "Destination Y", true)
			.with_number_param("z", "Destination Z", true)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getHeroPath(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_market_info")
			.with_description("Get info about a market (town marketplace or standalone), optionally with exchange rates for a specific item")
			.with_number_param("marketId", "Market/town object instance ID", true)
			.with_number_param("mode", "EMarketMode value to query rates for (optional)", false)
			.with_number_param("itemId", "Item id being sold, to query rates for (optional)", false)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getMarketInfo(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_tavern_heroes")
			.with_description("List heroes currently available to recruit in a player's tavern")
			.with_string_param("player", "Player color (default: current player)", false)
			.build(),
		[](const mcp::json & params, const std::string &) { return mcptool::readTool([&params]() { return getTavernHeroes(params); }); }
	);

	srv->register_tool(
		mcp::tool_builder("get_quests")
			.with_description("List active quests for the current player")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(getQuests); }
	);

	srv->register_tool(
		mcp::tool_builder("list_buildings")
			.with_description("List all town buildings per faction, with costs and upgrade chains")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(listBuildings); }
	);

	srv->register_tool(
		mcp::tool_builder("list_hero_types")
			.with_description("List all hero types with class, starting army and skills")
			.build(),
		[](const mcp::json &, const std::string &) { return mcptool::readTool(listHeroTypes); }
	);
}

#endif
