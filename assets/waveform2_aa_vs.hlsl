#include "waveform.hlsli"

Buffer<float> vertex_input : register(t0);

float2 get_minmax_value_hq(uint pos)
{
    float sample_pos = float(pos) * scale_x;
    float frac_sample_pos = frac(sample_pos);
    float scan_len = scale_x + frac_sample_pos;
    float frac_scan_len = frac(scan_len);
    float min_val = 1.0f;
    float max_val = -1.0f;
    
    //float s0 = vertex_input.Load(uint(sample_pos));
    //float s1 = vertex_input.Load(uint(sample_pos) + 1);
    //float s = lerp(s0, s1, frac_sample_pos);
    //min_val = min(min_val, s);
    //max_val = max(max_val, s);
    float s = 0.0;
    
    uint i = 0;
    for (; i < uint(scan_len); i++)
    {
        s = vertex_input.Load(uint(sample_pos) + i);
        min_val = min(min_val, s);
        max_val = max(max_val, s);
    }
    
    //i++;
    if (frac_scan_len > 0.0)
    {
        float s1 = vertex_input.Load(uint(sample_pos) + i);
        s = lerp(s, s1, frac_scan_len);
        min_val = min(min_val, s);
        max_val = max(max_val, s);
    }
    
    return float2(min_val, max_val);
}

float2 get_minmax_value(uint pos)
{
    float sample_pos = float(pos) * scale_x;
    uint scan_len = uint(ceil(scale_x + frac(sample_pos)));
    float min_val = 1.0f;
    float max_val = -1.0f;
    
    for (uint i = 0; i < scan_len; i++)
    {
        float s = vertex_input.Load(uint(sample_pos) + i);
        min_val = min(min_val, s);
        max_val = max(max_val, s);
    }
    
    return float2(min_val, max_val);
}

PSInput main(uint vertex_id : SV_VertexID)
{
    uint peak_pos = vertex_id / 6;
    uint sample_pos = peak_pos + start_idx;
    uint vertex_idx = vertex_id % 6;
    
    float2 minmax0 = get_minmax_value(sample_pos);
    float2 minmax1 = get_minmax_value(sample_pos + 1);
    
    float y0 = is_min ? minmax0.x : minmax0.y;
    float y1 = is_min ? minmax1.x : minmax1.y;
    
    float height_y = scale_y * 0.5;
    float2 offset = float2(origin.x, origin.y + height_y);
    float2 pos0 = float2(float(peak_pos), -y0 * height_y) + offset;
    float2 pos1 = float2((float(peak_pos) + 1), -y1 * height_y) + offset;
    
    float2 t0 = normalize(pos1 - pos0);
    float2 n0 = float2(t0.y, -t0.x);
    
    float sign = is_min ? -1 : 1;
    uint side = vertex_idx & 1;
    uint dir = (vertex_idx >> (vertex_idx < 3 ? 1 : 2)) & 1;
    float up = sign * float(dir);
    float2 offset_t = (float(side) * 0.0625) * t0;
    float2 pos = (side == 0 ? pos0 : pos1) + up * n0 + offset_t;
    
    PSInput ps_input = (PSInput) 0;
    ps_input.pos.x = pos.x * vp_width - 1.0f;
    ps_input.pos.y = 1.0 - pos.y * vp_height;
    ps_input.pos.zw = float2(0.5f, 1.0f);
    ps_input.width = float(1 - dir); // * 2.0 - 1.0;
    //ps_input.length = float(side) * len - len * 0.5;
    //ps_input.half_length = len * 0.5;
    //ps_input.pos_x = float4((n0 + n1) * 0.5, 0.0, 0.0);
    
    return ps_input;
}