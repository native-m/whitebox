#pragma once

#include "core/memory.h"
#include "core/vector.h"
#include "renderer2.h"
#include "vk_stub.h"
#include <deque>
#include <mutex>

#define WB_VULKAN_SYNC_COUNT (WB_GPU_RENDER_BUFFER_SIZE + 1)

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
    void* persistent_map_ptr[WB_GPU_RENDER_BUFFER_SIZE];
};

struct GPUTextureVK : public GPUTexture {
    VkImage image[WB_GPU_RENDER_BUFFER_SIZE] {};
    VmaAllocation allocation[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkImageView view[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkFramebuffer fb[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkImageLayout layout[WB_GPU_RENDER_BUFFER_SIZE] {};
    GPUViewportDataVK* parent_viewport {};
    bool window_framebuffer {};
};

struct GPUPipelineVK : public GPUPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct GPUViewportDataVK : public GPUViewportData {
    ImGuiViewport* viewport {};
    VkSwapchainKHR swapchain {};
    VkSurfaceKHR surface {};
    VkSemaphore image_acquire_semaphore[WB_VULKAN_SYNC_COUNT];
    uint32_t sync_id;
    uint32_t image_id;
    uint32_t num_sync {};
    bool need_rebuild = false;

    VkResult acquire(VkDevice device);
};

struct GPUUploadItemVK {
    enum {
        Buffer,
        Image
    };

    uint32_t type;
    uint32_t width; // If this is a buffer upload, this will be the size of the buffer
    uint32_t height;
    VkImageLayout old_layout;
    VkImageLayout new_layout;
    VkBuffer src_buffer;
    VmaAllocation src_allocation;

    union {
        VkImage dst_image;
        VkBuffer dst_buffer;
    };
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

struct GPUPipelineDisposalVK {
    VkPipeline pipeline;
    VkPipelineLayout layout;
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
        Pipeline,
        Swapchain,
        SyncObject,
    };

    uint64_t frame_stamp;
    uint32_t type;

    union {
        GPUBufferDisposalVK buffer;
        GPUTextureDisposalVK texture;
        GPUPipelineDisposalVK pipeline;
        GPUSwapchainDisposalVK swapchain;
        GPUSyncObjectDisposalVK sync_obj;
    };
};

struct GPUDescriptorStreamChunkVK {
    VkDescriptorPool pool;
    uint32_t max_descriptors;
    uint32_t num_storage_buffers;
    uint32_t num_sampled_images;
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
    VkRenderPass fb_rp_rgba_{};
    VkRenderPass fb_rp_bgra_{};
    VkSampler common_sampler_ {};

    VmaPool staging_pool_ {};
    std::deque<GPUUploadItemVK> pending_uploads_;
    VkCommandPool upload_cmd_pool_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkCommandBuffer upload_cmd_buf_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkSemaphore upload_finished_semaphore_[WB_GPU_RENDER_BUFFER_SIZE] {};
    uint32_t upload_id_ = WB_GPU_RENDER_BUFFER_SIZE - 1;

    Vector<GPUViewportDataVK*> viewports;
    Vector<GPUViewportDataVK*> added_viewports;

    GPUDescriptorStreamVK descriptor_stream_;
    VkDescriptorSetLayout texture_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout storage_buffer_set_layout_ = VK_NULL_HANDLE;
    VkFence fences_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkSemaphore render_finished_semaphore_[WB_VULKAN_SYNC_COUNT] {};
    VkSemaphore current_render_finished_semaphore_ {};
    VkCommandPool cmd_pool_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkCommandBuffer cmd_buf_[WB_GPU_RENDER_BUFFER_SIZE] {};
    VkCommandBuffer current_cb_ {};
    uint32_t sync_id_ = 0;
    uint64_t frame_count_ = 0;
    uint32_t num_sync_ = WB_VULKAN_SYNC_COUNT;
    uint32_t num_inflight_frames_ = WB_GPU_RENDER_BUFFER_SIZE;
    InplaceList<GPUResource> active_resources_list_;

    GPUTextureVK* current_rt_ = {};
    bool render_pass_started_ = false;
    bool should_clear_fb_ = false;
    VkRenderPass current_render_pass_;
    VkFramebuffer current_fb_;
    VkClearValue rp_clear_color_;

    Pool<GPUBufferVK> buffer_pool_ {};
    Pool<GPUTextureVK> texture_pool_ {};
    Pool<GPUPipelineVK> pipeline_pool_ {};
    std::mutex mtx_;
    std::deque<GPUResourceDisposeItemVK> resource_disposal_;

    // NOTE(native-m): Use arena allocator to allocate temporary data
    Vector<VkResult> swapchain_results;
    Vector<VkSemaphore> submit_wait_semaphores;
    Vector<VkSwapchainKHR> swapchain_present;
    Vector<VkPipelineStageFlags> submit_wait_stages;
    Vector<uint32_t> sc_image_index_present;

    GPURendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR main_surface,
                  uint32_t graphics_queue_index, uint32_t present_queue_index);

    bool init(SDL_Window* window) override;
    void shutdown() override;

    GPUBuffer* create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, bool dedicated_allocation, size_t init_size,
                             const void* init_data) override;
    GPUTexture* create_texture(GPUTextureUsageFlags usage, GPUFormat format, uint32_t w, uint32_t h,
                               bool dedicated_allocation, uint32_t init_w, uint32_t init_h,
                               const void* init_data) override;
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

    void enqueue_resource_upload_(VkBuffer buffer, uint32_t size, const void* data);
    void enqueue_resource_upload_(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t w,
                                  uint32_t h, const void* data);

    void submit_pending_uploads_();
    void begin_render_pass_();
    void end_render_pass_();
    bool create_or_recreate_swapchain_(GPUViewportDataVK* vp_data);
    void dispose_buffer_(GPUBufferVK* buffer);
    void dispose_texture_(GPUTextureVK* texture);
    void dispose_pipeline_(GPUPipelineVK* pipeline);
    void dispose_viewport_data_(GPUViewportDataVK* vp_data, VkSurfaceKHR surface);
    void dispose_resources_(uint64_t frame_count);

    static GPURenderer* create(SDL_Window* window);
};
} // namespace wb
