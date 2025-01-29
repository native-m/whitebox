#include "renderer2.h"
#include "core/debug.h"
#include "renderer_vulkan2.h"

namespace wb {
static void imgui_renderer_create_window(ImGuiViewport* viewport) {
    SDL_Window* window = SDL_GetWindowFromID((uint32_t)(uint64_t)viewport->PlatformHandle);
}

static void imgui_renderer_destroy_window(ImGuiViewport* viewport) {
}

static void imgui_renderer_set_window_size(ImGuiViewport* viewport, ImVec2 size) {
}

static void imgui_renderer_render_window(ImGuiViewport* viewport, void* userdata) {
}

static void imgui_renderer_swap_buffers(ImGuiViewport* viewport, void* userdata) {
}

void GPURenderer::clear_state() {
    current_pipeline = {};
    current_vtx_buf = {};
    current_idx_buf = {};
    std::memset(current_storage_buf, 0, sizeof(current_storage_buf));
    std::memset(current_texture, 0, sizeof(current_texture));
}

void init_renderer2(SDL_Window* window) {
    Log::info("Initializing renderer...");
    g_renderer2 = GPURendererVK::create(window);
    if (!g_renderer2)
        Log::error("Failed to create renderer");
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow = imgui_renderer_create_window;
    platform_io.Renderer_DestroyWindow = imgui_renderer_destroy_window;
    platform_io.Renderer_SetWindowSize = imgui_renderer_set_window_size;
    platform_io.Renderer_RenderWindow = imgui_renderer_render_window;
    platform_io.Renderer_SwapBuffers = imgui_renderer_swap_buffers;
}
void shutdown_renderer2() {
    delete g_renderer2;
}

GPURenderer* g_renderer2;
} // namespace wb