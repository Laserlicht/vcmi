/*
 * task_group.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// Tasks run immediately on the calling thread, so there is never anything to
// wait for.
#pragma once

#include <functional>
#include <utility>

namespace tbb
{

/// Mirrors tbb::task_group_status. In sequential mode every wait completes.
enum task_group_status
{
	not_complete,
	complete,
	canceled
};

/// Opaque handle produced by task_group::defer(). It owns the deferred work and
/// runs it when executed. task_arena::enqueue() consumes such a handle.
/// In oneTBB this defers scheduling; here it simply carries the functor so it
/// can be run synchronously at enqueue time.
class task_handle
{
public:
	task_handle() = default;

	template<typename Functor>
	explicit task_handle(Functor && f)
		: myWork(std::forward<Functor>(f))
	{
	}

	void operator()() const
	{
		if(myWork)
			myWork();
	}

	explicit operator bool() const { return static_cast<bool>(myWork); }

private:
	std::function<void()> myWork;
};

/// Sequential equivalent of tbb::task_group.
class task_group
{
public:
	task_group() = default;

	/// Runs the functor immediately (oneTBB would schedule it asynchronously).
	template<typename Functor>
	void run(Functor && f)
	{
		std::forward<Functor>(f)();
	}

	/// Runs the functor immediately and reports completion.
	template<typename Functor>
	task_group_status run_and_wait(Functor && f)
	{
		std::forward<Functor>(f)();
		return complete;
	}

	/// Wraps the functor in a task_handle for later execution by an arena.
	/// The work is NOT run here, matching oneTBB's deferral contract; it runs
	/// when the returned handle is executed (e.g. via task_arena::enqueue).
	template<typename Functor>
	task_handle defer(Functor && f)
	{
		return task_handle(std::forward<Functor>(f));
	}

	/// Nothing is ever outstanding in sequential mode.
	task_group_status wait()
	{
		return complete;
	}

	void cancel() {}
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
