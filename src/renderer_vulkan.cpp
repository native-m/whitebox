#include "renderer_vulkan.h"
#include "app_sdl2.h"
#include "core/debug.h"
#include "core/defer.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>

namespace wb {

RendererVK::RendererVK(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
                       uint32_t graphics_queue_index, uint32_t present_queue_index) :
    physical_device_(physical_device),
    device_(device),
    surface_(surface),
    graphics_queue_index_(graphics_queue_index),
    present_queue_index_(present_queue_index) {
    vkGetDeviceQueue(device_, graphics_queue_index_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_queue_index_, 0, &present_queue_);
}

RendererVK::~RendererVK() {
}

bool RendererVK::init() {
    vkb::SwapchainBuilder swapchain_builder(physical_device_, device_, surface_,
                                            graphics_queue_index_, present_queue_index_);

    auto swapchain_result = swapchain_builder.build();
    if (!swapchain_result) {
        Log::error("Failed to find suitable Vulkan device");
        return false;
    }

    vkb::Swapchain new_swapchain = swapchain_result.value();
    swapchain_ = new_swapchain.swapchain;

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

    VkRenderPassCreateInfo rp_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att_desc,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VK_CHECK(vkCreateRenderPass(device_, &rp_info, nullptr, &fb_render_pass));

    VkCommandPoolCreateInfo cmd_pool {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = graphics_queue_index_,
    };

    VkCommandBufferAllocateInfo cmd_buf_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    for (uint32_t i = 0; i < VULKAN_BUFFER_SIZE; i++) {
        CommandBufferVK& cmd = cmd_buf_[i];
        VK_CHECK(vkCreateCommandPool(device_, &cmd_pool, nullptr, &cmd.cmd_pool_));
        cmd_buf_info.commandPool = cmd.cmd_pool_;
        VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &cmd.cmd_buffer_));
    }

    return true;
}

std::shared_ptr<Framebuffer> RendererVK::create_framebuffer(uint32_t width, uint32_t height) {
    return std::shared_ptr<Framebuffer>();
}

std::shared_ptr<SamplePeaks> RendererVK::create_sample_peaks(const Sample& sample,
                                                             SamplePeaksPrecision precision) {
    return std::shared_ptr<SamplePeaks>();
}

void RendererVK::new_frame() {
}

void RendererVK::resize_swapchain() {
}

void RendererVK::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) {
}

void RendererVK::begin_draw(const std::shared_ptr<Framebuffer>& framebuffer) {
}

void RendererVK::finish_draw() {
}

void RendererVK::clear(float r, float g, float b, float a) {
}

void RendererVK::draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) {
}

void RendererVK::render_draw_data(ImDrawData* draw_data) {
}

void RendererVK::present() {
}

Renderer* RendererVK::create(App* app) {
    if (VK_FAILED(volkInitialize()))
        return nullptr;

    auto inst_ret = vkb::InstanceBuilder()
                        .set_app_name("wb_vulkan")
                        .request_validation_layers()
                        .use_default_debug_messenger()
                        .build();

    if (!inst_ret) {
        Log::error("Failed to create vulkan instance. Error: {}", inst_ret.error().message());
        return nullptr;
    }

    vkb::Instance instance = inst_ret.value();

    SDL_Window* window = ((AppSDL2*)app)->window;
    SDL_SysWMinfo wm_info {};
    SDL_GetWindowWMInfo(window, &wm_info);
    volkLoadInstanceOnly(instance);

    VkSurfaceKHR surface;
    VkWin32SurfaceCreateInfoKHR surface_info {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = wm_info.info.win.window,
    };

    if (VK_FAILED(vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface))) {
        Log::error("Failed to create window surface");
        vkb::destroy_instance(instance);
        return nullptr;
    }

    auto selected_physical_device = vkb::PhysicalDeviceSelector(instance)
                                        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                        .set_surface(surface)
                                        .require_present()
                                        .select();

    if (!selected_physical_device) {
        Log::error("Failed to find suitable Vulkan device");
        vkb::destroy_instance(instance);
        return nullptr;
    }

    auto device_result = vkb::DeviceBuilder(selected_physical_device.value()).build();
    if (!device_result) {
        Log::error("Failed to create Vulkan device. Error: {}", device_result.error().message());
        vkb::destroy_instance(instance);
        return nullptr;
    }

    vkb::Device device = device_result.value();
    VkPhysicalDevice physical_device = selected_physical_device.value();
    VkDevice vulkan_device = device.device;
    uint32_t graphics_queue_index = device.get_queue_index(vkb::QueueType::graphics).value();
    uint32_t present_queue_index = device.get_queue_index(vkb::QueueType::present).value();

    volkLoadDevice(vulkan_device);

    RendererVK* renderer = new (std::nothrow)
        RendererVK(physical_device, device, surface, graphics_queue_index, present_queue_index);

    if (!renderer && !renderer->init()) {
        vkb::destroy_device(device);
        vkb::destroy_instance(instance);
        return nullptr;
    }

    return renderer;
}
} // namespace wb