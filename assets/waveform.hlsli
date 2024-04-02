#ifndef WAVEFORM_HLSLI_
#define WAVEFORM_HLSLI_

struct PSInput
{
    float4 pos : SV_Position;
    float width : WIDTH;
    float4 pos_x : POS_X;
    //float2 miter : MITER;
    //float2 pos_draw : POS_DRAW;
};

struct PSInput2
{
    float4 pos : SV_Position;
    float2 min_dir0 : MIN_DIR0;
    float2 min_dir1 : MIN_DIR1;
    float2 max_dir0 : MAX_DIR0;
    float2 max_dir1 : MAX_DIR1;
    float2 min_orig0 : MIN_ORIG0;
    float2 min_orig1 : MIN_ORIG1;
    float2 max_orig0 : MAX_ORIG0;
    float2 max_orig1 : MAX_ORIG1;
    //float2 miter : MITER;
    //float2 pos_draw : POS_DRAW;
};

cbuffer Parameters : register(b0)
{
    float2 origin;
    float scale_x;
    float scale_y;
    float4 color;
    float vp_width;
    float vp_height;
    int is_min;
    uint start_idx;
};

#endif