#include "renderer_vulkan.h"
#include "app_sdl2.h"
#include "core/debug.h"
#include "core/defer.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>

#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES

#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#define FRAME_ID_DISPOSE_ALL ~0U

namespace wb {

FramebufferVK::~FramebufferVK() {
    if (resource_disposal)
        resource_disposal->dispose_framebuffer(this);
}

ImTextureID FramebufferVK::as_imgui_texture_id() const {
    return ImTextureID(descriptor_set[resource_disposal->current_frame_id]);
}

//

void ResourceDisposalVK::dispose_framebuffer(FramebufferVK* obj) {
    for (uint32_t i = 0; i < VULKAN_BUFFER_SIZE; i++) {
        fb.push_back(FramebufferDisposalVK {
            .frame_id = i,
            .allocation = obj->allocations[i],
            .image = obj->image[i],
            .view = obj->view[i],
            .framebuffer = obj->framebuffer[i],
        });
    }
}

void ResourceDisposalVK::flush(VkDevice device, VmaAllocator allocator, uint32_t frame_id_dispose) {
    while (!fb.empty()) {
        auto [frame_id, allocation, image, view, framebuffer] = fb.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        vkDestroyImageView(device, view, nullptr);
        vmaDestroyImage(allocator, image, allocation);
        fb.pop_front();
    }
}

//

RendererVK::RendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                       VkSurfaceKHR surface, uint32_t graphics_queue_index,
                       uint32_t present_queue_index) :
    instance_(instance),
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

    ImGui_ImplVulkan_Shutdown();

    resource_disposal_.flush(device_, allocator_, FRAME_ID_DISPOSE_ALL);

    for (int i = 0; i < VULKAN_BUFFER_SIZE; i++) {
        vkDestroyCommandPool(device_, cmd_buf_[i].cmd_pool, nullptr);
        vkDestroyFence(device_, frame_sync_[i].fence, nullptr);
        vkDestroySemaphore(device_, frame_sync_[i].image_acquire_semaphore, nullptr);
        vkDestroySemaphore(device_, frame_sync_[i].render_finished_semaphore, nullptr);
        vkDestroyFramebuffer(device_, main_framebuffer_.framebuffer[i], nullptr);
        vkDestroyImageView(device_, main_framebuffer_.view[i], nullptr);
    }

    vmaDestroyAllocator(allocator_);

    vkDestroySampler(device_, imgui_sampler_, nullptr);
    vkDestroyDescriptorPool(device_, imgui_descriptor_pool_, nullptr);
    vkDestroyRenderPass(device_, fb_render_pass_, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

bool RendererVK::init() {
    VmaVulkanFunctions vma_func {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory = vkAllocateMemory,
        .vkFreeMemory = vkFreeMemory,
        .vkMapMemory = vkMapMemory,
        .vkUnmapMemory = vkUnmapMemory,
        .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory = vkBindBufferMemory,
        .vkBindImageMemory = vkBindImageMemory,
        .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
        .vkCreateBuffer = vkCreateBuffer,
        .vkDestroyBuffer = vkDestroyBuffer,
        .vkCreateImage = vkCreateImage,
        .vkDestroyImage = vkDestroyImage,
        .vkCmdCopyBuffer = vkCmdCopyBuffer,

    };

    VmaAllocatorCreateInfo allocator_info {
        .physicalDevice = physical_device_,
        .device = device_,
        .pVulkanFunctions = &vma_func,
        .instance = instance_,
    };
    VK_CHECK(vmaCreateAllocator(&allocator_info, &allocator_));

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

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 4096,
        .poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes),
        .pPoolSizes = pool_sizes,
    };
    VK_CHECK(vkCreateDescriptorPool(device_, &pool_info, nullptr, &imgui_descriptor_pool_));

    VkSamplerCreateInfo sampler_info {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 1.0f,
        .minLod = -1000,
        .maxLod = 1000,
    };
    VK_CHECK(vkCreateSampler(device_, &sampler_info, nullptr, &imgui_sampler_));

    ImGui_ImplVulkan_InitInfo init_info {
        .Instance = instance_,
        .PhysicalDevice = physical_device_,
        .Device = device_,
        .QueueFamily = graphics_queue_index_,
        .Queue = graphics_queue_,
        .DescriptorPool = imgui_descriptor_pool_,
        .RenderPass = fb_render_pass_,
        .MinImageCount = VULKAN_BUFFER_SIZE,
        .ImageCount = VULKAN_BUFFER_SIZE,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineCache = VK_NULL_HANDLE,
        .Subpass = 0,
        .Allocator = nullptr,
        .CheckVkResultFn = nullptr,
    };

    return ImGui_ImplVulkan_Init(&init_info);
}

