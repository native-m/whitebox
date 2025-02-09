#include "renderer_vulkan2.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include "core/defer.h"
#include "platform/platform.h"
#include <SDL_syswm.h>
#include <imgui_impl_sdl2.h>

#define WB_LOG_VULKAN_RESOURCE_DISPOSAL 1

namespace wb {

static VkFormat get_vk_format(GPUFormat format) {
    switch (format) {
        case GPUFormat::UnormR8G8B8A8:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case GPUFormat::UnormB8G8R8A8:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case GPUFormat::FloatR32G32:
            return VK_FORMAT_R32G32_SFLOAT;
        case GPUFormat::FloatR32G32B32:
            return VK_FORMAT_R32G32B32_SFLOAT;
        default:
            WB_UNREACHABLE();
    }
}

static VkPrimitiveTopology get_vk_primitive_topology(GPUPrimitiveTopology topology) {
    switch (topology) {
        case GPUPrimitiveTopology::TriangleList:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case GPUPrimitiveTopology::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case GPUPrimitiveTopology::LineList:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case GPUPrimitiveTopology::LineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        default:
            WB_UNREACHABLE();
    }
}

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
                .mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
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
        default:
            break;
    }
    return {};
}

static VkRenderPass create_render_pass_for_format(VkDevice device, GPUFormat format) {
    VkAttachmentDescription att_desc {
        .format = get_vk_format(format),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
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
        .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask =
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    };

    VkRenderPass render_pass;
    VkRenderPassCreateInfo rp_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att_desc,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpass_dependency,
    };
    if (VK_FAILED(vkCreateRenderPass(device, &rp_info, nullptr, &render_pass)))
        return VK_NULL_HANDLE;

    return render_pass;
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
        chunk_list[current_frame_id] = create_chunk(device, 64, 512);
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
    chunk->num_storage_buffers = 0;
    chunk->num_sampled_images = 0;
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
        chunk->num_storage_buffers = 0;
        chunk->num_sampled_images = 0;
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
    draw_fn = (DrawFn)vkCmdDraw;
    draw_indexed_fn = (DrawIndexedFn)vkCmdDrawIndexed;
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

    VkBufferCreateInfo staging_buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 0x10000,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VmaAllocationCreateInfo staging_buffer_alloc_info {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };

    uint32_t memory_type_index;
    VK_CHECK(vmaFindMemoryTypeIndexForBufferInfo(allocator_, &staging_buffer_info, &staging_buffer_alloc_info,
                                                 &memory_type_index));

    VmaPoolCreateInfo staging_pool_info {
        .memoryTypeIndex = memory_type_index,
        .flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
    };
    VK_CHECK(vmaCreatePool(allocator_, &staging_pool_info, &staging_pool_));

    fb_rp_rgba_ = create_render_pass_for_format(device_, GPUFormat::UnormR8G8B8A8);
    fb_rp_bgra_ = create_render_pass_for_format(device_, GPUFormat::UnormB8G8R8A8);
    assert(fb_rp_rgba_ && fb_rp_bgra_);

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

    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VkSemaphoreCreateInfo semaphore_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    for (uint32_t i = 0; i < num_sync_; i++)
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_semaphore_[i]));

    for (uint32_t i = 0; i < num_inflight_frames_; i++) {
        VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &fences_[i]));
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &upload_finished_semaphore_[i]));
        VK_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &cmd_pool_[i]));
        VK_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &upload_cmd_pool_[i]));
        cmd_buf_info.commandPool = cmd_pool_[i];
        VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &cmd_buf_[i]));
        cmd_buf_info.commandPool = upload_cmd_pool_[i];
        VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &upload_cmd_buf_[i]));
    }

    VkDescriptorSetLayoutBinding bindings[4];
    for (uint32_t i = 0; auto& binding : bindings) {
        binding = {
            .binding = i++,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }

    VkDescriptorSetLayoutCreateInfo layout_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings = bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &texture_set_layout_));

    for (auto& binding : bindings)
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    VK_CHECK(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &storage_buffer_set_layout_));

    GPUViewportDataVK* main_viewport = new GPUViewportDataVK();
    main_viewport->surface = main_surface_;
    create_or_recreate_swapchain_(main_viewport);
    viewports.push_back(main_viewport);
    main_vp = main_viewport;

    return GPURenderer::init(window);
}

void GPURendererVK::shutdown() {
    GPURenderer::shutdown();
    vkDeviceWaitIdle(device_);

    for (uint32_t i = 0; i < num_sync_; i++) {
        vkDestroySemaphore(device_, render_finished_semaphore_[i], nullptr);
    }

    for (uint32_t i = 0; i < num_inflight_frames_; i++) {
        vkDestroySemaphore(device_, upload_finished_semaphore_[i], nullptr);
        vkDestroyFence(device_, fences_[i], nullptr);
        vkDestroyCommandPool(device_, cmd_pool_[i], nullptr);
        vkDestroyCommandPool(device_, upload_cmd_pool_[i], nullptr);
    }

    for (auto viewport : viewports) {
        if (viewport->viewport) {
            viewport->viewport->RendererUserData = nullptr;
            viewport->viewport = nullptr;
        }
        dispose_viewport_data_(viewport, viewport->surface);
        texture_pool_.destroy(static_cast<GPUTextureVK*>(viewport->render_target));
        delete viewport;
    }

    descriptor_stream_.destroy(device_);
    dispose_resources_(~0ull);

    if (staging_pool_)
        vmaDestroyPool(allocator_, staging_pool_);
    if (allocator_)
        vmaDestroyAllocator(allocator_);
    if (texture_set_layout_)
        vkDestroyDescriptorSetLayout(device_, texture_set_layout_, nullptr);
    if (storage_buffer_set_layout_)
        vkDestroyDescriptorSetLayout(device_, storage_buffer_set_layout_, nullptr);
    if (common_sampler_)
        vkDestroySampler(device_, common_sampler_, nullptr);
    if (fb_rp_bgra_)
        vkDestroyRenderPass(device_, fb_rp_bgra_, nullptr);
    if (fb_rp_rgba_)
        vkDestroyRenderPass(device_, fb_rp_rgba_, nullptr);
    if (device_)
        vkDestroyDevice(device_, nullptr);
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}

