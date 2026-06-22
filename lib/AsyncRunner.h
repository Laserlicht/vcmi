/*
 * AsyncRunner.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#include <tbb/global_control.h>

#ifdef VCMI_SWITCH
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#endif

/// Helper class for running asynchronous tasks using TBB thread pool
class AsyncRunner : boost::noncopyable
{
	tbb::task_arena arena;
	tbb::task_group taskGroup;
	tbb::global_control control;

#ifdef VCMI_SWITCH
	// The Switch TBB shim is sequential; an AI turn blocks on the network thread, so
	// running it inline would deadlock. AI runners opt into a real (joinable) thread.
	bool useRealThreads;
	std::mutex switchMutex;
	std::vector<std::thread> switchThreads;
#endif

	static int selectArenaSize()
	{
		// WARNING:
		// Due to blocking waits in AI logic, we require some oversubscription on system with small number of cores
		// othervice, it is possible for AI threads to stuck in blocking wait, while task that will unblock AI never assigned to worker thread
		// TBB provides resumable tasks support, however this support is not available on all systems (most notably - Android)
		// To work around this problem, request TBB to create few extra threads when running on CPU with low max concurrency
		// Issue confirmed to happen (but likely not limited to) on iPhone 12 Pro (2+4 cores) and on virtual systems with 2 cores
		int cores = tbb::this_task_arena::max_concurrency();
		int requiredWorkersCount = 4;
		return std::max(cores, requiredWorkersCount);
	}

public:
	explicit AsyncRunner(bool useRealThreads = false)
		:arena(selectArenaSize())
		,control(tbb::global_control::max_allowed_parallelism, selectArenaSize())
#ifdef VCMI_SWITCH
		,useRealThreads(useRealThreads)
#endif
	{
#ifndef VCMI_SWITCH
		(void)useRealThreads;
#endif
	}

	/// Runs the provided functor asynchronously on a thread from the TBB worker pool.
	template<typename Functor>
	void run(Functor && f)
	{
#ifdef VCMI_SWITCH
		if(useRealThreads)
		{
			std::function<void()> work(std::forward<Functor>(f));
			std::lock_guard<std::mutex> lock(switchMutex);
			switchThreads.emplace_back([work = std::move(work)]() { try { work(); } catch(...) {} });
			return;
		}
#endif
		arena.enqueue(taskGroup.defer(std::forward<Functor>(f)));
	}

	/// Waits for all previously enqueued task.
	/// Re-entrable - waiting for tasks does not prevent submitting new tasks
	void wait()
	{
#ifdef VCMI_SWITCH
		if(useRealThreads)
		{
			std::vector<std::thread> local;
			{
				std::lock_guard<std::mutex> lock(switchMutex);
				local.swap(switchThreads);
			}
			for(auto & worker : local)
			{
				try { if(worker.joinable()) worker.join(); } catch(...) {}
			}
			return;
		}
#endif
		taskGroup.wait();
	}

	~AsyncRunner()
	{
		wait();
	}
};
