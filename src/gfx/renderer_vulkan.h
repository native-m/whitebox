#pragma once

#include "renderer.h"
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "vk_stub.h"

#define VULKAN_MAX_BUFFER_SIZE 2
#define VULKAN_MAX_SYNC_COUNT 3

// Reusable buffers used for rendering 1 current in-flight frame, for
// ImGui_ImplVulkan_RenderDrawData() [Please zero-clear before use!]
struct ImGui_ImplVulkan_FrameRenderBuffers {
    VkDeviceMemory VertexBufferMemory;
    VkDeviceMemory IndexBufferMemory;
    VkDeviceSize VertexBufferSize;
    VkDeviceSize IndexBufferSize;
    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
};

namespace wb {
struct ResourceDisposalVK;

struct ImageAccessVK {
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags access = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct FramebufferVK : public Framebuffer {
    VmaAllocation allocations[VULKAN_MAX_BUFFER_SIZE] {};
    VkImage image[VULKAN_MAX_BUFFER_SIZE];
    VkImageView view[VULKAN_MAX_BUFFER_SIZE];
    VkFramebuffer framebuffer[VULKAN_MAX_BUFFER_SIZE];
    VkDescriptorSet descriptor_set[VULKAN_MAX_BUFFER_SIZE] {};
    ImageAccessVK current_access[VULKAN_MAX_BUFFER_SIZE] {};
    uint32_t num_buffers {};
    uint32_t image_id {};

    ResourceDisposalVK* resource_disposal {};

    ~FramebufferVK();
    ImTextureID as_imgui_texture_id() const override;
};

struct SamplePeaksMipVK {
    VkBuffer buffer;
    VmaAllocation allocation;
    uint32_t sample_count;
};

struct SamplePeaksVK : public SamplePeaks {
    std::vector<SamplePeaksMipVK> mipmap;
    ResourceDisposalVK* resource_disposal {};

    ~SamplePeaksVK();
};

struct CommandBufferVK {
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;
    ImDrawVert* immediate_vtx;
    ImDrawIdx* immediate_idx;
    uint32_t immediate_vtx_offset;
    uint32_t immediate_idx_offset;
    uint32_t total_vtx_count;
    uint32_t total_idx_count;

    VkBuffer polygon_buffer;
    VkDeviceSize polygon_buffer_size;
    VkDeviceMemory polygon_buffer_mem;
    ImVec2* polygon_vtx;
    uint32_t polygon_vtx_offset;
    uint32_t polygon_vtx_count;
};

struct ImageVK {
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    ImageAccessVK current_access {};
    uint32_t width;
    uint32_t height;
};

struct FrameSync {
    VkSemaphore image_acquire_semaphore;
    VkSemaphore render_finished_semaphore;
};

struct BufferDisposalVK {
    uint32_t frame_id;
    VmaAllocation allocation;
    VkBuffer buffer;
};

struct ImageDisposalVK {
    uint32_t frame_id;
    VmaAllocation allocation;
    VkImage image;
    VkImageView view;
};

struct FramebufferDisposalVK {
    uint32_t frame_id;
    VmaAllocation allocation;
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
};

struct ImmediateBufferDisposalVK {
    uint32_t frame_id;
    VkDeviceMemory memory;
    VkBuffer buffer;
};

struct SwapchainDisposalVK
{
    uint32_t frame_id;
    VkSwapchainKHR swapchain;
};

// GPU resource disposal collector. Vulkan does not allow you to destroy resources while they are
// being used by the GPU. The solution is to collect first and delete them later at the end of use.
struct ResourceDisposalVK {
    uint32_t current_frame_id {};
    std::deque<BufferDisposalVK> buffer;
    std::deque<ImageDisposalVK> img;
    std::deque<FramebufferDisposalVK> fb;
    std::deque<ImmediateBufferDisposalVK> imm_buffer;
    std::deque<SwapchainDisposalVK> swapchains;
    std::mutex mtx;

    void dispose_buffer(VmaAllocation allocation, VkBuffer buf);
    void dispose_framebuffer(FramebufferVK* obj);
    void dispose_image(ImageVK* obj);
    void dispose_immediate_buffer(VkDeviceMemory buffer_memory, VkBuffer buffer);
    void dispose_swapchain(VkSwapchainKHR swapchain, FramebufferVK* fb);
    void flush(VkDevice device, VmaAllocator allocator, uint32_t frame_id_dispose);
};

struct PipelineResourceLayoutVK {
    VkDescriptorSetLayout set_layout[2] {};
    VkPipelineLayout layout {};

    void destroy(VkDevice device) {
        for (auto ds_layout : set_layout) {
            if (ds_layout) {
                vkDestroyDescriptorSetLayout(device, ds_layout, nullptr);
            }
        }
        vkDestroyPipelineLayout(device, layout, nullptr);
    }
};

struct DescriptorStreamChunkVK {
    VkDescriptorPool pool;
    uint32_t max_descriptors;
    uint32_t num_uniform_buffers;
    uint32_t num_storage_buffers;
    uint32_t num_sampled_images;
    uint32_t num_storage_images;
    uint32_t max_descriptor_sets;
    uint32_t num_descriptor_sets;
    DescriptorStreamChunkVK* next;
};

// Handles stream of descriptors across the frame.
struct DescriptorStreamVK {
    DescriptorStreamChunkVK* chunk_list[VULKAN_MAX_BUFFER_SIZE] {};
    DescriptorStreamChunkVK* current_chunk {};
    uint32_t current_frame_id {};

