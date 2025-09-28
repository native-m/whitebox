#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL_metal.h>

#include "core/vector.h"
#include "renderer.h"

namespace wb {

struct GPUViewportDataMTL : public GPUViewportData {
  SDL_MetalView mtl_view;
  SDL_Window* window;
};

struct GPURendererMTL : public GPURenderer {
  id<MTLDevice> device;
  id<MTLCommandQueue> cmd_queue;
  id<MTLCommandBuffer> cmd_buf;
  Vector<GPUViewportDataMTL*> viewports;

  GPURendererMTL(id<MTLDevice> new_device);

  bool init(SDL_Window* window) override;
  void shutdown() override;

  // Resources
  GPUBuffer* create_buffer(
      GPUBufferUsageFlags usage,
      size_t buffer_size,
      bool dedicated_allocation,
      size_t init_size,
      const void* init_data) override;
  GPUTexture* create_texture(
      GPUTextureUsageFlags usage,
      GPUFormat format,
      uint32_t w,
      uint32_t h,
      bool dedicated_allocation,
      uint32_t init_w,
      uint32_t init_h,
      const void* init_data) override;
  GPUPipeline* create_pipeline(const GPUPipelineDesc& desc) override;
  void destroy_buffer(GPUBuffer* buffer) override;
  void destroy_texture(GPUTexture* buffer) override;
  void destroy_pipeline(GPUPipeline* buffer) override;

  // Viewports
  void add_viewport(ImGuiViewport* viewport) override;
  void remove_viewport(ImGuiViewport* viewport) override;
  void resize_viewport(ImGuiViewport* viewport, ImVec2 vec) override;

  void begin_frame() override;
  void end_frame() override;
  void present() override;

  void* map_buffer(GPUBuffer* buffer) override;
  void unmap_buffer(GPUBuffer* buffer) override;
  void* begin_upload_data(GPUBuffer* buffer, size_t upload_size) override;
  void end_upload_data() override;

  void update_texture_region(GPUTexture* tex, uint32_t num_regions, const GPUUpdateTextureRegion* regions) override;

  void begin_render(GPUTexture* render_target, const ImVec4& clear_color) override;
  void end_render() override;
  void set_shader_parameter(size_t size, const void* data) override;
  void flush_state() override;

  void init_viewport_(GPUViewportDataMTL* vp_data);

  static GPURenderer* create(SDL_Window* window);
};

}  // namespace wb
