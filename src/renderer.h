#pragma once
#include "app.h"
#include <memory>
#include <SDL.h>
#include <imgui.h>
#include <optional>

namespace wb
{
    enum class GPUMemoryType
    {
        Device,
        Host
    };

    enum class PixelFormat
    {
        RGBA_8_8_8_8_UNORM,
        R_8_I,
        R_8_U,
        R_16_I,
        R_16_U,
    };

    struct SamplePreviewBuffer
    {

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
        virtual std::shared_ptr<SamplePreviewBuffer> create_sample_preview_buffer(uint32_t num_samples, PixelFormat format, GPUMemoryType memory_type) = 0;
        virtual void resize_swapchain() = 0;
        virtual void new_frame() = 0;
        virtual void render_draw_data(ImDrawData* draw_data) = 0;
        virtual void present() = 0;

        static void init(App* app, uint32_t renderer_type);
        static void shutdown();
        static Renderer* instance;
    };
}
