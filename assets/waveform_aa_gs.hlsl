struct GSInput
{
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};

struct GSOutput
{
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv_pos : TEXCOORD1;
};

cbuffer Parameters : register(b0)
{
    float origin_x;
    float origin_y;
    float scale_x;
    float scale_y;
    float4 color;
    float vp_width;
    float vp_height;
};

[maxvertexcount(4)]
void main(line GSInput input[2], inout TriangleStream<GSOutput> output)
{
    float2 a = input[0].pos.xy;
    float2 b = input[1].pos.xy;
    float2 t = normalize(b - a);
    float2 o = 1.0f * float2(-t.y, t.x);
    
    GSOutput gs_out;
    gs_out.pos.z = 0.0;
    gs_out.pos.w = 1.0;
    gs_out.color = color;
    
    float2 pos0 = float2(a - o);
    gs_out.pos.x = pos0.x * vp_width - 1.0;
    gs_out.pos.y = 1.0 - pos0.y * vp_height;
    gs_out.uv_pos = float2(-1.0, -1.0);
    output.Append(gs_out);
    
    float2 pos1 = float2(b - o);
    gs_out.pos.x = pos1.x * vp_width - 1.0;
    gs_out.pos.y = 1.0 - pos1.y * vp_height;
    gs_out.uv_pos = float2(1.0, -1.0);
    output.Append(gs_out);
    
    float2 pos2 = float2(a + o);
    gs_out.pos.x = pos2.x * vp_width - 1.0;
    gs_out.pos.y = 1.0 - pos2.y * vp_height;
    gs_out.uv_pos = float2(-1.0, 1.0);
    output.Append(gs_out);
    
    float2 pos3 = float2(b + o);
    gs_out.pos.x = pos3.x * vp_width - 1.0;
    gs_out.pos.y = 1.0 - pos3.y * vp_height;
    gs_out.uv_pos = float2(1.0, 1.0);
    output.Append(gs_out);
    
    output.RestartStrip();

}