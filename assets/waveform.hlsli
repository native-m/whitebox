struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    half4 color : TEXCOORD1;
};

cbuffer Parameters : register(b0)
{
    float2 origin;
    float scale_x;
    float scale_y;
    float4 color;
    float vp_width;
    float vp_height;
    uint chunk_size;
    uint max_sample_idx;
};
