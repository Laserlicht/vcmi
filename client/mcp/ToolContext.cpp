/*
 * ToolContext.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/CConfigHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/callback/CCallback.h"

#include "../GameInstance.h"
#include "../GameEngine.h"
#include "../CPlayerInterface.h"
#include "../McpServer.h"

#include "EventJournal.h"
#include "RequestTracker.h"
#include "QueryRegistry.h"

#ifdef ENABLE_MCP_SERVER

namespace mcptool
{

mcp::json textContent(const std::string & text)
{
	mcp::json arr = mcp::json::array();
	arr.push_back({{"type", "text"}, {"text", text}});
	return arr;
}

mcp::json textContent(const JsonNode & json)
{
	return textContent(json.toCompactString());
}

CCallback & activeCallback()
{
	auto pi = GAME->interface();
	if(!pi || !pi->cb)
		throw std::runtime_error("No active game");
	return *pi->cb;
}

mcp::json readTool(const std::function<JsonNode()> & fn)
{
	std::shared_lock lock(CGameState::mutex);
	return textContent(fn());
}

mcp::json actionTool(const std::function<void()> & fn)
{
	auto & mcp = ENGINE->mcpServer();

	auto actionLock = mcp.requestTracker().acquireActionLock();
	mcp.requestTracker().beginWait();
	uint64_t markerSeq = mcp.journal().currentSeq();

	ENGINE->dispatchMainThread(fn);

	int timeoutMs = settings["mcp"]["requestTimeoutMs"].Integer();
	if(timeoutMs <= 0)
		timeoutMs = 10000;
	auto applied = mcp.requestTracker().waitResult(std::chrono::milliseconds(timeoutMs));

	JsonNode envelope;
	if(!applied.has_value())
		envelope["status"] = JsonNode(std::string("pending"));
	else if(*applied)
		envelope["status"] = JsonNode(std::string("ok"));
	else
		envelope["status"] = JsonNode(std::string("rejected"));

	JsonNode eventsJson;
	for(auto & entry : mcp.journal().since(markerSeq))
		eventsJson.Vector().push_back(entry.toJson());
	envelope["events"] = eventsJson;

	JsonNode queriesJson;
	for(auto & query : mcp.queryRegistry().list())
		queriesJson.Vector().push_back(query);
	envelope["pendingQueries"] = queriesJson;

	return textContent(envelope);
}

}

#endif
