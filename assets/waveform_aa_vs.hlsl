#include "waveform.hlsli"

Buffer<float> vertex_input : register(t0);

VSOutput main(uint vertex : SV_VertexID)
{
    uint sample_idx = vertex / 6 + start_sample_idx;
    uint vertex_idx = vertex % 6;
    uint a_idx = clamp(sample_idx, 0, end_sample_idx);
    uint b_idx = clamp(sample_idx + 1, 0, end_sample_idx);
    float a_val = vertex_input.Load(a_idx);
    float b_val = vertex_input.Load(b_idx);
    float offset_y = scale_y * 0.5;
    
    // Convert sample points to line segment
    float2 a;
    a.x = float(a_idx * chunk_size) * scale_x;
    a.y = a_val * offset_y + offset_y;
    
    float2 b;
    b.x = float(b_idx * chunk_size) * scale_x;
    b.y = b_val * offset_y + offset_y;
    
    uint side = vertex_idx & 1;
    uint dir = (vertex_idx >> (vertex_idx < 3 ? 1 : 2)) & 1;
    float2 t = b - a;
    float2 o = normalize(float2(-t.y, t.x));
    float2 uv_pos = (float2(side, dir) * 2.0 - 1.0);
    float2 vtx_pos = (side == 0 ? a : b) + uv_pos.y * o + origin;
    
    VSOutput output;
    output.pos.x = vtx_pos.x * vp_width - 1.0;
    output.pos.y = 1.0 - vtx_pos.y * vp_height;
    output.pos.z = 0.0;
    output.pos.w = 1.0;
    output.uv = uv_pos;
    output.color = color;
    
    return output;
}