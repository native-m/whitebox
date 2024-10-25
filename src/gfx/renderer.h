#pragma once

#include "core/common.h"
#include "core/vector.h"
#include "draw.h"
#include "engine/clip.h"
#include "engine/sample.h"
#include "engine/sample_peaks.h"
#include <SDL_video.h>
#include <imgui.h>
#include <memory>

namespace wb {

struct App;

enum class PathCommand {
    MoveTo,
    LineTo,
    Close,
};

struct ClipContentDrawCmd {
    SamplePeaks* peaks;
    ImVec2 min_bb;
    ImVec2 max_bb;
    ImColor color;
    float scale_x;
    int32_t mip_index;
    uint32_t start_idx;
    uint32_t draw_count;
};

struct Framebuffer {
    uint32_t width;
    uint32_t height;
    bool window_framebuffer = false;

    virtual ~Framebuffer() {}
    virtual ImTextureID as_imgui_texture_id() const = 0;
};

struct Path {
    Vector<ImVec2> lines;
    Vector<PathCommand> cmd;
    float first_x = 0;
    float first_y = 0;
    float last_x = 0;
    float last_y = 0;

    void move_to(float x, float y) {
        lines.emplace_back(x, y);
        cmd.emplace_back(PathCommand::MoveTo);
    }

    void line_to(float x, float y) {
        lines.emplace_back(x, y);
        cmd.emplace_back(PathCommand::LineTo);
    }

    void close() { cmd.emplace_back(PathCommand::Close); }

    void clear(bool fast_clear = true) { lines.resize(0); }
};

struct Renderer {
    float vp_width {};
    float vp_height {};

    virtual ~Renderer() {}
    virtual std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) = 0;
    virtual std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample, SamplePeaksPrecision precision) = 0;
    virtual void new_frame() = 0;
    virtual void end_frame() = 0;
    virtual void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) = 0;
    virtual void begin_draw(Framebuffer* framebuffer, const ImVec4& clear_color) = 0;
    virtual void finish_draw() = 0;
    virtual void clear(float r, float g, float b, float a) = 0;
    virtual ImTextureID prepare_as_imgui_texture(const std::shared_ptr<Framebuffer>& framebuffer) = 0;
    virtual void fill_polygon(const ImVec2* points, uint32_t count) = 0;
    virtual void fill_path(const Path& path, uint32_t color) = 0;
    virtual void draw_waveforms(const ImVector<ClipContentDrawCmd>& clips) = 0;
    virtual void render_draw_command_list(DrawCommandList* command_list) = 0;
    virtual void render_imgui_draw_data(ImDrawData* draw_data) = 0;
    virtual void resize_viewport(ImGuiViewport* viewport, ImVec2 vec) = 0;
    virtual bool add_viewport(ImGuiViewport* viewport) { return false; }
    virtual bool remove_viewport(ImGuiViewport* viewport) { return false; }
    virtual void present() = 0;

    inline void clear(const ImColor& color) { clear(color.Value.x, color.Value.y, color.Value.z, color.Value.w); }
};

extern Renderer* g_renderer;
void init_renderer(SDL_Window* window);
void shutdown_renderer();

} // namespace wb