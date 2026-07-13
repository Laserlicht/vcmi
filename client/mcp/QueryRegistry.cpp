/*
 * QueryRegistry.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "QueryRegistry.h"

namespace mcptool
{

void QueryRegistry::add(int32_t queryId, JsonNode description)
{
	std::lock_guard lock(mutex);
	pending[queryId] = std::move(description);
}

void QueryRegistry::remove(int32_t queryId)
{
	std::lock_guard lock(mutex);
	pending.erase(queryId);
}

bool QueryRegistry::contains(int32_t queryId) const
{
	std::lock_guard lock(mutex);
	return pending.contains(queryId);
}

std::vector<JsonNode> QueryRegistry::list() const
{
	std::lock_guard lock(mutex);
	std::vector<JsonNode> result;
	result.reserve(pending.size());
	for(auto & [id, description] : pending)
		result.push_back(description);
	return result;
}

}
