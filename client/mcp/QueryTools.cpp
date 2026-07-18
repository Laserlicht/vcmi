/*
 * QueryTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "QueryTools.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/CConfigHandler.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGObjectInstance.h"

#include "../GameEngine.h"
#include "../McpServer.h"

#include "EventJournal.h"
#include "RequestTracker.h"
#include "QueryRegistry.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	std::vector<std::string> readTypeFilter(const mcp::json & params)
	{
		std::vector<std::string> types;
		if(params.contains("types") && params["types"].is_array())
			for(auto & t : params["types"])
				types.push_back(t.get<std::string>());
		return types;
	}

	JsonNode entriesToJson(const std::vector<mcptool::JournalEntry> & entries, uint64_t latestSeq)
	{
		JsonNode result;
		JsonNode eventsJson;
		for(auto & entry : entries)
			eventsJson.Vector().push_back(entry.toJson());
		result["events"] = eventsJson;
		result["latestSeq"] = JsonNode(static_cast<si64>(latestSeq));
		return result;
	}

	mcp::json handleGetEvents(const mcp::json & params, const std::string &)
	{
		uint64_t sinceSeq = params.contains("sinceSeq") ? params["sinceSeq"].get<uint64_t>() : 0;
		size_t limit = params.contains("limit") ? params["limit"].get<size_t>() : 0;
		auto types = readTypeFilter(params);

		auto & journal = ENGINE->mcpServer().journal();
		auto entries = journal.since(sinceSeq, types, limit);
		return mcptool::textContent(entriesToJson(entries, journal.currentSeq()));
	}

	mcp::json handleWaitForEvent(const mcp::json & params, const std::string &)
	{
		auto & journal = ENGINE->mcpServer().journal();
		uint64_t sinceSeq = params.contains("sinceSeq") ? params["sinceSeq"].get<uint64_t>() : journal.currentSeq();
		auto types = readTypeFilter(params);

		int defaultTimeoutMs = static_cast<int>(settings["mcp"]["eventWaitTimeoutMs"].Integer());
		if(defaultTimeoutMs <= 0)
			defaultTimeoutMs = 60000;
		int timeoutMs = params.contains("timeoutMs") ? params["timeoutMs"].get<int>() : defaultTimeoutMs;

		auto entries = journal.waitFor(sinceSeq, types, std::chrono::milliseconds(timeoutMs));
		return mcptool::textContent(entriesToJson(entries, journal.currentSeq()));
	}

	mcp::json handleGetPendingQueries(const mcp::json &, const std::string &)
	{
		JsonNode result;
		JsonNode arr;
		for(auto & query : ENGINE->mcpServer().queryRegistry().list())
			arr.Vector().push_back(query);
		result["queries"] = arr;
		return mcptool::textContent(result);
	}

	mcp::json handleSetLlmControl(const mcp::json & params, const std::string &)
	{
		bool enabled = params["enabled"].get<bool>();
		// The flag is atomic and only read on the GUI thread when deciding whether to open a
		// dialog window, so it is safe to set directly from this MCP worker thread.
		ENGINE->mcpServer().setLlmDialogControl(enabled);
		JsonNode result;
		result["llmDialogControl"] = JsonNode(enabled);
		return mcptool::textContent(result);
	}

	mcp::json handleAnswerQuery(const mcp::json & params, const std::string &)
	{
		int32_t queryId = params["queryId"].get<int32_t>();
		std::optional<int32_t> reply = (params.contains("reply") && !params["reply"].is_null())
			? std::optional<int32_t>(params["reply"].get<int32_t>())
			: std::nullopt;

		return mcptool::actionTool([queryId, reply]()
		{
			// Registry cleanup + the queryAnswered journal event are handled centrally by
			// McpServer::onRequestSent (hooked into CClient::sendRequest), so this works
			// identically whether the QueryReply originates here or from a GUI window.
			mcptool::activeCallback().sendQueryReply(reply, QueryID(queryId));
		});
	}

	mcp::json handleGetStatistics(const mcp::json &, const std::string &)
	{
		// The stats payload itself rides in the "statisticsReady" journal event within the
		// action envelope (see JournalVisitor::visitResponseStatistic) - no separate fetch needed.
		return mcptool::actionTool([]()
		{
			mcptool::activeCallback().requestStatistic();
		});
	}

	mcp::json handleSaveGame(const mcp::json & params, const std::string &)
	{
		auto filename = params["filename"].get<std::string>();
		bool notifySuccess = params.contains("notifySuccess") ? params["notifySuccess"].get<bool>() : true;

		return mcptool::actionTool([filename, notifySuccess]()
		{
			mcptool::activeCallback().save(filename, notifySuccess);
		});
	}

	mcp::json handleSendChatMessage(const mcp::json & params, const std::string &)
	{
		auto text = params["text"].get<std::string>();
		auto objectId = params.contains("objectId") ? params["objectId"].get<int>() : ObjectInstanceID::NONE.getNum();

		return mcptool::actionTool([text, objectId]()
		{
			auto & cb = mcptool::activeCallback();
			const CGObjectInstance * obj = (objectId != ObjectInstanceID::NONE.getNum())
				? cb.getObj(ObjectInstanceID(objectId), false)
				: nullptr;
			cb.sendMessage(text, obj);
		});
	}

	mcp::json handleSetGamePause(const mcp::json & params, const std::string &)
	{
		bool paused = params["paused"].get<bool>();

		return mcptool::actionTool([paused]()
		{
			mcptool::activeCallback().gamePause(paused);
		});
	}
}

void registerQueryTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("get_events")
			.with_description("Get journal entries (things that happened server-side) since a given sequence number")
			.with_number_param("sinceSeq", "Only return entries with seq greater than this (default: 0, i.e. everything retained)", false)
			.with_array_param("types", "Restrict to these event types (default: all types)", "string", false)
			.with_number_param("limit", "Maximum number of entries to return (default: unlimited)", false)
			.build(),
		handleGetEvents
	);

	srv->register_tool(
		mcp::tool_builder("wait_for_event")
			.with_description("Block until a journal entry matching the filter arrives, or until timeout. Use this to idle while waiting for AI/other players instead of polling get_events.")
			.with_number_param("sinceSeq", "Only consider entries with seq greater than this (default: current latest, i.e. only future events)", false)
			.with_array_param("types", "Restrict to these event types (default: all types)", "string", false)
			.with_number_param("timeoutMs", "Maximum time to block, in milliseconds (default: mcp.eventWaitTimeoutMs)", false)
			.build(),
		handleWaitForEvent
	);

	srv->register_tool(
		mcp::tool_builder("get_pending_queries")
			.with_description("List server-side dialogs that are currently blocking the game until answered via answer_query")
			.build(),
		handleGetPendingQueries
	);

	srv->register_tool(
		mcp::tool_builder("set_llm_control")
			.with_description("Enable/disable LLM dialog control. When enabled, the game client stops opening GUI windows for server dialogs (level-up, yes/no, garrison, market, tavern, teleport, ...) - they flow only through get_pending_queries/answer_query, so an LLM and a human can't both try to answer the same dialog. Enable this once at the start of an LLM-driven session.")
			.with_boolean_param("enabled", "true to let the LLM own dialogs (suppress GUI windows), false to restore normal GUI dialogs", true)
			.build(),
		handleSetLlmControl
	);

	srv->register_tool(
		mcp::tool_builder("answer_query")
			.with_description("Answer a pending dialog (from get_pending_queries). reply semantics depend on the query kind: 0/omitted usually means cancel, 1..n selects an option, indices for teleport exits / map object selection / skill choice.")
			.with_number_param("queryId", "Query id, from get_pending_queries", true)
			.with_number_param("reply", "Selected option (omit for cancel, if the dialog allows it)", false)
			.build(),
		handleAnswerQuery
	);

	srv->register_tool(
		mcp::tool_builder("get_statistics")
			.with_description("Request the game statistics dataset (per-player history: resources, army strength, exploration, battles, ...). The data is returned inside the response's events array, as a 'statisticsReady' entry.")
			.build(),
		handleGetStatistics
	);

	srv->register_tool(
		mcp::tool_builder("save_game")
			.with_description("Save the current game")
			.with_string_param("filename", "Save file name", true)
			.with_boolean_param("notifySuccess", "Show a confirmation to the player on success (default true)", false)
			.build(),
		handleSaveGame
	);

	srv->register_tool(
		mcp::tool_builder("send_chat_message")
			.with_description("Send a chat message. Also accepts VCMI's console cheat codes (e.g. 'vcmiistari'), same as a human typing them in chat.")
			.with_string_param("text", "Message text (or cheat code)", true)
			.with_number_param("objectId", "Currently selected/relevant object, for object-scoped cheats (optional)", false)
			.build(),
		handleSendChatMessage
	);

	srv->register_tool(
		mcp::tool_builder("set_game_pause")
			.with_description("Pause or unpause the game (single-player only)")
			.with_boolean_param("paused", "true to pause, false to resume", true)
			.build(),
		handleSetGamePause
	);
}

#endif
