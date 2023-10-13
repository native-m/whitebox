#pragma once

#include <wrl.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <memory>

#include "renderer.h"

namespace wb
{
    struct WaveformViewBufferD3D11 : public WaveformViewBuffer
    {
        ID3D11Buffer* buffer{};
        ID3D11ShaderResourceView* srv{};

        ~WaveformViewBufferD3D11()
        {
            if (srv) srv->Release();
            if (buffer) buffer->Release();
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

    struct RendererD3D11 : public Renderer
    {
        IDXGISwapChain2* swapchain_{};
        ID3D11Device* device_{};
        ID3D11DeviceContext* context_{};
        ID3D11RenderTargetView* backbuffer_rtv_{};
        ID3D11VertexShader* waveform_vs_{};
        ID3D11PixelShader* waveform_ps_{};
        ID3D11InputLayout* waveform_input_layout_{};
        ID3D11Buffer* parameter_cbuffer_{};
        HANDLE frame_latency_waitable_object_{};

        ID3D11RenderTargetView* current_render_target_{};

        float vp_width = 0.0f;
        float vp_height = 0.0f;

        RendererD3D11(IDXGISwapChain2* swapchain,
                      ID3D11Device* device,
                      ID3D11DeviceContext* context);

        virtual ~RendererD3D11();

        bool init();

        std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) override;
        std::shared_ptr<WaveformViewBuffer> create_waveform_view_buffer(const Sample& sample, PixelFormat format, GPUMemoryType memory_type) override;
        void resize_swapchain() override;
        void new_frame() override;
        void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) override;
        void clear_framebuffer(float r, float g, float b, float a) override;
        void draw_waveform(const std::shared_ptr<WaveformViewBuffer>& waveform_view_buffer, const ImColor& color, const ImVec2& origin, float scale_x, float scale_y) override;
        void draw_clip_content(const ImVector<ClipContentDrawArgs>& clip_contents) override;
        void render_imgui(ImDrawData* draw_data) override;
        void present() override;

        static Renderer* create(App* app);
    };
}
