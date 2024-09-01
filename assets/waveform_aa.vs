#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "waveform.glsli"

layout(location = 0) out float coverage;

layout(set = 0, binding = 0) readonly buffer WaveformBuffer {
    uint minmax[];
} vertex_input;

float lookup_value(uint pos) {
    vec2 values = unpackSnorm2x16(vertex_input.minmax[pos / 2]);
    return (pos % 2) == 0 ? values.x : values.y;
}

vec2 get_minmax_value(uint pos) {
    float scale_x = draw_cmd.scale_x;
    float sample_pos = float(pos) * scale_x;
    uint scan_len = uint(ceil(scale_x + fract(sample_pos)));
    float min_val = 1.0;
    float max_val = -1.0;

    for (uint i = 0; i < scan_len; i++) {
        float s = lookup_value(uint(sample_pos) + i);
        min_val = min(min_val, s);
        max_val = max(max_val, s);
    }
    
    return vec2(min_val, max_val);
}

void main() {
    uint vertex_id =  gl_VertexIndex;
    uint peak_pos = vertex_id / 6;
    uint sample_pos = peak_pos + draw_cmd.start_idx;
    uint vertex_idx = vertex_id % 6;
    
    vec2 minmax0 = get_minmax_value(sample_pos);
    vec2 minmax1 = get_minmax_value(sample_pos + 1);
    
    float y0 = draw_cmd.is_min == 1 ? minmax0.x : minmax0.y;
    float y1 = draw_cmd.is_min == 1 ? minmax1.x : minmax1.y;
    
    float height_y = draw_cmd.scale_y * 0.5;
    vec2 offset = vec2(draw_cmd.origin.x, draw_cmd.origin.y + height_y);
    vec2 pos0 = vec2(float(peak_pos), -y0 * height_y) + offset;
    vec2 pos1 = vec2((float(peak_pos) + 1), -y1 * height_y) + offset;
    
    vec2 t0 = normalize(pos1 - pos0);
    vec2 n0 = vec2(t0.y, -t0.x);
    
    float sign = draw_cmd.is_min == 1 ? -1 : 1;
    uint side = vertex_idx & 1;
    uint dir = (vertex_idx >> (vertex_idx < 3 ? 1 : 2)) & 1;
    float up = sign * float(dir);
    vec2 offset_t = (float(side) * 0.0625) * t0;
    vec2 pos = (side == 0 ? pos0 : pos1) + up * n0 + offset_t;
    
    gl_Position.x = pos.x * draw_cmd.vp_width - 1.0f;
    gl_Position.y = pos.y * draw_cmd.vp_height - 1.0f;
    gl_Position.zw = vec2(0.5f, 1.0f);
    coverage = float(1 - dir);
    //ps_input.length = float(side) * len - len * 0.5;
    //ps_input.half_length = len * 0.5;
    //ps_input.pos_x = float4((n0 + n1) * 0.5, 0.0, 0.0);
}