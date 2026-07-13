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

	/// Blocks until reportApplied() runs or timeout elapses. nullopt means timeout.
	std::optional<bool> waitResult(std::chrono::milliseconds timeout);

private:
	std::mutex actionMutex;

	std::mutex stateMutex;
	std::condition_variable cv;
	bool waiting = false;
	std::optional<bool> result;
};

}
