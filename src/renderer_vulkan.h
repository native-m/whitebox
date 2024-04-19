#pragma once

#include "renderer.h"
#include <vector>

namespace wb {
struct FramebufferVK : public Framebuffer {};

struct SamplePeaksMipVK {};

struct SamplePeaksVK : public SamplePeaks {};

struct RendererVK : public Renderer {
    ~RendererVK();
    bool init();
    std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) override;
    std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample,
                                                     SamplePeaksPrecision precision) override;
    void new_frame() override;
    void resize_swapchain() override;
    void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) override;
    void clear(float r, float g, float b, float a) override;
    void draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) override;
    void render_draw_data(ImDrawData* draw_data) override;
    void present() override;
    static Renderer* create(App* app);
};
} // namespace wb