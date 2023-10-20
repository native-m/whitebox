#pragma once

#include <wrl.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <memory>
#include <array>

#include "renderer.h"

namespace wb
{
    struct SamplePeaksD3D11 : public SamplePeaks
    {
        struct BufferMipmap
        {
            ID3D11Buffer* buffer{};
            ID3D11ShaderResourceView* srv{};
        };

        BufferMipmap mip_map[8]{};

        ~SamplePeaksD3D11()
        {
            for (auto& buffer_mip : mip_map) {
                if (buffer_mip.srv) buffer_mip.srv->Release();
                if (buffer_mip.buffer) buffer_mip.buffer->Release();
            }
        }
    };

    struct FramebufferD3D11 : public Framebuffer
    {
        ID3D11Texture2D* texture{};
        ID3D11RenderTargetView* rtv{};
        ID3D11ShaderResourceView* srv{};

        ~FramebufferD3D11()
        {
            if (srv) srv->Release();
            if (rtv) rtv->Release();
            if (texture) texture->Release();
        }

        ImTextureID get_imgui_texture_id() const override
        {
            return (ImTextureID)srv;
        }
    };

    struct ShadersD3D11
    {
        ID3D11VertexShader* vs;
        ID3D11GeometryShader* gs;
        ID3D11PixelShader* ps;

        void destroy()
        {
            if (vs) vs->Release();
            if (gs) gs->Release();
            if (ps) ps->Release();
            vs = nullptr;
            gs = nullptr;
            ps = nullptr;
        }
    };

    struct RendererD3D11 : public Renderer
    {
        IDXGISwapChain2* swapchain_{};
        ID3D11Device* device_{};
        ID3D11DeviceContext* context_{};
        ID3D11RenderTargetView* backbuffer_rtv_{};
        ID3D11InputLayout* waveform_input_layout_{};
        ID3D11Buffer* parameter_cbuffer_{};
        ID3D11BlendState* blend_state_{};
        HANDLE frame_latency_waitable_object_{};

        // Shaders
        ShadersD3D11 waveform_{};
        ShadersD3D11 waveform_aa_{};

        ID3D11RenderTargetView* current_render_target_{};

        float vp_width = 0.0f;
        float vp_height = 0.0f;

        RendererD3D11(IDXGISwapChain2* swapchain,
                      ID3D11Device* device,
                      ID3D11DeviceContext* context);

        virtual ~RendererD3D11();

        bool init();

        std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) override;
        std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample, PixelFormat format, GPUMemoryType memory_type) override;
        void resize_swapchain() override;
        void new_frame() override;
        void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) override;
        void clear_framebuffer(float r, float g, float b, float a) override;
        void draw_waveform(const std::shared_ptr<SamplePeaks>& waveform_view_buffer, const ImColor& color, const ImVec2& origin, float scale_x, float scale_y) override;
        void draw_clip_content(const ImVector<ClipContentDrawArgs>& clip_contents, bool anti_aliasing) noexcept override;
        void render_imgui(ImDrawData* draw_data) override;
        void present() override;

        bool load_shaders_(const char* vs, const char* gs, const char* ps, ShadersD3D11* ret);

        static Renderer* create(App* app);
    };
}
