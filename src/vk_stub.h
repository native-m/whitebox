#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <volk.h>
#include <vk_mem_alloc.h>

#define VK_FAILED(x) (x < VK_SUCCESS)
#define VK_CHECK(x) assert((x) >= VK_SUCCESS)