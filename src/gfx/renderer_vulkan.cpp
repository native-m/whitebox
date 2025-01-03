#include "renderer_vulkan.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include "core/defer.h"
#include "core/thread.h"
#include "vsync_provider.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>
#include <chrono>

#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES

#ifdef VK_USE_PLATFORM_XCB_KHR
#include <X11/Xlib-xcb.h>
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
extern "C" {
#include <X11/Xutil.h>
}
#endif

#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#define VULKAN_ENABLE_VALIDATION_AND_DEBUG_MSG 1
#define VULKAN_LOG_RESOURCE_DISPOSAL 0

#ifdef NDEBUG
#undef VULKAN_LOG_RESOURCE_DISPOSAL
#endif

#define FRAME_ID_DISPOSE_ALL ~0U

// Each viewport will hold 1 ImGui_ImplVulkanH_WindowRenderBuffers
// [Please zero-clear before use!]
struct ImGui_ImplVulkan_WindowRenderBuffers {
    uint32_t Index;
    uint32_t Count;
    ImGui_ImplVulkan_FrameRenderBuffers* FrameRenderBuffers;
};

// For multi-viewport support:
// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily
// retrieve our backend data.
struct ImGui_ImplVulkan_ViewportData {
    bool WindowOwned;
    ImGui_ImplVulkanH_Window Window;                    // Used by secondary viewports only
    ImGui_ImplVulkan_WindowRenderBuffers RenderBuffers; // Used by all viewports

    ImGui_ImplVulkan_ViewportData() {
        WindowOwned = false;
        memset(&RenderBuffers, 0, sizeof(RenderBuffers));
    }
    ~ImGui_ImplVulkan_ViewportData() {}
};

// Vulkan data
struct ImGui_ImplVulkan_Data {
    ImGui_ImplVulkan_InitInfo VulkanInitInfo;
    VkDeviceSize BufferMemoryAlignment;
    VkPipelineCreateFlags PipelineCreateFlags;
    VkDescriptorSetLayout DescriptorSetLayout;
    VkPipelineLayout PipelineLayout;
    VkPipeline Pipeline;
    VkShaderModule ShaderModuleVert;
    VkShaderModule ShaderModuleFrag;

    // Font data
    VkSampler FontSampler;
    VkDeviceMemory FontMemory;
    VkImage FontImage;
    VkImageView FontView;
    VkDescriptorSet FontDescriptorSet;
    VkCommandPool FontCommandPool;
    VkCommandBuffer FontCommandBuffer;

    // Render buffers for main window
    ImGui_ImplVulkan_WindowRenderBuffers MainWindowRenderBuffers;

    ImGui_ImplVulkan_Data() {
        memset((void*)this, 0, sizeof(*this));
        BufferMemoryAlignment = 256;
    }
};

static uint32_t ImGui_ImplVulkan_MemoryType(VkPhysicalDevice physical_device, VkMemoryPropertyFlags properties,
                                            uint32_t type_bits) {
    VkPhysicalDeviceMemoryProperties prop;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &prop);
    for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
        if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
            return i;
    return 0xFFFFFFFF; // Unable to find memoryType
}

