#pragma once

#include "core/platform.h"

#ifdef WB_PLATFORM_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#ifdef WB_PLATFORM_LINUX
//#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_XCB_KHR
#endif

#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <volk.h>
#include <vk_mem_alloc.h>

#define VK_FAILED(x) (x < VK_SUCCESS)

#ifndef NDEBUG
#define VK_CHECK(x) assert((x) >= VK_SUCCESS)
#else
#define VK_CHECK(x) x
#endif