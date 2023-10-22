#include "waveform.hlsli"

Buffer<float> vertex_input : register(t0);

VSOutput main(uint vertex : SV_VertexID)
{
    uint sample_idx = vertex / 3;
    uint vertex_idx = vertex % 3;
    uint a_idx = clamp(sample_idx, 0, max_sample_idx);
    uint b_idx = clamp(sample_idx + 1, 0, max_sample_idx);
    uint c_idx = clamp(sample_idx + 2, 0, max_sample_idx);
    float a_val = vertex_input.Load(a_idx);
    float b_val = vertex_input.Load(b_idx);
    float c_val = vertex_input.Load(c_idx);
    float offset_scale_y = scale_y * 0.5;

    float2 a;
    a.x = float(a_idx * chunk_size) * scale_x;
    a.y = a_val * offset_scale_y + offset_scale_y;
    
    float2 b;
    b.x = float(b_idx * chunk_size) * scale_x;
    b.y = b_val * offset_scale_y + offset_scale_y;
    
    float2 c;
    c.x = float(c_idx * chunk_size) * scale_x;
    c.y = c_val * offset_scale_y + offset_scale_y;
    
    uint dir = vertex_idx & 1;
    float is_bevel = float(~(vertex_idx >> 1U) & 1U);
    float2 ba_tangent = normalize(b - a);
    float2 cb_tangent = normalize(c - b);
    float2 ba_normal = float2(-ba_tangent.y, ba_tangent.x);
    float2 cb_normal = float2(-cb_tangent.y, cb_tangent.x);
    float heading = sign(dot(ba_tangent, cb_normal)); // determine heading
    float2 vtx_pos = origin + b + is_bevel * heading * (dir == 0 ? ba_normal : cb_normal);
    
    VSOutput output;
    //output.pos.xy = b + is_bevel * heading * (dir == 0 ? ba_normal : cb_normal);
    output.pos.x = vtx_pos.x * vp_width - 1.0;
    output.pos.y = 1.0 - vtx_pos.y * vp_height;
    output.pos.zw = float2(0.0f, 1.0f);
    output.uv = float2(0.0f, is_bevel);
    output.color = color;
    
    return output;
}