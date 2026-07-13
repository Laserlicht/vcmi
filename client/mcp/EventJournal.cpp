/*
 * EventJournal.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "EventJournal.h"

#include <algorithm>

namespace mcptool
{

JsonNode JournalEntry::toJson() const
{
	JsonNode entry;
	entry["seq"] = JsonNode(static_cast<si64>(seq));
	entry["type"] = JsonNode(type);
	entry["data"] = data;
	return entry;
}

EventJournal::EventJournal(size_t capacity)
	: capacity(capacity)
{
}

uint64_t EventJournal::push(const std::string & type, JsonNode data)
{
	std::lock_guard lock(mutex);

	uint64_t seq = nextSeq++;
	entries.push_back(JournalEntry{seq, type, std::move(data)});
	while(entries.size() > capacity)
		entries.pop_front();

	cv.notify_all();
	return seq;
}

uint64_t EventJournal::currentSeq() const
{
	std::lock_guard lock(mutex);
	return nextSeq - 1;
}

bool EventJournal::matches(const JournalEntry & entry, const std::vector<std::string> & typeFilter) const
{
	return typeFilter.empty() || std::find(typeFilter.begin(), typeFilter.end(), entry.type) != typeFilter.end();
}

std::vector<JournalEntry> EventJournal::since(uint64_t sinceSeq, const std::vector<std::string> & typeFilter, size_t limit) const
{
	std::lock_guard lock(mutex);

	std::vector<JournalEntry> result;
	for(const auto & entry : entries)
	{
		if(entry.seq <= sinceSeq)
			continue;
		if(!matches(entry, typeFilter))
			continue;
		result.push_back(entry);
		if(limit != 0 && result.size() >= limit)
			break;
	}
	return result;
}

std::vector<JournalEntry> EventJournal::waitFor(uint64_t sinceSeq, const std::vector<std::string> & typeFilter, std::chrono::milliseconds timeout)
{
	std::unique_lock lock(mutex);

	std::vector<JournalEntry> result;
	auto collect = [&]() -> bool
	{
		result.clear();
		for(const auto & entry : entries)
		{
			if(entry.seq > sinceSeq && matches(entry, typeFilter))
				result.push_back(entry);
		}
		return !result.empty();
	};

	cv.wait_for(lock, timeout, collect);
	return result;
}

}