std::shared_ptr<Framebuffer> RendererVK::create_framebuffer(uint32_t width, uint32_t height) {
    std::shared_ptr<FramebufferVK> framebuffer {std::make_shared<FramebufferVK>()};
    FramebufferVK* fb = framebuffer.get();

    VkImageCreateInfo image_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info {
        .usage = VMA_MEMORY_USAGE_UNKNOWN,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImageViewCreateInfo view_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_info.format,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkFramebufferCreateInfo fb_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = fb_render_pass_,
        .attachmentCount = 1,
        .width = width,
        .height = height,
        .layers = 1,
    };

    VkDebugUtilsObjectNameInfoEXT debug_info {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_IMAGE,
        .pObjectName = "Framebuffer",
    };

    for (uint32_t i = 0; i < VULKAN_BUFFER_SIZE; i++) {
        VK_CHECK(vmaCreateImage(allocator_, &image_info, &alloc_info, &fb->image[i],
                                &fb->allocations[i], nullptr));

        debug_info.objectHandle = (uint64_t)fb->image[i];
        vkSetDebugUtilsObjectNameEXT(device_, &debug_info);

        view_info.image = fb->image[i];
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &fb->view[i]));

        fb_info.pAttachments = &fb->view[i];
        VK_CHECK(vkCreateFramebuffer(device_, &fb_info, nullptr, &fb->framebuffer[i]));

        fb->descriptor_set[i] = ImGui_ImplVulkan_AddTexture(
            imgui_sampler_, fb->view[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    fb->width = width;
    fb->height = height;
    fb->resource_disposal = &resource_disposal_;

    return framebuffer;
}

std::shared_ptr<SamplePeaks> RendererVK::create_sample_peaks(const Sample& sample,
                                                             SamplePeaksPrecision precision) {
    return std::shared_ptr<SamplePeaks>();
}

void RendererVK::resize_swapchain() {
    vkDeviceWaitIdle(device_);
    init_swapchain_();
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
    resource_disposal_.flush(device_, allocator_, frame_id_);
    vkResetCommandPool(device_, cmd_buf.cmd_pool, 0);
    vkBeginCommandBuffer(cmd_buf.cmd_buffer, &begin_info);

    ImGui_ImplVulkan_NewFrame();

    current_frame_sync_ = &frame_sync;
    current_cb_ = cmd_buf.cmd_buffer;

    //Log::debug("Begin frame: {}", frame_id_);
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
    resource_disposal_.current_frame_id = frame_id_;
}

void RendererVK::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) {
}

