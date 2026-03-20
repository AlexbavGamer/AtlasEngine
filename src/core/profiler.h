#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#define PROFILE_SCOPE(x) ZoneScopedN(x)
#define PROFILE_FUNCTION() ZoneScoped
#define PROFILE_FRAME() FrameMark

#define PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuf) TracyVkContext(physDev, dev, queue, cmdBuf)
#define PROFILE_GPU_DESTROY(ctx) TracyVkDestroy(ctx)
#define PROFILE_GPU_ZONE(ctx, cmdBuf, name) TracyVkZone(ctx, cmdBuf, name)
#define PROFILE_GPU_COLLECT(ctx, cmdBuf) TracyVkCollect(ctx, cmdBuf)

#else
#define PROFILE_SCOPE(x)
#define PROFILE_FUNCTION()
#define PROFILE_FRAME()

#define PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuf) nullptr
#define PROFILE_GPU_DESTROY(ctx)
#define PROFILE_GPU_ZONE(ctx, cmdBuf, name)
#define PROFILE_GPU_COLLECT(ctx, cmdBuf)
#endif
