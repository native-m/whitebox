#include "renderer_d3d11.h"
#include "core/debug.h"
#include "core/file.h"
#include "types.h"
#include "app_sdl2.h"

#include <SDL_syswm.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl2.h>

namespace wb
{
    struct WaveformViewParam
    {
        float origin_x;
        float origin_y;
        float scale_x;
        float scale_y;
        ImVec4 color;
        float vp_width;
        float vp_height;
    };

    RendererD3D11::RendererD3D11(IDXGISwapChain2* swapchain,
                                 ID3D11Device* device,
                                 ID3D11DeviceContext* context) :
        swapchain_(swapchain),
        device_(device),
        context_(context),
        frame_latency_waitable_object_(swapchain_->GetFrameLatencyWaitableObject())
    {
        swapchain_->SetMaximumFrameLatency(1);
        resize_swapchain();
    }

    RendererD3D11::~RendererD3D11()
    {
        if (frame_latency_waitable_object_) {
            WaitForSingleObjectEx(frame_latency_waitable_object_, 1000, true);
            CloseHandle(frame_latency_waitable_object_);
        }

        ImGui_ImplDX11_Shutdown();

        waveform_.destroy();
        waveform_aa_.destroy();

        if (waveform_input_layout_) waveform_input_layout_->Release();
        if (parameter_cbuffer_) parameter_cbuffer_->Release();
        if (backbuffer_rtv_) backbuffer_rtv_->Release();
        if (swapchain_) swapchain_->Release();
        if (context_) context_->Release();
        if (device_) device_->Release();
    }

    bool RendererD3D11::init()
    {
        if (!load_shaders_("assets/waveform2_vs.hlsl.dxbc", nullptr, "assets/waveform_ps.hlsl.dxbc", &waveform_))
            return false;

        if (!load_shaders_("assets/waveform_aa_vs.hlsl.dxbc",
                           "assets/waveform_aa_gs.hlsl.dxbc",
                           "assets/waveform_aa_ps.hlsl.dxbc",
                           &waveform_aa_))
            return false;

        //D3D11_INPUT_ELEMENT_DESC waveform_vtx;
        //waveform_vtx.SemanticName = "POSITION";
        //waveform_vtx.SemanticIndex = 0;
        //waveform_vtx.Format = DXGI_FORMAT_R8_SNORM;
        //waveform_vtx.InputSlot = 0;
        //waveform_vtx.AlignedByteOffset = 0;
        //waveform_vtx.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        //waveform_vtx.InstanceDataStepRate = 0;

        //Microsoft::WRL::ComPtr<ID3D11InputLayout> waveform_input_layout{};
        //device_->CreateInputLayout(&waveform_vtx, 1, waveform_vs_bytecode->data(), waveform_vs_bytecode->size(), &waveform_input_layout);
        //if (!waveform_input_layout)
        //    return false;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 256;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Microsoft::WRL::ComPtr<ID3D11Buffer> parameter_cbuffer{};
        device_->CreateBuffer(&desc, nullptr, &parameter_cbuffer);
        if (!parameter_cbuffer)
            return false;

        D3D11_BLEND_DESC blend_desc{};
        blend_desc.AlphaToCoverageEnable = false;
        blend_desc.RenderTarget[0].BlendEnable = true;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device_->CreateBlendState(&blend_desc, &blend_state_);

        parameter_cbuffer_ = parameter_cbuffer.Detach();

        return true;
    }

    std::shared_ptr<Framebuffer> RendererD3D11::create_framebuffer(uint32_t width, uint32_t height)
    {
        D3D11_TEXTURE2D_DESC desc;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
        device_->CreateTexture2D(&desc, nullptr, &texture);
        if (!texture)
            return {};

        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv{};
        device_->CreateRenderTargetView(texture.Get(), nullptr, &rtv);
        if (!rtv)
            return {};

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
        device_->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (!rtv)
            return {};

        std::shared_ptr<FramebufferD3D11> fb { std::make_shared<FramebufferD3D11>() };
        fb->width = width;
        fb->height = height;
        fb->texture = texture.Detach();
        fb->rtv = rtv.Detach();
        fb->srv = srv.Detach();

        return fb;
    }

