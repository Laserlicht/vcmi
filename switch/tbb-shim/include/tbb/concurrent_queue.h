/*
 * concurrent_queue.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port. Backed by std::deque.
 */
#pragma once

#include <cstddef>
#include <deque>
#include <utility>

namespace tbb
{

template<typename T, typename Allocator = std::allocator<T>>
class concurrent_queue
{
	using queue_type = std::deque<T, Allocator>;

public:
	using value_type = T;
	using size_type = std::size_t;

	concurrent_queue() = default;

	void push(const T & value) { myQueue.push_back(value); }
	void push(T && value) { myQueue.push_back(std::move(value)); }

	template<typename... Args>
	void emplace(Args &&... args)
	{
		myQueue.emplace_back(std::forward<Args>(args)...);
	}

	/// oneTBB: pops the front element into 'result' and returns true, or returns
	/// false if the queue is empty. Single-threaded, so never blocks.
	bool try_pop(T & result)
	{
		if(myQueue.empty())
			return false;
		result = std::move(myQueue.front());
		myQueue.pop_front();
		return true;
	}

	bool empty() const { return myQueue.empty(); }

	/// oneTBB's size estimate; exact here since there is no concurrency.
	size_type unsafe_size() const { return myQueue.size(); }

	void clear() { myQueue.clear(); }

private:
	queue_type myQueue;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
