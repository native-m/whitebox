#include "renderer_d3d11.h"
#include "core/debug.h"
#include "core/file.h"
#include "types.h"
#include "stdpch.h"
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
        uint32_t chunk_size;
        uint32_t start_sample_idx;
        uint32_t end_sample_idx;
    };

    static inline uint32_t get_mip_level(float scale_x)
    {
        double chunk_size = std::max(std::round(0.125 / (double)scale_x), 1.0);
        uint32_t mip_level = std::min((uint32_t)std::round(std::log2(chunk_size)), 7U);
        return mip_level;
    }

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
        waveform_bevel_aa_.destroy();

        if (blend_state_) blend_state_->Release();
        if (rasterizer_state_) rasterizer_state_->Release();
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

        if (!load_shaders_("assets/waveform_bevel_aa_vs.hlsl.dxbc", nullptr, nullptr, &waveform_bevel_aa_))
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

        D3D11_RASTERIZER_DESC raster_desc{
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
            .FrontCounterClockwise = FALSE,
            .DepthClipEnable = TRUE,
            .ScissorEnable = TRUE,
            .MultisampleEnable = FALSE,
            .AntialiasedLineEnable = FALSE,
        };
        device_->CreateRasterizerState(&raster_desc, &rasterizer_state_);

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

    std::shared_ptr<SamplePeaks> RendererD3D11::create_sample_peaks(const Sample& sample, PixelFormat format, GPUMemoryType memory_type)
    {
        int16_t* data = (int16_t*)std::malloc(sample.sample_count * sample.channels * sizeof(int16_t));
        if (!data)
            return {};

        switch (sample.format) {
            case AudioFormat::I16:
                for (uint32_t i = 0; i < sample.channels; i++) {
                    const int16_t* sample_data = sample.get_read_pointer<int16_t>(i);
                    int16_t* channel_data = data + (i * sample.sample_count);
                    std::memcpy(channel_data, sample_data, sample.sample_count * sizeof(int16_t));
                }
                break;
            case AudioFormat::F32:
                for (uint32_t i = 0; i < sample.channels; i++) {
                    const float* sample_data = sample.get_read_pointer<float>(i);
                    int16_t* channel_data = data + (i * sample.sample_count);
                    for (uint32_t j = 0; j < sample.sample_count; j++) {
                        double value = (double)sample_data[j] * (double)((1 << 15) - 1);
                        channel_data[j] = (int16_t)(value >= 0 ? value + 0.5 : value - 0.5);
                    }
                }
                break;
            case AudioFormat::F64:
            default:
                WB_ASSERT(false && "Incompatible format at the moment");
        }

        std::shared_ptr<SamplePeaksD3D11> ret = std::make_shared<SamplePeaksD3D11>();
        if (!ret)
            return {};

        ret->sample_count = (uint32_t)sample.sample_count;
        ret->num_channels = sample.channels;

        int16_t* last_mip_data = nullptr;
        for (uint32_t mip_level = 0; mip_level < 8; mip_level++) {
            uint32_t chunk_size = 1 << mip_level;
            uint32_t mip_sample_count = (uint32_t)sample.sample_count / chunk_size;
            uint32_t chunk_count = mip_sample_count - mip_sample_count % 2;
            int16_t* mip_data = data;
            
            if (mip_level >= 1) {
                mip_data = (int16_t*)std::malloc(mip_sample_count * sample.channels * sizeof(int16_t));
                
                if (!mip_data)
                    return {};

                uint32_t last_mip_sample_count = (uint32_t)sample.sample_count / (1 << (mip_level - 1));

                // min-max downsampling
                for (uint32_t i = 0; i < sample.channels; i++) {
                    int16_t* input_data = &last_mip_data[i * last_mip_sample_count];
                    int16_t* output_data = &mip_data[i * mip_sample_count];

                    for (uint32_t j = 0; j < chunk_count; j += 2) {
                        int16_t* chunk = &input_data[j * 2];
                        int32_t min_idx = 0;
                        int32_t max_idx = 0;
                        int16_t min_val = chunk[0];
                        int16_t max_val = chunk[0];

                        for (uint32_t k = 0; k < 4; k++) {
                            int16_t value = chunk[k];
                            if (value < min_val) {
                                min_val = value;
                                min_idx = k;
                            }
                            else if (value > max_val) {
                                max_val = value;
                                max_idx = k;
                            }
                        }

                        if (min_idx < max_idx) {
                            output_data[j] = min_val;
                            output_data[j + 1] = max_val;
                        }
                        else {
                            output_data[j] = max_val;
                            output_data[j + 1] = min_val;
                        }
                    }

                    if (chunk_count != mip_sample_count)
                        output_data[mip_sample_count - 1] = input_data[last_mip_sample_count - 1];
                }
            }

            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = (UINT)(mip_sample_count * sample.channels * sizeof(int16_t));
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA init_data{};
            init_data.pSysMem = mip_data;

            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer{};
            if (FAILED(device_->CreateBuffer(&desc, &init_data, &buffer))) {
                std::free(data);
                if (last_mip_data) std::free(last_mip_data);
                return {};
            }

            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{
                .Format = DXGI_FORMAT_R16_SNORM,
                .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
                .Buffer = {
                    .FirstElement = 0,
                    .NumElements = (UINT)(mip_sample_count * sample.channels),
                }
            };

            if (FAILED(device_->CreateShaderResourceView(buffer.Get(), &srv_desc, &srv))) {
                std::free(mip_data);
                if (last_mip_data) std::free(last_mip_data);
                return {};
            }

            ret->mip_map[mip_level].buffer = buffer.Detach();
            ret->mip_map[mip_level].srv = srv.Detach();
            if (last_mip_data) std::free(last_mip_data);
            last_mip_data = mip_data;
        }

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

        fb_width = impl->width;
        fb_height = impl->height;
        vp_width = 2.0f / vp.Width;
        vp_height = 2.0f / vp.Height;
        current_render_target_ = impl->rtv;
    }

    void RendererD3D11::clear_framebuffer(float r, float g, float b, float a)
    {
        const float clear_color[4] = { r, g, b, a };
        context_->ClearRenderTargetView(current_render_target_, clear_color);
    }

    void RendererD3D11::draw_waveform(const std::shared_ptr<SamplePeaks>& waveform_view_buffer, const ImColor& color, const ImVec2& origin, float scale_x, float scale_y)
    {
#if 0
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
#endif
    }

    void RendererD3D11::draw_clip_content(const ImVector<ClipContentDrawArgs>& clip_contents, bool anti_aliasing) noexcept
    {
        ID3D11ShaderResourceView* srv = nullptr;
        const UINT stride = 1;
        const UINT offset = 0;

        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetConstantBuffers(0, 1, &parameter_cbuffer_);
        context_->PSSetConstantBuffers(0, 1, &parameter_cbuffer_);
        context_->OMSetBlendState(blend_state_, {}, 0xffffffff);
        context_->RSSetState(rasterizer_state_);

        if (anti_aliasing) {
            context_->PSSetShader(waveform_aa_.ps, nullptr, 0);

            for (auto& clip_content : clip_contents) {
                SamplePeaksD3D11* waveform_view_buf = static_cast<SamplePeaksD3D11*>(clip_content.sample_peaks);
                uint32_t mip_level = get_mip_level(clip_content.scale_x);
                ID3D11ShaderResourceView* mip_srv = waveform_view_buf->mip_map[mip_level].srv;

                if (srv != mip_srv) {
                    context_->VSSetShaderResources(0, 1, &mip_srv);
                    srv = mip_srv;
                }

                uint32_t start_sample_idx = clip_content.start_sample_idx / (1 << mip_level);
                uint32_t end_sample_idx = clip_content.end_sample_idx / (1 << mip_level);
                uint32_t sample_count = end_sample_idx - start_sample_idx;
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
                param->chunk_size = 1 << mip_level;
                param->start_sample_idx = start_sample_idx;
                param->end_sample_idx = end_sample_idx - 1;
                context_->Unmap(parameter_cbuffer_, 0);

                RECT scissor_rect{
                    std::max((int32_t)clip_content.min.x, 0),
                    std::max((int32_t)clip_content.min.y, 0),
                    std::min((int32_t)clip_content.max.x, fb_width),
                    std::min((int32_t)clip_content.max.y, fb_height),
                };

                context_->RSSetScissorRects(1, &scissor_rect);
                context_->VSSetShader(waveform_aa_.vs, nullptr, 0);
                context_->Draw(sample_count * 6, 0);
                context_->VSSetShader(waveform_bevel_aa_.vs, nullptr, 0);
                context_->Draw(sample_count * 3, 0);
            }
        }
        else {
            context_->VSSetShader(waveform_.vs, nullptr, 0);
            context_->PSSetShader(waveform_.ps, nullptr, 0);

            for (auto& clip_content : clip_contents) {
                SamplePeaksD3D11* waveform_view_buf = static_cast<SamplePeaksD3D11*>(clip_content.sample_peaks);
                uint32_t mip_level = get_mip_level(clip_content.scale_x);
                ID3D11ShaderResourceView* mip_srv = waveform_view_buf->mip_map[mip_level].srv;

                if (srv != mip_srv) {
                    context_->VSSetShaderResources(0, 1, &mip_srv);
                    srv = mip_srv;
                }

                uint32_t vtx_count = waveform_view_buf->sample_count / (1 << mip_level);
                D3D11_MAPPED_SUBRESOURCE mapped_resource{};
                context_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
                WaveformViewParam* param = (WaveformViewParam*)mapped_resource.pData;
                param->origin_x = clip_content.min.x;
                param->origin_y = clip_content.min.y;
                param->scale_x = clip_content.scale_x;
                param->scale_y = clip_content.max.y - clip_content.min.y;
                param->color = clip_content.color;
                param->chunk_size = 1 << mip_level;
                param->vp_width = vp_width;
                param->vp_height = vp_height;
                context_->Unmap(parameter_cbuffer_, 0);

                RECT scissor_rect{
                    std::max((int32_t)clip_content.min.x, 0),
                    std::max((int32_t)clip_content.min.y, 0),
                    std::min((int32_t)clip_content.max.x, fb_width),
                    std::min((int32_t)clip_content.max.y, fb_height),
                };
                context_->RSSetScissorRects(1, &scissor_rect);
                context_->Draw(vtx_count, 0);
            }
        }
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