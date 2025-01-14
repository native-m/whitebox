#include "renderer.h"
#include "renderer_d3d11.h"
#include "renderer_vulkan.h"
#include "core/debug.h"

namespace wb {

Renderer* g_renderer = nullptr;

void init_renderer(SDL_Window* window) {
    Log::info("Initializing renderer...");
    g_renderer = RendererVK::create(window);
    if (!g_renderer)
        Log::error("Failed to create renderer");
}

void shutdown_renderer() {
    delete g_renderer;
}

} // namespace wb