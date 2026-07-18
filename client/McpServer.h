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
struct CPackForServer;
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

	/// When true, the GUI must not create windows for server dialogs (queries): the LLM handles
	/// them purely through the MCP QueryRegistry + answer_query. Toggled by the set_llm_control
	/// tool. Prevents the human GUI and the LLM both trying to answer the same query - see the
	/// dialog handling section of the LLM handbook.
	std::atomic<bool> dialogsHandledByLlm{false};

public:
	McpServer();
	~McpServer();

	bool isEnabled() const { return enabled; }

	mcptool::EventJournal & journal() { return *journalInstance; }
	mcptool::RequestTracker & requestTracker() { return *requestTrackerInstance; }
	mcptool::QueryRegistry & queryRegistry() { return *queryRegistryInstance; }

	bool llmDialogControl() const { return dialogsHandledByLlm.load(); }
	void setLlmDialogControl(bool on) { dialogsHandledByLlm.store(on); }

	/// Called by CClient::handlePack for every applied CPackForClient. No-op when disabled.
	void onPackApplied(CPackForClient & pack);

	/// Called by CClient::sendRequest for every outgoing request. Detects QueryReply (from the
	/// LLM or from a GUI window) and clears the matching registry entry + journals queryAnswered,
	/// keeping the pending-query list truthful regardless of who answered. No-op when disabled.
	void onRequestSent(const CPackForServer & pack);
};
