#include "core/platform_def.h"

#ifdef WB_PLATFORM_WINDOWS

#include "renderer_d3d11.h"
#include "app_sdl2.h"
#include "core/debug.h"
#include <SDL_syswm.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl2.h>
#include <wrl.h>

using namespace Microsoft::WRL;

namespace wb {

struct ClipContentDrawCmdD3D11 {
    float origin_x;
    float origin_y;
    float scale_x;
    float scale_y;
    ImColor color;
    float vp_width;
    float vp_height;
    int is_min;
    uint32_t start_idx;
};

static ID3D11VertexShader* load_vs(ID3D11Device* device, const char* file) {
    size_t size;
    const void* bytecode = SDL_LoadFile(file, &size);
    if (!bytecode)
        return nullptr;
    ComPtr<ID3D11VertexShader> shader;
    if (FAILED(device->CreateVertexShader(bytecode, size, nullptr, &shader)))
        return nullptr;
    return shader.Detach();
}

static ID3D11PixelShader* load_ps(ID3D11Device* device, const char* file) {
    size_t size;
    const void* bytecode = SDL_LoadFile(file, &size);
    if (!bytecode)
        return nullptr;
    ComPtr<ID3D11PixelShader> shader;
    if (FAILED(device->CreatePixelShader(bytecode, size, nullptr, &shader)))
        return nullptr;
    return shader.Detach();
}

RendererD3D11::RendererD3D11(IDXGISwapChain2* swapchain, ID3D11Device* device,
                             ID3D11DeviceContext* ctx) :
    swapchain_(swapchain), device_(device), ctx_(ctx) {
    swapchain_->SetMaximumFrameLatency(1);
    frame_latency_waitable_handle_ = swapchain->GetFrameLatencyWaitableObject();
    resize_swapchain();
}

RendererD3D11::~RendererD3D11() {
    if (frame_latency_waitable_handle_) {
        WaitForSingleObjectEx(frame_latency_waitable_handle_, 1000, true);
        CloseHandle(frame_latency_waitable_handle_);
    }

    ImGui_ImplDX11_Shutdown();

    if (waveform_aa_vs_)
        waveform_aa_vs_->Release();
    if (waveform_vs_)
        waveform_vs_->Release();
    if (waveform_ps_)
        waveform_ps_->Release();
    if (parameter_cbuffer_)
        parameter_cbuffer_->Release();
    if (standard_blend_)
        standard_blend_->Release();
    if (rasterizer_state_)
        rasterizer_state_->Release();
    if (backbuffer_rtv_)
        backbuffer_rtv_->Release();
    if (ctx_)
        ctx_->Release();
    if (device_)
        device_->Release();
    if (swapchain_)
        swapchain_->Release();
}

bool RendererD3D11::init() {
    if (!ImGui_ImplDX11_Init(device_, ctx_))
        return false;

    ID3D11VertexShader* waveform_aa_vs = load_vs(device_, "assets/waveform2_aa_vs.hlsl.dxbc");
    ID3D11VertexShader* waveform_vs = load_vs(device_, "assets/waveform2_vs.hlsl.dxbc");
    ID3D11PixelShader* waveform_ps = load_ps(device_, "assets/waveform_aa_ps.hlsl.dxbc");

    if (!(waveform_aa_vs && waveform_vs && waveform_ps))
        return false;

    D3D11_BUFFER_DESC desc {};
    desc.ByteWidth = 256;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    Microsoft::WRL::ComPtr<ID3D11Buffer> parameter_cbuffer {};
    device_->CreateBuffer(&desc, nullptr, &parameter_cbuffer);
    if (!parameter_cbuffer)
        return false;

    D3D11_RASTERIZER_DESC raster_desc {
        .FillMode = D3D11_FILL_SOLID,
        .CullMode = D3D11_CULL_NONE,
        .FrontCounterClockwise = FALSE,
        .DepthClipEnable = TRUE,
        .ScissorEnable = TRUE,
        .MultisampleEnable = TRUE,
        .AntialiasedLineEnable = FALSE,
    };

    ComPtr<ID3D11RasterizerState> rasterizer_state;
    device_->CreateRasterizerState(&raster_desc, &rasterizer_state);
    if (!parameter_cbuffer)
        return false;

    D3D11_BLEND_DESC blend_desc {};
    blend_desc.AlphaToCoverageEnable = false;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    ComPtr<ID3D11BlendState> blend {};
    device_->CreateBlendState(&blend_desc, &blend);
    if (!blend)
        return false;

    waveform_aa_vs_ = waveform_aa_vs;
    waveform_vs_ = waveform_vs;
    waveform_ps_ = waveform_ps;
    parameter_cbuffer_ = parameter_cbuffer.Detach();
    rasterizer_state_ = rasterizer_state.Detach();
    standard_blend_ = blend.Detach();

    return true;
}

std::shared_ptr<Framebuffer> RendererD3D11::create_framebuffer(uint32_t width, uint32_t height) {
    D3D11_TEXTURE2D_DESC desc {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
        .MiscFlags = 0,
    };

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)))
        return {};

    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device_->CreateRenderTargetView(texture.Get(), nullptr, &rtv)))
        return {};

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &srv)))
        return {};

    std::shared_ptr<FramebufferD3D11> ret {std::make_shared<FramebufferD3D11>()};
    ret->width = width;
    ret->height = height;
    ret->texture = texture.Detach();
    ret->rtv = rtv.Detach();
    ret->srv = srv.Detach();

    return ret;
}

