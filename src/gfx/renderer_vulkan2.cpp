#include "renderer_vulkan2.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include <SDL_syswm.h>
#include <imgui_impl_sdl2.h>

namespace wb {

static constexpr GPUTextureAccessVK get_texture_access(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return {
                .stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                .mask = 0,
                .layout = VK_IMAGE_LAYOUT_UNDEFINED,
            };
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return {
                .stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return {
                .stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .mask = VK_ACCESS_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return {
                .stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                .mask = 0,
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            };
    }
    return {};
}

VkResult GPUViewportDataVK::acquire(VkDevice device) {
    GPUTextureVK* rt = static_cast<GPUTextureVK*>(render_target);
    VkResult err =
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_acquire_semaphore[sync_id], nullptr, &rt->active_id);
    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VkDescriptorSet GPUDescriptorStreamVK::allocate_descriptor_set(VkDevice device, VkDescriptorSetLayout layout,
                                                               uint32_t num_storage_buffers,
                                                               uint32_t num_sampled_images) {
    if (current_chunk == nullptr) {
        // First use case
        chunk_list[current_frame_id] = create_chunk(device, 64, 64);
        assert(chunk_list[current_frame_id]);
        current_chunk = chunk_list[current_frame_id];
    } else {
        // Create new or use next existing chunk if there is not enough storage to allocate new
        // descriptor set
        uint32_t free_storage_buffers = current_chunk->max_descriptors - current_chunk->num_storage_buffers;
        uint32_t free_sampled_images = current_chunk->max_descriptors - current_chunk->num_sampled_images;
        uint32_t free_descriptor_sets = current_chunk->max_descriptor_sets - current_chunk->num_descriptor_sets;

        if (num_storage_buffers > free_storage_buffers || num_sampled_images > free_sampled_images ||
            free_descriptor_sets == 0) {
            if (current_chunk->next == nullptr) {
                const uint32_t max_descriptor_sets =
                    current_chunk->max_descriptor_sets + current_chunk->max_descriptor_sets / 2;
                const uint32_t max_descriptors = current_chunk->max_descriptors + current_chunk->max_descriptors / 2;
                GPUDescriptorStreamChunkVK* new_chunk = create_chunk(device, max_descriptor_sets, max_descriptors);
                assert(new_chunk);
                current_chunk->next = new_chunk;
                current_chunk = new_chunk;
            } else {
                current_chunk = current_chunk->next;
            }
        }
    }

    VkDescriptorSetAllocateInfo alloc_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = current_chunk->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };

    VkDescriptorSet descriptor_set;
    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set));

    current_chunk->num_storage_buffers += num_storage_buffers;
    current_chunk->num_sampled_images += num_sampled_images;
    current_chunk->num_descriptor_sets++;

    return descriptor_set;
}

GPUDescriptorStreamChunkVK* GPUDescriptorStreamVK::create_chunk(VkDevice device, uint32_t max_descriptor_sets,
                                                                uint32_t max_descriptors) {
    GPUDescriptorStreamChunkVK* chunk = (GPUDescriptorStreamChunkVK*)std::malloc(sizeof(GPUDescriptorStreamChunkVK));

    if (chunk == nullptr)
        return {};

    VkDescriptorPoolSize pool_sizes[2];
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[0].descriptorCount = max_descriptors;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    pool_sizes[1].descriptorCount = max_descriptors;

    VkDescriptorPoolCreateInfo pool_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = max_descriptor_sets,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
    };

    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool));

    chunk->pool = pool;
    chunk->max_descriptors = max_descriptors;
    chunk->num_uniform_buffers = 0;
    chunk->num_storage_buffers = 0;
    chunk->num_sampled_images = 0;
    chunk->num_storage_images = 0;
    chunk->max_descriptor_sets = max_descriptor_sets;
    chunk->num_descriptor_sets = 0;
    chunk->next = nullptr;

    return chunk;
}

void GPUDescriptorStreamVK::reset(VkDevice device, uint32_t frame_id) {
    current_frame_id = frame_id;

    GPUDescriptorStreamChunkVK* chunk = chunk_list[current_frame_id];
    while (chunk != nullptr) {
        vkResetDescriptorPool(device, chunk->pool, 0);
        chunk->num_uniform_buffers = 0;
        chunk->num_storage_buffers = 0;
        chunk->num_sampled_images = 0;
        chunk->num_storage_images = 0;
        chunk->num_descriptor_sets = 0;
        chunk = chunk->next;
    }

    current_chunk = chunk_list[current_frame_id];
}

