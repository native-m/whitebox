#pragma once

#if defined(WIN32) || defined(_WIN32)
#define WB_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define WB_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#define WB_PLATFORM_MACOS 1
#endif