GPUBuffer* GPURendererVK::create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, bool dedicated_allocation,
                                        size_t init_size, const void* init_data) {
    void* buffer_ptr = buffer_pool_.allocate();
    if (!buffer_ptr)
        return nullptr;

    bool cpu_access = false;
    VkBufferUsageFlags vk_usage = 0;
    if (contain_bit(usage, GPUBufferUsage::Vertex))
        vk_usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (contain_bit(usage, GPUBufferUsage::Index))
        vk_usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (contain_bit(usage, GPUBufferUsage::Storage))
        vk_usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (contain_bit(usage, GPUBufferUsage::CPUAccessible))
        cpu_access = true;

    VkBufferCreateInfo buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = vk_usage,
    };

    VmaAllocationCreateInfo allocation_info {
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    if (cpu_access) {
        allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        allocation_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (contain_bit(usage, GPUBufferUsage::Writeable))
            allocation_info.flags |=
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    } else {
        allocation_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocation_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        buffer_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    if (dedicated_allocation)
        allocation_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    GPUBufferVK* new_buffer = new (buffer_ptr) GPUBufferVK();
    new_buffer->usage = usage;
    new_buffer->size = buffer_size;

    VmaAllocationInfo alloc_result;
    if (contain_bit(usage, GPUBufferUsage::Writeable)) {
        new_buffer->num_resources = num_inflight_frames_;
        for (uint32_t i = 0; i < num_inflight_frames_; i++) {
            VkResult result = vmaCreateBuffer(allocator_, &buffer_info, &allocation_info, &new_buffer->buffer[i],
                                              &new_buffer->allocation[i], &alloc_result);
            if (VK_FAILED(result)) {
                for (uint32_t j = 0; j < i; j++)
                    vmaDestroyBuffer(allocator_, new_buffer->buffer[j], new_buffer->allocation[j]);
                buffer_pool_.destroy(new_buffer);
                return nullptr;
            }
            if (cpu_access)
                new_buffer->persistent_map_ptr[i] = alloc_result.pMappedData;
            if (cpu_access && init_data && init_size)
                std::memcpy(alloc_result.pMappedData, init_data, init_size);
        }
    } else {
        VkBuffer buffer;
        VmaAllocation allocation;
        VkResult result = vmaCreateBuffer(allocator_, &buffer_info, &allocation_info, &buffer, &allocation, nullptr);
        if (VK_FAILED(result)) {
            buffer_pool_.destroy(new_buffer);
            return nullptr;
        }

        if (init_data && init_size) {
            if (cpu_access) {
                void* mapped_ptr;
                vmaMapMemory(allocator_, allocation, &mapped_ptr);
                std::memcpy(mapped_ptr, init_data, init_size);
                vmaUnmapMemory(allocator_, allocation);
            } else {
                // Upload the resource indirectly
                enqueue_resource_upload_(buffer, buffer_size, init_data);
            }
        }

        for (uint32_t i = 0; i < num_inflight_frames_; i++) {
            new_buffer->buffer[i] = buffer;
            new_buffer->allocation[i] = allocation;
        }

        new_buffer->num_resources = 1;
    }

    return new_buffer;
}

GPUTexture* GPURendererVK::create_texture(GPUTextureUsageFlags usage, GPUFormat format, uint32_t w, uint32_t h,
                                          bool dedicated_allocation, uint32_t init_w, uint32_t init_h,
                                          const void* init_data) {
    void* texture_ptr = texture_pool_.allocate();
    if (!texture_ptr)
        return nullptr;

    VkImageUsageFlags vk_usage = 0;
    if (contain_bit(usage, GPUTextureUsage::RenderTarget))
        vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (contain_bit(usage, GPUTextureUsage::Sampled))
        vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    GPUTextureVK* new_texture = new (texture_ptr) GPUTextureVK();
    new_texture->usage = usage;
    new_texture->format = format;
    new_texture->width = w;
    new_texture->height = h;

    VkImageCreateInfo image_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = get_vk_format(format),
        .extent = {w, h, 1u},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = vk_usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImageViewCreateInfo image_view {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = get_vk_format(format),
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VmaAllocationCreateInfo allocation_info {
        .usage = VMA_MEMORY_USAGE_UNKNOWN,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    if (dedicated_allocation)
        allocation_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (contain_bit(usage, GPUTextureUsage::RenderTarget)) {
        VkFramebufferCreateInfo fb_info {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = fb_rp_bgra_,
            .attachmentCount = 1,
            .width = w,
            .height = h,
            .layers = 1,
        };

        new_texture->num_resources = num_inflight_frames_;

        for (uint32_t i = 0; i < num_inflight_frames_; i++) {
            VkResult result = vmaCreateImage(allocator_, &image_info, &allocation_info, &new_texture->image[i],
                                             &new_texture->allocation[i], nullptr);
            if (VK_FAILED(result)) {
                for (uint32_t j = 0; j < i; j++)
                    vmaDestroyImage(allocator_, new_texture->image[j], new_texture->allocation[j]);
                texture_pool_.destroy(new_texture);
                return nullptr;
            }

            image_view.image = new_texture->image[i],

            result = vkCreateImageView(device_, &image_view, nullptr, &new_texture->view[i]);
            if (VK_FAILED(result)) {
                for (uint32_t j = 0; j < i; j++) {
                    vkDestroyImageView(device_, new_texture->view[j], nullptr);
                    vmaDestroyImage(allocator_, new_texture->image[j], new_texture->allocation[j]);
                }
                vmaDestroyImage(allocator_, new_texture->image[i], new_texture->allocation[i]);
                texture_pool_.destroy(new_texture);
                return nullptr;
            }

            fb_info.pAttachments = &new_texture->view[i];
            result = vkCreateFramebuffer(device_, &fb_info, nullptr, &new_texture->fb[i]);
            if (VK_FAILED(result)) {
                for (uint32_t j = 0; j < i; j++) {
                    vkDestroyFramebuffer(device_, new_texture->fb[j], nullptr);
                    vkDestroyImageView(device_, new_texture->view[j], nullptr);
                    vmaDestroyImage(allocator_, new_texture->image[j], new_texture->allocation[j]);
                }
                vkDestroyImageView(device_, new_texture->view[i], nullptr);
                vmaDestroyImage(allocator_, new_texture->image[i], new_texture->allocation[i]);
                texture_pool_.destroy(new_texture);
                return nullptr;
            }
        }
    } else {
        VkImage image;
        VkImageView view;
        VmaAllocation allocation;

        VkResult result = vmaCreateImage(allocator_, &image_info, &allocation_info, &image, &allocation, nullptr);
        if (VK_FAILED(result)) {
            texture_pool_.destroy(new_texture);
            return nullptr;
        }

        VkImageViewCreateInfo image_view {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = get_vk_format(format),
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        result = vkCreateImageView(device_, &image_view, nullptr, &view);
        if (VK_FAILED(result)) {
            vmaDestroyImage(allocator_, image, allocation);
            texture_pool_.destroy(new_texture);
            return nullptr;
        }

        VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (init_w && init_h && init_data) {
            initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            enqueue_resource_upload_(image, VK_IMAGE_LAYOUT_UNDEFINED, initial_layout, init_w, init_h, init_data);
        }

        for (uint32_t i = 0; i < num_inflight_frames_; i++) {
            new_texture->image[i] = image;
            new_texture->view[i] = view;
            new_texture->allocation[i] = allocation;
            new_texture->layout[i] = initial_layout;
        }

        new_texture->num_resources = 1;
    }

    return new_texture;
}

GPUPipeline* GPURendererVK::create_pipeline(const GPUPipelineDesc& desc) {
    VkShaderModule vs_module;
    VkShaderModule fs_module;

    VkShaderModuleCreateInfo shader_module_info {};
    shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_info.codeSize = desc.vs_size;
    shader_module_info.pCode = (const uint32_t*)desc.vs;
    if (VK_FAILED(vkCreateShaderModule(device_, &shader_module_info, nullptr, &vs_module)))
        return nullptr;
    defer(vkDestroyShaderModule(device_, vs_module, nullptr));

    shader_module_info.codeSize = desc.fs_size;
    shader_module_info.pCode = (const uint32_t*)desc.fs;
    if (VK_FAILED(vkCreateShaderModule(device_, &shader_module_info, nullptr, &fs_module)))
        return nullptr;
    defer(vkDestroyShaderModule(device_, fs_module, nullptr));

    VkDescriptorSetLayout set_layouts[2] {texture_set_layout_, storage_buffer_set_layout_};
    VkPushConstantRange push_constant_range {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = desc.shader_parameter_size,
    };

    VkPipelineLayout pipeline_layout;
    VkPipelineLayoutCreateInfo pipeline_layout_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    if (VK_FAILED(vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout)))
        return nullptr;

    VkPipelineShaderStageCreateInfo stages[2] {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs_module,
            .pName = "main",
        },
    };

    VkVertexInputBindingDescription vtx_binding {
        .binding = 0,
        .stride = desc.vertex_stride,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription vtx_attrs[8];
    for (uint32_t i = 0; i < desc.num_vertex_attributes; i++) {
        VkVertexInputAttributeDescription& vtx_attr = vtx_attrs[i];
        const GPUVertexAttribute& attribute = desc.vertex_attributes[i];
        vtx_attr.location = attribute.slot;
        vtx_attr.binding = 0;
        vtx_attr.format = get_vk_format(attribute.format);
        vtx_attr.offset = attribute.offset;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = desc.num_vertex_attributes > 0 ? 1u : 0u,
        .pVertexBindingDescriptions = desc.num_vertex_attributes > 0 ? &vtx_binding : nullptr,
        .vertexAttributeDescriptionCount = desc.num_vertex_attributes,
        .pVertexAttributeDescriptions = vtx_attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = get_vk_primitive_topology(desc.primitive_topology),
    };

    VkPipelineViewportStateCreateInfo viewport_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo raster_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState color_attachment {
        .blendEnable = desc.enable_blending,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    if (!desc.enable_color_write)
        color_attachment.colorWriteMask = 0;

    VkPipelineColorBlendStateCreateInfo blend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
    };

    static const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = IM_ARRAYSIZE(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    VkGraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = nullptr;
    pipeline_info.flags = 0;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pTessellationState = nullptr;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &raster_state;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = nullptr;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = fb_rp_bgra_;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = nullptr;
    pipeline_info.basePipelineIndex = 0;

    VkPipeline pipeline;
    if (VK_FAILED(vkCreateGraphicsPipelines(device_, nullptr, 1, &pipeline_info, nullptr, &pipeline)))
        return nullptr;

    GPUPipelineVK* new_pipeline = (GPUPipelineVK*)pipeline_pool_.allocate();
    if (!new_pipeline)
        return nullptr;

    new_pipeline->layout = pipeline_layout;
    new_pipeline->pipeline = pipeline;

    return new_pipeline;
}

void GPURendererVK::destroy_buffer(GPUBuffer* buffer) {
    GPUBufferVK* impl = static_cast<GPUBufferVK*>(buffer);
    dispose_buffer_(impl);
    if (impl->is_connected_to_list())
        impl->remove_from_list();
    buffer_pool_.destroy(impl);
}

void GPURendererVK::destroy_texture(GPUTexture* texture) {
    GPUTextureVK* impl = static_cast<GPUTextureVK*>(texture);
    dispose_texture_(impl);
    if (impl->is_connected_to_list())
        impl->remove_from_list();
    texture_pool_.destroy(impl);
}

void GPURendererVK::destroy_pipeline(GPUPipeline* pipeline) {
    dispose_pipeline_(static_cast<GPUPipelineVK*>(pipeline));
    pipeline_pool_.free(pipeline);
}

void GPURendererVK::add_viewport(ImGuiViewport* viewport) {
    SDL_Window* window = SDL_GetWindowFromID((uint32_t)(uint64_t)viewport->PlatformHandle);
    VkWin32SurfaceCreateInfoKHR surface_info {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = (HWND)wm_get_native_window_handle(window),
    };

    VkSurfaceKHR surface;
    if (VK_FAILED(vkCreateWin32SurfaceKHR(instance_, &surface_info, nullptr, &surface))) {
        Log::error("Failed to create window surface");
        return;
    }

    GPUViewportDataVK* vp_data = new GPUViewportDataVK();
    vp_data->viewport = viewport;
    vp_data->surface = surface;
    create_or_recreate_swapchain_(vp_data);
    viewport->RendererUserData = vp_data->render_target;
    added_viewports.push_back(vp_data);
}

void GPURendererVK::remove_viewport(ImGuiViewport* viewport) {
    GPUViewportDataVK* removed_viewport = nullptr;
    Vector<GPUViewportDataVK*> old_viewports;
    uint32_t index = 0;
    for (auto vp_data : viewports) {
        GPUTextureVK* texture = (GPUTextureVK*)viewport->RendererUserData;
        if (vp_data == texture->parent_viewport) {
            removed_viewport = vp_data;
            continue;
        }
        old_viewports.push_back(vp_data);
    }
    if (removed_viewport) {
        viewports = std::move(old_viewports);
        dispose_viewport_data_(removed_viewport, removed_viewport->surface);
        viewport->RendererUserData = nullptr;
        delete removed_viewport;
    }
}

void GPURendererVK::resize_viewport(ImGuiViewport* viewport, ImVec2 vec) {
    if (GImGui->Viewports[0] == viewport) {
        static_cast<GPUViewportDataVK*>(main_vp)->need_rebuild = true;
        return;
    }
    GPUTextureVK* texture = (GPUTextureVK*)viewport->RendererUserData;
    vkQueueWaitIdle(graphics_queue_);
    create_or_recreate_swapchain_(texture->parent_viewport);
    texture->parent_viewport->acquire(device_);
}

void GPURendererVK::begin_frame() {
    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkWaitForFences(device_, 1, &fences_[frame_id], VK_TRUE, UINT64_MAX);

    bool been_waiting = false;
    for (auto viewport : viewports) {
        if (viewport->need_rebuild) {
            if (!been_waiting) {
                vkQueueWaitIdle(present_queue_);
                been_waiting = true;
            }
            create_or_recreate_swapchain_(viewport);
            viewport->need_rebuild = false;
        }
        viewport->acquire(device_);
    }

    dispose_resources_(frame_count_);
    descriptor_stream_.reset(device_, frame_id);
    vkResetCommandPool(device_, cmd_pool_[frame_id], 0);
    vkBeginCommandBuffer(cmd_buf_[frame_id], &begin_info);
    current_cb_ = cmd_buf_[frame_id];
    cmd_private_data = current_cb_;
    clear_state();
    GPURenderer::begin_frame();
}

void GPURendererVK::end_frame() {
    if (!pending_uploads_.empty()) {
        submit_pending_uploads_();
        submit_wait_semaphores.push_back(upload_finished_semaphore_[frame_id]);
        submit_wait_stages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    for (auto viewport : added_viewports) {
        viewport->acquire(device_);
        viewports.push_back(viewport);
    }

    // NOTE(native-m): use arena allocator to allocate temporary data
    for (auto viewport : viewports) {
        GPUTextureVK* rt = static_cast<GPUTextureVK*>(viewport->render_target);
        uint32_t sync_id = viewport->sync_id;
        uint32_t image_id = rt->active_id;
        // Make it presentable
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
            rt->layout[image_id] = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        submit_wait_semaphores.push_back(viewport->image_acquire_semaphore[viewport->sync_id]);
        submit_wait_stages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        swapchain_present.push_back(viewport->swapchain);
        sc_image_index_present.push_back(rt->active_id);
        viewport->sync_id = (sync_id + 1) % viewport->num_sync;
    }

    vkEndCommandBuffer(current_cb_);

    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = (uint32_t)submit_wait_semaphores.size(),
        .pWaitSemaphores = submit_wait_semaphores.data(),
        .pWaitDstStageMask = submit_wait_stages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &current_cb_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished_semaphore_[sync_id_],
    };
    vkResetFences(device_, 1, &fences_[frame_id]);
    vkQueueSubmit(graphics_queue_, 1, &submit, fences_[frame_id]);

    while (auto resource = static_cast<GPUResource*>(active_resources_list_.pop_next_item())) {
        resource->active_id = (resource->active_id + 1) % resource->num_resources;
    }

    current_render_finished_semaphore_ = render_finished_semaphore_[sync_id_];
    frame_id = (frame_id + 1) % num_inflight_frames_;
    sync_id_ = (sync_id_ + 1) % num_sync_;
    frame_count_++;
    submit_wait_semaphores.resize(0);
    submit_wait_stages.resize(0);
    added_viewports.resize(0);
}

void GPURendererVK::present() {
    swapchain_results.resize(viewports.size());

    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_render_finished_semaphore_,
        .swapchainCount = (uint32_t)swapchain_present.size(),
        .pSwapchains = swapchain_present.data(),
        .pImageIndices = sc_image_index_present.data(),
        .pResults = swapchain_results.data(),
    };
    vkQueuePresentKHR(present_queue_, &present_info);

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
    GPUBufferVK* impl = static_cast<GPUBufferVK*>(buffer);
    if (!impl->is_connected_to_list()) {
        impl->read_id = impl->active_id;
        active_resources_list_.push_item(impl);
    }
    return impl->persistent_map_ptr[impl->active_id];
}

void GPURendererVK::unmap_buffer(GPUBuffer* buffer) {
    GPUBufferVK* impl = static_cast<GPUBufferVK*>(buffer);
    VmaAllocation allocation = impl->allocation[impl->active_id];
    vmaFlushAllocation(allocator_, allocation, 0, VK_WHOLE_SIZE);
}

void* GPURendererVK::begin_upload_data(GPUBuffer* buffer, size_t upload_size) {
    assert(!inside_render_pass);
    assert(buffer->size <= upload_size);

    VmaAllocationCreateInfo alloc_info {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .pool = staging_pool_,
    };

    VkBufferCreateInfo buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)upload_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VkBuffer staging_buffer;
    VmaAllocation allocation;
    VmaAllocationInfo alloc_result;
    VK_CHECK(vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &staging_buffer, &allocation, &alloc_result));
    
    GPUBufferVK* impl = static_cast<GPUBufferVK*>(buffer);
    GPUUploadItemVK& upload = pending_uploads_.emplace_back();
    upload.type = GPUUploadItemVK::Buffer;
    upload.width = upload_size;
    upload.src_buffer = staging_buffer;
    upload.src_allocation = allocation;
    upload.dst_buffer = impl->buffer[impl->active_id];
    upload.should_stall = contain_bit(buffer->usage, GPUBufferUsage::Writeable) ? VK_FALSE : VK_TRUE;
    current_upload_item_ = &upload;

    if (!impl->is_connected_to_list()) {
        impl->read_id = impl->active_id;
        active_resources_list_.push_item(impl);
    }
    
    return alloc_result.pMappedData;
}

void GPURendererVK::end_upload_data() {
    assert(!inside_render_pass);
    vmaFlushAllocation(allocator_, current_upload_item_->src_allocation, 0, VK_WHOLE_SIZE);
}

void GPURendererVK::begin_render(GPUTexture* render_target, const ImVec4& clear_color) {
    assert(!inside_render_pass);
    assert(render_target->usage & GPUTextureUsage::RenderTarget);
    GPUTextureVK* rt = static_cast<GPUTextureVK*>(render_target);
    uint32_t image_id = rt->active_id;

    if (auto layout = rt->layout[image_id]; layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        GPUTextureAccessVK src_access = get_texture_access(layout);
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
        rt->layout[image_id] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    inside_render_pass = true;
    should_clear_fb_ = true;
    std::memcpy(&rp_clear_color_, &clear_color, sizeof(VkClearValue));
    current_rt_ = rt;
    current_fb_ = rt->fb[image_id];
    fb_w = rt->width;
    fb_h = rt->height;
}

void GPURendererVK::end_render() {
    if (render_pass_started_)
        end_render_pass_();
    if (!current_rt_->is_connected_to_list()) {
        current_rt_->read_id = current_rt_->active_id;
        active_resources_list_.push_item(current_rt_);
    }
    inside_render_pass = false;
}

void GPURendererVK::set_shader_parameter(size_t size, const void* data) {
    assert(current_pipeline && "A pipeline must be bound before calling set_shader_parameter");
    GPUPipelineVK* pipeline = static_cast<GPUPipelineVK*>(current_pipeline);
    vkCmdPushConstants(current_cb_, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       (uint32_t)size, data);
}

void GPURendererVK::flush_state() {
    VkDescriptorSet descriptor_set_updates[2];
    VkDescriptorImageInfo image_descriptor[4];
    VkDescriptorBufferInfo buffer_descriptor[4];
    VkWriteDescriptorSet update_writes[8];
    uint32_t descriptor_set_first_slot = 0;
    uint32_t num_descriptor_set_updates = 0;
    uint32_t num_descriptor_writes = 0;
    VkCommandBuffer cb = current_cb_;

    // Update texture descriptors
    if (uint32_t dirty_bits = dirty_flags.texture) {
        VkDescriptorSet descriptor_set = descriptor_stream_.allocate_descriptor_set(device_, texture_set_layout_, 0, 4);
        VkImageMemoryBarrier barriers[4];
        VkPipelineStageFlags src_stage = 0;
        VkPipelineStageFlags dst_stage = 0;
        uint32_t num_barriers = 0;

        while (dirty_bits) {
            int slot = next_set_bits(dirty_bits);
            GPUTextureVK* tex = static_cast<GPUTextureVK*>(current_texture[slot]);
            uint32_t active_id = tex->read_id;
            uint32_t num_descriptors = 0;
            image_descriptor[num_descriptors] = {
                .sampler = common_sampler_,
                .imageView = tex->view[active_id],
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            update_writes[num_descriptor_writes] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = (uint32_t)slot,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_descriptor[num_descriptors],
            };
            num_descriptor_writes++;
            num_descriptors++;

            if (auto layout = tex->layout[active_id]; layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                GPUTextureAccessVK src_access = get_texture_access(layout);
                constexpr GPUTextureAccessVK dst_access = get_texture_access(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
                src_stage |= src_access.stages;
                dst_stage |= dst_access.stages;
                tex->layout[active_id] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                num_barriers++;
            }
        }

        if (num_barriers > 0) {
            // Need to pause the render pass before transitioning images
            if (render_pass_started_)
                end_render_pass_();
            vkCmdPipelineBarrier(cb, src_stage, dst_stage, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr,
                                 num_barriers, barriers);
        }

        descriptor_set_updates[num_descriptor_set_updates] = descriptor_set;
        num_descriptor_set_updates++;
    } else {
        descriptor_set_first_slot++;
    }

    // Update buffer descriptors
    if (uint32_t dirty_bits = dirty_flags.storage_buf) {
        uint32_t num_descriptors = 0;
        VkDescriptorSet descriptor_set =
            descriptor_stream_.allocate_descriptor_set(device_, storage_buffer_set_layout_, 4, 0);

        while (dirty_bits) {
            int slot = next_set_bits(dirty_bits);
            GPUBufferVK* buf = static_cast<GPUBufferVK*>(current_storage_buf[slot]);
            uint32_t active_id = buf->active_id;
            buffer_descriptor[num_descriptors] = {
                .buffer = buf->buffer[active_id],
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
            update_writes[num_descriptor_writes] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = (uint32_t)slot,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_descriptor[num_descriptors],
            };
            num_descriptor_writes++;
            num_descriptors++;
        }

        descriptor_set_updates[num_descriptor_set_updates] = descriptor_set;
        num_descriptor_set_updates++;
    }

    if (!render_pass_started_)
        begin_render_pass_();

    if (dirty_flags.pipeline) {
        GPUPipelineVK* pipeline = static_cast<GPUPipelineVK*>(current_pipeline);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    }

    if (num_descriptor_set_updates > 0) {
        GPUPipelineVK* pipeline = static_cast<GPUPipelineVK*>(current_pipeline);
        vkUpdateDescriptorSets(device_, num_descriptor_writes, update_writes, 0, nullptr);
        vkCmdBindDescriptorSets(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout,
                                descriptor_set_first_slot, num_descriptor_set_updates, descriptor_set_updates, 0,
                                nullptr);
    }

    if (dirty_flags.vtx_buf) {
        GPUBufferVK* vtx_buf = static_cast<GPUBufferVK*>(current_vtx_buf);
        VkDeviceSize vtx_offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vtx_buf->buffer[vtx_buf->active_id], &vtx_offset);
    }

    if (dirty_flags.idx_buf) {
        GPUBufferVK* idx_buf = static_cast<GPUBufferVK*>(current_idx_buf);
        vkCmdBindIndexBuffer(cb, idx_buf->buffer[idx_buf->active_id], 0, VK_INDEX_TYPE_UINT32);
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

void GPURendererVK::enqueue_resource_upload_(VkBuffer buffer, uint32_t size, const void* data) {
    VmaAllocationCreateInfo alloc_info {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .pool = staging_pool_,
    };

    VkBufferCreateInfo buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VkBuffer staging_buffer;
    VmaAllocation allocation;
    VmaAllocationInfo alloc_result;
    VK_CHECK(vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &staging_buffer, &allocation, &alloc_result));

    std::memcpy(alloc_result.pMappedData, data, buffer_info.size);
    vmaFlushAllocation(allocator_, allocation, 0, VK_WHOLE_SIZE);

    GPUUploadItemVK& upload = pending_uploads_.emplace_back();
    upload.type = GPUUploadItemVK::Buffer;
    upload.width = size;
    upload.src_buffer = staging_buffer;
    upload.src_allocation = allocation;
    upload.dst_buffer = buffer;
}

void GPURendererVK::enqueue_resource_upload_(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                                             uint32_t w, uint32_t h, const void* data) {
    VmaAllocationCreateInfo alloc_info {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .pool = staging_pool_,
    };

    VkBufferCreateInfo buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = w * h * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo alloc_result;
    VK_CHECK(vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer, &allocation, &alloc_result));

    std::memcpy(alloc_result.pMappedData, data, buffer_info.size);
    vmaFlushAllocation(allocator_, allocation, 0, VK_WHOLE_SIZE);

    GPUUploadItemVK& upload = pending_uploads_.emplace_back();
    upload.type = GPUUploadItemVK::Image;
    upload.width = w;
    upload.height = h;
    upload.old_layout = old_layout;
    upload.new_layout = new_layout;
    upload.src_buffer = buffer;
    upload.src_allocation = allocation;
    upload.dst_image = image;
}

void GPURendererVK::submit_pending_uploads_() {
    upload_id_ = (upload_id_ + 1) % num_inflight_frames_;

    bool emit_memory_barrier = false;
    VkCommandBuffer upload_cb = upload_cmd_buf_[upload_id_];
    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkResetCommandPool(device_, upload_cmd_pool_[upload_id_], VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    vkBeginCommandBuffer(upload_cb, &begin_info);

    bool been_stalled = false;
    while (!pending_uploads_.empty()) {
        auto& item = pending_uploads_.front();
        
        if (item.should_stall && !been_stalled) {
            vkQueueWaitIdle(graphics_queue_);
            been_stalled = true;
        }

        switch (item.type) {
            case GPUUploadItemVK::Buffer: {
                VkBufferCopy region {.size = (VkDeviceSize)item.width};
                vkCmdCopyBuffer(upload_cb, item.src_buffer, item.dst_buffer, 1, &region);
                emit_memory_barrier = true;
                break;
            }
            case GPUUploadItemVK::Image: {
                GPUTextureAccessVK old_access = get_texture_access(item.old_layout);
                GPUTextureAccessVK new_access = get_texture_access(item.new_layout);

                VkImageMemoryBarrier barrier;
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.pNext = nullptr;
                barrier.srcAccessMask = old_access.mask;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.oldLayout = old_access.layout;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = graphics_queue_index_;
                barrier.dstQueueFamilyIndex = graphics_queue_index_;
                barrier.image = item.dst_image;
                barrier.subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };

                vkCmdPipelineBarrier(upload_cb, old_access.stages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                                     nullptr, 1, &barrier);

                VkBufferImageCopy region {
                    .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                    .imageExtent = {(uint32_t)item.width, item.height, 1},
                };
                vkCmdCopyBufferToImage(upload_cb, item.src_buffer, item.dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1, &region);

                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = new_access.mask;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = new_access.layout;

                vkCmdPipelineBarrier(upload_cb, VK_PIPELINE_STAGE_TRANSFER_BIT, new_access.stages, 0, 0, nullptr, 0,
                                     nullptr, 1, &barrier);
                break;
            }
        }

        GPUResourceDisposeItemVK& buf = resource_disposal_.emplace_back();
        buf.type = GPUResourceDisposeItemVK::Buffer;
        buf.frame_stamp = frame_count_;
        buf.buffer = {
            .buffer = item.src_buffer,
            .allocation = item.src_allocation,
        };

        pending_uploads_.pop_front();
    }

    if (emit_memory_barrier) {
        VkMemoryBarrier barrier;
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(upload_cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    vkEndCommandBuffer(upload_cb);

    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &upload_cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &upload_finished_semaphore_[upload_id_],
    };

    vkQueueSubmit(graphics_queue_, 1, &submit, VK_NULL_HANDLE);
}

void GPURendererVK::begin_render_pass_() {
    VkRenderPassBeginInfo rp_begin {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = fb_rp_bgra_,
        .framebuffer = current_fb_,
        .renderArea = {0, 0, fb_w, fb_h},
    };

    vkCmdBeginRenderPass(current_cb_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    if (should_clear_fb_) {
        VkClearAttachment clear_attachment {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0,
            .clearValue = rp_clear_color_,
        };

        VkClearRect clear_rect {
            .rect = {0, 0, fb_w, fb_h},
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        vkCmdClearAttachments(current_cb_, 1, &clear_attachment, 1, &clear_rect);
        should_clear_fb_ = false;
    }

    render_pass_started_ = true;
}

void GPURendererVK::end_render_pass_() {
    vkCmdEndRenderPass(current_cb_);
    render_pass_started_ = false;
}

bool GPURendererVK::create_or_recreate_swapchain_(GPUViewportDataVK* vp_data) {
    VkSurfaceKHR surface = vp_data->surface;
    VkBool32 surface_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, present_queue_index_, surface, &surface_supported);
    assert(surface_supported);

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

    uint32_t queue_family_indicies[2] {graphics_queue_index_, present_queue_index_};

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
        .queueFamilyIndexCount = 2,
        .pQueueFamilyIndices = queue_family_indicies,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR,
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
        .renderPass = fb_rp_bgra_,
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
        texture->num_resources = num_inflight_frames_;
        vp_data->render_target = render_target = texture;
    } else {
        render_target = static_cast<GPUTextureVK*>(vp_data->render_target);
    }

    render_target->parent_viewport = vp_data;
    render_target->active_id = 0;
    render_target->usage = GPUTextureUsage::RenderTarget;
    render_target->width = fb_info.width;
    render_target->height = fb_info.height;
    vp_data->surface = surface;
    vp_data->swapchain = vk_swapchain;
    vp_data->num_sync = num_sync_;
    vp_data->sync_id = 0;

    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(device_, vk_swapchain, &swapchain_image_count, nullptr);
    vkGetSwapchainImagesKHR(device_, vk_swapchain, &swapchain_image_count, render_target->image);

    for (uint32_t i = 0; i < num_sync_; i++) {
        VK_CHECK(vkCreateSemaphore(device_, &semaphore, nullptr, &vp_data->image_acquire_semaphore[i]));
    }

    for (uint32_t i = 0; i < num_inflight_frames_; i++) {
        view_info.image = render_target->image[i];
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &render_target->view[i]));
        fb_info.pAttachments = &render_target->view[i];
        VK_CHECK(vkCreateFramebuffer(device_, &fb_info, nullptr, &render_target->fb[i]));
        render_target->layout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    return true;
}

void GPURendererVK::dispose_buffer_(GPUBufferVK* buffer) {
    std::scoped_lock lock(mtx_);
    for (uint32_t i = 0; i < buffer->num_resources; i++) {
        GPUResourceDisposeItemVK& buf = resource_disposal_.emplace_back();
        buf.type = GPUResourceDisposeItemVK::Buffer;
        buf.frame_stamp = frame_count_;
        buf.buffer = {
            .buffer = buffer->buffer[i],
            .allocation = buffer->allocation[i],
        };
    }
}

void GPURendererVK::dispose_texture_(GPUTextureVK* texture) {
    std::scoped_lock lock(mtx_);
    for (uint32_t i = 0; i < texture->num_resources; i++) {
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

void GPURendererVK::dispose_pipeline_(GPUPipelineVK* pipeline) {
    std::scoped_lock lock(mtx_);
    GPUResourceDisposeItemVK& item = resource_disposal_.emplace_back();
    item.type = GPUResourceDisposeItemVK::Pipeline;
    item.frame_stamp = frame_count_;
    item.pipeline = {pipeline->pipeline, pipeline->layout};
}

void GPURendererVK::dispose_viewport_data_(GPUViewportDataVK* vp_data, VkSurfaceKHR surface) {
    std::scoped_lock lock(mtx_);
    GPUTextureVK* vk_texture = static_cast<GPUTextureVK*>(vp_data->render_target);
    for (uint32_t i = 0; i < vk_texture->num_resources; i++) {
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
        .surface = surface,
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
#if WB_LOG_VULKAN_RESOURCE_DISPOSAL
                    Log::debug("Buffer destroyed {:x} on frame {}", (uintptr_t)item.buffer.buffer, item.frame_stamp);
#endif
                    break;
                case GPUResourceDisposeItemVK::Texture:
                    if (item.texture.fb)
                        vkDestroyFramebuffer(device_, item.texture.fb, nullptr);
                    vkDestroyImageView(device_, item.texture.view, nullptr);
                    if (item.texture.image && item.texture.allocation)
                        vmaDestroyImage(allocator_, item.texture.image, item.texture.allocation);
#if WB_LOG_VULKAN_RESOURCE_DISPOSAL
                    Log::debug("Texture destroyed {:x} on frame {}", (uintptr_t)item.texture.image, item.frame_stamp);
#endif
                    break;
                case GPUResourceDisposeItemVK::Pipeline:
                    vkDestroyPipelineLayout(device_, item.pipeline.layout, nullptr);
                    vkDestroyPipeline(device_, item.pipeline.pipeline, nullptr);
#if WB_LOG_VULKAN_RESOURCE_DISPOSAL
                    Log::debug("Pipeline destroyed {:x} on frame {}", (uintptr_t)item.pipeline.pipeline,
                               item.frame_stamp);
#endif
                    break;
                case GPUResourceDisposeItemVK::Swapchain:
                    vkDestroySwapchainKHR(device_, item.swapchain.swapchain, nullptr);
                    if (item.swapchain.surface)
                        vkDestroySurfaceKHR(instance_, item.swapchain.surface, nullptr);
#if WB_LOG_VULKAN_RESOURCE_DISPOSAL
                    Log::debug("Swapchain destroyed {:x} on frame {}", (uintptr_t)item.swapchain.swapchain,
                               item.frame_stamp);
#endif
                    break;
                case GPUResourceDisposeItemVK::SyncObject:
                    vkDestroySemaphore(device_, item.sync_obj.semaphore, nullptr);
#if WB_LOG_VULKAN_RESOURCE_DISPOSAL
                    Log::debug("Sync object destroyed {:x} on frame {}", (uintptr_t)item.sync_obj.semaphore,
                               item.frame_stamp);
#endif
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

#if 0
    // Prefer discrete gpu
    for (auto physical_device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected_physical_device = physical_device;
            break;
        }
    }
#endif

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

    Vector<VkDeviceQueueCreateInfo> queue_info;

    // Find graphics queue and presentation queue
    uint32_t graphics_queue_index = (uint32_t)-1;
    uint32_t present_queue_index = (uint32_t)-1;
    const float queue_priority = 1.0f;
    for (uint32_t i = 0; const auto& queue_family : queue_families) {
        if (graphics_queue_index == (uint32_t)-1 && contain_bit(queue_family.queueFlags, VK_QUEUE_GRAPHICS_BIT)) {
            queue_info.push_back({
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = i,
                .queueCount = 1,
                .pQueuePriorities = &queue_priority,
            });
            graphics_queue_index = i;
        }
        VkBool32 presentation_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(selected_physical_device, i, surface, &presentation_supported);
        if (presentation_supported && present_queue_index == (uint32_t)-1) {
            if (graphics_queue_index != i) {
                queue_info.push_back({
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = i,
                    .queueCount = 1,
                    .pQueuePriorities = &queue_priority,
                });
            }
            present_queue_index = i;
        }
        // if (graphics_queue_index != (uint32_t)-1 && present_queue_index != (uint32_t)-1)
        //     break;
        i++;
    }

    if (graphics_queue_index == (uint32_t)-1 || present_queue_index == (uint32_t)-1) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    assert(graphics_queue_index == present_queue_index && "Separate presentation queue is not supported at the moment");

    VkPhysicalDeviceFeatures features {};
    vkGetPhysicalDeviceFeatures(selected_physical_device, &features);

    const char* extension_name = "VK_KHR_swapchain";
    VkDeviceCreateInfo device_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = (uint32_t)queue_info.size(),
        .pQueueCreateInfos = queue_info.data(),
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
