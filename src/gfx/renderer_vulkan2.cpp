#include "renderer_vulkan2.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include "core/vector.h"
#include <SDL_syswm.h>

namespace wb {
GPURendererVK::GPURendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                             VkSurfaceKHR main_surface, uint32_t graphics_queue_index, uint32_t present_queue_index) :
    instance_(instance),
    physical_device_(physical_device),
    device_(device),
    main_surface_(main_surface),
    graphics_queue_index_(graphics_queue_index),
    present_queue_index_(present_queue_index) {
}

bool GPURendererVK::init(SDL_Window* window) {
    VkAttachmentDescription att_desc {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference att_ref {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &att_ref,
    };

    VkSubpassDependency subpass_dependency {
        .srcSubpass = 0,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    };

    VkRenderPassCreateInfo rp_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att_desc,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpass_dependency,
    };
    VK_CHECK(vkCreateRenderPass(device_, &rp_info, nullptr, &fb_render_pass_));

    return false;
}

void GPURendererVK::shutdown() {
    if (fb_render_pass_)
        vkDestroyRenderPass(device_, fb_render_pass_, nullptr);
    if (main_surface_)
        vkDestroySurfaceKHR(instance_, main_surface_, nullptr);
    if (device_)
        vkDestroyDevice(device_, nullptr);
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}

void GPURendererVK::begin_frame() {
}

void GPURendererVK::end_frame() {
}

GPUBuffer* GPURendererVK::create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, size_t init_size,
                                        void* init_data) {
    return nullptr;
}

GPUTexture* GPURendererVK::create_texture(GPUTextureUsageFlags usage, uint32_t w, uint32_t h, size_t init_size) {
    return nullptr;
}

GPUPipeline* GPURendererVK::create_pipeline(const GPUPipelineDesc& desc) {
    return nullptr;
}

void GPURendererVK::destroy_buffer(GPUBuffer* buffer) {
}

void GPURendererVK::destroy_texture(GPUTexture* buffer) {
}

void GPURendererVK::destroy_pipeline(GPUPipeline* buffer) {
}

void GPURendererVK::add_viewport(ImGuiViewport* viewport) {
}

void GPURendererVK::remove_viewport(ImGuiViewport* viewport) {
}

void* GPURendererVK::map_buffer(GPUBuffer* buffer) {
    return nullptr;
}

void GPURendererVK::unmap_buffer(GPUBuffer* buffer) {
}

void GPURendererVK::begin_render(GPUTexture* render_target, const ImVec4& clear_color) {
}

void GPURendererVK::end_render() {
}

void GPURendererVK::set_pipeline(GPUPipeline* pipeline) {
}

void GPURendererVK::set_shader_parameter(size_t size, const void* data) {
}

void GPURendererVK::flush_state() {
}

