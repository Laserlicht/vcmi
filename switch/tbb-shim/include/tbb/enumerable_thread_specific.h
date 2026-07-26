/*
 * enumerable_thread_specific.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// With a single execution context there is exactly one thread-local slot.
// Not referenced by VCMI today, but provided for API completeness.
#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace tbb
{

template<typename T>
class enumerable_thread_specific
{
	// A single underlying slot, stored in a one-element vector so iteration and
	// size() behave naturally.
	using storage_type = std::vector<T>;

public:
	using value_type = T;
	using reference = T &;
	using const_reference = const T &;
	using size_type = std::size_t;
	using iterator = typename storage_type::iterator;
	using const_iterator = typename storage_type::const_iterator;

	enumerable_thread_specific()
		: myHasExemplar(false)
	{
	}

	/// oneTBB initialises new slots from an exemplar value.
	explicit enumerable_thread_specific(const T & exemplar)
		: myExemplar(exemplar), myHasExemplar(true)
	{
	}

	/// oneTBB initialises new slots by calling a factory functor.
	template<typename Finit,
		typename = std::enable_if_t<!std::is_same_v<std::decay_t<Finit>, T>>>
	explicit enumerable_thread_specific(Finit finit)
		: myFinit(std::move(finit)), myHasFinit(true)
	{
	}

	/// Returns the single thread-local instance, creating it on first use.
	reference local()
	{
		bool exists;
		return local(exists);
	}

	reference local(bool & exists)
	{
		if(mySlot.empty())
		{
			if(myHasFinit)
				mySlot.push_back(myFinit());
			else if(myHasExemplar)
				mySlot.push_back(myExemplar);
			else
				mySlot.emplace_back();
			exists = false;
		}
		else
		{
			exists = true;
		}
		return mySlot.front();
	}

	size_type size() const { return mySlot.size(); }
	bool empty() const { return mySlot.empty(); }
	void clear() { mySlot.clear(); }

	iterator begin() { return mySlot.begin(); }
	iterator end() { return mySlot.end(); }
	const_iterator begin() const { return mySlot.begin(); }
	const_iterator end() const { return mySlot.end(); }

	/// oneTBB folds all per-thread values together. With one slot this returns
	/// that slot (or a default-constructed value if none exists yet).
	template<typename CombineFunc>
	T combine(CombineFunc f)
	{
		if(mySlot.empty())
			return T();
		T result = mySlot.front();
		for(size_type i = 1; i < mySlot.size(); ++i)
			result = f(result, mySlot[i]);
		return result;
	}

	template<typename CombineBody>
	void combine_each(CombineBody body)
	{
		for(auto & value : mySlot)
			body(value);
	}

private:
	storage_type mySlot;
	T myExemplar{};
	bool myHasExemplar = false;
	std::function<T()> myFinit;
	bool myHasFinit = false;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