static inline VkDeviceSize AlignBufferSize(VkDeviceSize size, VkDeviceSize alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

namespace wb {

struct ClipContentDrawCmdVK {
    float origin_x;
    float origin_y;
    float scale_x;
    float scale_y;
    uint32_t color;
    float vp_width;
    float vp_height;
    int is_min;
    uint32_t channel;
    uint32_t start_idx;
    uint32_t sample_count;
};

struct VectorDrawCmdVK {
    ImVec2 inv_viewport;
    ImVec2 min_bb;
    ImVec2 max_bb;
    uint32_t color;
    uint32_t vtx_offset;
};

FramebufferVK::~FramebufferVK() {
    if (resource_disposal)
        resource_disposal->dispose_framebuffer(this);
}

ImTextureID FramebufferVK::as_imgui_texture_id() const {
    return ImTextureID(descriptor_set[resource_disposal->current_frame_id]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

SamplePeaksVK::~SamplePeaksVK() {
    for (auto [buffer, allocation, _] : mipmap)
        resource_disposal->dispose_buffer(allocation, buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

VkResult SwapchainVK::acquire(VkDevice device) {
    VkResult err =
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_acquire_semaphore[sync_id], nullptr, &image_index);
    return err;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void ResourceDisposalVK::dispose_buffer(VmaAllocation allocation, VkBuffer buf) {
    std::scoped_lock lock(mtx);
    buffer.emplace_back(current_frame_id, allocation, buf);
#if VULKAN_LOG_RESOURCE_DISPOSAL
    Log::debug("Enqueuing buffer disposal: frame_id {}", current_frame_id);
#endif
}

void ResourceDisposalVK::dispose_framebuffer(FramebufferVK* obj) {
    std::scoped_lock lock(mtx);
    for (uint32_t i = 0; i < obj->num_buffers; i++) {
        fb.push_back(FramebufferDisposalVK {
            .frame_id = current_frame_id,
            .allocation = obj->allocations[i],
            .image = obj->image[i],
            .view = obj->view[i],
            .framebuffer = obj->framebuffer[i],
        });
    }
#if VULKAN_LOG_RESOURCE_DISPOSAL
    Log::debug("Enqueuing framebuffer disposal: frame_id {}", current_frame_id);
#endif
}

void ResourceDisposalVK::dispose_image(ImageVK* obj) {
    std::scoped_lock lock(mtx);
    img.push_back(ImageDisposalVK {
        .frame_id = current_frame_id,
        .allocation = obj->allocation,
        .image = obj->image,
        .view = obj->view,
    });
#if VULKAN_LOG_RESOURCE_DISPOSAL
    Log::debug("Enqueuing image disposal: frame_id {}", current_frame_id);
#endif
}

void ResourceDisposalVK::dispose_immediate_buffer(VkDeviceMemory buffer_memory, VkBuffer buffer) {
    std::scoped_lock lock(mtx);
    imm_buffer.push_back({current_frame_id, buffer_memory, buffer});
#if VULKAN_LOG_RESOURCE_DISPOSAL
    Log::debug("Enqueuing immediate buffer disposal: frame_id {}", current_frame_id);
#endif
}

void ResourceDisposalVK::dispose_swapchain(SwapchainVK* obj, VkSurfaceKHR surface) {
    std::scoped_lock lock(mtx);
    for (uint32_t i = 0; i < obj->fb.num_buffers; i++) {
        fb.push_back(FramebufferDisposalVK {
            .frame_id = current_frame_id,
            .allocation = VK_NULL_HANDLE,
            .image = VK_NULL_HANDLE,
            .view = obj->fb.view[i],
            .framebuffer = obj->fb.framebuffer[i],
        });
    }
    for (uint32_t i = 0; i < obj->num_sync; i++) {
        sync_objs.push_back(SyncObjectDisposalVK {
            .frame_id = current_frame_id,
            .semaphore = obj->image_acquire_semaphore[i],
        });
    }
    swapchains.push_back(SwapchainDisposalVK {
        .frame_id = current_frame_id,
        .swapchain = obj->swapchain,
        .surface = surface,
    });
#if VULKAN_LOG_RESOURCE_DISPOSAL
    Log::debug("Enqueuing swapchain buffer disposal: frame_id {}", current_frame_id);
#endif
}

void ResourceDisposalVK::flush(VkDevice device, VkInstance instance, VmaAllocator allocator,
                               uint32_t frame_id_dispose) {
    std::scoped_lock lock(mtx);

    while (!buffer.empty()) {
        auto [frame_id, allocation, buf] = buffer.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vmaDestroyBuffer(allocator, buf, allocation);
        buffer.pop_front();
#if VULKAN_LOG_RESOURCE_DISPOSAL
        Log::debug("Buffer disposed: {:x}, frame_id {}", (uint64_t)buf, frame_id);
#endif
    }

    while (!img.empty()) {
        auto [frame_id, allocation, image, view] = img.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vkDestroyImageView(device, view, nullptr);
        vmaDestroyImage(allocator, image, allocation);
        img.pop_front();
#if VULKAN_LOG_RESOURCE_DISPOSAL
        Log::debug("Image disposed: {:x}, frame_id: {}", (uint64_t)image, frame_id);
#endif
    }

    while (!fb.empty()) {
        auto [frame_id, allocation, image, view, framebuffer] = fb.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        vkDestroyImageView(device, view, nullptr);
        if (image && allocation)
            vmaDestroyImage(allocator, image, allocation);
        fb.pop_front();
#if VULKAN_LOG_RESOURCE_DISPOSAL
        Log::debug("Framebuffer disposed: {:x}, frame_id: {}", (uint64_t)framebuffer, frame_id);
#endif
    }

    while (!imm_buffer.empty()) {
        auto [frame_id, memory, buffer] = imm_buffer.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
        imm_buffer.pop_front();
#if VULKAN_LOG_RESOURCE_DISPOSAL
        Log::debug("Immediate buffer disposed: {:x}, frame_id: {}", (uint64_t)buffer, frame_id);
#endif
    }

    while (!sync_objs.empty()) {
        auto [frame_id, semaphore] = sync_objs.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vkDestroySemaphore(device, semaphore, nullptr);
        sync_objs.pop_front();
#if VULKAN_LOG_RESOURCE_DISPOSAL
        Log::debug("Semaphore disposed: {:x}, frame_id: {}", (uint64_t)semaphore, frame_id);
#endif
    }

    while (!swapchains.empty()) {
        auto [frame_id, swapchain, surface] = swapchains.front();
        if (frame_id != frame_id_dispose && frame_id_dispose != FRAME_ID_DISPOSE_ALL)
            break;
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        swapchains.pop_front();
#if VULKAN_LOG_RESOURCE_DISPOSAL
        Log::debug("Swapchain disposed: {:x}, frame_id: {}", (uint64_t)swapchain, frame_id);
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

VkDescriptorSet DescriptorStreamVK::allocate_descriptor_set(VkDevice device, VkDescriptorSetLayout layout,
                                                            uint32_t num_uniform_buffers, uint32_t num_storage_buffers,
                                                            uint32_t num_sampled_images, uint32_t num_storage_images) {
    if (current_chunk == nullptr) {
        // First use case
        chunk_list[current_frame_id] = create_chunk(device, 64, 64);
        assert(chunk_list[current_frame_id]);
        current_chunk = chunk_list[current_frame_id];
    } else {
        // Create new or use next existing chunk if there is not enough storage to allocate new
        // descriptor set
        uint32_t free_uniform_buffers = current_chunk->max_descriptors - current_chunk->num_uniform_buffers;
        uint32_t free_storage_buffers = current_chunk->max_descriptors - current_chunk->num_storage_buffers;
        uint32_t free_sampled_images = current_chunk->max_descriptors - current_chunk->num_sampled_images;
        uint32_t free_storage_images = current_chunk->max_descriptors - current_chunk->num_storage_images;
        uint32_t free_descriptor_sets = current_chunk->max_descriptor_sets - current_chunk->num_descriptor_sets;

        if (num_uniform_buffers > free_uniform_buffers || num_storage_buffers > free_storage_buffers ||
            num_sampled_images > free_sampled_images || num_storage_images > free_storage_images ||
            free_descriptor_sets == 0) {
            if (current_chunk->next == nullptr) {
                const uint32_t max_descriptor_sets =
                    current_chunk->max_descriptor_sets + current_chunk->max_descriptor_sets / 2;
                const uint32_t max_descriptors = current_chunk->max_descriptors + current_chunk->max_descriptors / 2;
                DescriptorStreamChunkVK* new_chunk = create_chunk(device, max_descriptor_sets, max_descriptors);
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

    current_chunk->num_uniform_buffers += num_uniform_buffers;
    current_chunk->num_storage_buffers += num_storage_buffers;
    current_chunk->num_sampled_images += num_sampled_images;
    current_chunk->num_storage_images += num_storage_images;
    current_chunk->num_descriptor_sets++;

    return descriptor_set;
}

DescriptorStreamChunkVK* DescriptorStreamVK::create_chunk(VkDevice device, uint32_t max_descriptor_sets,
                                                          uint32_t max_descriptors) {
    DescriptorStreamChunkVK* chunk = (DescriptorStreamChunkVK*)std::malloc(sizeof(DescriptorStreamChunkVK));

    if (chunk == nullptr)
        return {};

    VkDescriptorPoolSize pool_sizes[4];
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = max_descriptors;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = max_descriptors;
    pool_sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    pool_sizes[2].descriptorCount = max_descriptors;
    pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[3].descriptorCount = max_descriptors;

    VkDescriptorPoolCreateInfo pool_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = max_descriptor_sets,
        .poolSizeCount = 4,
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

void DescriptorStreamVK::reset(VkDevice device, uint32_t frame_id) {
    current_frame_id = frame_id;

    DescriptorStreamChunkVK* chunk = chunk_list[current_frame_id];
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

void DescriptorStreamVK::destroy(VkDevice device) {
    for (auto chunk : chunk_list) {
        while (chunk) {
            DescriptorStreamChunkVK* chunk_to_destroy = chunk;
            vkDestroyDescriptorPool(device, chunk->pool, nullptr);
            chunk = chunk->next;
            std::free(chunk_to_destroy);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

RendererVK::RendererVK(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, VkPhysicalDevice physical_device,
                       VkDevice device, uint32_t graphics_queue_index, uint32_t present_queue_index) :
    instance_(instance),
    debug_messenger_(debug_messenger),
    physical_device_(physical_device),
    device_(device),
    graphics_queue_index_(graphics_queue_index),
    present_queue_index_(present_queue_index) {
    vkGetDeviceQueue(device_, graphics_queue_index_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_queue_index_, 0, &present_queue_);
}

RendererVK::~RendererVK() {
    vkDeviceWaitIdle(device_);
    destroy_pipelines();

    for (uint32_t i = 0; i < frame_latency_; i++) {
        vkDestroyFence(device_, fences_[i], nullptr);
        resource_disposal_.dispose_image(&winding_images_[i]);

        ImGui_ImplVulkan_FrameRenderBuffers& rb = render_buffers_[i];
        resource_disposal_.dispose_immediate_buffer(rb.VertexBufferMemory, rb.VertexBuffer);
        resource_disposal_.dispose_immediate_buffer(rb.IndexBufferMemory, rb.IndexBuffer);

        CommandBufferVK& cmd_buf = cmd_buf_[i];
        vkDestroyCommandPool(device_, cmd_buf.cmd_pool, nullptr);
        resource_disposal_.dispose_immediate_buffer(cmd_buf.polygon_buffer_mem, cmd_buf.polygon_buffer);
    }

    for (auto& sync : frame_sync_) {
        vkDestroySemaphore(device_, sync.image_acquire_semaphore, nullptr);
        vkDestroySemaphore(device_, sync.render_finished_semaphore, nullptr);
    }

    for (auto swapchain : swapchains) {
        if (swapchain->viewport) {
            swapchain->viewport->RendererUserData = nullptr;
            swapchain->viewport = nullptr;
        }
        resource_disposal_.dispose_swapchain(swapchain, swapchain->surface);
        delete swapchain;
    }

    swapchains.resize(0);
    descriptor_stream_.destroy(device_);
    ImGui_ImplVulkan_Shutdown();

    vkDestroyCommandPool(device_, imm_cmd_pool_, nullptr);
    vkDestroySampler(device_, imgui_sampler_, nullptr);
    vkDestroyDescriptorPool(device_, imgui_descriptor_pool_, nullptr);
    vkDestroyRenderPass(device_, fb_render_pass_, nullptr);
    resource_disposal_.flush(device_, instance_, allocator_, FRAME_ID_DISPOSE_ALL);
    vmaDestroyAllocator(allocator_);
    vkDestroyDevice(device_, nullptr);
    if (debug_messenger_)
        vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
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

    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < frame_latency_; i++) {
        CommandBufferVK& cmd = cmd_buf_[i];
        VK_CHECK(vkCreateCommandPool(device_, &cmd_pool, nullptr, &cmd.cmd_pool));
        cmd_buf_info.commandPool = cmd.cmd_pool;
        VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &cmd.cmd_buffer));
        VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &fences_[i]));
    }

    VK_CHECK(vkCreateCommandPool(device_, &cmd_pool, nullptr, &imm_cmd_pool_));
    cmd_buf_info.commandPool = imm_cmd_pool_;
    VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_buf_info, &imm_cmd_buf_));

    VkSemaphoreCreateInfo semaphore_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    for (uint32_t i = 0; i < sync_count_; i++) {
        FrameSync& frame_sync = frame_sync_[i];
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frame_sync.image_acquire_semaphore));
        VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frame_sync.render_finished_semaphore));
    }

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1024,
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

    init_pipelines();

    ImGui_ImplVulkan_InitInfo init_info {
        .Instance = instance_,
        .PhysicalDevice = physical_device_,
        .Device = device_,
        .QueueFamily = graphics_queue_index_,
        .Queue = graphics_queue_,
        .DescriptorPool = imgui_descriptor_pool_,
        .RenderPass = fb_render_pass_,
        .MinImageCount = frame_latency_,
        .ImageCount = frame_latency_,
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

#if VULKAN_ENABLE_VALIDATION_AND_DEBUG_MSG
    VkDebugUtilsObjectNameInfoEXT debug_info {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_IMAGE,
        .pObjectName = "Framebuffer",
    };
#endif

    for (uint32_t i = 0; i < frame_latency_; i++) {
        VK_CHECK(vmaCreateImage(allocator_, &image_info, &alloc_info, &fb->image[i], &fb->allocations[i], nullptr));

#if VULKAN_ENABLE_VALIDATION_AND_DEBUG_MSG
        debug_info.objectHandle = (uint64_t)fb->image[i];
        vkSetDebugUtilsObjectNameEXT(device_, &debug_info);
#endif

        view_info.image = fb->image[i];
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &fb->view[i]));

        fb_info.pAttachments = &fb->view[i];
        VK_CHECK(vkCreateFramebuffer(device_, &fb_info, nullptr, &fb->framebuffer[i]));

        fb->descriptor_set[i] =
            ImGui_ImplVulkan_AddTexture(imgui_sampler_, fb->view[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    fb->width = width;
    fb->height = height;
    fb->resource_disposal = &resource_disposal_;
    fb->num_buffers = frame_latency_;
    fb->image_id = frame_latency_ - 1;

    return framebuffer;
}

std::shared_ptr<SamplePeaks> RendererVK::create_sample_peaks(const Sample& sample, SamplePeaksPrecision precision) {
    size_t sample_count = sample.count;
    uint32_t current_mip = 1;
    uint32_t max_mip = 0;
    uint32_t elem_size = 0;

    switch (precision) {
        case SamplePeaksPrecision::Low:
            elem_size = sizeof(int8_t);
            break;
        case SamplePeaksPrecision::High:
            elem_size = sizeof(int16_t);
            break;
        default:
            WB_UNREACHABLE();
    }

    struct BufferCopy {
        VkBuffer staging_buffer {};
        VmaAllocation staging_allocation {};
        VmaPool staging_pool {};
        VkBuffer dst_buffer {};
        VkDeviceSize size {};
    };

    std::vector<BufferCopy> buffer_copies;
    std::vector<SamplePeaksMipVK> mipmap;
    bool failed = false;
    auto destroy_all = [&] {
        for (auto& buffer_copy : buffer_copies) {
            if (buffer_copy.staging_buffer && buffer_copy.staging_allocation)
                vmaDestroyBuffer(allocator_, buffer_copy.staging_buffer, buffer_copy.staging_allocation);
            if (buffer_copy.staging_pool)
                vmaDestroyPool(allocator_, buffer_copy.staging_pool);
        }
        if (failed) {
            for (auto& mip : mipmap) {
                if (mip.buffer && mip.allocation)
                    vmaDestroyBuffer(allocator_, mip.buffer, mip.allocation);
            }
        }
    };

    defer(destroy_all());

    VkBufferCreateInfo buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    VkBufferCreateInfo staging_buffer_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VmaAllocationCreateInfo alloc_info {
        .usage = VMA_MEMORY_USAGE_UNKNOWN,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VmaAllocationCreateInfo staging_alloc_info {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };

    VkCommandBufferBeginInfo cmd_begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    while (sample_count > 64) {
        Log::info("Generating mip-map {} ({})", current_mip, sample_count);

        BufferCopy& buffer_copy = buffer_copies.emplace_back();
        SamplePeaksMipVK& mip = mipmap.emplace_back();

        size_t required_length;
        sample.summarize_for_mipmaps(precision, 0, current_mip, 0, &required_length, nullptr);

        size_t total_length = required_length * sample.channels;
        buffer_info.size = total_length * elem_size;
        staging_buffer_info.size = buffer_info.size;
        buffer_copy.size = buffer_info.size;
        mip.sample_count = (uint32_t)required_length;

        uint32_t memory_type_index;
        VK_CHECK(vmaFindMemoryTypeIndexForBufferInfo(allocator_, &staging_buffer_info, &staging_alloc_info,
                                                     &memory_type_index));

        VmaPoolCreateInfo staging_pool_info {
            .memoryTypeIndex = memory_type_index,
            .flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
            .blockSize = buffer_info.size,
        };

        if (VK_FAILED(vmaCreatePool(allocator_, &staging_pool_info, &buffer_copy.staging_pool))) {
            failed = true;
            return {};
        }

        staging_alloc_info.pool = buffer_copy.staging_pool;

        if (VK_FAILED(vmaCreateBuffer(allocator_, &staging_buffer_info, &staging_alloc_info,
                                      &buffer_copy.staging_buffer, &buffer_copy.staging_allocation, nullptr))) {
            failed = true;
            return {};
        }

        void* ptr;
        VK_CHECK(vmaMapMemory(allocator_, buffer_copy.staging_allocation, &ptr));
        for (uint32_t i = 0; i < sample.channels; i++) {
            sample.summarize_for_mipmaps(precision, i, current_mip, required_length * i, &required_length, ptr);
        }
        VK_CHECK(vmaFlushAllocation(allocator_, buffer_copy.staging_allocation, 0, VK_WHOLE_SIZE));
        vmaUnmapMemory(allocator_, buffer_copy.staging_allocation);

        if (VK_FAILED(vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &mip.buffer, &mip.allocation, nullptr))) {
            failed = true;
            return {};
        }

        buffer_copy.dst_buffer = mip.buffer;

        sample_count /= 4;
        current_mip += 2;
        max_mip = current_mip - 1;
    }

    VkBufferMemoryBarrier buffer_barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = graphics_queue_index_,
        .dstQueueFamilyIndex = graphics_queue_index_,
    };

    vkDeviceWaitIdle(device_);
    vkResetCommandPool(device_, imm_cmd_pool_, 0);
    vkBeginCommandBuffer(imm_cmd_buf_, &cmd_begin_info);

    for (auto& buffer_copy : buffer_copies) {
        VkBufferCopy region {
            .size = buffer_copy.size,
        };

        buffer_barrier.buffer = buffer_copy.dst_buffer;
        buffer_barrier.size = buffer_copy.size;

        vkCmdCopyBuffer(imm_cmd_buf_, buffer_copy.staging_buffer, buffer_copy.dst_buffer, 1, &region);
        vkCmdPipelineBarrier(imm_cmd_buf_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                             nullptr, 1, &buffer_barrier, 0, nullptr);
    }

    vkEndCommandBuffer(imm_cmd_buf_);

    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &imm_cmd_buf_,
    };

    vkQueueSubmit(graphics_queue_, 1, &submit, nullptr);
    vkDeviceWaitIdle(device_);

    std::shared_ptr<SamplePeaksVK> ret {std::make_shared<SamplePeaksVK>()};
    ret->sample_count = sample.count;
    ret->mipmap_count = (int32_t)mipmap.size();
    ret->channels = sample.channels;
    ret->precision = precision;
    ret->cpu_accessible = false;
    ret->mipmap = std::move(mipmap);
    ret->resource_disposal = &resource_disposal_;

    return ret;
}

void RendererVK::new_frame() {
    FrameSync& frame_sync = frame_sync_[sync_id_];
    CommandBufferVK& cmd_buf = cmd_buf_[frame_id_];
    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    for (auto swapchain : swapchains) {
        swapchain->acquire(device_);
    }

    vkWaitForFences(device_, 1, &fences_[frame_id_], VK_TRUE, UINT64_MAX);
    resource_disposal_.flush(device_, instance_, allocator_, frame_id_);
    descriptor_stream_.reset(device_, frame_id_);
    buffer_descriptor_writes_.resize(0);
    write_descriptor_sets_.resize(0);
    vkResetCommandPool(device_, cmd_buf.cmd_pool, 0);
    vkBeginCommandBuffer(cmd_buf.cmd_buffer, &begin_info);

    ImGui_ImplVulkan_NewFrame();

    resource_disposal_.current_frame_id = frame_id_;
    current_frame_sync_ = &frame_sync;
    current_cb_ = cmd_buf.cmd_buffer;
    cmd_buf.immediate_vtx_offset = 0;
    cmd_buf.immediate_idx_offset = 0;
    cmd_buf.polygon_vtx_offset = 0;

    // g_vsync_provider->wait_for_vblank();
}

void RendererVK::end_frame() {
    vkEndCommandBuffer(current_cb_);

    for (auto swapchain : added_swapchains) {
        swapchain->acquire(device_);
        swapchains.push_back(swapchain);
    }

    for (auto swapchain : swapchains) {
        uint32_t sync_id = swapchain->sync_id;
        image_acquired_semaphore.push_back(swapchain->image_acquire_semaphore[swapchain->sync_id]);
        swapchain_image_wait_stage.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        swapchain->sync_id = (sync_id + 1) % swapchain->num_sync;
    }

    VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = (uint32_t)image_acquired_semaphore.size(),
        .pWaitSemaphores = image_acquired_semaphore.data(),
        .pWaitDstStageMask = swapchain_image_wait_stage.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &current_cb_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &current_frame_sync_->render_finished_semaphore,
    };

    vkResetFences(device_, 1, &fences_[frame_id_]);
    vkQueueSubmit(graphics_queue_, 1, &submit, fences_[frame_id_]);
    frame_id_ = (frame_id_ + 1) % frame_latency_;
    sync_id_ = (sync_id_ + 1) % sync_count_;
    image_acquired_semaphore.resize(0);
    swapchain_image_wait_stage.resize(0);
    added_swapchains.resize(0);
}

void RendererVK::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) {
}

void RendererVK::begin_draw(Framebuffer* framebuffer, const ImVec4& clear_color) {
    FramebufferVK* fb = (!framebuffer) ? &main_swapchain_->fb : static_cast<FramebufferVK*>(framebuffer);

    ImageVK& winding_image = winding_images_[frame_id_];
    if (winding_image.image == VK_NULL_HANDLE || winding_image.width < fb->width || winding_image.height < fb->height) {
        if (winding_image.image) {
            resource_disposal_.dispose_image(&winding_image);
        }

        VkImageCreateInfo image_info {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R32_SINT,
            .extent = {(uint32_t)fb->width, (uint32_t)fb->height, 1u},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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

        VK_CHECK(vmaCreateImage(allocator_, &image_info, &alloc_info, &winding_image.image, &winding_image.allocation,
                                nullptr));
        view_info.image = winding_image.image;
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &winding_image.view));
        winding_image.width = fb->width;
        winding_image.height = fb->height;
        winding_image.current_access = {};
    }

    VkImageMemoryBarrier winding_barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = winding_image.current_access.access,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = winding_image.current_access.layout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = graphics_queue_index_,
        .dstQueueFamilyIndex = graphics_queue_index_,
        .image = winding_image.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(current_cb_, winding_image.current_access.stages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &winding_barrier);

    VkClearColorValue color_value {};
    vkCmdClearColorImage(current_cb_, winding_image.image, VK_IMAGE_LAYOUT_GENERAL, &color_value, 1,
                         &winding_barrier.subresourceRange);

    winding_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    winding_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    winding_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    winding_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(current_cb_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &winding_barrier);

    winding_image.current_access = {
        .stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .access = winding_barrier.dstAccessMask,
        .layout = winding_barrier.newLayout,
    };

    fb->image_id = (fb->image_id + 1) % frame_latency_;
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

    VkImageMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = graphics_queue_index_;
    barrier.dstQueueFamilyIndex = graphics_queue_index_;
    barrier.image = fb->image[image_id];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stages;
    if (!fb->window_framebuffer) {
        src_stages = fb->current_access[image_id].stages;
        barrier.srcAccessMask = fb->current_access[image_id].access;
        barrier.oldLayout = fb->current_access[image_id].layout;
    } else {
        src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    vkCmdPipelineBarrier(current_cb_, src_stages, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    vkCmdBeginRenderPass(current_cb_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkRect2D rect {
        .offset = {0, 0},
        .extent = {(uint32_t)fb_width, (uint32_t)fb_height},
    };
    vkCmdSetScissor(current_cb_, 0, 1, &rect);

    VkViewport vp {
        .width = (float)fb->width,
        .height = (float)fb->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(current_cb_, 0, 1, &vp);

    fb_width = fb->width;
    fb_height = fb->height;
    vp_width = 2.0f / vp.width;
    vp_height = 2.0f / vp.height;

    current_framebuffer_ = fb;
}

void RendererVK::finish_draw() {
    vkCmdEndRenderPass(current_cb_);

    if (current_framebuffer_->window_framebuffer) {
        uint32_t image_id = current_framebuffer_->image_id;
        VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags dst_access = 0;
        VkImageLayout new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkImageMemoryBarrier barrier;
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = graphics_queue_index_;
        barrier.dstQueueFamilyIndex = graphics_queue_index_;
        barrier.image = current_framebuffer_->image[image_id];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(current_cb_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, dst_stage, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
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

        vkCmdPipelineBarrier(current_cb_, img_access.stages, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        img_access.stages = dst_stage;
        img_access.access = dst_access;
        img_access.layout = new_layout;
    }

    return (ImTextureID)fb->descriptor_set[fb->image_id];
}

void RendererVK::fill_polygon(const ImVec2* points, uint32_t count) {
}

void RendererVK::fill_path(const Path& path, uint32_t color) {
}

void RendererVK::draw_waveforms(const ImVector<ClipContentDrawCmd>& clips) {
    VkBuffer current_buffer {};
    uint32_t current_channel = 0;

    float fb_width_f32 = (float)fb_width;
    float fb_height_f32 = (float)fb_height;

    for (auto& clip : clips) {
        if (clip.min_bb.y >= fb_height_f32 || clip.max_bb.y < 0.0f)
            continue;
        if (clip.min_bb.x >= fb_width_f32 || clip.max_bb.x < 0.0f)
            continue;

        SamplePeaksVK* peaks = static_cast<SamplePeaksVK*>(clip.peaks);
        const SamplePeaksMipVK& mip = peaks->mipmap[clip.mip_index];
        uint32_t channel = clip.channel;
        VkBuffer buffer = mip.buffer;

        if (current_buffer != buffer) {
            VkDescriptorSet descriptor_set =
                descriptor_stream_.allocate_descriptor_set(device_, waveform_layout.set_layout[0], 0, 1, 0, 0);

            VkDescriptorBufferInfo buffer_descriptor {buffer, 0, VK_WHOLE_SIZE};

            VkWriteDescriptorSet write_descriptor {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_descriptor,
            };

            current_buffer = buffer;
            vkUpdateDescriptorSets(device_, 1, &write_descriptor, 0, {});
            vkCmdBindDescriptorSets(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, waveform_layout.layout, 0, 1,
                                    &descriptor_set, 0, nullptr);
        }

        int32_t x0 = std::max((int32_t)clip.min_bb.x, 0);
        int32_t y0 = std::max((int32_t)clip.min_bb.y, 0);
        int32_t x1 = std::min((int32_t)clip.max_bb.x, fb_width);
        int32_t y1 = std::min((int32_t)clip.max_bb.y, fb_height);
        uint32_t vertex_count = clip.draw_count * 2;

        VkRect2D rect {
            .offset = {x0, y0},
            .extent = {uint32_t(x1 - x0), uint32_t(y1 - y0)},
        };
        vkCmdSetScissor(current_cb_, 0, 1, &rect);
        vkCmdBindPipeline(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, waveform_fill);

        ClipContentDrawCmdVK draw_cmd {
            .origin_x = clip.min_bb.x + 0.5f,
            .origin_y = clip.min_bb.y,
            .scale_x = clip.scale_x,
            .scale_y = clip.max_bb.y - clip.min_bb.y,
            .color = clip.color,
            .vp_width = vp_width,
            .vp_height = vp_height,
            .is_min = 0,
            .channel = channel,
            .start_idx = clip.start_idx,
            .sample_count = mip.sample_count,
        };

        vkCmdPushConstants(current_cb_, waveform_layout.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ClipContentDrawCmdVK),
                           &draw_cmd);
        vkCmdDraw(current_cb_, vertex_count, 1, 0, 0);

        vkCmdBindPipeline(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, waveform_aa);
        vkCmdDraw(current_cb_, vertex_count * 3, 1, 0, 0);
        draw_cmd.is_min = 1;
        vkCmdPushConstants(current_cb_, waveform_layout.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ClipContentDrawCmdVK),
                           &draw_cmd);
        vkCmdDraw(current_cb_, vertex_count * 3, 1, 0, 0);
    }

    VkRect2D scissor = {{0, 0}, {(uint32_t)current_framebuffer_->width, (uint32_t)current_framebuffer_->height}};
    vkCmdSetScissor(current_cb_, 0, 1, &scissor);

    // vkUpdateDescriptorSets(device_, write_descriptor_sets_.size())
}

void RendererVK::render_draw_command_list(DrawCommandList* command_list) {
    if (command_list->commands_.size() == 0)
        return;

    CommandBufferVK& cmd_buf = cmd_buf_[frame_id_];
    ImageVK& winding_image = winding_images_[frame_id_];
    uint32_t required_vtx_count = command_list->vtx_offset_;
    uint32_t new_vtx_count = cmd_buf.polygon_vtx_offset + required_vtx_count;
    if (cmd_buf.polygon_buffer == VK_NULL_HANDLE || new_vtx_count > cmd_buf.total_vtx_count) {
        ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData;
        VkDeviceSize buffer_size = AlignBufferSize(new_vtx_count * sizeof(ImVec2), bd->BufferMemoryAlignment);
        create_or_resize_buffer(cmd_buf.polygon_buffer, cmd_buf.polygon_buffer_mem, cmd_buf.polygon_buffer_size,
                                buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        VK_CHECK(vkMapMemory(device_, cmd_buf.polygon_buffer_mem, 0, VK_WHOLE_SIZE, 0, (void**)&cmd_buf.polygon_vtx));
        cmd_buf.polygon_vtx_count = new_vtx_count;
    }

    ImVec2* vtx_dst = cmd_buf.polygon_vtx + cmd_buf.polygon_vtx_offset;
    std::memcpy(vtx_dst, command_list->vtx_buffer_.data(), command_list->vtx_buffer_.size() * sizeof(ImVec2));

    VkMappedMemoryRange range {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = cmd_buf.polygon_buffer_mem;
    range.size = VK_WHOLE_SIZE;
    VK_CHECK(vkFlushMappedMemoryRanges(device_, 1, &range));

    VkDescriptorSet polygon_buffer_descriptor =
        descriptor_stream_.allocate_descriptor_set(device_, vector_ras_layout.set_layout[0], 0, 1, 0, 1);

    VkDescriptorBufferInfo buffer_descriptor {
        .buffer = cmd_buf.polygon_buffer,
        .offset = cmd_buf.polygon_vtx_offset,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorImageInfo image_descriptor {
        .imageView = winding_image.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkWriteDescriptorSet write_descriptor[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = polygon_buffer_descriptor,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer_descriptor,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = polygon_buffer_descriptor,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &image_descriptor,
        },
    };

    cmd_buf.polygon_vtx_offset += new_vtx_count;
    vkUpdateDescriptorSets(device_, 2, write_descriptor, 0, nullptr);
    vkCmdBindDescriptorSets(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_ras_layout.layout, 0, 1,
                            &polygon_buffer_descriptor, 0, nullptr);

    for (const auto& command : command_list->commands_) {
        switch (command.type) {
            case DrawCommand::Rasterize: {
                VectorDrawCmdVK cmd_data {
                    .inv_viewport = {vp_width, vp_height},
                    .min_bb = command.rasterize.fill_rect.Min,
                    .max_bb = command.rasterize.fill_rect.Max,
                    .vtx_offset = command.rasterize.vtx_offset,
                };
                vkCmdPushConstants(current_cb_, vector_ras_layout.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(VectorDrawCmdVK), &cmd_data);
                vkCmdBindPipeline(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_ras);
                vkCmdDraw(current_cb_, (command.rasterize.vtx_count - 1) * 6, 1, 0, 0);

                VkMemoryBarrier memory_barrier {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                };
                vkCmdPipelineBarrier(current_cb_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1,
                                     &memory_barrier, 0, nullptr, 0, nullptr);

                break;
            }
            case DrawCommand::Fill: {
                VectorDrawCmdVK cmd_data {
                    .inv_viewport = {vp_width, vp_height},
                    .min_bb = command.fill.fill_rect.Min,
                    .max_bb = command.fill.fill_rect.Max,
                    .color = command.fill.color,
                };
                vkCmdPushConstants(current_cb_, vector_ras_layout.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(VectorDrawCmdVK), &cmd_data);
                vkCmdBindPipeline(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_fill);
                vkCmdDraw(current_cb_, 4, 1, 0, 0);

                break;
            }
        }
    }
}

// We slightly modified ImGui_ImplVulkan_RenderDrawData function to fit our vulkan backend
void RendererVK::render_imgui_draw_data(ImDrawData* draw_data) {
    if (draw_data->CmdListsCount == 0)
        return;

    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData;
    ImGui_ImplVulkan_FrameRenderBuffers* rb = &render_buffers_[frame_id_];
    CommandBufferVK& cmd_buf = cmd_buf_[frame_id_];
    VkPipeline pipeline = bd->Pipeline;

    uint32_t new_total_vtx_count = cmd_buf.immediate_vtx_offset + draw_data->TotalVtxCount;
    uint32_t new_total_idx_count = cmd_buf.immediate_idx_offset + draw_data->TotalIdxCount;
    if (new_total_vtx_count > cmd_buf.total_vtx_count)
        cmd_buf.total_vtx_count = new_total_vtx_count;
    if (new_total_idx_count > cmd_buf.total_idx_count)
        cmd_buf.total_idx_count = new_total_idx_count;

    // Create or resize the vertex/index buffers
    size_t vertex_size = AlignBufferSize(cmd_buf.total_vtx_count * sizeof(ImDrawVert), bd->BufferMemoryAlignment);
    size_t index_size = AlignBufferSize(cmd_buf.total_idx_count * sizeof(ImDrawIdx), bd->BufferMemoryAlignment);

    if (rb->VertexBuffer == VK_NULL_HANDLE || rb->VertexBufferSize < vertex_size) {
        create_or_resize_buffer(rb->VertexBuffer, rb->VertexBufferMemory, rb->VertexBufferSize, vertex_size,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VK_CHECK(
            vkMapMemory(device_, rb->VertexBufferMemory, 0, rb->VertexBufferSize, 0, (void**)&cmd_buf.immediate_vtx));
        cmd_buf.immediate_vtx_offset = 0;
        // Log::debug("Resizing immediate vertex buffer: {}", frame_id_);
    }

    if (rb->IndexBuffer == VK_NULL_HANDLE || rb->IndexBufferSize < index_size) {
        create_or_resize_buffer(rb->IndexBuffer, rb->IndexBufferMemory, rb->IndexBufferSize, index_size,
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        VK_CHECK(
            vkMapMemory(device_, rb->IndexBufferMemory, 0, rb->IndexBufferSize, 0, (void**)&cmd_buf.immediate_idx));
        cmd_buf.immediate_idx_offset = 0;
        // Log::debug("Resizing immediate index buffer: {}", frame_id_);
    }

    ImDrawVert* vtx_dst = cmd_buf.immediate_vtx + cmd_buf.immediate_vtx_offset;
    ImDrawIdx* idx_dst = cmd_buf.immediate_idx + cmd_buf.immediate_idx_offset;

    // Upload vertex/index data into a single contiguous GPU buffer
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }

    // Just flush, don't unmap
    VkMappedMemoryRange range[2] = {};
    range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range[0].memory = rb->VertexBufferMemory;
    range[0].size = VK_WHOLE_SIZE;
    range[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range[1].memory = rb->IndexBufferMemory;
    range[1].size = VK_WHOLE_SIZE;
    VK_CHECK(vkFlushMappedMemoryRanges(device_, 2, range));

    // Setup desired Vulkan state
    setup_imgui_render_state(draw_data, pipeline, current_cb_, rb, fb_width, fb_height);

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    int global_vtx_offset = cmd_buf.immediate_vtx_offset;
    int global_idx_offset = cmd_buf.immediate_idx_offset;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user
                // to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    setup_imgui_render_state(draw_data, pipeline, current_cb_, rb, fb_width, fb_height);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as vkCmdSetScissor() won't accept values that are off
                // bounds
                if (clip_min.x < 0.0f) {
                    clip_min.x = 0.0f;
                }
                if (clip_min.y < 0.0f) {
                    clip_min.y = 0.0f;
                }
                if (clip_max.x > fb_width) {
                    clip_max.x = (float)fb_width;
                }
                if (clip_max.y > fb_height) {
                    clip_max.y = (float)fb_height;
                }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                VkRect2D scissor;
                scissor.offset.x = (int32_t)(clip_min.x);
                scissor.offset.y = (int32_t)(clip_min.y);
                scissor.extent.width = (uint32_t)(clip_max.x - clip_min.x);
                scissor.extent.height = (uint32_t)(clip_max.y - clip_min.y);
                vkCmdSetScissor(current_cb_, 0, 1, &scissor);

                // Bind DescriptorSet with font or user texture
                VkDescriptorSet desc_set[1] = {(VkDescriptorSet)pcmd->TextureId};
                if (sizeof(ImTextureID) < sizeof(ImU64)) {
                    // We don't support texture switches if ImTextureID hasn't been redefined to
                    // be 64-bit. Do a flaky check that other textures haven't been used.
                    IM_ASSERT(pcmd->TextureId == (ImTextureID)bd->FontDescriptorSet);
                    desc_set[0] = bd->FontDescriptorSet;
                }
                vkCmdBindDescriptorSets(current_cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, bd->PipelineLayout, 0, 1,
                                        desc_set, 0, nullptr);

                // Draw
                vkCmdDrawIndexed(current_cb_, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset,
                                 pcmd->VtxOffset + global_vtx_offset, 0);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
    cmd_buf.immediate_vtx_offset = global_vtx_offset;
    cmd_buf.immediate_idx_offset = global_idx_offset;

    // Restore the state
    VkRect2D scissor = {{0, 0}, {(uint32_t)current_framebuffer_->width, (uint32_t)current_framebuffer_->height}};
    vkCmdSetScissor(current_cb_, 0, 1, &scissor);

    VkViewport vp {
        .width = (float)current_framebuffer_->width,
        .height = (float)current_framebuffer_->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(current_cb_, 0, 1, &vp);
}

bool RendererVK::add_viewport(ImGuiViewport* viewport) {
    SwapchainVK* swapchain = new SwapchainVK();
    VkSurfaceKHR surface;
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    VK_CHECK(platform_io.Platform_CreateVkSurface(viewport, (ImU64)instance_, nullptr, (ImU64*)&surface));
    viewport->RendererUserData = &swapchain->fb;
    swapchain->surface = surface;
    swapchain->viewport = viewport;
    added_swapchains.push_back(swapchain);
    vkDeviceWaitIdle(device_);
    return create_or_recreate_swapchain(swapchain);
}

bool RendererVK::remove_viewport(ImGuiViewport* viewport) {
    SwapchainVK* removed_swapchain = nullptr;
    Vector<SwapchainVK*> new_swapchains;
    uint32_t index = 0;
    for (auto swapchain : swapchains) {
        FramebufferVK* framebuffer = (FramebufferVK*)viewport->RendererUserData;
        if (swapchain == framebuffer->parent_swapchain) {
            removed_swapchain = swapchain;
            continue;
        }
        new_swapchains.push_back(swapchain);
    }
    Log::debug("Resize viewport ({}, {}): {}", removed_swapchain->image_index, frame_id_, viewport->ID);
    if (removed_swapchain) {
        swapchains = std::move(new_swapchains);
        resource_disposal_.dispose_swapchain(removed_swapchain, removed_swapchain->surface);
        viewport->RendererUserData = nullptr;
        delete removed_swapchain;
    }
    return true;
}

void RendererVK::resize_viewport(ImGuiViewport* viewport, ImVec2 vec) {
    if (GImGui->Viewports[0] == viewport) {
        vkDeviceWaitIdle(device_);
        create_or_recreate_swapchain(main_swapchain_);
        return;
    }
    FramebufferVK* framebuffer = (FramebufferVK*)viewport->RendererUserData;
    vkDeviceWaitIdle(device_);
    create_or_recreate_swapchain(framebuffer->parent_swapchain);
    framebuffer->parent_swapchain->acquire(device_);
    Log::debug("Resize viewport ({}, {}): {}", framebuffer->parent_swapchain->image_index, frame_id_, viewport->ID);
    // init_swapchain_();
}

void RendererVK::present() {
    for (auto swapchain : swapchains) {
        swapchain_present.push_back(swapchain->swapchain);
        sc_image_index_present.push_back(swapchain->image_index);
    }
    swapchain_results.resize(swapchains.size());

    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_frame_sync_->render_finished_semaphore,
        .swapchainCount = (uint32_t)swapchain_present.size(),
        .pSwapchains = swapchain_present.data(),
        .pImageIndices = sc_image_index_present.data(),
        .pResults = swapchain_results.data(),
    };
    vkQueuePresentKHR(graphics_queue_, &present_info);

    for (uint32_t i = 0; i < (uint32_t)swapchains.size(); i++) {
        VkResult result = swapchain_results[i];
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            vkDeviceWaitIdle(device_);
            create_or_recreate_swapchain(swapchains[i]);
        }
    }

    swapchain_present.resize(0);
    sc_image_index_present.resize(0);
    swapchain_results.resize(0);
}

bool RendererVK::create_or_recreate_swapchain(SwapchainVK* swapchain) {
    VkSurfaceKHR surface = swapchain->surface;
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

    // Find desired present mode
    VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    /*for (uint32_t i = 0; i < present_mode_count; i++) {
        if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            selected_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        }
    }*/

    // Fallback to FIFO if desired present mode is not supported
    if (selected_present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
        for (uint32_t i = 0; i < present_mode_count; i++) {
            if (present_modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
                selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
                break;
            }
        }
    }

    if (selected_present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
        return false;
    }

    VkSwapchainCreateInfoKHR swapchain_info {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = VULKAN_MAX_BUFFER_SIZE,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = surface_caps.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = selected_present_mode,
        .clipped = VK_FALSE,
        .oldSwapchain = swapchain->swapchain,
    };

    VkSwapchainKHR vk_swapchain = swapchain->swapchain;
    if (vk_swapchain != VK_NULL_HANDLE) {
        resource_disposal_.dispose_swapchain(swapchain, nullptr);
    }

    VkResult result = vkCreateSwapchainKHR(device_, &swapchain_info, nullptr, &vk_swapchain);
    if (VK_FAILED(result)) {
        return false;
    }

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

    swapchain->surface = surface;
    swapchain->swapchain = vk_swapchain;
    swapchain->num_sync = sync_count_;
    swapchain->sync_id = 0;
    swapchain->image_index = 0;
    swapchain->fb.parent_swapchain = swapchain;
    swapchain->fb.window_framebuffer = true;
    swapchain->fb.num_buffers = 2;
    swapchain->fb.image_id = frame_latency_ - 1;
    swapchain->fb.width = fb_info.width;
    swapchain->fb.height = fb_info.height;

    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(device_, vk_swapchain, &swapchain_image_count, nullptr);
    vkGetSwapchainImagesKHR(device_, vk_swapchain, &swapchain_image_count, swapchain->fb.image);

    for (uint32_t i = 0; i < sync_count_; i++) {
        VK_CHECK(vkCreateSemaphore(device_, &semaphore, nullptr, &swapchain->image_acquire_semaphore[i]));
    }

    for (uint32_t i = 0; i < frame_latency_; i++) {
#if VULKAN_ENABLE_VALIDATION_AND_DEBUG_MSG
        char obj_name[64] {};
        fmt::format_to(obj_name, "Swapchain Image {}", i);
        debug_info.pObjectName = obj_name;
        debug_info.objectType = VK_OBJECT_TYPE_IMAGE;
        debug_info.objectHandle = (uint64_t)swapchain->fb.image[i];
        vkSetDebugUtilsObjectNameEXT(device_, &debug_info);
#endif
        view_info.image = swapchain->fb.image[i];
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &swapchain->fb.view[i]));

#if VULKAN_ENABLE_VALIDATION_AND_DEBUG_MSG
        fmt::format_to(obj_name, "Swapchain Image View {}", i);
        debug_info.pObjectName = obj_name;
        debug_info.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
        debug_info.objectHandle = (uint64_t)swapchain->fb.view[i];
        vkSetDebugUtilsObjectNameEXT(device_, &debug_info);
#endif
        fb_info.pAttachments = &swapchain->fb.view[i];
        VK_CHECK(vkCreateFramebuffer(device_, &fb_info, nullptr, &swapchain->fb.framebuffer[i]));
    }

    return true;
}

void RendererVK::create_or_resize_buffer(VkBuffer& buffer, VkDeviceMemory& buffer_memory, VkDeviceSize& buffer_size,
                                         size_t new_size, VkBufferUsageFlagBits usage) {
    ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData;
    ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
    if (buffer != VK_NULL_HANDLE && buffer_memory != VK_NULL_HANDLE)
        resource_disposal_.dispose_immediate_buffer(buffer_memory, buffer);

    VkDeviceSize buffer_size_aligned =
        AlignBufferSize(std::max(v->MinAllocationSize, new_size), bd->BufferMemoryAlignment);
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size_aligned;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, buffer, &req);
    bd->BufferMemoryAlignment = (bd->BufferMemoryAlignment > req.alignment) ? bd->BufferMemoryAlignment : req.alignment;
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex =
        ImGui_ImplVulkan_MemoryType(physical_device_, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
    VK_CHECK(vkAllocateMemory(device_, &alloc_info, v->Allocator, &buffer_memory));

    VK_CHECK(vkBindBufferMemory(device_, buffer, buffer_memory, 0));
    buffer_size = buffer_size_aligned;
}

void RendererVK::setup_imgui_render_state(ImDrawData* draw_data, VkPipeline pipeline, VkCommandBuffer command_buffer,
                                          ImGui_ImplVulkan_FrameRenderBuffers* rb, int fb_width, int fb_height) {
    ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData;

    // Bind pipeline:
    { vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); }

    // Bind Vertex And Index Buffer:
    if (draw_data->TotalVtxCount > 0) {
        VkBuffer vertex_buffers[1] = {rb->VertexBuffer};
        VkDeviceSize vertex_offset[1] = {0};
        vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, vertex_offset);
        vkCmdBindIndexBuffer(command_buffer, rb->IndexBuffer, 0,
                             sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    }

    // Setup viewport:
    {
        VkViewport viewport;
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = (float)fb_width;
        viewport.height = (float)fb_height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    }

    // Setup scale and translation:
    // Our visible imgui space lies from draw_data->DisplayPps (top left) to
    // draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for
    // single viewport apps.
    {
        float scale[2];
        scale[0] = 2.0f / draw_data->DisplaySize.x;
        scale[1] = 2.0f / draw_data->DisplaySize.y;
        float translate[2];
        translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
        translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
        vkCmdPushConstants(command_buffer, bd->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0,
                           sizeof(float) * 2, scale);
        vkCmdPushConstants(command_buffer, bd->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2,
                           sizeof(float) * 2, translate);
    }
}

PipelineResourceLayoutVK
vk_create_pipeline_layout(VkDevice device, uint32_t push_constant_size,
                          std::initializer_list<VkDescriptorSetLayoutBinding> ds_bindings0,
                          std::initializer_list<VkDescriptorSetLayoutBinding> ds_bindings1 = {}) {
    PipelineResourceLayoutVK ret {};
    VkDescriptorSetLayoutCreateInfo set_layout_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)ds_bindings0.size(),
        .pBindings = ds_bindings0.begin(),
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &ret.set_layout[0]));

    if (ds_bindings1.size() > 0) {
        VkDescriptorSetLayoutCreateInfo set_layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = (uint32_t)ds_bindings1.size(),
            .pBindings = ds_bindings1.begin(),
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &ret.set_layout[1]));
    }

    VkPushConstantRange push_constant_range {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = push_constant_size,
    };

    VkPipelineLayoutCreateInfo pipeline_layout {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = ds_bindings1.size() > 0 ? 2u : 1u,
        .pSetLayouts = ret.set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout, nullptr, &ret.layout));
    return ret;
}

void RendererVK::init_pipelines() {
    waveform_layout = vk_create_pipeline_layout(device_, sizeof(ClipContentDrawCmdVK),
                                                {
                                                    {
                                                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                        .descriptorCount = 1,
                                                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                    },
                                                });

    waveform_aa = create_pipeline("assets/waveform_aa.vs.spv", "assets/waveform_aa.fs.spv", waveform_layout.layout,
                                  nullptr, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false);

    waveform_fill = create_pipeline("assets/waveform_fill.vs.spv", "assets/waveform_aa.fs.spv", waveform_layout.layout,
                                    nullptr, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, false, false);

    vector_ras_layout = vk_create_pipeline_layout(device_, sizeof(VectorDrawCmdVK),
                                                  {
                                                      {
                                                          .binding = 0,
                                                          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                          .descriptorCount = 1,
                                                          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                      },
                                                      {
                                                          .binding = 1,
                                                          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                          .descriptorCount = 1,
                                                          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                      },
                                                  });

    vector_ras = create_pipeline("assets/ras.vs.spv", "assets/ras.fs.spv", vector_ras_layout.layout, nullptr,
                                 VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true);

    vector_fill = create_pipeline("assets/ras_fill.vs.spv", "assets/ras_fill.fs.spv", vector_ras_layout.layout, nullptr,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, true, false);
}

void RendererVK::destroy_pipelines() {
    vkDestroyPipeline(device_, waveform_fill, nullptr);
    vkDestroyPipeline(device_, waveform_aa, nullptr);
    waveform_layout.destroy(device_);

    vkDestroyPipeline(device_, vector_fill, nullptr);
    vkDestroyPipeline(device_, vector_ras, nullptr);
    vector_ras_layout.destroy(device_);
}

VkPipeline RendererVK::create_pipeline(const char* vs, const char* fs, VkPipelineLayout layout,
                                       const VkPipelineVertexInputStateCreateInfo* vertex_input,
                                       VkPrimitiveTopology primitive_topology, bool enable_blending,
                                       bool disable_color_writes) {
    size_t vs_size;
    void* vs_bytecode = SDL_LoadFile(vs, &vs_size);
    if (!vs_bytecode)
        return {};
    defer(SDL_free(vs_bytecode));

    size_t fs_size;
    void* fs_bytecode = SDL_LoadFile(fs, &fs_size);
    if (!fs_bytecode)
        return {};
    defer(SDL_free(fs_bytecode));

    VkShaderModuleCreateInfo module_info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    VkShaderModule vs_module;
    module_info.codeSize = vs_size;
    module_info.pCode = (const uint32_t*)vs_bytecode;
    if (VK_FAILED(vkCreateShaderModule(device_, &module_info, nullptr, &vs_module)))
        return {};
    defer(vkDestroyShaderModule(device_, vs_module, nullptr));

    VkShaderModule fs_module;
    module_info.codeSize = fs_size;
    module_info.pCode = (const uint32_t*)fs_bytecode;
    if (VK_FAILED(vkCreateShaderModule(device_, &module_info, nullptr, &fs_module)))
        return {};
    defer(vkDestroyShaderModule(device_, fs_module, nullptr));

    static const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
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
        }};

    VkPipelineVertexInputStateCreateInfo empty_vertex_input {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexAttributeDescriptions = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = primitive_topology,
    };

    VkPipelineViewportStateCreateInfo viewport {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterization {
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
        .blendEnable = enable_blending,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    if (disable_color_writes) {
        color_attachment.colorWriteMask = 0;
    }

    VkPipelineColorBlendStateCreateInfo blend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = IM_ARRAYSIZE(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    VkGraphicsPipelineCreateInfo pipeline_info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = vertex_input ? vertex_input : &empty_vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = {},
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = {},
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic_state,
        .layout = layout,
        .renderPass = fb_render_pass_,
        .subpass = 0,
        .basePipelineHandle = {},
        .basePipelineIndex = {},
    };

    VkPipeline pipeline;
    if (VK_FAILED(vkCreateGraphicsPipelines(device_, nullptr, 1, &pipeline_info, nullptr, &pipeline)))
        return {};

    return pipeline;
}

Renderer* RendererVK::create(SDL_Window* window) {
    if (VK_FAILED(volkInitialize()))
        return nullptr;

    uint32_t vulkan_api_version = VKB_VK_API_VERSION_1_1;
    auto inst_ret = vkb::InstanceBuilder()
                        .set_app_name("wb_vulkan")
#if VULKAN_ENABLE_VALIDATION_AND_DEBUG_MSG
                        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                        .request_validation_layers()
                        .use_default_debug_messenger()
#endif
                        .require_api_version(vulkan_api_version)
                        .set_minimum_instance_version(vulkan_api_version)
                        .build();

    if (!inst_ret) {
        Log::error("Failed to create vulkan instance. Error: {}", inst_ret.error().message());
        return nullptr;
    }

    vkb::Instance instance = inst_ret.value();

    SDL_SysWMinfo wm_info {};
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
    volkLoadInstanceOnly(instance);

    VkSurfaceKHR surface;
#ifdef WB_PLATFORM_WINDOWS
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
#endif

#ifdef WB_PLATFORM_LINUX
#ifdef VK_USE_PLATFORM_XLIB_KHR
    Display* display = wm_info.info.x11.display;

    VkXlibSurfaceCreateInfoKHR surface_info {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = display,
        .window = wm_info.info.x11.window,
    };

    if (VK_FAILED(vkCreateXlibSurfaceKHR(instance, &surface_info, nullptr, &surface))) {
        Log::error("Failed to create window surface");
        vkb::destroy_instance(instance);
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

    auto selected_physical_device = vkb::PhysicalDeviceSelector(instance)
                                        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                        .allow_any_gpu_device_type(false)
                                        .set_surface(surface)
                                        .require_present(true)
                                        .select();

    if (!selected_physical_device) {
        Log::error("Failed to find suitable Vulkan device");
        vkb::destroy_instance(instance);
        return nullptr;
    }

    auto physical_device = selected_physical_device.value();
    vkGetPhysicalDeviceFeatures(physical_device, &physical_device.features);

    VkPhysicalDeviceFeatures2 features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = physical_device.features,
    };

    vkGetPhysicalDeviceFeatures2(physical_device, &features);

    auto device_builder = vkb::DeviceBuilder(physical_device).add_pNext(&features);
    auto device_result = device_builder.build();
    if (!device_result) {
        Log::error("Failed to create Vulkan device. Error: {}", device_result.error().message());
        vkb::destroy_instance(instance);
        return nullptr;
    }

    vkb::Device device = device_result.value();
    VkPhysicalDevice vulkan_physical_device = physical_device;
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

    RendererVK* renderer = new (std::nothrow) RendererVK(instance, instance.debug_messenger, vulkan_physical_device,
                                                         device, graphics_queue_index, present_queue_index);

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

    SwapchainVK* swapchain = new SwapchainVK();
    swapchain->surface = surface;
    renderer->create_or_recreate_swapchain(swapchain);
    renderer->swapchains.push_back(swapchain);
    renderer->main_swapchain_ = swapchain;

    return renderer;
}

} // namespace wb
