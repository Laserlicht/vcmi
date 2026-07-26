/*
 * concurrent_vector.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// Sequential stub backed by std::vector.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace tbb
{

template<typename T, typename Allocator = std::allocator<T>>
class concurrent_vector
{
	using vector_type = std::vector<T, Allocator>;

public:
	using value_type = T;
	using size_type = std::size_t;
	using reference = T &;
	using const_reference = const T &;
	using iterator = typename vector_type::iterator;
	using const_iterator = typename vector_type::const_iterator;

	concurrent_vector() = default;
	concurrent_vector(std::initializer_list<T> init) : myVec(init) {}

	/// oneTBB returns an iterator to the appended element. Matched here.
	iterator push_back(const T & value)
	{
		myVec.push_back(value);
		return myVec.end() - 1;
	}

	iterator push_back(T && value)
	{
		myVec.push_back(std::move(value));
		return myVec.end() - 1;
	}

	template<typename... Args>
	iterator emplace_back(Args &&... args)
	{
		myVec.emplace_back(std::forward<Args>(args)...);
		return myVec.end() - 1;
	}

	/// oneTBB grows the vector by 'delta' default-constructed elements and
	/// returns an iterator to the first new element.
	iterator grow_by(size_type delta)
	{
		size_type oldSize = myVec.size();
		myVec.resize(oldSize + delta);
		return myVec.begin() + oldSize;
	}

	iterator grow_by(size_type delta, const T & value)
	{
		size_type oldSize = myVec.size();
		myVec.resize(oldSize + delta, value);
		return myVec.begin() + oldSize;
	}

	/// oneTBB grows the vector to at least 'n' elements; returns iterator to
	/// the first new element (or end() if no growth occurred).
	iterator grow_to_at_least(size_type n)
	{
		size_type oldSize = myVec.size();
		if(n > oldSize)
			myVec.resize(n);
		return myVec.begin() + (n > oldSize ? oldSize : myVec.size());
	}

	reference operator[](size_type i) { return myVec[i]; }
	const_reference operator[](size_type i) const { return myVec[i]; }

	reference at(size_type i) { return myVec.at(i); }
	const_reference at(size_type i) const { return myVec.at(i); }

	reference front() { return myVec.front(); }
	const_reference front() const { return myVec.front(); }
	reference back() { return myVec.back(); }
	const_reference back() const { return myVec.back(); }

	size_type size() const { return myVec.size(); }
	bool empty() const { return myVec.empty(); }
	void reserve(size_type n) { myVec.reserve(n); }
	void resize(size_type n) { myVec.resize(n); }
	void clear() { myVec.clear(); }

	iterator begin() { return myVec.begin(); }
	iterator end() { return myVec.end(); }
	const_iterator begin() const { return myVec.begin(); }
	const_iterator end() const { return myVec.end(); }
	const_iterator cbegin() const { return myVec.cbegin(); }
	const_iterator cend() const { return myVec.cend(); }

private:
	vector_type myVec;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
