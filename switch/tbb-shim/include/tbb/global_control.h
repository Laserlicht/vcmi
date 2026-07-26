/*
 * global_control.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// Settings are recorded for API completeness but have no effect, since all
// work runs on the calling thread.
#pragma once

#include <cstddef>

namespace tbb
{

/// Sequential equivalent of tbb::global_control.
class global_control
{
public:
	/// Mirrors tbb::global_control::parameter.
	enum parameter
	{
		max_allowed_parallelism,
		thread_stack_size,
		terminate_on_exception,
		max_concurrency,
		scheduler_handle, // internal in oneTBB; kept for ABI-name compatibility
		parameter_max // must be last
	};

	global_control(parameter p, std::size_t value)
		: myParameter(p), myValue(value)
	{
	}

	~global_control() = default;

	/// oneTBB returns the currently active limit for a parameter. With no real
	/// scheduler, we report the conservative sequential values.
	static std::size_t active_value(parameter p)
	{
		switch(p)
		{
		case max_allowed_parallelism:
		case max_concurrency:
			return 1;
		default:
			return 0;
		}
	}

private:
	parameter myParameter;
	std::size_t myValue;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