    std::shared_ptr<WaveformViewBuffer> RendererD3D11::create_waveform_view_buffer(const Sample& sample, PixelFormat format, GPUMemoryType memory_type)
    {
        int16_t* data = (int16_t*)std::malloc(sample.sample_count * sample.channels * sizeof(int16_t));
        if (!data)
            return {};

        switch (sample.format) {
            case AudioFormat::F32:
                for (uint32_t i = 0; i < sample.channels; i++) {
                    const float* sample_data = sample.get_read_pointer<float>(i);
                    int16_t* channel_data = data + (i * sample.sample_count);
                    for (uint32_t j = 0; j < sample.sample_count; j++) {
                        float value = sample_data[j] * (float)((1 << 15) - 1);
                        channel_data[j] = (int16_t)(value >= 0 ? value + 0.5 : value - 0.5);
                    }
                }
                break;
            case AudioFormat::F64:
            default:
                WB_ASSERT(false && "Incompatible format at the moment");
        }

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = (UINT)(sample.sample_count * sample.channels * sizeof(int16_t));
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init_data{};
        init_data.pSysMem = data;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer{};
        if (FAILED(device_->CreateBuffer(&desc, &init_data, &buffer))) {
            std::free(data);
            return {};
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{
            .Format = DXGI_FORMAT_R16_SNORM,
            .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
            .Buffer = {
                .FirstElement = 0,
                .NumElements = (UINT)(sample.sample_count * sample.channels),
            }
        };

        if (FAILED(device_->CreateShaderResourceView(buffer.Get(), &srv_desc, &srv))) {
            std::free(data);
            return {};
        }

        std::shared_ptr<WaveformViewBufferD3D11> ret = std::make_shared<WaveformViewBufferD3D11>();
        if (!ret)
            return {};

        ret->sample_count = (uint32_t)sample.sample_count;
        ret->num_channels = sample.channels;
        ret->buffer = buffer.Detach();
        ret->srv = srv.Detach();

        std::free(data);

        return ret;
    }

    void RendererD3D11::resize_swapchain()
    {
        if (backbuffer_rtv_) {
            backbuffer_rtv_->Release();
        }

        swapchain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        
        ID3D11Texture2D* backbuffer = nullptr;
        swapchain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));

        if (!backbuffer) {
            Log::error("Backbuffer is nullptr");
            std::terminate();
        }

