#pragma once

#include "core/common.h"
#include "engine/sample.h"
#include "engine/sample_peaks.h"
#include <imgui.h>
#include <memory>

namespace wb {

struct App;

struct ClipContentDrawCmd {
    SamplePeaks* peaks;
    ImVec2 min_bb;
    ImVec2 max_bb;
    ImColor color;
    float scale_x;
    uint32_t mip_index;
    uint32_t start_idx;
    uint32_t draw_count;
};

struct Framebuffer {
    uint32_t width;
    uint32_t height;

    virtual ~Framebuffer() {}
    virtual ImTextureID as_imgui_texture_id() const = 0;
};

struct Renderer {
    float vp_width {};
    float vp_height {};

    virtual ~Renderer() {}
    virtual std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) = 0;
    virtual std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample,
                                                             SamplePeaksPrecision precision) = 0;
    virtual void resize_swapchain() = 0;
    virtual void new_frame() = 0;
    virtual void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) = 0;
    virtual void clear(float r, float g, float b, float a) = 0;
    virtual void draw_clip_content(const ImVector<ClipContentDrawCmd>& clips) = 0;
    virtual void render_draw_data(ImDrawData* draw_data) = 0;
    virtual void present() = 0;

    inline void clear(const ImColor& color) {
        clear(color.Value.x, color.Value.y, color.Value.z, color.Value.w);
    }
};

extern Renderer* g_renderer;
void init_renderer(App* app);
void shutdown_renderer();

} // namespace wb