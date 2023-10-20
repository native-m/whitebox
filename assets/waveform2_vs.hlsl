struct VSOutput
{
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};

cbuffer Parameters : register(b0)
{
    float origin_x;
    float origin_y;
    float scale_x;
    float scale_y;
    float4 color;
    uint chunk_size;
    float vp_width;
    float vp_height;
};

Buffer<float> vertex_input : register(t0);

VSOutput main(uint vertex : SV_VertexID)
{
    float amplitude = vertex_input.Load(vertex);
    float x = origin_x + float(vertex * chunk_size) * scale_x;
    float y = origin_y + (amplitude * scale_y * 0.5) + scale_y * 0.5;
    
    VSOutput output;
    output.pos.x = x * vp_width - 1.0;
    output.pos.y = 1.0 - y * vp_height;
    output.pos.z = 0.0;
    output.pos.w = 1.0;
    output.color = color;
    
    return output;
}