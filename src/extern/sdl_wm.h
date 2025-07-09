#pragma once

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>

#if defined(SDL_VIDEO_DRIVER_X11)
#undef None
#endif
