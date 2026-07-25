// PS Vita build: header-only sequential/mutex-guarded stand-in for the subset of the
// oneTBB API that VCMI actually calls (no real TBB build exists for vitasdk/newlib).
//
// All definitions live in this single file, and every public tbb/<name>.h header below
// just re-exports it. This deliberately does not mirror oneTBB's real per-header
// dependency graph: several VCMI translation units use tbb::parallel_for,
// tbb::blocked_range, tbb::concurrent_vector etc. without including the matching real
// TBB header at all, relying on oneTBB's own headers transitively pulling each other
// in. Rather than reverse-engineer that graph, every entry point here exposes the
// complete symbol set, which is a strict superset of what any single real TBB header
// would provide - safe to over-include, unsafe to under-include.
//
// Concurrency note: the tasking primitives (parallel_for/parallel_reduce/task_group/
// task_arena) execute synchronously on the calling thread - there is no worker pool.
// That's fine for VCMI's usage (parallel_for etc. are only ever used for speed, never
// for cross-thread coordination). The concurrent_* containers are the exception: they
// ARE genuinely shared across real threads, because AsyncRunner (lib/AsyncRunner.h)
// runs the Nullkiller2 AI turn on an actual std::thread in parallel with the
// main/render thread when the tasking pool is sequential. So, unlike the tasking
// primitives, every concurrent_* container here is internally mutex-guarded rather
// than a bare STL container.
#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tbb
{

// ---------------------------------------------------------------------------------
// blocked_range / blocked_range2d
// ---------------------------------------------------------------------------------

template<typename Value>
class blocked_range
{
public:
	using const_iterator = Value;
	using size_type = std::size_t;

	blocked_range(Value begin, Value end, size_type grainsize = 1)
		: myBegin(begin), myEnd(end), myGrainsize(grainsize)
	{
	}

	const_iterator begin() const { return myBegin; }
	const_iterator end() const { return myEnd; }
	size_type size() const { return static_cast<size_type>(myEnd - myBegin); }
	bool empty() const { return !(myBegin < myEnd); }
	size_type grainsize() const { return myGrainsize; }

private:
	Value myBegin;
	Value myEnd;
	size_type myGrainsize;
};

template<typename RowValue, typename ColValue = RowValue>
class blocked_range2d
{
public:
	blocked_range2d(RowValue rowBegin, RowValue rowEnd, typename blocked_range<RowValue>::size_type rowGrainsize,
		ColValue colBegin, ColValue colEnd, typename blocked_range<ColValue>::size_type colGrainsize)
		: myRows(rowBegin, rowEnd, rowGrainsize), myCols(colBegin, colEnd, colGrainsize)
	{
	}

	blocked_range2d(RowValue rowBegin, RowValue rowEnd, ColValue colBegin, ColValue colEnd)
		: myRows(rowBegin, rowEnd), myCols(colBegin, colEnd)
	{
	}

	const blocked_range<RowValue> & rows() const { return myRows; }
	const blocked_range<ColValue> & cols() const { return myCols; }

private:
	blocked_range<RowValue> myRows;
	blocked_range<ColValue> myCols;
};

// ---------------------------------------------------------------------------------
// parallel_for / parallel_reduce
// ---------------------------------------------------------------------------------

template<typename Range, typename Body>
void parallel_for(const Range & range, const Body & body)
{
	body(range);
}

template<typename Index, typename Function>
void parallel_for(Index first, Index last, const Function & f)
{
	for(Index i = first; i < last; ++i)
		f(i);
}

template<typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function & f)
{
	for(Index i = first; i < last; i += step)
		f(i);
}

template<typename Range, typename Value, typename Body, typename Reduction>
Value parallel_reduce(const Range & range, const Value & identity, const Body & body, const Reduction & /*reduction*/)
{
	return body(range, identity);
}

template<typename... Functions>
void parallel_invoke(Functions &&... fs)
{
	(fs(), ...);
}

// ---------------------------------------------------------------------------------
// concurrent_hash_map
// ---------------------------------------------------------------------------------

