#pragma once

#include "core/common.h"

#ifdef WB_PLATFORM_WINDOWS
#include "renderer.h"
#include <SDL_video.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <vector>

#undef min
#undef max

namespace wb {

struct FramebufferD3D11 : public Framebuffer {
    ID3D11Texture2D* texture {};
    ID3D11RenderTargetView* rtv {};
    ID3D11ShaderResourceView* srv {};

    ~FramebufferD3D11() {
        if (texture)
            texture->Release();
        if (rtv)
            rtv->Release();
        if (srv)
            srv->Release();
    }

    ImTextureID as_imgui_texture_id() const override { return (ImTextureID)srv; }
};

struct SamplePeaksMipD3D11 {
    ID3D11Buffer* buffer {};
    ID3D11ShaderResourceView* srv {};
    size_t size;

    void destroy() {
        if (buffer)
            buffer->Release();
        if (srv)
            srv->Release();
    }
};

struct SamplePeaksD3D11 : public SamplePeaks {
    std::vector<SamplePeaksMipD3D11> mipmap;

    ~SamplePeaksD3D11() {
        for (auto mip : mipmap)
            mip.destroy();
    }
};

struct RendererD3D11 : public Renderer {
    IDXGISwapChain2* swapchain_;
    ID3D11Device* device_;
    ID3D11DeviceContext* ctx_;
    HANDLE frame_latency_waitable_handle_;

    ID3D11RenderTargetView* backbuffer_rtv_ {};
    ID3D11RenderTargetView* current_rtv_ {};

    ID3D11Buffer* parameter_cbuffer_ {};
    ID3D11BlendState* standard_blend_ {};
    ID3D11RasterizerState* rasterizer_state_ {};

    ID3D11VertexShader* waveform_aa_vs_ {};
    ID3D11VertexShader* waveform_vs_ {};
    ID3D11PixelShader* waveform_ps_ {};

    float vp_width = 0.0f;
    float vp_height = 0.0f;
    int32_t fb_width = 0;
    int32_t fb_height = 0;

    RendererD3D11(IDXGISwapChain2* swapchain_, ID3D11Device* device_, ID3D11DeviceContext* ctx_);
    ~RendererD3D11();
    bool init();
    std::shared_ptr<Framebuffer> create_framebuffer(uint32_t width, uint32_t height) override;
    std::shared_ptr<SamplePeaks> create_sample_peaks(const Sample& sample, SamplePeaksPrecision precision) override;
    void resize_viewport(ImGuiViewport* viewport, ImVec2 vec) override;
    void new_frame() override;
    void end_frame() override;
    void set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) override;
    void begin_draw(Framebuffer* framebuffer, const ImVec4& clear_color) override;
    void finish_draw() override;
    void clear(float r, float g, float b, float a) override;
    ImTextureID prepare_as_imgui_texture(const std::shared_ptr<Framebuffer>& framebuffer) override;
    void fill_polygon(const ImVec2* points, uint32_t count) override {};
    void fill_path(const Path& path, uint32_t color) override;
    void draw_waveforms(const ImVector<ClipContentDrawCmd>& clips) override;
    void render_draw_command_list(DrawCommandList* command_list) override {}
    void render_imgui_draw_data(ImDrawData* draw_data) override;
    void present() override;
    static Renderer* create(SDL_Window* window);
};

} // namespace wb
#endif