void GPUDescriptorStreamVK::destroy(VkDevice device) {
    for (auto chunk : chunk_list) {
        while (chunk) {
            GPUDescriptorStreamChunkVK* chunk_to_destroy = chunk;
            vkDestroyDescriptorPool(device, chunk->pool, nullptr);
            chunk = chunk->next;
            std::free(chunk_to_destroy);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GPURendererVK::GPURendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                             VkSurfaceKHR main_surface, uint32_t graphics_queue_index, uint32_t present_queue_index) :
    instance_(instance),
    physical_device_(physical_device),
    device_(device),
    main_surface_(main_surface),
    graphics_queue_index_(graphics_queue_index),
    present_queue_index_(present_queue_index) {
    vkGetDeviceQueue(device_, graphics_queue_index_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_queue_index_, 0, &present_queue_);
}

bool GPURendererVK::init(SDL_Window* window) {
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
    VK_CHECK(vkCreateSampler(device_, &sampler_info, nullptr, &common_sampler_));

    VkCommandPoolCreateInfo pool_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_index_,
    };

    VkCommandBufferAllocateInfo cmd_buf_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < num_inflight_frames_; i++) {
        VK_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &cmd_pool_[i]));
        cmd_buf_info.commandPool = cmd_pool_[i];
        VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &cmd_buf_[i]));
        VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &fences_[i]));
    }

    VK_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &imm_cmd_pool_));
    cmd_buf_info.commandPool = imm_cmd_pool_;
    VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &imm_cmd_buf_));

    GPUViewportDataVK* main_viewport = new GPUViewportDataVK();
    main_viewport->surface = main_surface_;
    create_or_recreate_swapchain_(main_viewport);
    viewports.push_back(main_viewport);
    main_vp = main_viewport;

    return true;
}

void GPURendererVK::shutdown() {
    vkDeviceWaitIdle(device_);

    for (uint32_t i = 0; i < num_inflight_frames_; i++) {
        vkDestroyFence(device_, fences_[i], nullptr);
        vkDestroyCommandPool(device_, cmd_pool_[i], nullptr);
    }

    for (auto viewport : viewports) {
        if (viewport->viewport) {
            viewport->viewport->RendererUserData = nullptr;
            viewport->viewport = nullptr;
        }
        dispose_viewport_data_(viewport, viewport->surface);
        delete viewport;
    }

    dispose_resources_(~0ull);

    if (cmd_pool_)
        vkDestroyCommandPool(device_, imm_cmd_pool_, nullptr);
    if (common_sampler_)
        vkDestroySampler(device_, common_sampler_, nullptr);
    if (fb_render_pass_)
        vkDestroyRenderPass(device_, fb_render_pass_, nullptr);
    if (main_surface_)
        vkDestroySurfaceKHR(instance_, main_surface_, nullptr);
    if (device_)
        vkDestroyDevice(device_, nullptr);
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}

GPUBuffer* GPURendererVK::create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, size_t init_size,
                                        void* init_data) {
    return nullptr;
}

GPUTexture* GPURendererVK::create_texture(GPUTextureUsageFlags usage, uint32_t w, uint32_t h, size_t init_size) {
    return nullptr;
}

