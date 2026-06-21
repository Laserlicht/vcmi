/*
 * parallel_for.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port. Everything runs on the calling
 * thread, in order, but the public API matches oneTBB.
 */
#pragma once

#include "blocked_range.h"
// Real oneTBB makes tbb::this_task_arena reachable through its parallel headers;
// some VCMI call sites (e.g. AI/Nullkiller2/Pathfinding/AINodeStorage.cpp) rely on
// that, using tbb::this_task_arena without including <tbb/task_arena.h> directly.
#include "task_arena.h"

#include <type_traits>
#include <utility>

namespace tbb
{

// Sentinel partitioner types so that call sites passing a partitioner still
// compile. They carry no behaviour in sequential mode.
struct auto_partitioner {};
struct simple_partitioner {};
struct static_partitioner {};
struct affinity_partitioner {};

/// Range form: parallel_for(range, body [, partitioner/context...])
/// Sequential semantics: invoke body(range) exactly once with the full range.
/// Constrained so the integral (first, last, ...) overloads win when the first
/// argument is an integer index.
template<typename Range, typename Body, typename... Ignored,
	typename = std::enable_if_t<!std::is_integral_v<std::decay_t<Range>>>>
void parallel_for(const Range & range, const Body & body, Ignored &&...)
{
	body(range);
}

/// Index form: parallel_for(first, last, func [, partitioner/context...])
/// Sequential semantics: for (i = first; i < last; ++i) func(i);
template<typename Index, typename Function, typename... Ignored,
	typename = std::enable_if_t<std::is_integral_v<Index>>>
void parallel_for(Index first, Index last, const Function & func, Ignored &&...)
{
	for(Index i = first; i < last; ++i)
		func(i);
}

/// Strided index form: parallel_for(first, last, step, func [, ...])
/// Sequential semantics: for (i = first; i < last; i += step) func(i);
template<typename Index, typename Function, typename... Ignored,
	typename = std::enable_if_t<std::is_integral_v<Index>>>
void parallel_for(Index first, Index last, Index step, const Function & func, Ignored &&...)
{
	for(Index i = first; i < last; i += step)
		func(i);
}

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
