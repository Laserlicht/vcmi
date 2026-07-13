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

#ifdef ENABLE_MCP_SERVER
#include <mcp_message.h>
#endif

#include "../../lib/json/JsonNode.h"

VCMI_LIB_NAMESPACE_BEGIN
class CCallback;
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
/// All read-only tools should go through this so the locking discipline lives in one place.
mcp::json readTool(const std::function<JsonNode()> & fn);

/// Dispatches fn to the main thread, waits for the server to acknowledge the request it is
/// expected to issue (bounded by mcp.requestTimeoutMs), and returns an envelope describing
/// what happened: {status: "ok"|"rejected"|"pending", events: [...], pendingQueries: [...]}.
///
/// Only one actionTool() invocation may be in flight at a time (enforced internally) since
/// completion is detected via "next PackageApplied observed", not by request id - see
/// RequestTracker for the reasoning. fn must perform exactly one CCallback call.
mcp::json actionTool(const std::function<void()> & fn);

#endif

}
