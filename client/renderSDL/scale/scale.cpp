#include <SDL_surface.h>
#include <tbb/parallel_for.h>

#include "StdInc.h"
#include "scale.h"
#include "../../../lib/logging/CBasicLogConfigurator.h"
#include "xBRZ/xbrz.h"

SDL_Surface* scale::xbrz(SDL_Surface* surf, int z)
{
	if(surf == nullptr)
		return nullptr;

	if (z > 5) {
        logGlobal->warn("Cannot use xbrz scaling with zoom factor > 5");
		z = 1;
	}

	if (z <= 0) {
        logGlobal->warn("Cannot use xbrz scaling with zoom factor <= 0");
		z = 1;
	}

	if (z == 1) {
		return surf;
	}

#ifdef VCMI_ENDIAN_BIG
	int bmask = 0xff000000;
	int gmask = 0x00ff0000;
	int rmask = 0x0000ff00;
	int amask = 0x000000ff;
#else
	int bmask = 0x000000ff;
	int gmask = 0x0000ff00;
	int rmask = 0x00ff0000;
	int amask = 0xFF000000;
#endif

	SDL_Surface* dst = SDL_CreateRGBSurface(0, surf->w * z, surf->h * z, 32, rmask, gmask, bmask, amask);

	if(surf == nullptr || dst == nullptr) {
        logGlobal->warn("Could not create surface to scale onto");
		return nullptr;
	}

    bool src_locked = false, dst_locked = false;
    if(SDL_MUSTLOCK(surf))
	{
        src_locked = SDL_LockSurface(surf) == 0;
	}
    if(SDL_MUSTLOCK(dst))
	{
        dst_locked = SDL_LockSurface(dst) == 0;
	}

    //xbrz::scale(z, reinterpret_cast<uint32_t*>(surf->pixels), reinterpret_cast<uint32_t*>(dst->pixels), surf->w, surf->h);
	tbb::parallel_for(tbb::blocked_range<int>(0, surf->h, tbb::this_task_arena::max_concurrency()), [=](const tbb::blocked_range<int>& r)
	{
		xbrz::scale(z, reinterpret_cast<uint32_t*>(surf->pixels), reinterpret_cast<uint32_t*>(dst->pixels), surf->w, surf->h, xbrz::ScalerCfg(), r.begin(), r.end());
	});

    if(src_locked)
	{
        SDL_UnlockSurface(surf);
	}
    if(dst_locked)
	{
        SDL_UnlockSurface(dst);
	}

	return dst;
}