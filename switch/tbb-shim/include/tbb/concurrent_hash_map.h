/*
 * concurrent_hash_map.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port. Backed by std::unordered_map. Because
 * everything is single-threaded, no locking is required, but the accessor /
 * const_accessor RAII handles are preserved so call sites compile and behave
 * identically.
 *
 * The HashCompare type follows the oneTBB convention: it provides hash(key) and
 * equal(a, b). VCMI uses two flavours:
 *   - static members (CBonusSystemNode::HashStringCompare), and
 *   - a functor data member + non-static equal (ObjectInstanceIDHash).
 * Calling them through an instance (hc.hash(k), hc.equal(a, b)) works for both.
 */
#pragma once

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace tbb
{

/// Default hash/compare used when a call site omits the third template
/// argument. Mirrors tbb::tbb_hash_compare.
template<typename Key>
struct tbb_hash_compare
{
	static std::size_t hash(const Key & k)
	{
		return std::hash<Key>()(k);
	}
	static bool equal(const Key & a, const Key & b)
	{
		return a == b;
	}
};

template<typename Key, typename T, typename HashCompare = tbb_hash_compare<Key>>
class concurrent_hash_map
{
	// Adapt a oneTBB-style HashCompare (hash/equal) to the std::unordered_map
	// Hash and KeyEqual requirements.
	struct HasherAdapter
	{
		HashCompare hc;
		std::size_t operator()(const Key & k) const { return hc.hash(k); }
	};
	struct EqualAdapter
	{
		HashCompare hc;
		bool operator()(const Key & a, const Key & b) const { return hc.equal(a, b); }
	};

	using map_type = std::unordered_map<Key, T, HasherAdapter, EqualAdapter>;

public:
	using key_type = Key;
	using mapped_type = T;
	using value_type = std::pair<const Key, T>;
	using size_type = std::size_t;
	using iterator = typename map_type::iterator;
	using const_iterator = typename map_type::const_iterator;

	concurrent_hash_map() = default;

	// --- RAII access handles -------------------------------------------------
	// In oneTBB these hold a fine-grained reader/writer lock on a bucket. Here
	// they are just a pointer into the map, but expose the same interface:
	// operator-> yields a value_type*, empty() reports whether a key is bound,
	// and release() detaches the handle.

	class const_accessor
	{
	public:
		using value_type = const std::pair<const Key, T>;

		const_accessor() = default;
		~const_accessor() = default;

		bool empty() const { return myEntry == nullptr; }
		void release() { myEntry = nullptr; }

		const value_type & operator*() const { return *myEntry; }
		const value_type * operator->() const { return myEntry; }

	protected:
		friend class concurrent_hash_map;
		// Points at a live element inside the underlying map (whose value_type
		// is std::pair<const Key, T>). const_cast lets the writable accessor
		// reuse the same storage pointer.
		std::pair<const Key, T> * myEntry = nullptr;
	};

	class accessor : public const_accessor
	{
	public:
		using value_type = std::pair<const Key, T>;

		value_type & operator*() const { return *this->myEntry; }
		value_type * operator->() const { return this->myEntry; }
	};

	// --- lookups -------------------------------------------------------------

	bool find(const_accessor & result, const Key & key)
	{
		result.release();
		auto it = myMap.find(key);
		if(it == myMap.end())
			return false;
		result.myEntry = &(*it);
		return true;
	}

	bool find(accessor & result, const Key & key)
	{
		result.release();
		auto it = myMap.find(key);
		if(it == myMap.end())
			return false;
		result.myEntry = &(*it);
		return true;
	}

	/// const lookup with no handle, mirroring oneTBB's bool count-style probe.
	bool find(const_accessor & result, const Key & key) const
	{
		result.release();
		auto it = myMap.find(key);
		if(it == myMap.end())
			return false;
		result.myEntry = const_cast<std::pair<const Key, T> *>(&(*it));
		return true;
	}

	// --- insertions ----------------------------------------------------------
	// oneTBB's insert returns true if a NEW element was created (false if the
	// key already existed); the accessor always ends up bound to the element.

	bool insert(const_accessor & result, const Key & key)
	{
		result.release();
		auto res = myMap.emplace(key, T());
		result.myEntry = &(*res.first);
		return res.second;
	}

	bool insert(accessor & result, const Key & key)
	{
		result.release();
		auto res = myMap.emplace(key, T());
		result.myEntry = &(*res.first);
		return res.second;
	}

	bool insert(const_accessor & result, const value_type & value)
	{
		result.release();
		auto res = myMap.insert(value);
		result.myEntry = &(*res.first);
		return res.second;
	}

	bool insert(accessor & result, const value_type & value)
	{
		result.release();
		auto res = myMap.insert(value);
		result.myEntry = &(*res.first);
		return res.second;
	}

	bool insert(const value_type & value)
	{
		return myMap.insert(value).second;
	}

	template<typename... Args>
	bool emplace(Args &&... args)
	{
		return myMap.emplace(std::forward<Args>(args)...).second;
	}

	// --- erasures ------------------------------------------------------------

	bool erase(const Key & key)
	{
		return myMap.erase(key) != 0;
	}

	bool erase(const_accessor & item_accessor)
	{
		if(item_accessor.empty())
			return false;
		bool erased = myMap.erase(item_accessor->first) != 0;
		item_accessor.release();
		return erased;
	}

	// --- queries -------------------------------------------------------------

	size_type count(const Key & key) const { return myMap.count(key); }
	size_type size() const { return myMap.size(); }
	bool empty() const { return myMap.empty(); }
	void clear() { myMap.clear(); }

	iterator begin() { return myMap.begin(); }
	iterator end() { return myMap.end(); }
	const_iterator begin() const { return myMap.begin(); }
	const_iterator end() const { return myMap.end(); }

private:
	map_type myMap;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