GPUPipeline* GPURendererVK::create_pipeline(const GPUPipelineDesc& desc) {
    if (auto& set_layout = texture_set_layout[desc.num_texture_resources]; !set_layout) {
        texture_set_layout[desc.num_texture_resources].emplace();
    }
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

void GPURendererVK::resize_viewport(ImGuiViewport* viewport, ImVec2 vec) {
    if (GImGui->Viewports[0] == viewport) {
        static_cast<GPUViewportDataVK*>(main_vp)->need_rebuild = true;
        return;
    }
    GPUViewportDataVK* vp_data = static_cast<GPUViewportDataVK*>(viewport->RendererUserData);
    vp_data->need_rebuild = true;
}

void GPURendererVK::begin_frame() {
    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkWaitForFences(device_, 1, &fences_[frame_id], VK_TRUE, UINT64_MAX);

    for (auto viewport : added_viewports)
        viewports.push_back(viewport);

    bool been_waiting = false;
    for (auto viewport : viewports) {
        if (viewport->need_rebuild) {
            if (!been_waiting)
                vkQueueWaitIdle(present_queue_);
            create_or_recreate_swapchain_(viewport);
        }
        viewport->acquire(device_);
    }

    dispose_resources_(frame_count_);
    vkResetCommandPool(device_, cmd_pool_[frame_id], 0);
    vkBeginCommandBuffer(cmd_buf_[frame_id], &begin_info);
    current_cb_ = cmd_buf_[frame_id];
    cmd_private_data = current_cb_;
    clear_state();
}

void GPURendererVK::end_frame() {
    // NOTE(native-m): use arena allocator to allocate temporary data
    for (auto viewport : viewports) {
        GPUTextureVK* rt = static_cast<GPUTextureVK*>(viewport->render_target);
        uint32_t sync_id = viewport->sync_id;
        uint32_t image_id = rt->active_id;
        if (auto layout = rt->layout[image_id]; layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            GPUTextureAccessVK src_access = get_texture_access(layout);
            constexpr GPUTextureAccessVK dst_access = get_texture_access(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            VkImageMemoryBarrier barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = src_access.mask,
                .dstAccessMask = dst_access.mask,
                .oldLayout = src_access.layout,
                .newLayout = dst_access.layout,
                .srcQueueFamilyIndex = graphics_queue_index_,
                .dstQueueFamilyIndex = graphics_queue_index_,
                .image = rt->image[image_id],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(current_cb_, src_access.stages, dst_access.stages, 0, 0, nullptr, 0, nullptr, 1,
                                 &barrier);
        }
        image_acquired_semaphore.push_back(viewport->image_acquire_semaphore[viewport->sync_id]);
        swapchain_image_wait_stage.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        viewport->sync_id = (sync_id + 1) % viewport->num_sync;
    }

    vkEndCommandBuffer(current_cb_);

    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = (uint32_t)image_acquired_semaphore.size(),
        .pWaitSemaphores = image_acquired_semaphore.data(),
        .pWaitDstStageMask = swapchain_image_wait_stage.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &current_cb_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished_semaphore_[frame_id],
    };
    vkResetFences(device_, 1, &fences_[frame_id]);
    vkQueueSubmit(graphics_queue_, 1, &submit, fences_[frame_id]);

    while (auto resource = static_cast<GPUResource*>(active_resources_list_.pop_next_item())) {
        resource->active_id = (resource->active_id + 1) % num_inflight_frames_;
    }

    frame_id = (frame_id + 1) % num_inflight_frames_;
    frame_count_++;
    image_acquired_semaphore.resize(0);
    swapchain_image_wait_stage.resize(0);
}

void GPURendererVK::present() {
    // NOTE(native-m): use arena allocator to allocate temporary data
    for (auto viewport : viewports) {
        GPUTextureVK* rt = static_cast<GPUTextureVK*>(viewport->render_target);
        swapchain_present.push_back(viewport->swapchain);
        sc_image_index_present.push_back(rt->active_id);
    }
    swapchain_results.resize(viewports.size());

    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished_semaphore_[frame_id],
        .swapchainCount = (uint32_t)swapchain_present.size(),
        .pSwapchains = swapchain_present.data(),
        .pImageIndices = sc_image_index_present.data(),
        .pResults = swapchain_results.data(),
    };
    vkQueuePresentKHR(graphics_queue_, &present_info);

    for (uint32_t i = 0; i < (uint32_t)viewports.size(); i++) {
        VkResult result = swapchain_results[i];
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            viewports[i]->need_rebuild = true;
    }

    swapchain_present.resize(0);
    swapchain_results.resize(0);
    sc_image_index_present.resize(0);
}

void* GPURendererVK::map_buffer(GPUBuffer* buffer) {
    return nullptr;
}

void GPURendererVK::unmap_buffer(GPUBuffer* buffer) {
}

