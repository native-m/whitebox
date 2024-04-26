#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "waveform.glsli"

layout(location = 0) out float width;

layout(set = 0, binding = 0) readonly buffer WaveformBuffer {
    uint minmax[];
} vertex_input;

vec2 get_minmax_value(uint pos) {
    const float norm_scale = 1.0 / 32767.0;
    float scale_x = draw_cmd.scale_x;
    float sample_pos = float(pos) * scale_x;
    uint scan_len = uint(ceil(scale_x + fract(sample_pos)));
    float min_val = 1.0f;
    float max_val = -1.0f;
    uint scan_len_aligned = scan_len / 2;
    
    uint i = 0;
    for (; i < scan_len_aligned; i++) {
        uint minmax = vertex_input.minmax[i];
        float s0 = float(minmax & 0xFFFF);
        float s1 = float(minmax >> 16);
        min_val = min(min_val, min(s0, s1));
        max_val = max(max_val, max(s0, s1));
    }

    if (scan_len % 2 == 1) {
        uint minmax = vertex_input.minmax[i];
        float s = float(minmax & 0xFFFF);
        min_val = min(min_val, s);
        max_val = max(max_val, s);
    }
    
    return vec2(min_val, max_val) * norm_scale;
}

void main() {
    uint vertex_id =  gl_VertexIndex;
    uint peak_pos = vertex_id / 2;
    uint sample_pos = peak_pos + draw_cmd.start_idx;
    uint vertex_idx = vertex_id % 2;
    vec2 minmax = get_minmax_value(sample_pos);
    
    //uint dir = (vertex_idx >> (vertex_idx < 3 ? 1 : 2)) & 1;
    float offset_y = draw_cmd.scale_y * 0.5;
    float y = vertex_idx == 1 ? minmax.y : minmax.x;
    
    vec2 pos;
    pos.x = draw_cmd.origin.x + float(peak_pos);
    pos.y = draw_cmd.origin.y + offset_y + -y * offset_y;
    
    gl_Position.x = pos.x * draw_cmd.vp_width - 1.0f;
    gl_Position.y = pos.y * draw_cmd.vp_height;
    gl_Position.zw = vec2(0.5f, 1.0f);
    width = 1.0f;
}