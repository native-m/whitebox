#include "renderer_vulkan.h"

namespace wb {

RendererVK::~RendererVK() {
}

bool RendererVK::init() {
    return false;
}

std::shared_ptr<Framebuffer> RendererVK::create_framebuffer(uint32_t width, uint32_t height) {
    return std::shared_ptr<Framebuffer>();
}

std::shared_ptr<SamplePeaks> RendererVK::create_sample_peaks(const Sample& sample,
                                                             SamplePeaksPrecision precision) {
    return std::shared_ptr<SamplePeaks>();
}

void RendererVK::new_frame() {
}

void RendererVK::resize_swapchain() {
}

void RendererVK::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) {
}

void RendererVK::clear(float r, float g, float b, float a) {
}

void RendererVK::draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) {
}

void RendererVK::render_draw_data(ImDrawData* draw_data) {
}

void RendererVK::present() {
}

Renderer* RendererVK::create(App* app) {
    return nullptr;
}
} // namespace wb