void GPURendererVK::begin_render(GPUTexture* render_target, const ImVec4& clear_color) {
    assert(!inside_render_pass);
    GPUTextureVK* rt = static_cast<GPUTextureVK*>(render_target);
    uint32_t image_id = rt->active_id;

    VkClearValue vk_clear_color {
        .color = {clear_color.x, clear_color.y, clear_color.z, clear_color.w},
    };

    VkRenderPassBeginInfo rp_begin {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = fb_render_pass_,
        .framebuffer = rt->fb[image_id],
        .renderArea = {0, 0, rt->width, rt->height},
        .clearValueCount = 1,
        .pClearValues = &vk_clear_color,
    };

    if (rt->layout[image_id] != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        GPUTextureAccessVK src_access = get_texture_access(rt->layout[image_id]);
        constexpr GPUTextureAccessVK dst_access = get_texture_access(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkImageMemoryBarrier barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = src_access.mask,
            .dstAccessMask = dst_access.mask,
            .oldLayout = src_access.layout,
            .newLayout = dst_access.layout,
            .srcQueueFamilyIndex = graphics_queue_index_,
            .dstQueueFamilyIndex = graphics_queue_index_,
            .image = rt->image[image_id],
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        vkCmdPipelineBarrier(current_cb_, src_access.stages, dst_access.stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkCmdBeginRenderPass(current_cb_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    if (!render_target->is_connected_to_list())
        active_resources_list_.push_item(render_target);

    inside_render_pass = true;
}

void GPURendererVK::end_render() {
    vkCmdEndRenderPass(current_cb_);
    inside_render_pass = false;
}

void GPURendererVK::set_shader_parameter(size_t size, const void* data) {
}

void GPURendererVK::flush_state() {
    VkCommandBuffer cb = current_cb_;

    if (dirty_flags.pipeline) {
        GPUPipelineVK* pipeline = static_cast<GPUPipelineVK*>(current_pipeline);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    }

    if (uint32_t dirty_bits = dirty_flags.texture) {
        VkDescriptorImageInfo descriptor[4];
        VkWriteDescriptorSet write_set[4];
        VkImageMemoryBarrier barriers[4];
        uint32_t num_updates = 0;
        uint32_t num_barriers = 0;

        while (dirty_bits) {
            int slot = next_set_bits(dirty_bits);
            GPUTextureVK* tex = static_cast<GPUTextureVK*>(current_texture[slot]);
            uint32_t active_id = tex->active_id;

            descriptor[num_updates] = {
                .sampler = common_sampler_,
                .imageView = tex->view[active_id],
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            write_set[num_updates] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = VK_NULL_HANDLE,
                .dstBinding = WB_VULKAN_IMAGE_DESCRIPTOR_SET_SLOT,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &descriptor[num_updates],
            };

            num_updates++;

            if (auto layout = tex->layout[active_id]; layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                GPUTextureAccessVK src_access = get_texture_access(layout);
                constexpr GPUTextureAccessVK dst_access = get_texture_access(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                barriers[num_barriers] = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = src_access.mask,
                    .dstAccessMask = dst_access.mask,
                    .oldLayout = src_access.layout,
                    .newLayout = dst_access.layout,
                    .srcQueueFamilyIndex = graphics_queue_index_,
                    .dstQueueFamilyIndex = graphics_queue_index_,
                    .image = tex->image[active_id],
                    .subresourceRange =
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                };
                num_barriers++;
            }
        }

        if (num_updates > 0) {
            // descriptor_stream.allocate_descriptor_set(device_, )
        }
    }

    if (uint32_t dirty_bits = dirty_flags.storage_buf) {
        while (dirty_bits) {
            int slot = next_set_bits(dirty_bits);
        }
    }

    if (dirty_flags.vtx_buf) {
        GPUBufferVK* vtx_buf = static_cast<GPUBufferVK*>(current_vtx_buf);
        VkDeviceSize vtx_offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vtx_buf->buffer[vtx_buf->active_id], &vtx_offset);
        if (!vtx_buf->is_connected_to_list())
            active_resources_list_.push_item(vtx_buf);
    }

    if (dirty_flags.idx_buf) {
        GPUBufferVK* idx_buf = static_cast<GPUBufferVK*>(current_idx_buf);
        vkCmdBindIndexBuffer(cb, idx_buf->buffer[idx_buf->active_id], 0, VK_INDEX_TYPE_UINT32);
        if (!idx_buf->is_connected_to_list())
            active_resources_list_.push_item(idx_buf);
    }

    if (dirty_flags.scissor) {
        VkRect2D scissor {
            .offset = {sc_x, sc_y},
            .extent = {(uint32_t)sc_w, (uint32_t)sc_h},
        };
        vkCmdSetScissor(cb, 0, 1, &scissor);
    }

    if (dirty_flags.vp) {
        VkViewport viewport {
            .x = vp_x,
            .y = vp_y,
            .width = vp_w,
            .height = vp_h,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cb, 0, 1, &viewport);
    }

    dirty_flags.u32 = 0;
}

bool GPURendererVK::create_or_recreate_swapchain_(GPUViewportDataVK* vp_data) {
    VkSurfaceKHR surface = vp_data->surface;
    VkBool32 surface_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, graphics_queue_index_, surface, &surface_supported);

    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface, &surface_caps);

    if (surface_caps.minImageCount > 2) {
        return false;
    }

    if (!has_bit(surface_caps.supportedUsageFlags, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        return false;
    }

    VkPresentModeKHR present_modes[6] {};
    uint32_t present_mode_count = 6;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface, &present_mode_count, present_modes);

    VkSwapchainCreateInfoKHR swapchain_info {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = WB_GPU_RENDER_BUFFER_SIZE,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = surface_caps.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_FALSE,
        .oldSwapchain = vp_data->swapchain,
    };

    VkSwapchainKHR vk_swapchain = vp_data->swapchain;
    if (vk_swapchain != VK_NULL_HANDLE)
        dispose_viewport_data_(vp_data, nullptr);

    VkResult result = vkCreateSwapchainKHR(device_, &swapchain_info, nullptr, &vk_swapchain);
    if (VK_FAILED(result))
        return false;

    VkFramebufferCreateInfo fb_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = fb_render_pass_,
        .attachmentCount = 1,
        .width = surface_caps.currentExtent.width,
        .height = surface_caps.currentExtent.height,
        .layers = 1,
    };

    VkImageViewCreateInfo view_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain_info.imageFormat,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkSemaphoreCreateInfo semaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkDebugUtilsObjectNameInfoEXT debug_info {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_IMAGE,
    };

    GPUTextureVK* render_target;
    if (vp_data->render_target == nullptr) {
        void* rt_mem = texture_pool_.allocate();
        GPUTextureVK* texture = new (rt_mem) GPUTextureVK();
        texture->window_framebuffer = true;
        texture->num_buffers = num_inflight_frames_;
        vp_data->render_target = render_target = texture;
    } else {
        render_target = static_cast<GPUTextureVK*>(vp_data->render_target);
    }

    render_target->parent_viewport = vp_data;
    render_target->active_id = 0;
    render_target->width = fb_info.width;
    render_target->height = fb_info.height;
    vp_data->surface = surface;
    vp_data->swapchain = vk_swapchain;
    vp_data->num_sync = num_inflight_frames_;
    vp_data->sync_id = 0;

    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(device_, vk_swapchain, &swapchain_image_count, nullptr);
    vkGetSwapchainImagesKHR(device_, vk_swapchain, &swapchain_image_count, render_target->image);

    for (uint32_t i = 0; i < num_inflight_frames_; i++) {
        VK_CHECK(vkCreateSemaphore(device_, &semaphore, nullptr, &vp_data->image_acquire_semaphore[i]));
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &render_target->view[i]));
        fb_info.pAttachments = &render_target->view[i];
        VK_CHECK(vkCreateFramebuffer(device_, &fb_info, nullptr, &render_target->fb[i]));
    }

    return true;
}