template<typename Key, typename T, typename HashCompare>
class concurrent_hash_map
{
private:
	struct HashAdaptor
	{
		size_t operator()(const Key & key) const
		{
			HashCompare compare;
			return compare.hash(key);
		}
	};

	struct EqualAdaptor
	{
		bool operator()(const Key & a, const Key & b) const
		{
			HashCompare compare;
			return compare.equal(a, b);
		}
	};

	using MapType = std::unordered_map<Key, T, HashAdaptor, EqualAdaptor>;

public:
	using value_type = typename MapType::value_type; // std::pair<const Key, T>
	using iterator = typename MapType::iterator;
	using const_iterator = typename MapType::const_iterator;

	// Unlocked, same rationale as concurrent_vector.h: fill (insert/emplace) while
	// potentially concurrent, then iterate only after the producing work has finished.
	iterator begin() { return map.begin(); }
	iterator end() { return map.end(); }
	const_iterator begin() const { return map.begin(); }
	const_iterator end() const { return map.end(); }

	class const_accessor
	{
	public:
		const value_type & operator*() const { return *ptr; }
		const value_type * operator->() const { return ptr; }

	private:
		friend class concurrent_hash_map;
		const value_type * ptr = nullptr;
		std::unique_lock<std::mutex> lock;
	};

	class accessor
	{
	public:
		value_type & operator*() const { return *ptr; }
		value_type * operator->() const { return ptr; }

	private:
		friend class concurrent_hash_map;
		value_type * ptr = nullptr;
		std::unique_lock<std::mutex> lock;
	};

	bool find(const_accessor & result, const Key & key) const
	{
		std::unique_lock<std::mutex> lock(mutex);
		auto it = map.find(key);
		if(it == map.end())
		{
			result.ptr = nullptr;
			result.lock.unlock();
			return false;
		}
		result.ptr = &(*it);
		result.lock = std::move(lock);
		return true;
	}

	bool find(accessor & result, const Key & key)
	{
		std::unique_lock<std::mutex> lock(mutex);
		auto it = map.find(key);
		if(it == map.end())
		{
			result.ptr = nullptr;
			result.lock.unlock();
			return false;
		}
		result.ptr = const_cast<value_type *>(&(*it));
		result.lock = std::move(lock);
		return true;
	}

	bool insert(accessor & result, const value_type & value)
	{
		std::unique_lock<std::mutex> lock(mutex);
		auto insertResult = map.insert(value);
		result.ptr = const_cast<value_type *>(&(*insertResult.first));
		result.lock = std::move(lock);
		return insertResult.second;
	}

	template<typename... Args>
	bool emplace(Args &&... args)
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto insertResult = map.emplace(std::forward<Args>(args)...);
		return insertResult.second;
	}

	bool erase(const Key & key)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return map.erase(key) > 0;
	}

	// The accessor already holds the map's lock (from a prior find()/insert()), so this
	// must not lock again - std::mutex is not recursive.
	bool erase(accessor & item)
	{
		if(!item.ptr)
			return false;
		map.erase(item.ptr->first);
		item.ptr = nullptr;
		item.lock.unlock();
		return true;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(mutex);
		map.clear();
	}

	size_t size() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return map.size();
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return map.empty();
	}

private:
	mutable std::mutex mutex;
	MapType map;
};

// ---------------------------------------------------------------------------------
// concurrent_queue
// ---------------------------------------------------------------------------------

template<typename T>
class concurrent_queue
{
public:
	void push(const T & value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.push_back(value);
	}

	void push(T && value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.push_back(std::move(value));
	}

	bool try_pop(T & result)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(items.empty())
			return false;
		result = std::move(items.front());
		items.pop_front();
		return true;
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.empty();
	}

	size_t unsafe_size() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.size();
	}

private:
	mutable std::mutex mutex;
	std::deque<T> items;
};

// ---------------------------------------------------------------------------------
// concurrent_vector
// ---------------------------------------------------------------------------------

template<typename T>
class concurrent_vector
{
public:
	using iterator = typename std::vector<T>::iterator;
	using const_iterator = typename std::vector<T>::const_iterator;

