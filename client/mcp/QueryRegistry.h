/*
 * QueryRegistry.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

#include "../../lib/json/JsonNode.h"

namespace mcptool
{

/// Tracks server-side dialogs (Query-derived packs) that are blocking the game until answered
/// with QueryReply. Populated by JournalVisitor as such packs arrive, cleared by the tool that
/// answers a query (see QueryTools).
///
/// Note: if a human answers the dialog directly through the GUI instead of via MCP, the entry
/// is not removed here (VCMI does not echo QueryReply back to the client that could observe it
/// generically). Mixing MCP-driven play with manual dialog interaction is not supported yet.
class QueryRegistry
{
public:
	void add(int32_t queryId, JsonNode description);
	void remove(int32_t queryId);
	bool contains(int32_t queryId) const;
	std::vector<JsonNode> list() const;

private:
	mutable std::mutex mutex;
	std::map<int32_t, JsonNode> pending;
};

}
