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
    vkDeviceWaitIdle(device_);
    vkDestroyDevice(device_, nullptr);
}

bool RendererVK::init() {
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

    VK_CHECK(vkCreateRenderPass(device_, &rp_info, nullptr, &fb_render_pass_));

    if (!init_swapchain_())
        return false;

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
        VK_CHECK(vkCreateCommandPool(device_, &cmd_pool, nullptr, &cmd.cmd_pool));
        cmd_buf_info.commandPool = cmd.cmd_pool;
        VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &cmd.cmd_buffer));
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

void RendererVK::resize_swapchain() {
}

void RendererVK::new_frame() {
    FrameSync& frame_sync = frame_sync_[frame_id_];
    vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame_sync.image_acquire_semaphore,
                          nullptr, &sc_image_index_);

    CommandBufferVK& cmd_buf = cmd_buf_[frame_id_];
    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkWaitForFences(device_, 1, &frame_sync.fence, VK_TRUE, UINT64_MAX);
    vkResetCommandPool(device_, cmd_buf.cmd_pool, 0);
    vkBeginCommandBuffer(cmd_buf.cmd_buffer, &begin_info);

    current_frame_sync_ = &frame_sync;
    current_cb_ = cmd_buf.cmd_buffer;
}

void RendererVK::end_frame() {
    vkEndCommandBuffer(current_cb_);

    VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_frame_sync_->image_acquire_semaphore,
        .pWaitDstStageMask = &wait_dst_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &current_cb_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &current_frame_sync_->render_finished_semaphore,
    };

    vkResetFences(device_, 1, &current_frame_sync_->fence);
    vkQueueSubmit(graphics_queue_, 1, &submit, current_frame_sync_->fence);

    frame_id_ = (frame_id_ + 1) % VULKAN_BUFFER_SIZE;
}

void RendererVK::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) {
}

void RendererVK::begin_draw(const std::shared_ptr<Framebuffer>& framebuffer,
                            const ImVec4& clear_color) {
    FramebufferVK* fb =
        (!framebuffer) ? &main_framebuffer_ : static_cast<FramebufferVK*>(framebuffer.get());

    VkClearValue vk_clear_color {
        .color = {clear_color.x, clear_color.y, clear_color.z, clear_color.w},
    };

    VkRenderPassBeginInfo rp_begin {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = fb_render_pass_,
        .framebuffer = fb->framebuffer[frame_id_],
        .renderArea = {0, 0, fb->width, fb->height},
        .clearValueCount = 1,
        .pClearValues = &vk_clear_color,
    };

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = graphics_queue_index_;
    barrier.dstQueueFamilyIndex = graphics_queue_index_;
    barrier.image = fb->image[frame_id_];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(current_cb_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);

    vkCmdBeginRenderPass(current_cb_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    current_framebuffer_ = fb;
}

void RendererVK::finish_draw() {
    vkCmdEndRenderPass(current_cb_);

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = graphics_queue_index_;
    barrier.dstQueueFamilyIndex = graphics_queue_index_;
    barrier.image = current_framebuffer_->image[frame_id_];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(current_cb_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

void RendererVK::clear(float r, float g, float b, float a) {
}

void RendererVK::draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) {
}

void RendererVK::render_draw_data(ImDrawData* draw_data) {
}

void RendererVK::present() {
    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_frame_sync_->render_finished_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &sc_image_index_,
    };

    VkResult result = vkQueuePresentKHR(graphics_queue_, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        assert(false);
}

bool RendererVK::init_swapchain_() {
    vkb::SwapchainBuilder swapchain_builder(physical_device_, device_, surface_,
                                            graphics_queue_index_, present_queue_index_);

    auto swapchain_result = swapchain_builder.set_required_min_image_count(2)
                                .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM})
                                .build();

    if (!swapchain_result) {
        Log::error("Failed to find suitable Vulkan device");
        return false;
    }

    vkb::Swapchain new_swapchain = swapchain_result.value();
    std::vector<VkImage> swapchain_images(new_swapchain.get_images().value());
    std::vector<VkImageView> swapchain_image_views(new_swapchain.get_image_views().value());

    swapchain_ = new_swapchain.swapchain;
    main_framebuffer_.width = new_swapchain.extent.width;
    main_framebuffer_.height = new_swapchain.extent.height;
    main_framebuffer_.window_framebuffer = true;

    VkFramebufferCreateInfo fb_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = fb_render_pass_,
        .attachmentCount = 1,
        .width = main_framebuffer_.width,
        .height = main_framebuffer_.height,
        .layers = 1,
    };

    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VkSemaphoreCreateInfo semaphore_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    for (int i = 0; i < VULKAN_BUFFER_SIZE; i++) {
        main_framebuffer_.image[i] = swapchain_images[i];
        main_framebuffer_.view[i] = swapchain_image_views[i];
        fb_info.pAttachments = &swapchain_image_views[i];
        VK_CHECK(
            vkCreateFramebuffer(device_, &fb_info, nullptr, &main_framebuffer_.framebuffer[i]));

        FrameSync& frame_sync = frame_sync_[i];
        VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &frame_sync.fence));
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frame_sync.image_acquire_semaphore));
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frame_sync.render_finished_semaphore));
    }

    return true;
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

    if (!renderer) {
        vkb::destroy_device(device);
        vkb::destroy_instance(instance);
        return nullptr;
    }

    if (!renderer->init()) {
        vkb::destroy_device(device);
        vkb::destroy_instance(instance);
        return nullptr;
    }

    return renderer;
}
} // namespace wb