void GPURendererVK::dispose_buffer_(GPUBufferVK* buffer) {
    std::scoped_lock lock(mtx_);
    for (uint32_t i = 0; i < buffer->num_buffers; i++) {
        GPUResourceDisposeItemVK& buf = resource_disposal_.emplace_back();
        buf.type = GPUResourceDisposeItemVK::Buffer;
        buf.frame_stamp = frame_count_;
        buf.buffer = {
            .buffer = (VkBuffer)buffer->buffer[i],
            .allocation = buffer->allocation[i],
        };
    }
}

void GPURendererVK::dispose_texture_(GPUTextureVK* texture) {
    std::scoped_lock lock(mtx_);
    for (uint32_t i = 0; i < texture->num_buffers; i++) {
        GPUResourceDisposeItemVK& tex = resource_disposal_.emplace_back();
        tex.type = GPUResourceDisposeItemVK::Texture;
        tex.frame_stamp = frame_count_;
        tex.texture = {
            .image = texture->image[i],
            .view = texture->view[i],
            .fb = texture->fb[i],
            .allocation = texture->allocation[i],
        };
    }
}

void GPURendererVK::dispose_viewport_data_(GPUViewportDataVK* vp_data, VkSurfaceKHR surface) {
    std::scoped_lock lock(mtx_);
    GPUTextureVK* vk_texture = static_cast<GPUTextureVK*>(vp_data->render_target);
    for (uint32_t i = 0; i < vk_texture->num_buffers; i++) {
        GPUResourceDisposeItemVK& tex = resource_disposal_.emplace_back();
        tex.type = GPUResourceDisposeItemVK::Texture;
        tex.frame_stamp = frame_count_;
        tex.texture = {
            .view = vk_texture->view[i],
            .fb = vk_texture->fb[i],
        };
    }
    for (uint32_t i = 0; i < vp_data->num_sync; i++) {
        GPUResourceDisposeItemVK& sync = resource_disposal_.emplace_back();
        sync.type = GPUResourceDisposeItemVK::SyncObject;
        sync.frame_stamp = frame_count_;
        sync.sync_obj.semaphore = vp_data->image_acquire_semaphore[i];
    }
    GPUResourceDisposeItemVK& swapchain = resource_disposal_.emplace_back();
    swapchain.type = GPUResourceDisposeItemVK::Swapchain;
    swapchain.frame_stamp = frame_count_;
    swapchain.swapchain = {
        .swapchain = vp_data->swapchain,
        .surface = vp_data->surface,
    };
}

