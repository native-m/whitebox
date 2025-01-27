#include "renderer2.h"
#include "renderer_vulkan2.h"
#include "core/debug.h"

namespace wb {
void init_renderer2(SDL_Window* window) {
    Log::info("Initializing renderer...");
    g_renderer2 = GPURendererVK::create(window);
    if (!g_renderer2)
        Log::error("Failed to create renderer");
}
void shutdown_renderer2() {
    delete g_renderer;
}
} // namespace wb