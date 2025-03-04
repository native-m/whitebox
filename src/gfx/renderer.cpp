#include "renderer.h"
#include "core/bit_manipulation.h"
#include "core/debug.h"
#include "core/fs.h"
#include "platform/platform.h"
#include "renderer_vulkan.h"
#include "waveform_visual.h"

namespace wb {

static void imgui_renderer_create_window(ImGuiViewport* viewport) {
    SDL_Window* window = SDL_GetWindowFromID((uint32_t)(uint64_t)viewport->PlatformHandle);
    wm_make_child_window(window, wm_get_main_window(), true);
    g_renderer->add_viewport(viewport);
}

static void imgui_renderer_destroy_window(ImGuiViewport* viewport) {
    if (viewport->RendererUserData)
        g_renderer->remove_viewport(viewport);
}

static void imgui_renderer_set_window_size(ImGuiViewport* viewport, ImVec2 size) {
    g_renderer->resize_viewport(viewport, size);
}

static void imgui_renderer_render_window(ImGuiViewport* viewport, void* userdata) {
    if (!has_bit(viewport->Flags, ImGuiViewportFlags_IsMinimized)) {
        g_renderer->begin_render((GPUTexture*)viewport->RendererUserData, {0.0f, 0.0f, 0.0f, 1.0f});
        g_renderer->render_imgui_draw_data(viewport->DrawData);
        g_renderer->end_render();
    }
}

static void imgui_renderer_swap_buffers(ImGuiViewport* viewport, void* userdata) {
}

bool GPURenderer::init(SDL_Window* window) {
    auto imgui_vs = read_file_content("assets/imgui.vert.spv");
    auto imgui_fs = read_file_content("assets/imgui.frag.spv");
    auto waveform_aa_vs = read_file_content("assets/waveform_aa.vs.spv");
    auto waveform_aa_fs = read_file_content("assets/waveform_aa.fs.spv");
    auto waveform_fill_vs = read_file_content("assets/waveform_fill.vs.spv");

    waveform_aa = create_pipeline({
        .vs = waveform_aa_vs.data(),
        .vs_size = (uint32_t)waveform_aa_vs.size(),
        .fs = waveform_aa_fs.data(),
        .fs_size = (uint32_t)waveform_aa_fs.size(),
        .shader_parameter_size = sizeof(WaveformDrawParam),
        .primitive_topology = GPUPrimitiveTopology::TriangleList,
        .enable_blending = true,
        .enable_color_write = true,
    });

    waveform_fill = create_pipeline({
        .vs = waveform_fill_vs.data(),
        .vs_size = (uint32_t)waveform_fill_vs.size(),
        .fs = waveform_aa_fs.data(),
        .fs_size = (uint32_t)waveform_aa_fs.size(),
        .shader_parameter_size = sizeof(WaveformDrawParam),
        .primitive_topology = GPUPrimitiveTopology::TriangleStrip,
        .enable_blending = false,
        .enable_color_write = true,
    });
    assert(waveform_aa && waveform_fill);

    GPUVertexAttribute imgui_pipeline_attributes[3] {
        {
            .semantic_name = "POSITION",
            .slot = 0,
            .format = GPUFormat::FloatR32G32,
            .offset = offsetof(ImDrawVert, pos),
        },
        {
            .semantic_name = "COLOR0",
            .slot = 1,
            .format = GPUFormat::FloatR32G32,
            .offset = offsetof(ImDrawVert, uv),
        },
        {
            .semantic_name = "TEXCOORD0",
            .slot = 2,
            .format = GPUFormat::UnormR8G8B8A8,
            .offset = offsetof(ImDrawVert, col),
        },
    };

    imgui_pipeline = create_pipeline({
        .vs = imgui_vs.data(),
        .vs_size = (uint32_t)imgui_vs.size(),
        .fs = imgui_fs.data(),
        .fs_size = (uint32_t)imgui_fs.size(),
        .shader_parameter_size = sizeof(float) * 4,
        .vertex_stride = sizeof(ImDrawVert),
        .num_vertex_attributes = 3,
        .vertex_attributes = imgui_pipeline_attributes,
        .primitive_topology = GPUPrimitiveTopology::TriangleList,
        .enable_blending = true,
        .enable_color_write = true,
    });

    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    font_texture =
        create_texture(GPUTextureUsage::Sampled, GPUFormat::UnormR8G8B8A8, width, height, true, width, height, pixels);
    io.Fonts->SetTexID((ImTextureID)font_texture);

    return true;
}

void GPURenderer::shutdown() {
    if (waveform_aa)
        destroy_pipeline(waveform_aa);
    if (waveform_fill)
        destroy_pipeline(waveform_fill);

    if (font_texture)
        destroy_texture(font_texture);
    if (imm_vtx_buf)
        destroy_buffer(imm_vtx_buf);
    if (imm_idx_buf)
        destroy_buffer(imm_idx_buf);
    if (imgui_pipeline)
        destroy_pipeline(imgui_pipeline);
}

void GPURenderer::begin_frame() {
    immediate_vtx_offset = 0;
    immediate_idx_offset = 0;
}

void GPURenderer::render_imgui_draw_data(ImDrawData* draw_data) {
    static constexpr GPUBufferUsageFlags usage = GPUBufferUsage::Writeable | GPUBufferUsage::CPUAccessible;
    uint32_t new_total_vtx_count = immediate_vtx_offset + draw_data->TotalVtxCount;
    uint32_t new_total_idx_count = immediate_idx_offset + draw_data->TotalIdxCount;

    if (new_total_vtx_count == 0)
        new_total_vtx_count = 1024;
    if (new_total_idx_count == 0)
        new_total_idx_count = 1024;

    if (imm_vtx_buf == nullptr || new_total_vtx_count > total_vtx_count) {
        size_t vertex_size = new_total_vtx_count * sizeof(ImDrawVert);
        GPUBuffer* buffer = create_buffer(usage | GPUBufferUsage::Vertex, vertex_size, true, 0, nullptr);
        if (imm_vtx_buf)
            destroy_buffer(imm_vtx_buf);
        imm_vtx_buf = buffer;
        immediate_vtx_offset = 0;
        total_vtx_count = new_total_vtx_count;
    }
    if (imm_idx_buf == nullptr || new_total_idx_count > total_idx_count) {
        size_t index_size = new_total_idx_count * sizeof(ImDrawIdx);
        GPUBuffer* buffer = create_buffer(usage | GPUBufferUsage::Index, index_size, true, 0, nullptr);
        if (imm_idx_buf)
            destroy_buffer(imm_idx_buf);
        imm_idx_buf = buffer;
        immediate_idx_offset = 0;
        total_idx_count = new_total_idx_count;
    }

    // Copy vertices and indices to the actual vertex & index buffer
    ImDrawVert* vtx_dst = (ImDrawVert*)map_buffer(imm_vtx_buf) + immediate_vtx_offset;
    ImDrawIdx* idx_dst = (ImDrawIdx*)map_buffer(imm_idx_buf) + immediate_idx_offset;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    unmap_buffer(imm_vtx_buf);
    unmap_buffer(imm_idx_buf);

    auto setup_imgui_render_state = [this, draw_data] {
        float scale_x = 2.0f / draw_data->DisplaySize.x;
        float scale_y = 2.0f / draw_data->DisplaySize.y;
        float shader_param[4] = {
            scale_x,
            scale_y,
            -1.0f - draw_data->DisplayPos.x * scale_x,
            -1.0f - draw_data->DisplayPos.y * scale_y,
        };

        bind_pipeline(imgui_pipeline);
        bind_vertex_buffer(imm_vtx_buf);
        bind_index_buffer(imm_idx_buf);
        set_shader_parameter(sizeof(shader_param), shader_param);
        set_viewport(0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y);
    };

    setup_imgui_render_state();

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
    ImVec2 clip_limit((float)fb_w, (float)fb_h);

    // Render command lists
    int global_vtx_offset = immediate_vtx_offset;
    int global_idx_offset = immediate_idx_offset;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user
                // to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    setup_imgui_render_state();
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as vkCmdSetScissor() won't accept values that are off
                // bounds
                if (clip_min.x < 0.0f) {
                    clip_min.x = 0.0f;
                }
                if (clip_min.y < 0.0f) {
                    clip_min.y = 0.0f;
                }
                if (clip_max.x > clip_limit.x) {
                    clip_max.x = clip_limit.x;
                }
                if (clip_max.y > clip_limit.y) {
                    clip_max.y = clip_limit.y;
                }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                uint32_t clip_w = (uint32_t)(clip_max.x - clip_min.x);
                uint32_t clip_h = (uint32_t)(clip_max.y - clip_min.y);
                set_scissor((int32_t)clip_min.x, (int32_t)clip_min.y, clip_w, clip_h);

                // Bind font or user texture
                bind_texture(0, (GPUTexture*)pcmd->TextureId);

                // Draw
                draw_indexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
    immediate_vtx_offset = global_vtx_offset;
    immediate_idx_offset = global_idx_offset;
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
    g_renderer = GPURendererVK::create(window);
    if (!g_renderer)
        Log::error("Failed to create renderer");

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = (void*)g_renderer;
    io.BackendRendererName = "imgui_impl_whitebox";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow = imgui_renderer_create_window;
    platform_io.Renderer_DestroyWindow = imgui_renderer_destroy_window;
    platform_io.Renderer_SetWindowSize = imgui_renderer_set_window_size;
    platform_io.Renderer_RenderWindow = imgui_renderer_render_window;
    platform_io.Renderer_SwapBuffers = imgui_renderer_swap_buffers;
}

void shutdown_renderer2() {
    g_renderer->shutdown();
    delete g_renderer;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = nullptr;
}

GPURenderer* g_renderer;

} // namespace wb