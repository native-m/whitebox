#include "renderer.h"
#include "renderer_d3d11.h"
#include "core/debug.h"

namespace wb
{
    void Renderer::init(App* app, uint32_t renderer_type)
    {
        switch (renderer_type) {
            case Renderer::D3D11:   instance = RendererD3D11::create(app); break;
            case Renderer::OpenGL:  break;
            case Renderer::Vulkan:  break;
        }
    }

    void Renderer::shutdown()
    {
        delete instance;
    }

    Renderer* RendererD3D11::instance = nullptr;
}