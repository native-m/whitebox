#pragma once

#include <d3d11.h>
#include <dxgi1_3.h>
#include <memory>

#include "renderer.h"

namespace wb
{
    struct SamplePreviewBufferD3D11 : public SamplePreviewBuffer
    {
        ID3D11Buffer* buffer;
        ID3D11ShaderResourceView* srv;
    };

    struct RendererD3D11 : public Renderer
    {
        IDXGISwapChain2* swapchain_;
        ID3D11Device* device_;
        ID3D11DeviceContext* context_;
        ID3D11RenderTargetView* backbuffer_rtv_ = nullptr;
        HANDLE frame_latency_waitable_object_;

        RendererD3D11(IDXGISwapChain2* swapchain,
                      ID3D11Device* device,
                      ID3D11DeviceContext* context);

        virtual ~RendererD3D11();

        std::shared_ptr<SamplePreviewBuffer> create_sample_preview_buffer(uint32_t num_samples, PixelFormat format, GPUMemoryType memory_type) override;
        void resize_swapchain() override;
        void new_frame() override;
        void render_draw_data(ImDrawData* draw_data) override;
        void present() override;

        static Renderer* create(App* app);
    };
}