    VkDescriptorSet allocate_descriptor_set(VkDevice device, VkDescriptorSetLayout layout,
                                            uint32_t num_uniform_buffers,
                                            uint32_t num_storage_buffers,
                                            uint32_t num_sampled_images,
                                            uint32_t num_storage_images);
    DescriptorStreamChunkVK* create_chunk(VkDevice device, uint32_t max_descriptor_sets,
                                          uint32_t max_descriptors);
    void reset(VkDevice device, uint32_t frame_id);
    void destroy(VkDevice device);
};

struct ClipContentDescriptorWrite {
    VkWriteDescriptorSet write;
    VkDescriptorBufferInfo info;
};

struct RendererVK : public Renderer {
    VkInstance instance_;
    VkDebugUtilsMessengerEXT debug_messenger_;
    VkPhysicalDevice physical_device_;
    VkDevice device_;
    VkSurfaceKHR surface_;
    VkSwapchainKHR swapchain_ {};
    VmaAllocator allocator_ {};
    uint32_t frame_latency_ {};
    uint32_t sync_count_ {};

    bool has_present_id = false;
    bool has_present_wait = false;
    uint32_t graphics_queue_index_;
    uint32_t present_queue_index_;
    VkQueue graphics_queue_;
    VkQueue present_queue_;

    VkRenderPass fb_render_pass_ {};
    FramebufferVK main_framebuffer_ {};
    VkDescriptorPool imgui_descriptor_pool_ {};
    VkSampler imgui_sampler_ {};
    VkFence fences_[VULKAN_MAX_BUFFER_SIZE] {};
    CommandBufferVK cmd_buf_[VULKAN_MAX_BUFFER_SIZE] {};
    FrameSync frame_sync_[VULKAN_MAX_SYNC_COUNT] {};
    ImGui_ImplVulkan_FrameRenderBuffers render_buffers_[VULKAN_MAX_BUFFER_SIZE] {};
    uint32_t frame_id_ = 0;
    uint32_t sync_id_ = 0;
    uint64_t present_id_ = 0;
    uint32_t sc_image_index_ = 0;

    VkCommandPool imm_cmd_pool_;
    VkCommandBuffer imm_cmd_buf_;
    DescriptorStreamVK descriptor_stream_;

    FrameSync* current_frame_sync_ {};
    VkCommandBuffer current_cb_ {};
    FramebufferVK* current_framebuffer_ {};
    ImageVK winding_images_[VULKAN_MAX_BUFFER_SIZE] {};

    ResourceDisposalVK resource_disposal_;
    ImVector<VkDescriptorBufferInfo> buffer_descriptor_writes_;
    ImVector<VkWriteDescriptorSet> write_descriptor_sets_;

    PipelineResourceLayoutVK waveform_layout;
    VkPipeline waveform_fill;
    VkPipeline waveform_aa;

    PipelineResourceLayoutVK vector_ras_layout;
    VkPipeline vector_ras {};
    VkPipeline vector_fill {};

    float vp_width = 0.0f;
    float vp_height = 0.0f;
    int32_t fb_width = 0;
    int32_t fb_height = 0;
    int32_t v_width = 0;
    int32_t v_height = 0;

    RendererVK(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger,
               VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
               uint32_t graphics_queue_index, uint32_t present_queue_index);

    ~RendererVK();

    bool init();

    std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) override;

    std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample,
                                                     SamplePeaksPrecision precision) override;
    void resize_swapchain() override;

    void new_frame() override;

    void end_frame() override;

    void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) override;

    void begin_draw(const std::shared_ptr<Framebuffer>& framebuffer,
                    const ImVec4& clear_color) override;

    void finish_draw() override;

    void clear(float r, float g, float b, float a) override;

    ImTextureID prepare_as_imgui_texture(const std::shared_ptr<Framebuffer>& framebuffer) override;

    void fill_polygon(const ImVec2* points, uint32_t count) override;

    void fill_path(const Path& path, uint32_t color) override;

    void draw_waveforms(const ImVector<ClipContentDrawCmd>& clips) override;

    void render_draw_command_list(DrawCommandList* command_list) override;

    void render_imgui_draw_data(ImDrawData* draw_data) override;

    void present() override;

    bool init_swapchain_();

    void create_or_resize_buffer(VkBuffer& buffer, VkDeviceMemory& buffer_memory,
                                 VkDeviceSize& buffer_size, size_t new_size,
                                 VkBufferUsageFlagBits usage);

    void setup_imgui_render_state(ImDrawData* draw_data, VkPipeline pipeline,
                                  VkCommandBuffer command_buffer,
                                  ImGui_ImplVulkan_FrameRenderBuffers* rb, int fb_width,
                                  int fb_height);
    void init_pipelines();
    void destroy_pipelines();

    VkPipeline create_pipeline(const char* vs, const char* fs, VkPipelineLayout layout,
                               const VkPipelineVertexInputStateCreateInfo* vertex_input,
                               VkPrimitiveTopology primitive_topology, bool enable_blending,
                               bool disable_color_writes);

    static Renderer* create(SDL_Window* main_window);
};
} // namespace wb