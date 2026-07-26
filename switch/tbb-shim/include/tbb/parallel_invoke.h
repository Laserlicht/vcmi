/*
 * parallel_invoke.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
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
