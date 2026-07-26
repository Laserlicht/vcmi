/*
 * concurrent_unordered_map.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// Sequential stub backed by std::unordered_map.
#pragma once

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace tbb
{

template<
	typename Key,
	typename T,
	typename Hash = std::hash<Key>,
	typename KeyEqual = std::equal_to<Key>,
	typename Allocator = std::allocator<std::pair<const Key, T>>>
class concurrent_unordered_map
{
	using map_type = std::unordered_map<Key, T, Hash, KeyEqual, Allocator>;

public:
	using key_type = Key;
	using mapped_type = T;
	using value_type = std::pair<const Key, T>;
	using size_type = std::size_t;
	using iterator = typename map_type::iterator;
	using const_iterator = typename map_type::const_iterator;

	concurrent_unordered_map() = default;

	std::pair<iterator, bool> insert(const value_type & value) { return myMap.insert(value); }

	template<typename P>
	std::pair<iterator, bool> insert(P && value) { return myMap.insert(std::forward<P>(value)); }

	template<typename... Args>
	std::pair<iterator, bool> emplace(Args &&... args)
	{
		return myMap.emplace(std::forward<Args>(args)...);
	}

	// oneTBB exposes operator[] on concurrent_unordered_map.
	mapped_type & operator[](const Key & key) { return myMap[key]; }
	mapped_type & operator[](Key && key) { return myMap[std::move(key)]; }

	iterator find(const Key & key) { return myMap.find(key); }
	const_iterator find(const Key & key) const { return myMap.find(key); }

	size_type count(const Key & key) const { return myMap.count(key); }
	size_type size() const { return myMap.size(); }
	bool empty() const { return myMap.empty(); }
	size_type unsafe_erase(const Key & key) { return myMap.erase(key); }
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
