#include "waveform.hlsli"

// Even index: maximum value
// Odd index: minimum value
Buffer<float> vertex_input : register(t0);

float4 get_minmax_value_hq(uint pos)
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
    
    return float4(min_val, max_val, frac_sample_pos, frac_scan_len);
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
    uint peak_pos = vertex_id / 2;
    uint sample_pos = peak_pos + start_idx;
    uint vertex_idx = vertex_id % 2;
    float2 minmax = get_minmax_value(sample_pos);
    
    //uint dir = (vertex_idx >> (vertex_idx < 3 ? 1 : 2)) & 1;
    float offset_y = scale_y * 0.5;
    float y = vertex_idx ? minmax.y : minmax.x;
    
    float2 pos;
    pos.x = origin.x + float(peak_pos);
    pos.y = origin.y + offset_y + -y * offset_y;
    
    PSInput ps_input = (PSInput) 0;
    ps_input.pos.x = pos.x * vp_width - 1.0f;
    ps_input.pos.y = 1.0 - pos.y * vp_height;
    ps_input.pos.zw = float2(0.5f, 1.0f);
    ps_input.width = 1.0f;
    
    return ps_input;
}