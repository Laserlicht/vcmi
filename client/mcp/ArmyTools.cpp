/*
 * ArmyTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "ArmyTools.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	mcp::json handleSwapStacks(const mcp::json & params, const std::string &)
	{
		auto objectId1 = params["objectId1"].get<int>();
		auto slot1 = params["slot1"].get<int>();
		auto objectId2 = params["objectId2"].get<int>();
		auto slot2 = params["slot2"].get<int>();

		return mcptool::actionTool([objectId1, slot1, objectId2, slot2]()
		{
			auto & cb = mcptool::activeCallback();
			auto & s1 = mcptool::requireArmedInstance(cb, objectId1);
			auto & s2 = mcptool::requireArmedInstance(cb, objectId2);
			cb.swapCreatures(&s1, &s2, SlotID(slot1), SlotID(slot2));
		});
	}

	mcp::json handleMergeStacks(const mcp::json & params, const std::string &)
	{
		auto objectId1 = params["objectId1"].get<int>();
		auto slot1 = params["slot1"].get<int>();
		auto objectId2 = params["objectId2"].get<int>();
		auto slot2 = params["slot2"].get<int>();

		return mcptool::actionTool([objectId1, slot1, objectId2, slot2]()
		{
			auto & cb = mcptool::activeCallback();
			auto & s1 = mcptool::requireArmedInstance(cb, objectId1);
			auto & s2 = mcptool::requireArmedInstance(cb, objectId2);
			cb.mergeStacks(&s1, &s2, SlotID(slot1), SlotID(slot2));
		});
	}

	mcp::json handleSplitStack(const mcp::json & params, const std::string &)
	{
		auto objectId1 = params["objectId1"].get<int>();
		auto slot1 = params["slot1"].get<int>();
		auto objectId2 = params["objectId2"].get<int>();
		auto slot2 = params["slot2"].get<int>();
		auto count = params["count"].get<int>();

		return mcptool::actionTool([objectId1, slot1, objectId2, slot2, count]()
		{
			auto & cb = mcptool::activeCallback();
			auto & s1 = mcptool::requireArmedInstance(cb, objectId1);
			auto & s2 = mcptool::requireArmedInstance(cb, objectId2);
			cb.splitStack(&s1, &s2, SlotID(slot1), SlotID(slot2), count);
		});
	}

	mcp::json handleMoveArmy(const mcp::json & params, const std::string &)
	{
		auto srcObjectId = params["srcObjectId"].get<int>();
		auto destObjectId = params["destObjectId"].get<int>();
		auto srcSlot = params["srcSlot"].get<int>();

		return mcptool::actionTool([srcObjectId, destObjectId, srcSlot]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireArmedInstance(cb, srcObjectId);
			mcptool::requireArmedInstance(cb, destObjectId);
			cb.bulkMoveArmy(ObjectInstanceID(srcObjectId), ObjectInstanceID(destObjectId), SlotID(srcSlot));
		});
	}

	mcp::json handleSplitStackEvenly(const mcp::json & params, const std::string &)
	{
		auto objectId = params["objectId"].get<int>();
		auto slot = params["slot"].get<int>();
		auto count = params["count"].get<int>();

		return mcptool::actionTool([objectId, slot, count]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireArmedInstance(cb, objectId);
			cb.bulkSplitStack(ObjectInstanceID(objectId), SlotID(slot), count);
		});
	}

	mcp::json handleMergeAllStacks(const mcp::json & params, const std::string &)
	{
		auto objectId = params["objectId"].get<int>();
		auto slot = params["slot"].get<int>();

		return mcptool::actionTool([objectId, slot]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireArmedInstance(cb, objectId);
			cb.bulkMergeStacks(ObjectInstanceID(objectId), SlotID(slot));
		});
	}

	mcp::json handleRebalanceStacks(const mcp::json & params, const std::string &)
	{
		auto objectId = params["objectId"].get<int>();
		auto slot = params["slot"].get<int>();

		return mcptool::actionTool([objectId, slot]()
		{
			auto & cb = mcptool::activeCallback();
			mcptool::requireArmedInstance(cb, objectId);
			cb.bulkSplitAndRebalanceStack(ObjectInstanceID(objectId), SlotID(slot));
		});
	}

	mcp::json handleDismissCreatures(const mcp::json & params, const std::string &)
	{
		auto objectId = params["objectId"].get<int>();
		auto slot = params["slot"].get<int>();

		return mcptool::actionTool([objectId, slot]()
		{
			auto & cb = mcptool::activeCallback();
			auto & army = mcptool::requireArmedInstance(cb, objectId);
			if(!cb.dismissCreature(&army, SlotID(slot)))
				throw std::runtime_error("Stack cannot be dismissed (not owned, or last stack required)");
		});
	}

	mcp::json handleUpgradeCreatures(const mcp::json & params, const std::string &)
	{
		auto objectId = params["objectId"].get<int>();
		auto slot = params["slot"].get<int>();
		auto creatureId = params.contains("creatureId") ? params["creatureId"].get<int>() : CreatureID().getNum();

		return mcptool::actionTool([objectId, slot, creatureId]()
		{
			auto & cb = mcptool::activeCallback();
			auto & army = mcptool::requireArmedInstance(cb, objectId);
			cb.upgradeCreature(&army, SlotID(slot), CreatureID(creatureId));
		});
	}
}

void registerArmyTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("swap_stacks")
			.with_description("Swap two creature stacks between (or within) two garrisons")
			.with_number_param("objectId1", "First garrison (hero/town) instance ID", true)
			.with_number_param("slot1", "First stack slot index", true)
			.with_number_param("objectId2", "Second garrison instance ID", true)
			.with_number_param("slot2", "Second stack slot index", true)
			.build(),
		handleSwapStacks
	);

	srv->register_tool(
		mcp::tool_builder("merge_stacks")
			.with_description("Merge a creature stack into another stack of the same creature type")
			.with_number_param("objectId1", "Source garrison instance ID", true)
			.with_number_param("slot1", "Source stack slot index", true)
			.with_number_param("objectId2", "Destination garrison instance ID", true)
			.with_number_param("slot2", "Destination stack slot index", true)
			.build(),
		handleMergeStacks
	);

	srv->register_tool(
		mcp::tool_builder("split_stack")
			.with_description("Split a number of creatures from one stack into a specific (possibly empty) slot")
			.with_number_param("objectId1", "Source garrison instance ID", true)
			.with_number_param("slot1", "Source stack slot index", true)
			.with_number_param("objectId2", "Destination garrison instance ID", true)
			.with_number_param("slot2", "Destination stack slot index", true)
			.with_number_param("count", "Number of creatures to move to the destination slot", true)
			.build(),
		handleSplitStack
	);

	srv->register_tool(
		mcp::tool_builder("move_army")
			.with_description("Move the entire army from one garrison to another (srcSlot only needs to name any occupied slot to prove the source army is non-empty)")
			.with_number_param("srcObjectId", "Source garrison instance ID", true)
			.with_number_param("destObjectId", "Destination garrison instance ID", true)
			.with_number_param("srcSlot", "Any occupied slot index in the source army", true)
			.build(),
		handleMoveArmy
	);

	srv->register_tool(
		mcp::tool_builder("split_stack_evenly")
			.with_description("Split a stack into the given number of roughly-equal stacks across free slots")
			.with_number_param("objectId", "Garrison instance ID", true)
			.with_number_param("slot", "Stack slot index to split", true)
			.with_number_param("count", "Number of resulting stacks", true)
			.build(),
		handleSplitStackEvenly
	);

	srv->register_tool(
		mcp::tool_builder("merge_all_stacks")
			.with_description("Merge all stacks of the same creature type as the given slot into one stack")
			.with_number_param("objectId", "Garrison instance ID", true)
			.with_number_param("slot", "Stack slot index", true)
			.build(),
		handleMergeAllStacks
	);

	srv->register_tool(
		mcp::tool_builder("rebalance_stacks")
			.with_description("Spread a stack's creatures evenly across all free slots of the same garrison")
			.with_number_param("objectId", "Garrison instance ID", true)
			.with_number_param("slot", "Stack slot index", true)
			.build(),
		handleRebalanceStacks
	);

	srv->register_tool(
		mcp::tool_builder("dismiss_creatures")
			.with_description("Dismiss (disband) a creature stack")
			.with_number_param("objectId", "Garrison instance ID", true)
			.with_number_param("slot", "Stack slot index", true)
			.build(),
		handleDismissCreatures
	);

	srv->register_tool(
		mcp::tool_builder("upgrade_creatures")
			.with_description("Upgrade a creature stack (requires the matching upgrade building in a town)")
			.with_number_param("objectId", "Garrison instance ID", true)
			.with_number_param("slot", "Stack slot index", true)
			.with_number_param("creatureId", "Target creature type (omit for the default upgrade)", false)
			.build(),
		handleUpgradeCreatures
	);
}

#endif
