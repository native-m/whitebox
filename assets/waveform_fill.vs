#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "waveform.glsli"

layout(location = 0) out float coverage;

void main() {
    uint vertex_id =  gl_VertexIndex;
    uint peak_pos = vertex_id / 2;
    uint sample_pos = peak_pos + draw_cmd.start_idx;
    uint vertex_idx = vertex_id % 2;
    vec2 minmax = get_minmax_value(sample_pos);
    
    //uint dir = (vertex_idx >> (vertex_idx < 3 ? 1 : 2)) & 1;
    float max_height_y = draw_cmd.scale_y * 0.5;
    float height = max_height_y * draw_cmd.gain;
    float y = vertex_idx == 1 ? minmax.y : minmax.x;
    
    vec2 pos;
    pos.x = draw_cmd.origin.x + float(peak_pos) * draw_cmd.gap_size;
    pos.y = draw_cmd.origin.y + max_height_y + -y * height;
    
    gl_Position.x = pos.x * draw_cmd.vp_width - 1.0f;
    gl_Position.y = pos.y * draw_cmd.vp_height - 1.0f;
    gl_Position.zw = vec2(0.5f, 1.0f);
    coverage = 1.0f;
}