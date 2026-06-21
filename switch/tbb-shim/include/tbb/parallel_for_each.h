/*
 * parallel_for_each.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port.
 */
#pragma once

#include <iterator>
#include <type_traits>
#include <utility>

namespace tbb
{

/// Iterator form: parallel_for_each(first, last, body)
/// Sequential semantics: invoke body(*it) for each element in [first, last).
template<typename Iterator, typename Body,
	typename = std::enable_if_t<!std::is_same_v<
		typename std::iterator_traits<Iterator>::value_type, void>>>
void parallel_for_each(Iterator first, Iterator last, const Body & body)
{
	for(; first != last; ++first)
		body(*first);
}

/// Container form: parallel_for_each(container, body)
/// Sequential semantics: invoke body(element) for each element of container.
template<typename Container, typename Body,
	typename = std::void_t<decltype(std::begin(std::declval<Container &>()))>>
void parallel_for_each(Container && container, const Body & body)
{
	for(auto && element : container)
		body(element);
}

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
