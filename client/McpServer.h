/*
 * McpServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "StdInc.h"

namespace mcp { class server; }

VCMI_LIB_NAMESPACE_BEGIN
class CGameState;
struct CPackForClient;
VCMI_LIB_NAMESPACE_END

namespace mcptool
{
class EventJournal;
class RequestTracker;
class QueryRegistry;
}

class JournalVisitor;

class McpServer : boost::noncopyable
{
	std::unique_ptr<mcp::server> server;
	bool enabled;

	std::unique_ptr<mcptool::EventJournal> journalInstance;
	std::unique_ptr<mcptool::RequestTracker> requestTrackerInstance;
	std::unique_ptr<mcptool::QueryRegistry> queryRegistryInstance;
	std::unique_ptr<JournalVisitor> visitorInstance;

public:
	McpServer();
	~McpServer();

	bool isEnabled() const { return enabled; }

	mcptool::EventJournal & journal() { return *journalInstance; }
	mcptool::RequestTracker & requestTracker() { return *requestTrackerInstance; }
	mcptool::QueryRegistry & queryRegistry() { return *queryRegistryInstance; }

	/// Called by CClient::handlePack for every applied CPackForClient. No-op when disabled.
	void onPackApplied(CPackForClient & pack);
};
