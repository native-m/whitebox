#pragma once
#include "waveform_view_buffer.h"
#include "engine/sample.h"
#include <memory>
#include <cmath>
#include <SDL.h>
#include <imgui.h>
#include <optional>

namespace wb
{
    struct App;

    enum class GPUMemoryType
    {
        Device,
        Host
    };

    enum class PixelFormat
    {
        RGBA_8_8_8_8_UNORM,
        R_8_SNORM,
        R_8_UNORM,
        R_16_SNORM,
        R_16_UNORM,
    };

    struct Framebuffer
    {
        uint32_t width;
        uint32_t height;

        virtual ~Framebuffer() {}
        virtual ImTextureID get_imgui_texture_id() const = 0;
    };

    struct ClipContentDrawArgs
    {
        SamplePeaks* sample_peaks;
        ImColor color;
        ImVec2 min;
        ImVec2 max;
        float scale_x;
    };

    struct Renderer
    {
        enum
        {
            D3D11,
            OpenGL,
            Vulkan,
        };

        virtual ~Renderer() { }
        virtual std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) = 0;
        virtual std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample, PixelFormat format, GPUMemoryType memory_type) = 0;
        virtual void resize_swapchain() = 0;
        virtual void new_frame() = 0;
        virtual void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) = 0;
        virtual void clear_framebuffer(float r, float g, float b, float a) = 0;
        virtual void draw_waveform(const std::shared_ptr<SamplePeaks>& waveform_view_buffer, const ImColor& color, const ImVec2& origin, float scale_x, float scale_y) = 0;
        virtual void draw_clip_content(const ImVector<ClipContentDrawArgs>& clip_contents, bool anti_aliasing) = 0;
        virtual void render_imgui(ImDrawData* draw_data) = 0;
        virtual void present() = 0;

        static void init(App* app, uint32_t renderer_type);
        static void shutdown();
        static Renderer* instance;
    };
}
