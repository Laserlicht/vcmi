/*
 * parallel_invoke.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port.
 */
#pragma once

#include <utility>

namespace tbb
{

/// Sequential equivalent of tbb::parallel_invoke.
/// Invokes each functor once, in the order given. oneTBB makes no ordering
/// guarantee, so running them in argument order is a valid implementation.
template<typename... Functions>
void parallel_invoke(Functions &&... fs)
{
	(static_cast<void>(std::forward<Functions>(fs)()), ...);
}

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
