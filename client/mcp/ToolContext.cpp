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
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"

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

mcp::json staticReadTool(const std::function<JsonNode()> & fn)
{
	return textContent(fn());
}

void dispatchMainThreadSafe(const std::function<void()> & fn)
{
	ENGINE->dispatchMainThread([fn]()
	{
		try
		{
			fn();
		}
		catch(const std::exception & e)
		{
			logGlobal->error("MCP: main-thread action failed: %s", e.what());
		}
		catch(...)
		{
			logGlobal->error("MCP: main-thread action failed with a non-standard exception");
		}
	});
}

mcp::json actionTool(const std::function<void()> & fn)
{
	auto & mcp = ENGINE->mcpServer();

	auto actionLock = mcp.requestTracker().acquireActionLock();
	mcp.requestTracker().beginWait();
	uint64_t markerSeq = mcp.journal().currentSeq();

	auto & tracker = mcp.requestTracker();
	ENGINE->dispatchMainThread([fn, &tracker]()
	{
		// See the actionTool() declaration comment: this catch is load-bearing, not defensive
		// polish. fn runs via InputHandler::handleUserEvent, which has no exception handling of
		// its own - letting fn's validation throws (requireHero, etc.) escape here would call
		// std::terminate() and kill the whole client process instead of reporting a tool error.
		try
		{
			fn();
		}
		catch(const std::exception & e)
		{
			tracker.reportLocalError(e.what());
		}
		catch(...)
		{
			tracker.reportLocalError("Unknown error");
		}
	});

	int timeoutMs = settings["mcp"]["requestTimeoutMs"].Integer();
	if(timeoutMs <= 0)
		timeoutMs = 10000;
	auto outcome = mcp.requestTracker().waitResult(std::chrono::milliseconds(timeoutMs));

	JsonNode envelope;
	if(!outcome.has_value())
		envelope["status"] = JsonNode(std::string("pending"));
	else if(outcome->applied)
		envelope["status"] = JsonNode(std::string("ok"));
	else
		envelope["status"] = JsonNode(std::string("rejected"));

	if(outcome.has_value() && !outcome->errorMessage.empty())
		envelope["error"] = JsonNode(outcome->errorMessage);

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

const CGHeroInstance & requireHero(CCallback & cb, int heroId)
{
	auto hero = dynamic_cast<const CGHeroInstance *>(cb.getObj(ObjectInstanceID(heroId), false));
	if(!hero)
		throw std::runtime_error("No hero with id " + std::to_string(heroId));
	return *hero;
}

const CGTownInstance & requireTown(CCallback & cb, int townId)
{
	auto town = dynamic_cast<const CGTownInstance *>(cb.getObj(ObjectInstanceID(townId), false));
	if(!town)
		throw std::runtime_error("No town with id " + std::to_string(townId));
	return *town;
}

const CArmedInstance & requireArmedInstance(CCallback & cb, int objectId)
{
	auto army = dynamic_cast<const CArmedInstance *>(cb.getObj(ObjectInstanceID(objectId), false));
	if(!army)
		throw std::runtime_error("No garrison with id " + std::to_string(objectId));
	return *army;
}

}

#endif
