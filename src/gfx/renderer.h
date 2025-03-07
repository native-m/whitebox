#pragma once

#include "core/common.h"
#include "core/list.h"
#include <SDL_video.h>
#include <imgui.h>

#define WB_GPU_RENDER_BUFFER_SIZE 2

namespace wb {

enum class GPUFormat {
    UnormR8G8B8A8,
    UnormB8G8R8A8,
    FloatR32G32,
    FloatR32G32B32,
};

enum class GPUResourceType {
    Texture,
    StorageBuffer,
    ReadOnlyStorageBuffer,
};

enum class GPUPrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
};

struct GPUBufferUsage {
    enum {
        Vertex = 1 << 0,        // The buffer will be used as vertex buffer
        Index = 1 << 1,         // The buffer will be used as index buffer
        Storage = 1 << 2,       // The buffer will be used as storage buffer
        Writeable = 1 << 3,     // The buffer will be written across frames
        CPUAccessible = 1 << 4, // The buffer is accessible by CPU
    };
};
using GPUBufferUsageFlags = uint32_t;

struct GPUTextureUsage {
    enum {
        RenderTarget = 1 << 0, // The texture will be used as render target
        Sampled = 1 << 1,      // The texture can be sampled in the shader
    };
};
using GPUTextureUsageFlags = uint32_t;

struct GPUVertexAttribute {
    const char* semantic_name;
    uint32_t slot;
    GPUFormat format;
    uint32_t offset;
};

struct GPUShaderResourceDesc {
    uint32_t binding;
    GPUResourceType type;
};

struct GPUPipelineDesc {
    const void* vs;
    uint32_t vs_size;
    const void* fs;
    uint32_t fs_size;
    uint32_t shader_parameter_size;
    uint32_t vertex_stride;
    uint32_t num_vertex_attributes;
    const GPUVertexAttribute* vertex_attributes;
    GPUPrimitiveTopology primitive_topology;
    bool enable_blending;
    bool enable_color_write;
};

struct GPUResource : public InplaceList<GPUResource> {
    // Some impls may require this to determine which internal resources to work on.
    uint32_t active_id = 0;
    uint32_t read_id = 0;
    uint32_t num_resources = 0;
};

struct GPUBuffer : public GPUResource {
    GPUBufferUsageFlags usage;
    size_t size;
};

struct GPUTexture : public GPUResource {
    GPUTextureUsageFlags usage;
    GPUFormat format;
    uint32_t width;
    uint32_t height;
};

struct GPUViewportData {
    GPUTexture* render_target;
};

struct GPUPipeline {
    uint32_t shader_parameter_size;
};

struct GPURenderer {
    using DrawFn = void (*)(void* private_data, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx,
                            uint32_t first_instance);
    using DrawIndexedFn = void (*)(void* private_data, uint32_t idx_count, uint32_t instance_count, uint32_t first_idx,
                                   int32_t vtx_offset, uint32_t first_instance);

    union StateUpdateFlags {
        uint32_t u32;
        struct {
            uint32_t texture : 4;
            uint32_t storage_buf : 4;
            uint32_t vtx_buf : 1;
            uint32_t idx_buf : 1;
            uint32_t pipeline : 1;
            uint32_t scissor : 1;
            uint32_t vp : 1;
        };

        inline bool state_dirty() { return u32 != 0; }
    };

    GPUViewportData* main_vp;
    uint32_t frame_id = 0;

    void* cmd_private_data {};
    DrawFn draw_fn;
    DrawIndexedFn draw_indexed_fn;

    GPUPipeline* current_pipeline;
    GPUBuffer* current_vtx_buf;
    GPUBuffer* current_idx_buf;
    GPUBuffer* current_storage_buf[4];
    GPUTexture* current_texture[4];
    int32_t sc_x, sc_y, sc_w, sc_h;
    float vp_x, vp_y, vp_w, vp_h;
    uint32_t fb_w, fb_h;
    StateUpdateFlags dirty_flags {};
    bool inside_render_pass = false;

    GPUPipeline* imgui_pipeline {};
    GPUPipeline* waveform_aa {};
    GPUPipeline* waveform_fill {};

    GPUTexture* font_texture {};
    GPUBuffer* imm_vtx_buf {};
    GPUBuffer* imm_idx_buf {};
    uint32_t immediate_vtx_offset = 0;
    uint32_t immediate_idx_offset = 0;
    uint32_t total_vtx_count = 0;
    uint32_t total_idx_count = 0;

