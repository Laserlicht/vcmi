/*
 * EventJournal.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "../../lib/json/JsonNode.h"

namespace mcptool
{

struct JournalEntry
{
	uint64_t seq;
	std::string type;
	JsonNode data;

	JsonNode toJson() const;
};

/// Ring buffer of everything that happened server-side since MCP started, so an LLM client
/// can catch up on the consequences of its own actions or on AI/other-player activity between
/// polls, without re-reading the whole game state.
class EventJournal
{
public:
	explicit EventJournal(size_t capacity);

	/// Appends an event and wakes up any wait() callers. Thread-safe.
	uint64_t push(const std::string & type, JsonNode data);

	uint64_t currentSeq() const;

	/// Returns entries with seq > sinceSeq, optionally restricted to the given types, oldest first.
	/// limit == 0 means unlimited.
	std::vector<JournalEntry> since(uint64_t sinceSeq, const std::vector<std::string> & typeFilter = {}, size_t limit = 0) const;

	/// Blocks the calling thread until an entry matching typeFilter appears after sinceSeq, or
	/// until timeout elapses. Returns the matching entries (possibly empty on timeout).
	std::vector<JournalEntry> waitFor(uint64_t sinceSeq, const std::vector<std::string> & typeFilter, std::chrono::milliseconds timeout);

private:
	bool matches(const JournalEntry & entry, const std::vector<std::string> & typeFilter) const;

	mutable std::mutex mutex;
	std::condition_variable cv;
	std::deque<JournalEntry> entries;
	size_t capacity;
	uint64_t nextSeq = 1;
};

}