GPURenderer* GPURendererVK::create(SDL_Window* window) {
    if (VK_FAILED(volkInitialize()))
        return nullptr;

    uint32_t api_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion)
        vkEnumerateInstanceVersion(&api_version);

    VkApplicationInfo app_info {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "wb_vulkan",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "wb_vulkan_renderer",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = api_version,
    };

    const char* instance_layer = "VK_LAYER_KHRONOS_validation";
    const char* instance_ext = "VK_KHR_surface";

    Vector<VkExtensionProperties> extensions;
    uint32_t num_extensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, nullptr);
    extensions.resize(num_extensions);
    vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, extensions.data());

    bool has_surface = false;
    bool has_platform_surface = false;
    Vector<const char*> enabled_extensions;
    for (const auto& ext : extensions) {
        if (std::strncmp(ext.extensionName, "VK_KHR_surface", sizeof("VK_KHR_surface")) == 0) {
            enabled_extensions.push_back("VK_KHR_surface");
            has_surface = true;
        }
#if defined(WB_PLATFORM_WINDOWS)
        else if (std::strncmp(ext.extensionName, "VK_KHR_win32_surface", sizeof("VK_KHR_win32_surface")) == 0) {
            enabled_extensions.push_back("VK_KHR_win32_surface");
            has_platform_surface = true;
        }
#elif defined(WB_PLATFORM_LINUX)
        else if (std::strncmp(ext.extensionName, "VK_KHR_xcb_surface", sizeof("VK_KHR_xcb_surface")) == 0) {
            enabled_extensions.push_back("VK_KHR_xcb_surface");
            has_platform_surface = true;
        } else if (std::strncmp(ext.extensionName, "VK_KHR_xlib_surface", sizeof("VK_KHR_xlib_surface")) == 0) {
            enabled_extensions.push_back("VK_KHR_xlib_surface");
            has_platform_surface = true;
        } else if (std::strncmp(ext.extensionName, "VK_KHR_wayland_surface", sizeof("VK_KHR_wayland_surface")) == 0) {
            enabled_extensions.push_back("VK_KHR_wayland_surface");
            has_platform_surface = true;
        }
#endif

#if defined(WB_PLATFORM_WINDOWS)
        if (has_surface && has_platform_surface)
            break;
#endif
    }

    if (!(has_surface && has_platform_surface)) {
        Log::error("Renderer: Cannot find surface extensions");
        return nullptr;
    }

    VkInstanceCreateInfo instance_info {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = &instance_layer,
        .enabledExtensionCount = (uint32_t)enabled_extensions.size(),
        .ppEnabledExtensionNames = enabled_extensions.data(),
    };

    VkInstance instance;
    if (VK_FAILED(vkCreateInstance(&instance_info, nullptr, &instance)))
        return nullptr;

    volkLoadInstanceOnly(instance);

    Vector<VkPhysicalDevice> physical_devices;
    uint32_t num_physical_device;
    vkEnumeratePhysicalDevices(instance, &num_physical_device, nullptr);
    physical_devices.resize(num_physical_device);
    vkEnumeratePhysicalDevices(instance, &num_physical_device, physical_devices.data());

    VkPhysicalDevice selected_physical_device = physical_devices[0];

    // Prefer discrete gpu
    for (auto physical_device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected_physical_device = physical_device;
            break;
        }
    }

    VkSurfaceKHR surface;
    SDL_SysWMinfo wm_info {};
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);

#if defined(WB_PLATFORM_WINDOWS)
    VkWin32SurfaceCreateInfoKHR surface_info {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = (HWND)wm_info.info.win.window,
    };

    if (VK_FAILED(vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface))) {
        Log::error("Failed to create window surface");
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }
#elif defined(WB_PLATFORM_LINUX)
    Display* display = wm_info.info.x11.display;

    VkXlibSurfaceCreateInfoKHR surface_info {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = display,
        .window = wm_info.info.x11.window,
    };

    if (VK_FAILED(vkCreateXlibSurfaceKHR(instance, &surface_info, nullptr, &surface))) {
        Log::error("Failed to create window surface");
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }
#endif

    Vector<VkQueueFamilyProperties> queue_families;
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(selected_physical_device, &queue_family_count, nullptr);
    queue_families.resize(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(selected_physical_device, &queue_family_count, queue_families.data());

    uint32_t graphics_queue_index = (uint32_t)-1;
    uint32_t present_queue_index = (uint32_t)-1;
    for (uint32_t i = 0; const auto& queue_family : queue_families) {
        if (contain_bit(queue_family.queueFlags, VK_QUEUE_GRAPHICS_BIT))
            graphics_queue_index = i;
        VkBool32 presentation_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(selected_physical_device, graphics_queue_index, surface,
                                             &presentation_supported);
        if (presentation_supported)
            present_queue_index = i;
        if (graphics_queue_index != (uint32_t)-1 && present_queue_index != (uint32_t)-1)
            break;
        i++;
    }

    if (graphics_queue_index == (uint32_t)-1 || present_queue_index == (uint32_t)-1) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkPhysicalDeviceFeatures features {};
    vkGetPhysicalDeviceFeatures(selected_physical_device, &features);

    const char* extension_name = "VK_KHR_swapchain";
    VkDeviceCreateInfo device_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = &extension_name,
        .pEnabledFeatures = &features,
    };

    VkDevice device;
    if (VK_FAILED(vkCreateDevice(selected_physical_device, &device_info, nullptr, &device))) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    GPURendererVK* renderer = new (std::nothrow)
        GPURendererVK(instance, selected_physical_device, device, surface, graphics_queue_index, present_queue_index);

    if (!renderer) {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    if (!renderer->init(window)) {
        delete renderer;
        return nullptr;
    }

    return renderer;
}
} // namespace wb