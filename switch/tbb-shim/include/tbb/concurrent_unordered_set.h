/*
 * concurrent_unordered_set.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port. Backed by std::unordered_set.
 */
#pragma once

#include <cstddef>
#include <unordered_set>
#include <utility>

namespace tbb
{

template<
	typename Key,
	typename Hash = std::hash<Key>,
	typename KeyEqual = std::equal_to<Key>,
	typename Allocator = std::allocator<Key>>
class concurrent_unordered_set
{
	using set_type = std::unordered_set<Key, Hash, KeyEqual, Allocator>;

public:
	using key_type = Key;
	using value_type = Key;
	using size_type = std::size_t;
	using iterator = typename set_type::iterator;
	using const_iterator = typename set_type::const_iterator;

	concurrent_unordered_set() = default;

	std::pair<iterator, bool> insert(const Key & value) { return mySet.insert(value); }
	std::pair<iterator, bool> insert(Key && value) { return mySet.insert(std::move(value)); }

	template<typename... Args>
	std::pair<iterator, bool> emplace(Args &&... args)
	{
		return mySet.emplace(std::forward<Args>(args)...);
	}

	iterator find(const Key & key) { return mySet.find(key); }
	const_iterator find(const Key & key) const { return mySet.find(key); }

	size_type count(const Key & key) const { return mySet.count(key); }
	size_type size() const { return mySet.size(); }
	bool empty() const { return mySet.empty(); }
	size_type unsafe_erase(const Key & key) { return mySet.erase(key); }
	void clear() { mySet.clear(); }

	iterator begin() { return mySet.begin(); }
	iterator end() { return mySet.end(); }
	const_iterator begin() const { return mySet.begin(); }
	const_iterator end() const { return mySet.end(); }

private:
	set_type mySet;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