	void push_back(const T & value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.push_back(value);
	}

	void push_back(T && value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.push_back(std::move(value));
	}

	template<typename... Args>
	T & emplace_back(Args &&... args)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.emplace_back(std::forward<Args>(args)...);
		return items.back();
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.clear();
	}

	size_t size() const { return items.size(); }
	bool empty() const { return items.empty(); }

	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	const_iterator begin() const { return items.begin(); }
	const_iterator end() const { return items.end(); }

	T & operator[](size_t index) { return items[index]; }
	const T & operator[](size_t index) const { return items[index]; }

private:
	mutable std::mutex mutex;
	std::vector<T> items;
};

// ---------------------------------------------------------------------------------
// concurrent_unordered_map
// ---------------------------------------------------------------------------------

template<typename Key, typename T, typename Hash = std::hash<Key>>
class concurrent_unordered_map
{
public:
	using iterator = typename std::unordered_map<Key, T, Hash>::iterator;
	using const_iterator = typename std::unordered_map<Key, T, Hash>::const_iterator;
	using value_type = typename std::unordered_map<Key, T, Hash>::value_type;

	std::pair<iterator, bool> insert(const value_type & value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.insert(value);
	}

	T & operator[](const Key & key)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items[key];
	}

	iterator find(const Key & key)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.find(key);
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.clear();
	}

	size_t size() const { return items.size(); }
	bool empty() const { return items.empty(); }

	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	const_iterator begin() const { return items.begin(); }
	const_iterator end() const { return items.end(); }

private:
	mutable std::mutex mutex;
	std::unordered_map<Key, T, Hash> items;
};

// ---------------------------------------------------------------------------------
// concurrent_unordered_set
// ---------------------------------------------------------------------------------

template<typename Key, typename Hash = std::hash<Key>>
class concurrent_unordered_set
{
public:
	using iterator = typename std::unordered_set<Key, Hash>::iterator;
	using const_iterator = typename std::unordered_set<Key, Hash>::const_iterator;

	std::pair<iterator, bool> insert(const Key & value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.insert(value);
	}

	size_t count(const Key & value) const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.count(value);
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.clear();
	}

	size_t size() const { return items.size(); }
	bool empty() const { return items.empty(); }

	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	const_iterator begin() const { return items.begin(); }
	const_iterator end() const { return items.end(); }

private:
	mutable std::mutex mutex;
	std::unordered_set<Key, Hash> items;
};

// ---------------------------------------------------------------------------------
// task_group / task_arena / global_control
// ---------------------------------------------------------------------------------

// Returned by task_group::defer(); a callable wrapping the deferred functor, invoked by
// task_arena::enqueue().
template<typename Functor>
class deferred_task
{
public:
	// By-value parameter (not Functor&&): Functor here is already the decayed,
	// concrete type (see defer() below), so this binds to both lvalue functors
	// (copied in) and rvalue ones (moved in) without the caller needing to know which.
	explicit deferred_task(Functor f) : func(std::move(f)) {}
	void operator()() { func(); }

private:
	Functor func;
};

class task_group
{
public:
	template<typename Functor>
	void run(Functor && f)
	{
		f();
	}

	template<typename Functor>
	auto defer(Functor && f)
	{
		return deferred_task<std::decay_t<Functor>>(std::forward<Functor>(f));
	}

	void wait()
	{
		// Nothing to wait for: run()/defer()+enqueue() already executed synchronously.
	}
};

class task_arena
{
public:
	explicit task_arena(int /*maxConcurrency*/) {}
	task_arena() = default;

	template<typename Task>
	void enqueue(Task && task)
	{
		task();
	}

	template<typename Functor>
	void execute(Functor && f)
	{
		f();
	}
};

namespace this_task_arena
{
	inline int max_concurrency() { return 1; }
	inline int current_thread_index() { return -1; }
}

class global_control
{
public:
	enum parameter
	{
		max_allowed_parallelism,
	};

	global_control(parameter /*p*/, size_t /*value*/) {}
};

}
