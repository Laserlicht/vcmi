/*
 * RequestTracker.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace mcptool
{

/// Bridges an MCP action tool (running on an MCP worker thread) with the server's
/// acknowledgement of the CPackForServer it caused (a PackageApplied pack, observed on the
/// network thread via JournalVisitor).
///
/// VCMI never hands back the requestID of a request issued through CCallback, so instead of
/// correlating by ID this tracker relies on a simpler invariant: only one MCP-issued action is
/// ever in flight at a time (enforced by acquireActionLock()), so the next PackageApplied to
/// arrive after beginWait() is necessarily the answer to that action.
class RequestTracker
{
public:
	struct Outcome
	{
		bool applied = false;
		/// Set when the action never reached the server: a validation failure (e.g. unknown
		/// object id, wrong owner) caught on the main thread before/instead of issuing a request.
		std::string errorMessage;
	};

	class ActionLock
	{
	public:
		explicit ActionLock(std::mutex & m) : lock(m) {}
	private:
		std::unique_lock<std::mutex> lock;
	};

	/// Serializes MCP write actions; hold this for the entire dispatch+wait cycle.
	ActionLock acquireActionLock() { return ActionLock(actionMutex); }

	/// Arms the tracker; call while holding the ActionLock, before dispatching the action.
	void beginWait();

	/// Called from JournalVisitor::visitPackageApplied for every applied request.
	void reportApplied(bool result);

	/// Called when the dispatched action threw instead of issuing a request - see actionTool()
	/// for why this must never be allowed to happen via an uncaught C++ exception instead.
	void reportLocalError(const std::string & message);

	/// Blocks until reportApplied()/reportLocalError() runs or timeout elapses. nullopt means timeout.
	std::optional<Outcome> waitResult(std::chrono::milliseconds timeout);

private:
	std::mutex actionMutex;

	std::mutex stateMutex;
	std::condition_variable cv;
	bool waiting = false;
	std::optional<Outcome> result;
};

}