void RendererVK::begin_draw(const std::shared_ptr<Framebuffer>& framebuffer,
                            const ImVec4& clear_color) {
    FramebufferVK* fb =
        (!framebuffer) ? &main_framebuffer_ : static_cast<FramebufferVK*>(framebuffer.get());

    fb->image_id = (fb->image_id + 1) % VULKAN_BUFFER_SIZE;
    uint32_t image_id = fb->image_id;

    VkClearValue vk_clear_color {
        .color = {clear_color.x, clear_color.y, clear_color.z, clear_color.w},
    };

    VkRenderPassBeginInfo rp_begin {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = fb_render_pass_,
        .framebuffer = fb->framebuffer[image_id],
        .renderArea = {0, 0, fb->width, fb->height},
        .clearValueCount = 1,
        .pClearValues = &vk_clear_color,
    };

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = fb->current_access[image_id].access;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = fb->current_access[image_id].layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = graphics_queue_index_;
    barrier.dstQueueFamilyIndex = graphics_queue_index_;
    barrier.image = fb->image[image_id];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(current_cb_, fb->current_access[image_id].stages,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);

    vkCmdBeginRenderPass(current_cb_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    current_framebuffer_ = fb;
}

void RendererVK::finish_draw() {
    vkCmdEndRenderPass(current_cb_);

    if (current_framebuffer_->window_framebuffer) {
        VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags dst_access = 0;
        VkImageLayout new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = graphics_queue_index_;
        barrier.dstQueueFamilyIndex = graphics_queue_index_;
        barrier.image = current_framebuffer_->image[frame_id_];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(current_cb_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, dst_stage,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    } else {
        uint32_t image_id = current_framebuffer_->image_id;
        auto& img_access = current_framebuffer_->current_access[image_id];
        img_access.stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        img_access.access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        img_access.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    current_framebuffer_ = nullptr;
}

void RendererVK::clear(float r, float g, float b, float a) {
}

ImTextureID RendererVK::prepare_as_imgui_texture(const std::shared_ptr<Framebuffer>& framebuffer) {
    FramebufferVK* fb = static_cast<FramebufferVK*>(framebuffer.get());
    uint32_t image_id = fb->image_id;
    auto& img_access = fb->current_access[image_id];
    VkPipelineStageFlags dst_stage;
    VkAccessFlags dst_access;
    VkImageLayout new_layout;

    dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dst_access = VK_ACCESS_SHADER_READ_BIT;
    new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (img_access.layout != new_layout) {
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.srcAccessMask = img_access.access;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = img_access.layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = graphics_queue_index_;
        barrier.dstQueueFamilyIndex = graphics_queue_index_;
        barrier.image = fb->image[image_id];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(current_cb_, img_access.stages, dst_stage, 0, 0, nullptr, 0, nullptr,
                             1, &barrier);

        img_access.stages = dst_stage;
        img_access.access = dst_access;
        img_access.layout = new_layout;
    }

    return (ImTextureID)fb->descriptor_set[fb->image_id];
}

void RendererVK::draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) {
}

void RendererVK::render_draw_data(ImDrawData* draw_data) {
    ImGui_ImplVulkan_RenderDrawData(draw_data, current_cb_);
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

    auto swapchain_result = swapchain_builder.set_old_swapchain(swapchain_)
                                .set_required_min_image_count(2)
                                .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM})
                                .build();

    if (!swapchain_result) {
        Log::error("Failed to find suitable Vulkan device");
        return false;
    }

    if (swapchain_) {
        for (int i = 0; i < VULKAN_BUFFER_SIZE; i++) {
            FrameSync& frame_sync = frame_sync_[i];
            vkDestroyFramebuffer(device_, main_framebuffer_.framebuffer[i], nullptr);
            vkDestroyImageView(device_, main_framebuffer_.view[i], nullptr);
            vkDestroyFence(device_, frame_sync.fence, nullptr);
            vkDestroySemaphore(device_, frame_sync.image_acquire_semaphore, nullptr);
            vkDestroySemaphore(device_, frame_sync.render_finished_semaphore, nullptr);
        }
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        frame_id_ = 0;
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
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                                   &frame_sync.image_acquire_semaphore));
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                                   &frame_sync.render_finished_semaphore));
    }

    return true;
}

Renderer* RendererVK::create(App* app) {
    if (VK_FAILED(volkInitialize()))
        return nullptr;

    auto inst_ret = vkb::InstanceBuilder()
                        .set_app_name("wb_vulkan")
                        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                        .request_validation_layers()
                        .desire_api_version(VKB_VK_API_VERSION_1_1)
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

    if (!ImGui_ImplSDL2_InitForVulkan(window)) {
        vkb::destroy_device(device);
        vkb::destroy_instance(instance);
        return nullptr;
    }

    struct LoadFunctionUserdata {
        VkInstance instance;
        VkDevice device;
    };

    LoadFunctionUserdata userdata {instance.instance, vulkan_device};

    ImGui_ImplVulkan_LoadFunctions(
        [](const char* name, void* userdata) {
            LoadFunctionUserdata* fn_userdata = (LoadFunctionUserdata*)userdata;
            PFN_vkVoidFunction fn = vkGetDeviceProcAddr(fn_userdata->device, name);
            if (!fn)
                fn = vkGetInstanceProcAddr(fn_userdata->instance, name);
            return fn;
        },
        &userdata);

    RendererVK* renderer = new (std::nothrow) RendererVK(instance, physical_device, device, surface,
                                                         graphics_queue_index, present_queue_index);

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