/*
 * CStopWatch.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#if defined(__FreeBSD__) || defined(__OpenBSD__)
	#include <sys/types.h>
	#include <sys/time.h>
	#include <sys/resource.h>
	// clock() below returns microseconds
	#define TICKS_TO_MS(t) ((t) / 1000)
#else
	#include <ctime>
	// multiply before dividing: CLOCKS_PER_SEC may be < 1000 (newlib: 100), where a
	// (CLOCKS_PER_SEC / 1000) divisor truncates to a division by zero (GCC 15 traps it)
	#define TICKS_TO_MS(t) ((t) * 1000 / CLOCKS_PER_SEC)
#endif

class CStopWatch
{
	si64 start;
	si64 last;
	si64 mem;

public:
	CStopWatch()
		: start(clock())
	{
		last=clock();
		mem=0;
	}

	si64 getDiff() //get diff in milliseconds
	{
		si64 ret = clock() - last;
		last = clock();
		return TICKS_TO_MS(ret);
	}
	void update()
	{
		last=clock();
	}
	void remember()
	{
		mem=clock();
	}
	si64 memDif()
	{
		return TICKS_TO_MS(clock() - mem);
	}

private:
	si64 clock() 
	{
	#if defined(__FreeBSD__) || defined(__OpenBSD__) // TODO: enable also for Apple?
		struct rusage usage;
		getrusage(RUSAGE_SELF, &usage);
		return static_cast<si64>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000 + usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
	#else
		return std::clock();
	#endif
	}
};
