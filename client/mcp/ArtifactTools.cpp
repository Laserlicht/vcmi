/*
 * ArtifactTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "ArtifactTools.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/ArtifactLocation.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	mcp::json handleMoveArtifact(const mcp::json & params, const std::string &)
	{
		auto srcObjectId = params["srcObjectId"].get<int>();
		auto srcSlot = params["srcSlot"].get<int>();
		auto dstObjectId = params["dstObjectId"].get<int>();
		auto dstSlot = params["dstSlot"].get<int>();

		return mcptool::actionTool([srcObjectId, srcSlot, dstObjectId, dstSlot]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireHero(cb, srcObjectId);
			mcptool::requireHero(cb, dstObjectId);
			ArtifactLocation src{ObjectInstanceID(srcObjectId), ArtifactPosition(srcSlot)};
			ArtifactLocation dst{ObjectInstanceID(dstObjectId), ArtifactPosition(dstSlot)};
			if(!cb.swapArtifacts(src, dst))
				throw std::runtime_error("Artifact move failed");
		});
	}

	mcp::json handleTransferArtifacts(const mcp::json & params, const std::string &)
	{
		auto srcHeroId = params["srcHeroId"].get<int>();
		auto dstHeroId = params["dstHeroId"].get<int>();
		bool swap = params.contains("swap") ? params["swap"].get<bool>() : false;
		bool equipped = params.contains("equipped") ? params["equipped"].get<bool>() : true;
		bool backpack = params.contains("backpack") ? params["backpack"].get<bool>() : true;

		return mcptool::actionTool([srcHeroId, dstHeroId, swap, equipped, backpack]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireHero(cb, srcHeroId);
			mcptool::requireHero(cb, dstHeroId);
			cb.bulkMoveArtifacts(ObjectInstanceID(srcHeroId), ObjectInstanceID(dstHeroId), swap, equipped, backpack);
		});
	}

	mcp::json handleAssembleArtifact(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		auto slot = params["slot"].get<int>();
		bool assemble = params["assemble"].get<bool>();
		auto assembleTo = params.contains("assembleToId") ? params["assembleToId"].get<int>() : ArtifactID().getNum();

		return mcptool::actionTool([heroId, slot, assemble, assembleTo]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireHero(cb, heroId);
			cb.assembleArtifacts(ObjectInstanceID(heroId), ArtifactPosition(slot), assemble, ArtifactID(assembleTo));
		});
	}

	mcp::json handleBuyArtifact(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		auto artifactId = params["artifactId"].get<int>();

		return mcptool::actionTool([heroId, artifactId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & hero = mcptool::requireHero(cb, heroId);
			cb.buyArtifact(&hero, ArtifactID(artifactId));
		});
	}

	mcp::json handleSortBackpack(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		auto command = params["command"].get<std::string>();

		return mcptool::actionTool([heroId, command]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireHero(cb, heroId);
			ObjectInstanceID hero(heroId);
			if(command == "scrollLeft")
				cb.scrollBackpackArtifacts(hero, true);
			else if(command == "scrollRight")
				cb.scrollBackpackArtifacts(hero, false);
			else if(command == "sortBySlot")
				cb.sortBackpackArtifactsBySlot(hero);
			else if(command == "sortByCost")
				cb.sortBackpackArtifactsByCost(hero);
			else if(command == "sortByClass")
				cb.sortBackpackArtifactsByClass(hero);
			else
				throw std::runtime_error("Unknown command: " + command);
		});
	}

	mcp::json handleManageArtifactCostume(const mcp::json & params, const std::string &)
	{
		auto heroId = params["heroId"].get<int>();
		auto costumeIdx = params["costumeIdx"].get<int>();
		bool save = params["save"].get<bool>();

		return mcptool::actionTool([heroId, costumeIdx, save]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireHero(cb, heroId);
			cb.manageHeroCostume(ObjectInstanceID(heroId), static_cast<size_t>(costumeIdx), save);
		});
	}
}

void registerArtifactTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("move_artifact")
			.with_description("Move a single artifact between slots, possibly across two different heroes")
			.with_number_param("srcObjectId", "Source hero instance ID", true)
			.with_number_param("srcSlot", "Source artifact slot (ArtifactPosition)", true)
			.with_number_param("dstObjectId", "Destination hero instance ID", true)
			.with_number_param("dstSlot", "Destination artifact slot (ArtifactPosition)", true)
			.build(),
		handleMoveArtifact
	);

	srv->register_tool(
		mcp::tool_builder("transfer_artifacts")
			.with_description("Move or swap all transferable artifacts between two heroes (e.g. before dismissing one)")
			.with_number_param("srcHeroId", "Source hero instance ID", true)
			.with_number_param("dstHeroId", "Destination hero instance ID", true)
			.with_boolean_param("swap", "Swap instead of one-way move (default false)", false)
			.with_boolean_param("equipped", "Include equipped artifacts (default true)", false)
			.with_boolean_param("backpack", "Include backpack artifacts (default true)", false)
			.build(),
		handleTransferArtifacts
	);

	srv->register_tool(
		mcp::tool_builder("assemble_artifact")
			.with_description("Assemble a combined artifact from its constituents, or disassemble one")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_number_param("slot", "Slot of the (constituent or combined) artifact", true)
			.with_boolean_param("assemble", "true to assemble, false to disassemble", true)
			.with_number_param("assembleToId", "Combined artifact id to assemble into (required if assemble=true)", false)
			.build(),
		handleAssembleArtifact
	);

	srv->register_tool(
		mcp::tool_builder("buy_artifact")
			.with_description("Buy an artifact in a town (spellbook in mage guild, war machines in blacksmith, ...)")
			.with_number_param("heroId", "Hero instance ID (must be in the town)", true)
			.with_number_param("artifactId", "Artifact ID to buy", true)
			.build(),
		handleBuyArtifact
	);

	srv->register_tool(
		mcp::tool_builder("sort_backpack")
			.with_description("Scroll or sort a hero's artifact backpack")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_string_param("command", "One of: scrollLeft, scrollRight, sortBySlot, sortByCost, sortByClass", true)
			.build(),
		handleSortBackpack
	);

	srv->register_tool(
		mcp::tool_builder("manage_artifact_costume")
			.with_description("Save the hero's current equipped artifacts as a costume slot, or load one")
			.with_number_param("heroId", "Hero instance ID", true)
			.with_number_param("costumeIdx", "Costume slot index", true)
			.with_boolean_param("save", "true to save current equipment, false to load the costume", true)
			.build(),
		handleManageArtifactCostume
	);
}

#endif
