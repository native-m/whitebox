#include "renderer_d3d11.h"
#include "core/debug.h"
#include "types.h"
#include "app_sdl2.h"

#include <SDL_syswm.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl2.h>

namespace wb
{
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
        WaitForSingleObjectEx(frame_latency_waitable_object_, 1000, true);
        ImGui_ImplDX11_Shutdown();

        if (backbuffer_rtv_) {
            backbuffer_rtv_->Release();
        }

        CloseHandle(frame_latency_waitable_object_);
        swapchain_->Release();
        context_->Release();
        device_->Release();
    }

    std::shared_ptr<SamplePreviewBuffer> RendererD3D11::create_sample_preview_buffer(uint32_t num_samples, PixelFormat format, GPUMemoryType memory_type)
    {
        return std::shared_ptr<SamplePreviewBuffer>();
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
    }

    void RendererD3D11::render_draw_data(ImDrawData* draw_data)
    {
        ImVec4 bg_col = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
        const float clear_color[4] = { bg_col.x, bg_col.y, bg_col.z, 1.0f };
        context_->OMSetRenderTargets(1, &backbuffer_rtv_, nullptr);
        context_->ClearRenderTargetView(backbuffer_rtv_, clear_color);
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

        return new RendererD3D11(swapchain2, device, device_context);
    }
}