/*
 * VCMIThread.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#ifdef VCMI_VITA
#include <pthread.h>
#include <functional>
#else
#include <thread>
#endif

#ifdef VCMI_VITA

// vitasdk's libstdc++ std::thread crashes on startup on Vita (reproduced identically on
// Vita3K and real hardware) for reasons isolated to precompiled libstdc++/vitasdk C
// library internals, not std::thread's own code (verified by disassembly to be a trivial
// pthread_create() call + trampoline) - see vita/README.md's "Runtime crash
// investigation" section. Raw pthread_create()/join()/detach() are confirmed to work
// correctly, so VCMIThread wraps those directly on this platform. Every other platform
// just aliases VCMIThread to std::thread below - this class exists only to give call
// sites a single, platform-independent type to use instead of std::thread.
class VCMIThread
{
	pthread_t handle = 0;
	bool active = false;

public:
	using id = pthread_t;

	VCMIThread() = default;

	template<typename Callable, typename... Args>
	explicit VCMIThread(Callable && f, Args &&... args)
	{
		auto closure = new std::function<void()>(
			[func = std::forward<Callable>(f), ...capturedArgs = std::forward<Args>(args)]() mutable
			{
				std::invoke(func, capturedArgs...);
			});
		auto trampoline = [](void * arg) -> void *
		{
			auto * fn = static_cast<std::function<void()> *>(arg);
			(*fn)();
			delete fn;
			return nullptr;
		};
		pthread_create(&handle, nullptr, trampoline, closure);
		active = true;
	}

	VCMIThread(const VCMIThread &) = delete;
	VCMIThread & operator=(const VCMIThread &) = delete;

	VCMIThread(VCMIThread && other) noexcept
		: handle(other.handle), active(other.active)
	{
		other.active = false;
	}

	VCMIThread & operator=(VCMIThread && other) noexcept
	{
		if(this != &other)
		{
			if(active)
				pthread_detach(handle);
			handle = other.handle;
			active = other.active;
			other.active = false;
		}
		return *this;
	}

	// Unlike std::thread, destroying a still-joinable VCMIThread detaches it instead of
	// calling std::terminate() - a deliberate divergence, since a hard terminate is a far
	// worse outcome on a homebrew console than a detached thread.
	~VCMIThread()
	{
		if(active)
			pthread_detach(handle);
	}

	bool joinable() const { return active; }

	void join()
	{
		pthread_join(handle, nullptr);
		active = false;
	}

	void detach()
	{
		pthread_detach(handle);
		active = false;
	}

	id get_id() const { return handle; }
};

namespace VCMIThisThread
{
	inline VCMIThread::id get_id() { return pthread_self(); }
}

#else

using VCMIThread = std::thread;

namespace VCMIThisThread
{
	using namespace std::this_thread;
}

#endif
