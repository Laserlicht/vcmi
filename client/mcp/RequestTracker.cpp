/*
 * RequestTracker.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "RequestTracker.h"

namespace mcptool
{

void RequestTracker::beginWait()
{
	std::lock_guard lock(stateMutex);
	waiting = true;
	result.reset();
}

void RequestTracker::reportApplied(bool applied)
{
	std::lock_guard lock(stateMutex);
	if(waiting)
	{
		result = applied;
		waiting = false;
		cv.notify_all();
	}
}

std::optional<bool> RequestTracker::waitResult(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(stateMutex);
	cv.wait_for(lock, timeout, [this]() { return result.has_value(); });
	return result;
}

}