    virtual ~GPURenderer() {}
    virtual bool init(SDL_Window* window);
    virtual void shutdown();

    virtual GPUBuffer* create_buffer(GPUBufferUsageFlags usage, size_t buffer_size, bool dedicated_allocation = false,
                                     size_t init_size = 0, const void* init_data = nullptr) = 0;
    virtual GPUTexture* create_texture(GPUTextureUsageFlags usage, GPUFormat format, uint32_t w, uint32_t h,
                                       bool dedicated_allocation = false, uint32_t init_w = 0, uint32_t init_h = 0,
                                       const void* init_data = nullptr) = 0;
    virtual GPUPipeline* create_pipeline(const GPUPipelineDesc& desc) = 0;
    virtual void destroy_buffer(GPUBuffer* buffer) = 0;
    virtual void destroy_texture(GPUTexture* buffer) = 0;
    virtual void destroy_pipeline(GPUPipeline* buffer) = 0;
    virtual void add_viewport(ImGuiViewport* viewport) = 0;
    virtual void remove_viewport(ImGuiViewport* viewport) = 0;
    virtual void resize_viewport(ImGuiViewport* viewport, ImVec2 vec) = 0;

    virtual void begin_frame();
    virtual void end_frame() = 0;
    virtual void present() = 0;

    virtual void* map_buffer(GPUBuffer* buffer) = 0;
    virtual void unmap_buffer(GPUBuffer* buffer) = 0;
    virtual void* begin_upload_data(GPUBuffer* buffer, size_t upload_size) = 0;
    virtual void end_upload_data() = 0;

    virtual void begin_render(GPUTexture* render_target, const ImVec4& clear_color) = 0;
    virtual void end_render() = 0;
    virtual void set_shader_parameter(size_t size, const void* data) = 0;
    virtual void flush_state() = 0;

    void bind_pipeline(GPUPipeline* pipeline) {
        if (pipeline != current_pipeline) {
            current_pipeline = pipeline;
            dirty_flags.pipeline = 1;
        }
    }

    void bind_texture(uint32_t index, GPUTexture* tex) {
        assert(index < 4 && "Index out of range");
        if (tex != current_texture[index]) {
            current_texture[index] = tex;
            dirty_flags.texture |= 1 << index;
        }
    }

    void bind_storage_buffer(uint32_t index, GPUBuffer* buf) {
        assert(index < 4 && "Index out of range");
        if (buf != current_storage_buf[index]) {
            current_storage_buf[index] = buf;
            dirty_flags.storage_buf |= 1 << index;
        }
    }

    void bind_vertex_buffer(GPUBuffer* vtx_buf) {
        if (vtx_buf != current_vtx_buf) {
            current_vtx_buf = vtx_buf;
            dirty_flags.vtx_buf = 1;
        }
    }

    void bind_index_buffer(GPUBuffer* idx_buf) {
        if (idx_buf != current_idx_buf) {
            current_idx_buf = idx_buf;
            dirty_flags.idx_buf = 1;
        }
    }

    void set_scissor(int32_t x, int32_t y, int32_t width, int32_t height) {
        sc_x = x;
        sc_y = y;
        sc_w = width;
        sc_h = height;
        dirty_flags.scissor = 1;
    }

    void set_viewport(float x, float y, float width, float height) {
        vp_x = x;
        vp_y = y;
        vp_w = width;
        vp_h = height;
        dirty_flags.vp = 1;
    }

    inline void draw(uint32_t vtx_count, int32_t first_vtx) {
        if (dirty_flags.state_dirty())
            flush_state();
        draw_fn(cmd_private_data, vtx_count, 1, first_vtx, 0);
    }

    inline void draw_indexed(uint32_t idx_count, uint32_t first_idx, int32_t vtx_offset) {
        if (dirty_flags.state_dirty())
            flush_state();
        draw_indexed_fn(cmd_private_data, idx_count, 1, first_idx, vtx_offset, 0);
    }

    void render_imgui_draw_data(ImDrawData* draw_data);

  protected:
    void clear_state(); // Must be called in begin_frame() by the implementation!
};

extern GPURenderer* g_renderer;
void init_renderer(SDL_Window* window);
void shutdown_renderer();

} // namespace wb
