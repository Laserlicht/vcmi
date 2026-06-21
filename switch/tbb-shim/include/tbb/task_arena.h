/*
 * task_arena.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port. There is exactly one execution
 * context (the calling thread), so concurrency is always 1 and the current
 * thread index is always 0. These two values MUST stay consistent: VCMI sizes
 * per-thread buffers by max_concurrency() and indexes them by
 * current_thread_index() (see AI/Nullkiller2/Pathfinding/AINodeStorage.cpp).
 */
#pragma once

#include "task_group.h"

#include <utility>

namespace tbb
{

/// Sequential equivalent of tbb::task_arena.
class task_arena
{
public:
	/// Sentinel for "let the library decide" concurrency, as in oneTBB.
	static constexpr int automatic = -1;

	/// Tag type accepted by the "attach to current arena" constructor.
	struct attach {};

	task_arena() = default;

	/// oneTBB: construct an arena with a requested concurrency. The value is
	/// recorded for API completeness but has no effect: work runs inline.
	explicit task_arena(int max_concurrency, unsigned reserved_for_masters = 1)
		: myMaxConcurrency(max_concurrency), myReservedForMasters(reserved_for_masters)
	{
	}

	task_arena(const task_arena &) = default;

	explicit task_arena(attach) {}

	void initialize() {}
	void initialize(int max_concurrency, unsigned reserved_for_masters = 1)
	{
		myMaxConcurrency = max_concurrency;
		myReservedForMasters = reserved_for_masters;
	}
	void initialize(attach) {}
	void terminate() {}
	bool is_active() const { return true; }

	/// Reports the effective concurrency. Always 1 in sequential mode.
	int max_concurrency() const { return 1; }

	/// oneTBB enqueues the functor for asynchronous execution. Here it runs
	/// immediately on the calling thread.
	template<typename Functor>
	void enqueue(Functor && f)
	{
		std::forward<Functor>(f)();
	}

	/// oneTBB runs the functor within the arena and returns its result. Here it
	/// is just called directly, perfect-forwarding the return value/type.
	template<typename Functor>
	decltype(auto) execute(Functor && f)
	{
		return std::forward<Functor>(f)();
	}

private:
	int myMaxConcurrency = automatic;
	unsigned myReservedForMasters = 1;
};

namespace this_task_arena
{
	/// Always 1: there is a single execution context.
	inline int max_concurrency() { return 1; }

	/// Always 0: the calling thread is the only worker. Consistent with
	/// max_concurrency() == 1.
	inline int current_thread_index() { return 0; }

	/// oneTBB runs the functor in the current arena; here, directly.
	template<typename Functor>
	decltype(auto) isolate(Functor && f)
	{
		return std::forward<Functor>(f)();
	}

	template<typename Functor>
	decltype(auto) enqueue(Functor && f)
	{
		return std::forward<Functor>(f)();
	}
} // namespace this_task_arena

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