        device_->CreateRenderTargetView(backbuffer, nullptr, &backbuffer_rtv_);
        backbuffer->Release();
    }

    void RendererD3D11::new_frame()
    {
        ImGui_ImplDX11_NewFrame();
        current_render_target_ = nullptr;
    }

    void RendererD3D11::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer)
    {
        if (framebuffer == nullptr) {
            UINT width, height;
            swapchain_->GetSourceSize(&width, &height);

            context_->OMSetRenderTargets(1, &backbuffer_rtv_, nullptr);

            const D3D11_RECT rect = { 0, 0, (LONG)width, (LONG)height };
            context_->RSSetScissorRects(1, &rect);

            D3D11_VIEWPORT vp;
            vp.Width = (float)width;
            vp.Height = (float)height;
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            vp.TopLeftX = vp.TopLeftY = 0;
            context_->RSSetViewports(1, &vp);

            vp_width = 2.0f / vp.Width;
            vp_height = 2.0f / vp.Height;
            current_render_target_ = backbuffer_rtv_;
            return;
        }

        FramebufferD3D11* impl = static_cast<FramebufferD3D11*>(framebuffer.get());
        if (current_render_target_ == impl->rtv)
            return;

        context_->OMSetRenderTargets(1, &impl->rtv, nullptr);

        const D3D11_RECT rect = { 0, 0, (LONG)impl->width, (LONG)impl->height };
        context_->RSSetScissorRects(1, &rect);

        D3D11_VIEWPORT vp;
        vp.Width = (float)impl->width;
        vp.Height = (float)impl->height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = vp.TopLeftY = 0;
        context_->RSSetViewports(1, &vp);

        vp_width = 2.0f / vp.Width;
        vp_height = 2.0f / vp.Height;
        current_render_target_ = impl->rtv;
    }

    void RendererD3D11::clear_framebuffer(float r, float g, float b, float a)
    {
        const float clear_color[4] = { r, g, b, a };
        context_->ClearRenderTargetView(current_render_target_, clear_color);
    }

    void RendererD3D11::draw_waveform(const std::shared_ptr<WaveformViewBuffer>& waveform_view_buffer, const ImColor& color, const ImVec2& origin, float scale_x, float scale_y)
    {
        auto impl_buffer{ static_cast<WaveformViewBufferD3D11*>(waveform_view_buffer.get()) };
        const UINT stride = 1;
        const UINT offset = 0;

        D3D11_MAPPED_SUBRESOURCE mapped_resource{};
        context_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
        WaveformViewParam* param = (WaveformViewParam*)mapped_resource.pData;
        param->origin_x = origin.x;
        param->origin_y = origin.y;
        param->scale_x = scale_x;
        param->scale_y = scale_y;
        param->color = color;
        param->vp_width = vp_width;
        param->vp_height = vp_height;
        context_->Unmap(parameter_cbuffer_, 0);

        ID3D11Buffer* vtx_buf = impl_buffer->buffer;
        context_->IASetInputLayout(nullptr);
        context_->IASetVertexBuffers(0, 1, &vtx_buf, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        context_->VSSetShader(waveform_.vs, nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, &parameter_cbuffer_);
        context_->PSSetShader(waveform_.ps, nullptr, 0);
        context_->Draw(impl_buffer->sample_count, 0);
    }

#define USE_AA

    void RendererD3D11::draw_clip_content(const ImVector<ClipContentDrawArgs>& clip_contents, bool anti_aliasing) noexcept
    {
#if 0
        ID3D11Buffer* buffer = nullptr;
        const UINT stride = 1;
        const UINT offset = 0;

        context_->IASetInputLayout(waveform_input_layout_);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        context_->VSSetShader(waveform_vs_, nullptr, 0);
        context_->PSSetShader(waveform_ps_, nullptr, 0);

        for (auto& clip_content : clip_contents) {
            WaveformViewBufferD3D11* waveform_view_buf = static_cast<WaveformViewBufferD3D11*>(clip_content.view_buffer);

            if (buffer != waveform_view_buf->buffer) {
                context_->IASetVertexBuffers(0, 1, &waveform_view_buf->buffer, &stride, &offset);
                buffer = waveform_view_buf->buffer;
            }

            D3D11_MAPPED_SUBRESOURCE mapped_resource{};
            context_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
            WaveformViewParam* param = (WaveformViewParam*)mapped_resource.pData;
            param->origin_x = clip_content.min.x;
            param->origin_y = clip_content.min.y;
            param->scale_x = clip_content.scale_x;
            param->scale_y = clip_content.max.y - clip_content.min.y;
            param->color = clip_content.color;
            param->vp_width = vp_width;
            param->vp_height = vp_height;
            context_->Unmap(parameter_cbuffer_, 0);

            context_->VSSetConstantBuffers(0, 1, &parameter_cbuffer_);
            context_->Draw(waveform_view_buf->sample_count, 0);
        }
#else
        ID3D11ShaderResourceView* srv = nullptr;
        const UINT stride = 1;
        const UINT offset = 0;

        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        
        context_->VSSetConstantBuffers(0, 1, &parameter_cbuffer_);
        context_->GSSetConstantBuffers(0, 1, &parameter_cbuffer_);
        context_->PSSetConstantBuffers(0, 1, &parameter_cbuffer_);
        context_->OMSetBlendState(blend_state_, {}, 0xffffffff);

        if (anti_aliasing) {
            context_->VSSetShader(waveform_aa_.vs, nullptr, 0);
            context_->GSSetShader(waveform_aa_.gs, nullptr, 0);
            context_->PSSetShader(waveform_aa_.ps, nullptr, 0);

            for (auto& clip_content : clip_contents) {
                WaveformViewBufferD3D11* waveform_view_buf = static_cast<WaveformViewBufferD3D11*>(clip_content.view_buffer);

                if (srv != waveform_view_buf->srv) {
                    context_->VSSetShaderResources(0, 1, &waveform_view_buf->srv);
                    srv = waveform_view_buf->srv;
                }

                //uint32_t bucket_size = (uint32_t)(1. / (double)clip_content.scale_x);
                //uint32_t bucket_count = waveform_view_buf->sample_count / bucket_size;

                D3D11_MAPPED_SUBRESOURCE mapped_resource{};
                context_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
                WaveformViewParam* param = (WaveformViewParam*)mapped_resource.pData;
                param->origin_x = clip_content.min.x;
                param->origin_y = clip_content.min.y;
                param->scale_x = clip_content.scale_x;
                param->scale_y = clip_content.max.y - clip_content.min.y;
                param->color = clip_content.color;
                param->vp_width = vp_width;
                param->vp_height = vp_height;
                context_->Unmap(parameter_cbuffer_, 0);

                context_->Draw(waveform_view_buf->sample_count, 0);
            }
        }
        else {
            context_->VSSetShader(waveform_.vs, nullptr, 0);
            context_->GSSetShader(nullptr, nullptr, 0);
            context_->PSSetShader(waveform_.ps, nullptr, 0);

            for (auto& clip_content : clip_contents) {
                WaveformViewBufferD3D11* waveform_view_buf = static_cast<WaveformViewBufferD3D11*>(clip_content.view_buffer);

                if (srv != waveform_view_buf->srv) {
                    context_->VSSetShaderResources(0, 1, &waveform_view_buf->srv);
                    srv = waveform_view_buf->srv;
                }

                D3D11_MAPPED_SUBRESOURCE mapped_resource{};
                context_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
                WaveformViewParam* param = (WaveformViewParam*)mapped_resource.pData;
                param->origin_x = clip_content.min.x;
                param->origin_y = clip_content.min.y;
                param->scale_x = clip_content.scale_x;
                param->scale_y = clip_content.max.y - clip_content.min.y;
                param->color = clip_content.color;
                param->vp_width = vp_width;
                param->vp_height = vp_height;
                context_->Unmap(parameter_cbuffer_, 0);

                context_->Draw(waveform_view_buf->sample_count, 0);
            }
        }
#endif
    }

    void RendererD3D11::render_imgui(ImDrawData* draw_data)
    {
        ImGui_ImplDX11_RenderDrawData(draw_data);
    }

    void RendererD3D11::present()
    {
        swapchain_->Present(1, 0);
        WaitForSingleObjectEx(frame_latency_waitable_object_, 1000, true);
    }
    
    Renderer* RendererD3D11::create(App* app)
    {
        SDL_Window* window = ((AppSDL2*)app)->main_window;

        Log::info("Initializing D3D11 renderer...");

        if (!ImGui_ImplSDL2_InitForD3D(window)) {
            return {};
        }

        void* d3d11_dll = SDL_LoadObject("d3d11.dll");

        if (!d3d11_dll) {
            return {};
        }

        auto d3d11_create_device_and_swapchain =
            (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)SDL_LoadFunction(d3d11_dll, "D3D11CreateDeviceAndSwapChain");

        SDL_SysWMinfo wm_info{};

        if (!SDL_GetWindowWMInfo(window, &wm_info)) {
            return {};
        }

        DXGI_SWAP_CHAIN_DESC swapchain_desc{};
        swapchain_desc.BufferCount = 2;
        swapchain_desc.BufferDesc.Width = 0;
        swapchain_desc.BufferDesc.Height = 0;
        swapchain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchain_desc.BufferDesc.RefreshRate.Numerator = 60;
        swapchain_desc.BufferDesc.RefreshRate.Denominator = 1;
        swapchain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.OutputWindow = wm_info.info.win.window;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.SampleDesc.Quality = 0;
        swapchain_desc.Windowed = TRUE;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        UINT create_device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG;// D3D11_CREATE_DEVICE_DEBUG;

        static const D3D_FEATURE_LEVEL featureLevelArray[2] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };

        IDXGISwapChain* swapchain_;
        ID3D11Device* device;
        ID3D11DeviceContext* device_context;

        HRESULT result = d3d11_create_device_and_swapchain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
                                                           nullptr, create_device_flags,
                                                           featureLevelArray, 2, D3D11_SDK_VERSION,
                                                           &swapchain_desc, &swapchain_,
                                                           &device, nullptr, &device_context);

        if (FAILED(result)) {
            return {};
        }

        if (!ImGui_ImplDX11_Init(device, device_context)) {
            swapchain_->Release();
            device->Release();
            device_context->Release();
            return {};
        }

        IDXGISwapChain2* swapchain2;
        swapchain_->QueryInterface(&swapchain2);
        swapchain2->Release();

        RendererD3D11* ret = new(std::nothrow) RendererD3D11(swapchain2, device, device_context);
        if (!ret) {
            swapchain2->Release();
            device->Release();
            device_context->Release();
            return {};
        }

        if (!ret->init()) {
            delete ret;
            return {};
        }

        return ret;
    }

    bool RendererD3D11::load_shaders_(const char* vs_file, const char* gs_file, const char* ps_file, ShadersD3D11* ret)
    {
        ShadersD3D11 tmp{};

        if (vs_file) {
            auto bytecode{ load_binary_file(vs_file) };
            if (!bytecode) return false;

            Microsoft::WRL::ComPtr<ID3D11VertexShader> shader{};
            device_->CreateVertexShader(bytecode->data(), bytecode->size(), nullptr, &shader);
            if (!shader) return false;
            tmp.vs = shader.Detach();
        }

        if (gs_file) {
            auto bytecode{ load_binary_file(gs_file) };
            if (!bytecode) return false;

            Microsoft::WRL::ComPtr<ID3D11GeometryShader> shader{};
            device_->CreateGeometryShader(bytecode->data(), bytecode->size(), nullptr, &shader);
            if (!shader) return false;
            tmp.gs = shader.Detach();
        }

        if (ps_file) {
            auto bytecode{ load_binary_file(ps_file) };
            if (!bytecode) return false;

            Microsoft::WRL::ComPtr<ID3D11PixelShader> shader{};
            device_->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
            if (!shader) return false;
            tmp.ps = shader.Detach();
        }

        *ret = tmp;
        return true;
    }
}