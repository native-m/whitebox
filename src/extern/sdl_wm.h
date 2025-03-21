#pragma once

#include <SDL_syswm.h>
#include <SDL_video.h>

#if defined(SDL_VIDEO_DRIVER_X11)
#undef None
#endif
