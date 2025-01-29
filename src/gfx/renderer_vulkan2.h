#pragma once

#include "core/memory.h"
#include "core/vector.h"
#include "renderer2.h"
#include "vk_stub.h"
#include <deque>
#include <mutex>
#include <optional>

#define WB_VULKAN_IMAGE_DESCRIPTOR_SET_SLOT 0
#define WB_VULKAN_BUFFER_DESCRIPTOR_SET_SLOT 1

namespace wb {
struct GPUViewportDataVK;

struct GPUTextureAccessVK {
    VkPipelineStageFlags stages;
    VkAccessFlags mask;
    VkImageLayout layout;
};

struct GPUBufferVK : public GPUBuffer {
    VkBuffer buffer[WB_GPU_RENDER_BUFFER_SIZE];
    VmaAllocation allocation[WB_GPU_RENDER_BUFFER_SIZE];
    uint32_t buffer_id {};
    uint32_t num_buffers {};
};

struct GPUTextureVK : public GPUTexture {
    VkImage image[WB_GPU_RENDER_BUFFER_SIZE] {};
    VmaAllocation allocation[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkImageView view[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkFramebuffer fb[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkImageLayout layout[WB_GPU_RENDER_BUFFER_SIZE] {};
    GPUViewportDataVK* parent_viewport {};
    uint32_t image_id {};
    uint32_t num_buffers {};
    bool window_framebuffer {};
};

struct GPUPipelineVK : public GPUPipeline {
    VkPipeline pipeline;
};

struct GPUViewportDataVK : public GPUViewportData {
    ImGuiViewport* viewport {};
    VkSwapchainKHR swapchain {};
    VkSurfaceKHR surface {};
    VkSemaphore image_acquire_semaphore[WB_GPU_RENDER_BUFFER_SIZE];
    uint32_t sync_id;
    uint32_t image_id;
    uint32_t num_sync {};
    bool need_rebuild = false;

    VkResult acquire(VkDevice device);
};

struct GPUBufferDisposalVK {
    VkBuffer buffer;
    VmaAllocation allocation;
};

struct GPUTextureDisposalVK {
    VkImage image;
    VkImageView view;
    VkFramebuffer fb;
    VmaAllocation allocation;
};

struct GPUSwapchainDisposalVK {
    VkSwapchainKHR swapchain;
    VkSurfaceKHR surface;
};

struct GPUSyncObjectDisposalVK {
    VkSemaphore semaphore;
};

struct GPUResourceDisposeItemVK {
    enum {
        Buffer,
        Texture,
        Swapchain,
        SyncObject,
    };

    uint64_t frame_stamp;
    uint32_t type;

    union {
        GPUBufferDisposalVK buffer;
        GPUTextureDisposalVK texture;
        GPUSwapchainDisposalVK swapchain;
        GPUSyncObjectDisposalVK sync_obj;
    };
};

struct GPUDescriptorStreamChunkVK {
    VkDescriptorPool pool;
    uint32_t max_descriptors;
    uint32_t num_uniform_buffers;
    uint32_t num_storage_buffers;
    uint32_t num_sampled_images;
    uint32_t num_storage_images;
    uint32_t max_descriptor_sets;
    uint32_t num_descriptor_sets;
    GPUDescriptorStreamChunkVK* next;
};

struct GPUDescriptorStreamVK {
    GPUDescriptorStreamChunkVK* chunk_list[WB_GPU_RENDER_BUFFER_SIZE] {};
    GPUDescriptorStreamChunkVK* current_chunk {};
    uint32_t current_frame_id {};

    VkDescriptorSet allocate_descriptor_set(VkDevice device, VkDescriptorSetLayout layout, uint32_t num_storage_buffers,
                                            uint32_t num_sampled_images);
    GPUDescriptorStreamChunkVK* create_chunk(VkDevice device, uint32_t max_descriptor_sets, uint32_t max_descriptors);
    void reset(VkDevice device, uint32_t frame_id);
    void destroy(VkDevice device);
};

struct GPURendererVK : public GPURenderer {
    VkInstance instance_ {};
    VkDebugUtilsMessengerEXT debug_messenger_ {};
    VkPhysicalDevice physical_device_ {};
    VkDevice device_ {};
    VmaAllocator allocator_ {};

    uint32_t graphics_queue_index_;
    uint32_t present_queue_index_;
    VkQueue graphics_queue_ {};
    VkQueue present_queue_ {};

    VkSurfaceKHR main_surface_ {};
    VkRenderPass fb_render_pass_ {};
    VkSampler common_sampler_ {};

    Vector<GPUViewportDataVK*> viewports;
    Vector<GPUViewportDataVK*> added_viewports;

    GPUDescriptorStreamVK descriptor_stream;
    std::optional<VkDescriptorSetLayout> texture_set_layout[4] {};
    std::optional<VkDescriptorSetLayout> storage_buffer_set_layout[4] {};
    VkFence fences_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkSemaphore render_finished_semaphore_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkCommandPool cmd_pool_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkCommandBuffer cmd_buf_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkCommandBuffer current_cb_ {};
    uint32_t sync_id_ = 0;
    uint64_t frame_count_ = 0;
    uint32_t num_inflight_frames_ = WB_GPU_RENDER_BUFFER_SIZE;
    InplaceList<GPUResource> active_resources_list_;

    VkCommandPool imm_cmd_pool_ {};
    VkCommandBuffer imm_cmd_buf_ {};

    Pool<GPUBufferVK> buffer_pool_ {};
    Pool<GPUTextureVK> texture_pool_ {};
    std::mutex mtx_;
    std::deque<GPUResourceDisposeItemVK> resource_disposal_;

    // TODO: Use arena allocator to allocate temporary data
    Vector<VkResult> swapchain_results;
    Vector<VkSemaphore> image_acquired_semaphore;
    Vector<VkSwapchainKHR> swapchain_present;
    Vector<VkPipelineStageFlags> swapchain_image_wait_stage;
    Vector<uint32_t> sc_image_index_present;

    GPURendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR main_surface,
                  uint32_t graphics_queue_index, uint32_t present_queue_index);

    bool init(SDL_Window* window) override;
    void shutdown() override;

    GPUBuffer* create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, size_t init_size, void* init_data) override;
    GPUTexture* create_texture(GPUTextureUsageFlags usage, uint32_t w, uint32_t h, size_t init_size) override;
    GPUPipeline* create_pipeline(const GPUPipelineDesc& desc) override;
    void destroy_buffer(GPUBuffer* buffer) override;
    void destroy_texture(GPUTexture* buffer) override;
    void destroy_pipeline(GPUPipeline* buffer) override;
    void add_viewport(ImGuiViewport* viewport) override;
    void remove_viewport(ImGuiViewport* viewport) override;
    void resize_viewport(ImGuiViewport* viewport, ImVec2 vec) override;

    void begin_frame() override;
    void end_frame() override;
    void present() override;

    void* map_buffer(GPUBuffer* buffer) override;
    void unmap_buffer(GPUBuffer* buffer) override;

    void begin_render(GPUTexture* render_target, const ImVec4& clear_color) override;
    void end_render() override;
    void set_shader_parameter(size_t size, const void* data) override;
    void flush_state() override;

    bool create_or_recreate_swapchain_(GPUViewportDataVK* vp_data);
    void dispose_buffer_(GPUBufferVK* buffer);
    void dispose_texture_(GPUTextureVK* texture);
    void dispose_viewport_data_(GPUViewportDataVK* vp_data, VkSurfaceKHR surface);
    void dispose_resources_(uint64_t frame_count);

    static GPURenderer* create(SDL_Window* window);
};
} // namespace wb