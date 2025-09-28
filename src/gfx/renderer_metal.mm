#include "renderer_metal.h"
#include <QuartzCore/QuartzCore.h>
#include "SDL3/SDL_metal.h"

namespace wb {

GPURendererMTL::GPURendererMTL(id<MTLDevice> new_device) : device(new_device) {
}

bool GPURendererMTL::init(SDL_Window* window) {
  cmd_queue = [device newCommandQueue];

  SDL_MetalView view = SDL_Metal_CreateView(window);
  if (!view)
    return false;

  GPUViewportDataMTL* main_vp = new GPUViewportDataMTL();
  main_vp->mtl_view = view;
  main_vp->window = window;
  init_viewport_(main_vp);
  viewports.push_back(main_vp);

  return true;
}

GPUBuffer* GPURendererMTL::create_buffer(
    GPUBufferUsageFlags usage,
    size_t buffer_size,
    bool dedicated_allocation,
    size_t init_size,
    const void* init_data) {
  return nullptr;
}

GPUTexture* GPURendererMTL::create_texture(
    GPUTextureUsageFlags usage,
    GPUFormat format,
    uint32_t w,
    uint32_t h,
    bool dedicated_allocation,
    uint32_t init_w,
    uint32_t init_h,
    const void* init_data) {
  return nullptr;
}

GPUPipeline* GPURendererMTL::create_pipeline(const GPUPipelineDesc& desc) {
  return nullptr;
}

void GPURendererMTL::destroy_buffer(GPUBuffer* buffer) {
}

void GPURendererMTL::destroy_texture(GPUTexture* buffer) {
}

void GPURendererMTL::destroy_pipeline(GPUPipeline* buffer) {
}

void GPURendererMTL::add_viewport(ImGuiViewport* viewport) {
}

void GPURendererMTL::remove_viewport(ImGuiViewport* viewport) {
}

void GPURendererMTL::resize_viewport(ImGuiViewport* viewport, ImVec2 vec) {
}

void GPURendererMTL::begin_frame() {
}

void GPURendererMTL::end_frame() {
}

void GPURendererMTL::present() {
}

void* GPURendererMTL::map_buffer(GPUBuffer* buffer) {
  return nullptr;
}

void GPURendererMTL::unmap_buffer(GPUBuffer* buffer) {
}

void* GPURendererMTL::begin_upload_data(GPUBuffer* buffer, size_t upload_size) {
  return nullptr;
}

void GPURendererMTL::end_upload_data() {
}

void GPURendererMTL::update_texture_region(GPUTexture* tex, uint32_t num_regions, const GPUUpdateTextureRegion* regions) {
}

void GPURendererMTL::begin_render(GPUTexture* render_target, const ImVec4& clear_color) {
}

void GPURendererMTL::end_render() {
}

void GPURendererMTL::set_shader_parameter(size_t size, const void* data) {
}

void GPURendererMTL::flush_state() {
}

void GPURendererMTL::init_viewport_(GPUViewportDataMTL* vp_data) {
  CAMetalLayer* mtl_layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(vp_data->mtl_view);
}

GPURenderer* GPURendererMTL::create(SDL_Window* window) {
  id<MTLDevice> mtl_device = MTLCreateSystemDefaultDevice();
  if (!mtl_device)
    return nullptr;

  return new GPURendererMTL(mtl_device);
}

}  // namespace wb
