/*
 * ToolContext.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#ifdef ENABLE_MCP_SERVER
#include <mcp_message.h>
#endif

#include "../../lib/json/JsonNode.h"

VCMI_LIB_NAMESPACE_BEGIN
class CCallback;
class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;
VCMI_LIB_NAMESPACE_END

namespace mcptool
{

#ifdef ENABLE_MCP_SERVER

/// Wraps a plain string as MCP tool text content.
mcp::json textContent(const std::string & text);

/// Wraps a JsonNode (serialized compact) as MCP tool text content.
mcp::json textContent(const JsonNode & json);

/// Returns the CCallback of the currently active local player.
/// Throws std::runtime_error (surfaced by cpp-mcp as a tool error) if there is no active game.
CCallback & activeCallback();

/// Runs fn() under a shared lock on CGameState::mutex and returns its result as tool content.
/// Use for tools that read live game/player state (CGameState, CCallback).
mcp::json readTool(const std::function<JsonNode()> & fn);

/// Like readTool, but skips the CGameState::mutex lock. Use only for tools that read purely
/// static, session-immutable data (LIBRARY->creatures()/artifacts()/spells()/... - never
/// CGameState/CCallback) - avoids blocking behind gamestate churn (e.g. a long AI turn) for
/// data that doesn't need the lock in the first place.
mcp::json staticReadTool(const std::function<JsonNode()> & fn);

/// Dispatches fn to the main thread, waits for the server to acknowledge the request it is
/// expected to issue (bounded by mcp.requestTimeoutMs), and returns an envelope describing
/// what happened: {status: "ok"|"rejected"|"pending", error?: "...", events: [...], pendingQueries: [...]}.
///
/// fn is expected to throw std::runtime_error (via requireHero/requireTown/... or otherwise) to
/// signal validation failures - actionTool() catches this *inside* the main-thread dispatch and
/// turns it into status:"rejected" with an error message. This catch is not optional: fn runs on
/// the main thread via ENGINE->dispatchMainThread, invoked from InputHandler::handleUserEvent,
/// which has no exception handling of its own - an uncaught exception there calls
/// std::terminate() and takes down the whole client process. Never call
/// ENGINE->dispatchMainThread directly with code that can throw; go through actionTool (or
/// dispatchMainThreadSafe for fire-and-forget dispatches that have no result to report).
///
/// Only one actionTool() invocation may be in flight at a time (enforced internally) since
/// completion is detected via "next PackageApplied observed", not by request id - see
/// RequestTracker for the reasoning. fn must perform exactly one CCallback call.
///
/// awaitEvents: optional journal event types to additionally wait for (briefly) after a
/// successful acknowledgement, so the envelope includes the follow-up the caller will act on
/// next - e.g. battle tools await the next "battleUnitActive"/"battleResult" so the LLM learns
/// whose turn it is from the same response instead of needing a wait_for_event round trip.
mcp::json actionTool(const std::function<void()> & fn, const std::vector<std::string> & awaitEvents = {});

/// Runs fn on the main thread, catching and logging (never propagating) any exception - see the
/// actionTool() comment for why letting one escape would crash the whole client. Use this for
/// main-thread dispatches that have no RequestTracker/envelope to report failure through (e.g.
/// execute_command, lobby actions before a server connection exists).
void dispatchMainThreadSafe(const std::function<void()> & fn);

/// Object lookup helpers shared by action tools; throw std::runtime_error (-> tool error) with
/// an actionable message instead of the silent no-ops the raw CCallback::getObj + dynamic_cast
/// pattern encourages.
const CGHeroInstance & requireHero(CCallback & cb, int heroId);
const CGTownInstance & requireTown(CCallback & cb, int townId);
const CArmedInstance & requireArmedInstance(CCallback & cb, int objectId);

#endif

}
