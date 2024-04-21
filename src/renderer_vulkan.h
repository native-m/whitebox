#pragma once

#include "renderer.h"
#include <deque>
#include <vector>

#include "vk_stub.h"

#define VULKAN_BUFFER_SIZE 2

namespace wb {
struct ResourceDisposalVK;

struct ImageAccessVK {
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags access = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct FramebufferVK : public Framebuffer {
    ResourceDisposalVK* resource_disposal {};
    VmaAllocation allocations[VULKAN_BUFFER_SIZE] {};
    VkImage image[VULKAN_BUFFER_SIZE];
    VkImageView view[VULKAN_BUFFER_SIZE];
    VkFramebuffer framebuffer[VULKAN_BUFFER_SIZE];
    VkDescriptorSet descriptor_set[VULKAN_BUFFER_SIZE] {};
    ImageAccessVK current_access[VULKAN_BUFFER_SIZE] {};
    uint32_t image_id = 1;

    ~FramebufferVK();
    ImTextureID as_imgui_texture_id() const override;
};

struct SamplePeaksMipVK {
    VkBuffer buffer;
};

struct SamplePeaksVK : public SamplePeaks {};

struct CommandBufferVK {
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;
};

struct FrameSync {
    VkFence fence;
    VkSemaphore image_acquire_semaphore;
    VkSemaphore render_finished_semaphore;
};

struct FramebufferDisposalVK {
    uint32_t frame_id;
    VmaAllocation allocation;
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
};

struct ResourceDisposalVK {
    uint32_t current_frame_id {};
    std::deque<FramebufferDisposalVK> fb;
    void dispose_framebuffer(FramebufferVK* obj);
    void flush(VkDevice device, VmaAllocator allocator, uint32_t frame_id_dispose);
};

struct RendererVK : public Renderer {
    VkInstance instance_;
    VkPhysicalDevice physical_device_;
    VkDevice device_;
    VkSurfaceKHR surface_;
    VkSwapchainKHR swapchain_ {};
    VmaAllocator allocator_ {};

    uint32_t graphics_queue_index_;
    uint32_t present_queue_index_;
    VkQueue graphics_queue_;
    VkQueue present_queue_;

    VkRenderPass fb_render_pass_ {};
    FramebufferVK main_framebuffer_ {};
    VkFence fences_[VULKAN_BUFFER_SIZE] {};
    CommandBufferVK cmd_buf_[VULKAN_BUFFER_SIZE] {};
    FrameSync frame_sync_[VULKAN_BUFFER_SIZE] {};
    VkDescriptorPool imgui_descriptor_pool_ {};
    VkSampler imgui_sampler_ {};
    uint32_t frame_id_ = 0;
    uint32_t sc_image_index_ = 0;

    FrameSync* current_frame_sync_ {};
    VkCommandBuffer current_cb_ {};
    FramebufferVK* current_framebuffer_ {};

    ResourceDisposalVK resource_disposal_;

    RendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
               VkSurfaceKHR surface, uint32_t graphics_queue_index, uint32_t present_queue_index);
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
    void draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) override;
    void render_draw_data(ImDrawData* draw_data) override;
    void present() override;

    bool init_swapchain_();
    void dispose_framebuffer_(FramebufferVK* obj);

    static Renderer* create(App* app);
};
} // namespace wb