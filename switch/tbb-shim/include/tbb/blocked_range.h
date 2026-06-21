/*
 * blocked_range.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port. Real oneTBB is not available on
 * devkitA64/libnx, so this header provides an API-faithful, single-threaded
 * implementation that preserves TBB's semantics exactly.
 */
#pragma once

#include <cstddef>

namespace tbb
{

/// Sequential equivalent of tbb::blocked_range.
/// Represents a half-open interval [begin, end) that, in real TBB, would be
/// recursively split across worker threads. Here it is never split.
template<typename Value>
class blocked_range
{
public:
	using const_iterator = Value;
	using value_type = Value;
	using size_type = std::size_t;

	blocked_range() : myBegin(), myEnd(), myGrainsize(1) {}

	blocked_range(Value begin_, Value end_, size_type grainsize_ = 1)
		: myBegin(begin_), myEnd(end_), myGrainsize(grainsize_)
	{
	}

	const_iterator begin() const { return myBegin; }
	const_iterator end() const { return myEnd; }

	size_type size() const
	{
		// Mirrors TBB: requires end() >= begin().
		return size_type(myEnd - myBegin);
	}

	bool empty() const { return !(myBegin < myEnd); }

	size_type grainsize() const { return myGrainsize; }

	/// In sequential mode a range is never divided, so it is reported as
	/// non-divisible. The result is consistent with the body always receiving
	/// the full range exactly once.
	bool is_divisible() const { return false; }

private:
	Value myBegin;
	Value myEnd;
	size_type myGrainsize;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
