/*
 * parallel_reduce.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port.
 */
#pragma once

#include "blocked_range.h"

#include <utility>

namespace tbb
{

/// Functional form:
///   parallel_reduce(range, identity, realbody, reduction [, partitioner...])
/// Sequential semantics: a single chunk covering the whole range is processed,
/// so the result is realbody(range, identity). The reduction functor is never
/// needed because there is only one partial result; it is accepted (and
/// ignored) to match the oneTBB signature. Returns by value, like oneTBB.
template<typename Range, typename Value, typename RealBody, typename Reduction, typename... Ignored>
Value parallel_reduce(
	const Range & range,
	const Value & identity,
	const RealBody & realbody,
	const Reduction & /*reduction*/,
	Ignored &&...)
{
	return realbody(range, identity);
}

/// Imperative form:
///   parallel_reduce(range, body [, partitioner...])
/// where 'body' is a splittable object exposing operator()(range). In
/// sequential mode the body is invoked once with the full range; the caller
/// reads the accumulated result out of 'body' afterwards, exactly as in oneTBB
/// when no splitting occurs.
template<typename Range, typename Body, typename... Ignored>
void parallel_reduce(const Range & range, Body & body, Ignored &&...)
{
	body(range);
}

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