std::shared_ptr<SamplePeaks> RendererD3D11::create_sample_peaks(const Sample& sample,
                                                                SamplePeaksPrecision precision) {
    size_t sample_count = sample.count;
    uint32_t current_mip = 1;
    uint32_t max_mip = 0;
    DXGI_FORMAT resource_format;
    uint32_t elem_size = 0;

    switch (precision) {
        case SamplePeaksPrecision::Low:
            resource_format = DXGI_FORMAT_R8_SNORM;
            elem_size = sizeof(int8_t);
            break;
        case SamplePeaksPrecision::High:
            resource_format = DXGI_FORMAT_R16_SNORM;
            elem_size = sizeof(int16_t);
            break;
        default:
            WB_UNREACHABLE();
    }

    std::vector<SamplePeaksMipD3D11> mipmap;
    bool failed = false;

    while (sample_count > 64) {
        Log::info("Generating mip-map {} ({})", current_mip, sample_count);

        // Calculate the required length for the buffer. This does not do the actual summarization.
        size_t required_length;
        sample.summarize_for_mipmaps(precision, 0, current_mip, 0, &required_length, nullptr);

        size_t total_length = required_length * sample.channels;

        // Using staging buffer to avoid additional memory allocation.
        D3D11_BUFFER_DESC staging_buffer_desc {
            .ByteWidth = (UINT)total_length * elem_size,
            .Usage = D3D11_USAGE_STAGING,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        ComPtr<ID3D11Buffer> staging_buffer;
        if (FAILED(device_->CreateBuffer(&staging_buffer_desc, nullptr, &staging_buffer))) {
            failed = true;
            break;
        }

        D3D11_MAPPED_SUBRESOURCE staging;
        ctx_->Map(staging_buffer.Get(), 0, D3D11_MAP_WRITE, 0, &staging);
        for (uint32_t i = 0; i < sample.channels; i++) {
            sample.summarize_for_mipmaps(precision, i, current_mip, required_length * i,
                                         &required_length, staging.pData);
        }
        ctx_->Unmap(staging_buffer.Get(), 0);

        D3D11_BUFFER_DESC buffer_desc {
            .ByteWidth = (UINT)total_length * elem_size,
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc {
            .Format = resource_format,
            .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
            .Buffer =
                {
                    .FirstElement = 0,
                    .NumElements = (UINT)total_length,
                },
        };

        ComPtr<ID3D11Buffer> mip_buffer;
        ComPtr<ID3D11ShaderResourceView> srv;

        if (FAILED(device_->CreateBuffer(&buffer_desc, nullptr, &mip_buffer))) {
            failed = true;
            break;
        }

        if (FAILED(device_->CreateShaderResourceView(mip_buffer.Get(), &srv_desc, &srv))) {
            failed = true;
            break;
        }

        ctx_->CopyResource(mip_buffer.Get(), staging_buffer.Get());

        mipmap.push_back({
            .buffer = mip_buffer.Detach(),
            .srv = srv.Detach(),
            .size = required_length,
        });

        sample_count /= 4;
        current_mip += 2;
        max_mip = current_mip - 1;
    }

    if (failed) {
        for (auto mip : mipmap)
            mip.destroy();
        return {};
    }

    std::shared_ptr<SamplePeaksD3D11> ret {std::make_shared<SamplePeaksD3D11>()};
    ret->sample_count = sample.count;
    ret->mipmap_count = (uint32_t)mipmap.size();
    ret->channels = sample.channels;
    ret->precision = precision;
    ret->cpu_accessible = false;
    ret->mipmap = std::move(mipmap);

    return ret;
}

void RendererD3D11::new_frame() {
    // This reduces input latency!
    WaitForSingleObjectEx(frame_latency_waitable_handle_, 1000, true);
    ImGui_ImplDX11_NewFrame();
}

void RendererD3D11::end_frame() {
}

void RendererD3D11::resize_swapchain() {
    constexpr UINT swapchain_flags =
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    if (backbuffer_rtv_)
        backbuffer_rtv_->Release();
    swapchain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, swapchain_flags);

    ID3D11Texture2D* backbuffer = nullptr;
    swapchain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
    assert(backbuffer);

    device_->CreateRenderTargetView(backbuffer, nullptr, &backbuffer_rtv_);
    backbuffer->Release();
}

void RendererD3D11::set_framebuffer(const std::shared_ptr<Framebuffer>& framebuffer) {
    if (framebuffer == nullptr) {
        UINT width, height;
        swapchain_->GetSourceSize(&width, &height);

        ctx_->OMSetRenderTargets(1, &backbuffer_rtv_, nullptr);

        const D3D11_RECT rect = {0, 0, (LONG)width, (LONG)height};
        ctx_->RSSetScissorRects(1, &rect);

        D3D11_VIEWPORT vp;
        vp.Width = (float)width;
        vp.Height = (float)height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = vp.TopLeftY = 0;
        ctx_->RSSetViewports(1, &vp);

        fb_width = width;
        fb_height = height;
        vp_width = 2.0f / vp.Width;
        vp_height = 2.0f / vp.Height;
        current_rtv_ = backbuffer_rtv_;
        return;
    }

    FramebufferD3D11* impl = static_cast<FramebufferD3D11*>(framebuffer.get());

    if (current_rtv_ != impl->rtv) {
        ctx_->OMSetRenderTargets(1, &impl->rtv, nullptr);
    }

    const D3D11_RECT rect = {0, 0, (LONG)impl->width, (LONG)impl->height};
    ctx_->RSSetScissorRects(1, &rect);

    D3D11_VIEWPORT vp;
    vp.Width = (float)impl->width;
    vp.Height = (float)impl->height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0;
    ctx_->RSSetViewports(1, &vp);

    fb_width = impl->width;
    fb_height = impl->height;
    vp_width = 2.0f / vp.Width;
    vp_height = 2.0f / vp.Height;
    current_rtv_ = impl->rtv;
    // vmask_target_ = impl->rtv;
}

void RendererD3D11::begin_draw(const std::shared_ptr<Framebuffer>& framebuffer,
                               const ImVec4& clear_color) {
}

void RendererD3D11::finish_draw() {
}

void RendererD3D11::clear(float r, float g, float b, float a) {
    const float clear_color[4] = {r, g, b, a};
    ctx_->ClearRenderTargetView(current_rtv_, clear_color);
}

void RendererD3D11::draw_waveforms(const ImVector<ClipContentDrawCmd>& clips) {
    if (clips.Size == 0)
        return;

    ID3D11ShaderResourceView* current_mip_srv = nullptr;

    ctx_->IASetInputLayout(nullptr);
    ctx_->PSSetShader(waveform_ps_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &parameter_cbuffer_);
    ctx_->PSSetConstantBuffers(0, 1, &parameter_cbuffer_);
    ctx_->RSSetState(rasterizer_state_);

    for (const auto& clip : clips) {
        SamplePeaksD3D11* peaks = static_cast<SamplePeaksD3D11*>(clip.peaks);
        const SamplePeaksMipD3D11& mip = peaks->mipmap[clip.mip_index];
        ID3D11ShaderResourceView* srv = mip.srv;

        if (current_mip_srv != srv) {
            ctx_->VSSetShaderResources(0, 1, &srv);
            current_mip_srv = srv;
        }

        RECT scissor_rect {
            std::max((int32_t)clip.min_bb.x, 0),
            std::max((int32_t)clip.min_bb.y, 0),
            std::min((int32_t)clip.max_bb.x, fb_width),
            std::min((int32_t)clip.max_bb.y, fb_height),
        };
        ctx_->RSSetScissorRects(1, &scissor_rect);

        D3D11_MAPPED_SUBRESOURCE mapped_resource {};
        ctx_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
        ClipContentDrawCmdD3D11* param = (ClipContentDrawCmdD3D11*)mapped_resource.pData;
        param->origin_x = clip.min_bb.x + 0.5f;
        param->origin_y = clip.min_bb.y;
        param->scale_x = clip.scale_x;
        param->scale_y = clip.max_bb.y - clip.min_bb.y;
        param->color = clip.color;
        param->vp_width = vp_width;
        param->vp_height = vp_height;
        param->is_min = 0;
        param->start_idx = clip.start_idx;
        ctx_->Unmap(parameter_cbuffer_, 0);

        // Draw filling
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx_->VSSetShader(waveform_vs_, nullptr, 0);
        ctx_->OMSetBlendState(nullptr, {}, 0xffffffff);
        ctx_->Draw(clip.draw_count, 0);

        // Draw anti-aliasing fringes
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(waveform_aa_vs_, nullptr, 0);
        ctx_->OMSetBlendState(standard_blend_, {}, 0xffffffff);
        ctx_->Draw(clip.draw_count * 3U, 0);

        ctx_->Map(parameter_cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
        param = (ClipContentDrawCmdD3D11*)mapped_resource.pData;
        param->origin_x = clip.min_bb.x + 0.5f;
        param->origin_y = clip.min_bb.y;
        param->scale_x = clip.scale_x;
        param->scale_y = clip.max_bb.y - clip.min_bb.y;
        param->vp_width = vp_width;
        param->vp_height = vp_height;
        param->color = clip.color;
        param->is_min = 1;
        param->start_idx = clip.start_idx;
        ctx_->Unmap(parameter_cbuffer_, 0);

        ctx_->Draw(clip.draw_count * 3U, 0);
    }
}

void RendererD3D11::render_draw_data(ImDrawData* draw_data) {
    ImGui_ImplDX11_RenderDrawData(draw_data);
}

void RendererD3D11::present() {
    swapchain_->Present(1, 0);
}

Renderer* RendererD3D11::create(App* app) {
    Log::info("Creating D3D11 renderer...");

    void* d3d11_dll = SDL_LoadObject("d3d11.dll");

    // Manually link d3d11 library
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN d3d11_create_device_and_swap_chain =
        (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)SDL_LoadFunction(d3d11_dll,
                                                                 "D3D11CreateDeviceAndSwapChain");

    HWND hwnd;
    SDL_Window* window = ((AppSDL2*)app)->window;
    SDL_SysWMinfo wm_info {};
    SDL_GetWindowWMInfo(window, &wm_info);
    hwnd = wm_info.info.win.window;

    if (!ImGui_ImplSDL2_InitForD3D(window))
        return {};

    DXGI_SWAP_CHAIN_DESC swapchain_desc {
        .BufferDesc =
            {
                .RefreshRate =
                    {
                        .Numerator = 60,
                        .Denominator = 1,
                    },
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            },
        .SampleDesc =
            {
                .Count = 1,
                .Quality = 0,
            },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .OutputWindow = hwnd,
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH |
                 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT,
    };

    static const D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    UINT device_flags = D3D11_CREATE_DEVICE_DEBUG;

    IDXGISwapChain* swapchain;
    ID3D11Device* device;
    ID3D11DeviceContext* ctx;

    HRESULT result = d3d11_create_device_and_swap_chain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, &feature_level, 1,
        D3D11_SDK_VERSION, &swapchain_desc, &swapchain, &device, nullptr, &ctx);

    if (FAILED(result))
        return {};

    IDXGISwapChain2* swapchain2;
    swapchain->QueryInterface(&swapchain2);
    swapchain->Release();

    IDXGIDevice1* device1;
    device->QueryInterface(&device1);

    RendererD3D11* ret = new (std::nothrow) RendererD3D11(swapchain2, device, ctx);
    if (!ret) {
        swapchain2->Release();
        device->Release();
        ctx->Release();
        return {};
    }

    if (!ret->init()) {
        delete ret;
        return {};
    }

    return ret;
}

} // namespace wb
#endif