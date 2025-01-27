#pragma once

#include "core/common.h"
#include <SDL_video.h>
#include <imgui.h>

#define WB_GPU_RENDER_BUFFER_SIZE 2

namespace wb {

struct GPUBufferUsage {
    enum {
        Vertex = 1 << 0,
        Index = 1 << 1,
        Storage = 1 << 2,
        Dynamic = 1 << 3,
    };
};
using GPUBufferUsageFlags = uint32_t;

struct GPUTextureUsage {
    enum {
        RenderTarget = 1 << 0,
        Sampled = 1 << 1,
        ReadOnly = 1 << 2,
    };
};
using GPUTextureUsageFlags = uint32_t;

struct GPUPipelineDesc {
    const void* vs;
    uint32_t vs_size;
    const void* fs;
    uint32_t fs_size;
};

struct GPUBuffer {
    GPUBufferUsageFlags usage;
    void* impl[WB_GPU_RENDER_BUFFER_SIZE];
};

struct GPUTexture {
    GPUTextureUsageFlags usage;
    void* impl[WB_GPU_RENDER_BUFFER_SIZE];
    virtual ImTextureID as_imgui_texture_id() const = 0;
};

struct GPUViewportData {
    GPUTexture* render_target;
};

struct GPUPipeline {};

struct GPURenderer {
    using DrawFn = void (*)(void* private_data, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx,
                            uint32_t first_instance);
    using DrawIndexedFn = void (*)(void* private_data, uint32_t idx_count, uint32_t instance_count, uint32_t first_idx,
                                   int32_t vtx_offset, uint32_t first_instance);

    union StateUpdateFlags {
        uint32_t u32;
        struct {
            uint32_t storage_buf : 4;
            uint32_t vtx_buf : 1;
            uint32_t idx_buf : 1;
            uint32_t pipeline : 1;
            uint32_t scissor : 1;
            uint32_t vp : 1;
        };

        inline bool state_dirty() { return u32 != 0; }
    };

    uint32_t current_frame_id = 0;

    void* cmd_private_data {};
    DrawFn draw_fn;
    DrawIndexedFn draw_indexed_fn;

    GPUPipeline* pipeline;
    void* current_vtx_buf;
    void* current_idx_buf;
    void* current_storage_buf[4];
    int32_t sc_x, sc_y, sc_w, sc_h;
    float vp_x, vp_y, vp_w, vp_h;
    StateUpdateFlags dirty_flags {};

    virtual bool init(SDL_Window* window) = 0;
    virtual void shutdown() = 0;
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;

    virtual GPUBuffer* create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, size_t init_size,
                                     void* init_data) = 0;
    virtual GPUTexture* create_texture(GPUTextureUsageFlags usage, uint32_t w, uint32_t h, size_t init_size) = 0;
    virtual GPUPipeline* create_pipeline(const GPUPipelineDesc& desc) = 0;
    virtual void destroy_buffer(GPUBuffer* buffer) = 0;
    virtual void destroy_texture(GPUTexture* buffer) = 0;
    virtual void destroy_pipeline(GPUPipeline* buffer) = 0;
    virtual void add_viewport(ImGuiViewport* viewport) = 0;
    virtual void remove_viewport(ImGuiViewport* viewport) = 0;

    virtual void* map_buffer(GPUBuffer* buffer) = 0;
    virtual void unmap_buffer(GPUBuffer* buffer) = 0;

    virtual void begin_render(GPUTexture* render_target, const ImVec4& clear_color) = 0;
    virtual void end_render() = 0;
    virtual void set_pipeline(GPUPipeline* pipeline) = 0;
    virtual void set_shader_parameter(size_t size, const void* data) = 0;
    virtual void flush_state() = 0;

    void bind_storage_buffer(uint32_t index, GPUBuffer* buf) {
        assert(index < 4 && "Index out of range");
        if (buf->impl[current_frame_id] != current_storage_buf[index]) {
            current_storage_buf[index] = buf->impl[current_frame_id];
            dirty_flags.vtx_buf |= 1 << index;
        }
    }

    void bind_vertex_buffer(GPUBuffer* vtx_buf) {
        if (vtx_buf->impl[current_frame_id] != vtx_buf) {
            current_vtx_buf = vtx_buf->impl[current_frame_id];
            dirty_flags.vtx_buf = 1;
        }
    }

    void bind_index_buffer(GPUBuffer* idx_buf) {
        if (idx_buf->impl[current_frame_id] != idx_buf) {
            current_idx_buf = idx_buf->impl[current_frame_id];
            dirty_flags.idx_buf = 1;
        }
    }

    void set_scissor(int32_t x, int32_t y, int32_t width, int32_t height) {
        sc_x = x;
        sc_y = x;
        sc_w = width;
        sc_h = height;
        dirty_flags.scissor = 1;
    }

    void set_viewport(float x, float y, float width, float height) {
        vp_x = x;
        vp_y = x;
        vp_w = width;
        vp_h = height;
        dirty_flags.vp = 1;
    }

    inline void draw(uint32_t vtx_count, int32_t first_vtx) {
        if (!dirty_flags.state_dirty())
            flush_state();
        draw_fn(cmd_private_data, vtx_count, 1, first_vtx, 0);
    }

    inline void draw_indexed(uint32_t idx_count, uint32_t first_idx, int32_t vtx_offset) {
        if (!dirty_flags.state_dirty())
            flush_state();
        draw_indexed_fn(cmd_private_data, idx_count, 1, first_idx, vtx_offset, 0);
    }
};

extern GPURenderer* g_renderer2;
void init_renderer2(SDL_Window* window);
void shutdown_renderer2();

} // namespace wb