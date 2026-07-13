/*
 * TownTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "TownTools.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/IMarket.h"
#include "../../lib/mapObjects/IObjectInterface.h"

#include "../GameInstance.h"
#include "../CServerHandler.h"
#include "../Client.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	TradeItemSell makeSellItem(EMarketMode mode, int id)
	{
		switch(mode)
		{
		case EMarketMode::CREATURE_RESOURCE:
		case EMarketMode::CREATURE_UNDEAD:
		case EMarketMode::CREATURE_EXP:
			return TradeItemSell(SlotID(id));
		case EMarketMode::ARTIFACT_RESOURCE:
		case EMarketMode::ARTIFACT_EXP:
			return TradeItemSell(ArtifactInstanceID(id));
		default:
			return TradeItemSell(GameResID(id));
		}
	}

	TradeItemBuy makeBuyItem(EMarketMode mode, int id)
	{
		switch(mode)
		{
		case EMarketMode::RESOURCE_PLAYER:
			return TradeItemBuy(PlayerColor(id));
		case EMarketMode::RESOURCE_ARTIFACT:
			return TradeItemBuy(ArtifactID(id));
		case EMarketMode::RESOURCE_SKILL:
			return TradeItemBuy(SecondarySkill(id));
		default:
			return TradeItemBuy(GameResID(id));
		}
	}

	mcp::json handleVisitTownBuilding(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();
		auto buildingId = params["buildingId"].get<int>();

		return mcptool::actionTool([townId, buildingId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & town = mcptool::requireTown(cb, townId);
			if(!cb.visitTownBuilding(&town, BuildingID(buildingId)))
				throw std::runtime_error("Building cannot be visited (not owned)");
		});
	}

	mcp::json handleHireHero(const mcp::json & params, const std::string &)
	{
		auto townId = params["townOrTavernId"].get<int>();
		auto heroTypeId = params["heroTypeId"].get<int>();
		auto nextHeroTypeId = params.contains("nextHeroTypeId") ? params["nextHeroTypeId"].get<int>() : HeroTypeID().getNum();

		return mcptool::actionTool([townId, heroTypeId, nextHeroTypeId]()
		{
			auto & cb = mcptool::activeCallback();
			auto obj = cb.getObj(ObjectInstanceID(townId), false);
			if(!obj)
				throw std::runtime_error("No object with id " + std::to_string(townId));

			auto & gs = GAME->server().client->gameState();
			auto color = cb.getPlayerID().value_or(PlayerColor::NEUTRAL);
			const CGHeroInstance * available = nullptr;
			for(auto * h : gs.heroesPool->getHeroesFor(color))
			{
				if(h->getHeroTypeID() == HeroTypeID(heroTypeId))
				{
					available = h;
					break;
				}
			}
			if(!available)
				throw std::runtime_error("Hero type " + std::to_string(heroTypeId) + " is not available in this tavern");

			cb.recruitHero(obj, available, HeroTypeID(nextHeroTypeId));
		});
	}

	mcp::json handleSwapGarrisonHero(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();

		return mcptool::actionTool([townId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & town = mcptool::requireTown(cb, townId);
			cb.swapGarrisonHero(&town);
		});
	}

	mcp::json handleResearchSpell(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();
		auto spellId = params["spellId"].get<int>();
		bool accept = params["accept"].get<bool>();

		return mcptool::actionTool([townId, spellId, accept]()
		{
			auto & cb = mcptool::activeCallback();
			auto & town = mcptool::requireTown(cb, townId);
			cb.spellResearch(&town, SpellID(spellId), accept);
		});
	}

	mcp::json handleRenameTown(const mcp::json & params, const std::string &)
	{
		auto townId = params["townId"].get<int>();
		auto name = params["name"].get<std::string>();

		return mcptool::actionTool([townId, name]()
		{
			auto & cb = mcptool::activeCallback();
			auto & town = mcptool::requireTown(cb, townId);
			std::string nameCopy = name;
			cb.setTownName(&town, nameCopy);
		});
	}

	mcp::json handleBuildBoat(const mcp::json & params, const std::string &)
	{
		auto shipyardObjectId = params["shipyardObjectId"].get<int>();

		return mcptool::actionTool([shipyardObjectId]()
		{
			auto & cb = mcptool::activeCallback();
			auto obj = cb.getObj(ObjectInstanceID(shipyardObjectId), false);
			auto shipyard = dynamic_cast<const IShipyard *>(obj);
			if(!shipyard)
				throw std::runtime_error("No shipyard with id " + std::to_string(shipyardObjectId));
			cb.buildBoat(shipyard);
		});
	}

	mcp::json handleTrade(const mcp::json & params, const std::string &)
	{
		auto marketId = params["marketId"].get<int>();
		auto mode = static_cast<EMarketMode>(params["mode"].get<int>());
		auto heroId = params.contains("heroId") ? params["heroId"].get<int>() : ObjectInstanceID::NONE.getNum();

		std::vector<int> sellIds;
		for(auto & v : params["sellIds"])
			sellIds.push_back(v.get<int>());
		std::vector<int> buyIds;
		for(auto & v : params["buyIds"])
			buyIds.push_back(v.get<int>());
		std::vector<ui32> amounts;
		for(auto & v : params["amounts"])
			amounts.push_back(v.get<ui32>());

		return mcptool::actionTool([marketId, mode, heroId, sellIds, buyIds, amounts]()
		{
			auto & cb = mcptool::activeCallback();
			auto obj = cb.getObj(ObjectInstanceID(marketId), false);
			if(!dynamic_cast<const IMarket *>(obj))
				throw std::runtime_error("No market with id " + std::to_string(marketId));

			const CGHeroInstance * hero = nullptr;
			if(heroId != ObjectInstanceID::NONE.getNum())
				hero = &mcptool::requireHero(cb, heroId);

			std::vector<TradeItemSell> sell;
			for(auto id : sellIds)
				sell.push_back(makeSellItem(mode, id));
			std::vector<TradeItemBuy> buy;
			for(auto id : buyIds)
				buy.push_back(makeBuyItem(mode, id));

			cb.trade(ObjectInstanceID(marketId), mode, sell, buy, amounts, hero);
		});
	}
}

void registerTownTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("visit_town_building")
			.with_description("Trigger a visitable town building (tavern, mage guild, shrine, ...) without a hero present - opens whatever window/effect it has")
			.with_number_param("townId", "Town instance ID", true)
			.with_number_param("buildingId", "Building ID to visit", true)
			.build(),
		handleVisitTownBuilding
	);

	srv->register_tool(
		mcp::tool_builder("hire_hero")
			.with_description("Recruit a hero from a town's tavern (see get_tavern_heroes for available heroTypeId values)")
			.with_number_param("townOrTavernId", "Town/tavern instance ID", true)
			.with_number_param("heroTypeId", "Hero type ID to recruit, from get_tavern_heroes", true)
			.with_number_param("nextHeroTypeId", "Hero type ID to offer as replacement in the pool (optional)", false)
			.build(),
		handleHireHero
	);

	srv->register_tool(
		mcp::tool_builder("swap_garrison_hero")
			.with_description("Swap the visiting and garrisoned heroes of a town")
			.with_number_param("townId", "Town instance ID", true)
			.build(),
		handleSwapGarrisonHero
	);

	srv->register_tool(
		mcp::tool_builder("research_spell")
			.with_description("Accept or decline mage guild spell research (VCMI extension)")
			.with_number_param("townId", "Town instance ID", true)
			.with_number_param("spellId", "Spell ID being researched", true)
			.with_boolean_param("accept", "true to accept, false to decline", true)
			.build(),
		handleResearchSpell
	);

	srv->register_tool(
		mcp::tool_builder("rename_town")
			.with_description("Rename a town")
			.with_number_param("townId", "Town instance ID", true)
			.with_string_param("name", "New town name", true)
			.build(),
		handleRenameTown
	);

	srv->register_tool(
		mcp::tool_builder("build_boat")
			.with_description("Buy a boat at a shipyard (town or standalone)")
			.with_number_param("shipyardObjectId", "Shipyard object instance ID", true)
			.build(),
		handleBuildBoat
	);

	srv->register_tool(
		mcp::tool_builder("trade")
			.with_description("Trade at a market. Item id meaning depends on mode: RESOURCE_RESOURCE(0)/RESOURCE_PLAYER(1)/RESOURCE_ARTIFACT(3)/RESOURCE_SKILL(8) sell resource ids; CREATURE_RESOURCE(2)/CREATURE_UNDEAD(7)/CREATURE_EXP(6) sell army slot indices; ARTIFACT_RESOURCE(4)/ARTIFACT_EXP(5) sell backpack artifact instance ids. Buy ids are resources unless mode is RESOURCE_PLAYER(player color id)/RESOURCE_ARTIFACT(artifact id)/RESOURCE_SKILL(skill id).")
			.with_number_param("marketId", "Market/town object instance ID", true)
			.with_number_param("mode", "EMarketMode value", true)
			.with_array_param("sellIds", "Ids of items being given, one per trade", "number", true)
			.with_array_param("buyIds", "Ids of items being received, one per trade", "number", true)
			.with_array_param("amounts", "Units of the sold item per trade", "number", true)
			.with_number_param("heroId", "Hero involved (required for artifact/creature trades)", false)
			.build(),
		handleTrade
	);
}

#endif
