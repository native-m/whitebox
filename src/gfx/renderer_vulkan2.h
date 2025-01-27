#pragma once

#include "core/memory.h"
#include "renderer2.h"
#include "vk_stub.h"

namespace wb {
struct GPUBufferVK : public GPUBuffer {
    VmaAllocation allocation[WB_GPU_RENDER_BUFFER_SIZE];
};

struct GPUTextureVK : public GPUTexture {
    VmaAllocation allocation[WB_GPU_RENDER_BUFFER_SIZE];
    VkImageView views[WB_GPU_RENDER_BUFFER_SIZE];
    VkFramebuffer fb[WB_GPU_RENDER_BUFFER_SIZE];
    VkDescriptorSet descriptor_set[WB_GPU_RENDER_BUFFER_SIZE] {};
    uint32_t image_id {};
    uint32_t num_buffers {};

    ImTextureID as_imgui_texture_id() const override { return (ImTextureID)descriptor_set[image_id]; }
};

struct GPUPipelineVK : public GPUPipeline {
    VkPipeline pipeline;
};

struct GPUViewportDataVK : public GPUViewportData {
    VkSwapchainKHR swapchain;
    VkSurfaceKHR surface;
};

struct GPURendererVK : public GPURenderer {
    VkInstance instance_ {};
    VkDebugUtilsMessengerEXT debug_messenger_ {};
    VkPhysicalDevice physical_device_ {};
    VkDevice device_ {};

    uint32_t graphics_queue_index_;
    uint32_t present_queue_index_;
    VkQueue graphics_queue_ {};
    VkQueue present_queue_ {};

    VkSurfaceKHR main_surface_ {};
    VkRenderPass fb_render_pass_ {};

    Pool<GPUBufferVK> buffer_pool_ {};
    Pool<GPUTextureVK> texture_pool_ {};

    GPURendererVK(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR main_surface,
                  uint32_t graphics_queue_index, uint32_t present_queue_index);

    bool init(SDL_Window* window) override;
    void shutdown() override;
    void begin_frame() override;
    void end_frame() override;

    GPUBuffer* create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, size_t init_size, void* init_data) override;
    GPUTexture* create_texture(GPUTextureUsageFlags usage, uint32_t w, uint32_t h, size_t init_size) override;
    GPUPipeline* create_pipeline(const GPUPipelineDesc& desc) override;
    void destroy_buffer(GPUBuffer* buffer) override;
    void destroy_texture(GPUTexture* buffer) override;
    void destroy_pipeline(GPUPipeline* buffer) override;
    void add_viewport(ImGuiViewport* viewport) override;
    void remove_viewport(ImGuiViewport* viewport) override;

    void* map_buffer(GPUBuffer* buffer) override;
    void unmap_buffer(GPUBuffer* buffer) override;

    void begin_render(GPUTexture* render_target, const ImVec4& clear_color) override;
    void end_render() override;
    void set_pipeline(GPUPipeline* pipeline) override;
    void set_shader_parameter(size_t size, const void* data) override;
    void flush_state() override;

    static GPURenderer* create(SDL_Window* window);
};
} // namespace wb