void GPURendererVK::dispose_resources_(uint64_t frame_count) {
    std::scoped_lock lock(mtx_);
    while (!resource_disposal_.empty()) {
        auto& item = resource_disposal_.front();
        if (item.frame_stamp + num_inflight_frames_ < frame_count) {
            switch (item.type) {
                case GPUResourceDisposeItemVK::Buffer:
                    vmaDestroyBuffer(allocator_, item.buffer.buffer, item.buffer.allocation);
                    break;
                case GPUResourceDisposeItemVK::Texture:
                    if (item.texture.fb)
                        vkDestroyFramebuffer(device_, item.texture.fb, nullptr);
                    vkDestroyImageView(device_, item.texture.view, nullptr);
                    if (item.texture.image && item.texture.allocation)
                        vmaDestroyImage(allocator_, item.texture.image, item.texture.allocation);
                    break;
                case GPUResourceDisposeItemVK::Swapchain:
                    vkDestroySwapchainKHR(device_, item.swapchain.swapchain, nullptr);
                    if (item.swapchain.surface)
                        vkDestroySurfaceKHR(instance_, item.swapchain.surface, nullptr);
                    break;
                case GPUResourceDisposeItemVK::SyncObject:
                    vkDestroySemaphore(device_, item.sync_obj.semaphore, nullptr);
                    break;
            }
            resource_disposal_.pop_front();
        } else {
            break;
        }
    }
}

GPURenderer* GPURendererVK::create(SDL_Window* window) {
    if (!window)
        return nullptr;

    if (!ImGui_ImplSDL2_InitForOther(window))
        return nullptr;

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
#ifdef VK_USE_PLATFORM_XLIB_KHR
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
#else
    VkXcbSurfaceCreateInfoKHR surface_info {
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = XGetXCBConnection(wm_info.info.x11.display),
        .window = static_cast<xcb_window_t>(wm_info.info.x11.window),
    };

    if (VK_FAILED(vkCreateXcbSurfaceKHR(instance, &surface_info, nullptr, &surface))) {
        Log::error("Failed to create window surface");
        vkb::destroy_instance(instance);
        return nullptr;
    }
#endif
#endif

    Vector<VkQueueFamilyProperties> queue_families;
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(selected_physical_device, &queue_family_count, nullptr);
    queue_families.resize(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(selected_physical_device, &queue_family_count, queue_families.data());

    // Find graphics queue and presentation queue
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

    assert(graphics_queue_index == present_queue_index && "Separate presentation queue is not supported at the moment");

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

    volkLoadDevice(device);

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