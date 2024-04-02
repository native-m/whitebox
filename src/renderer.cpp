#include "renderer.h"
#include "renderer_d3d11.h"
#include "core/debug.h"

namespace wb {

Renderer* g_renderer = nullptr;

void init_renderer(App* app) {
    g_renderer = RendererD3D11::create(app);
    if (!g_renderer)
        Log::error("Failed to create renderer");
}

void shutdown_renderer() {
    delete g_renderer;
}

} // namespace wb