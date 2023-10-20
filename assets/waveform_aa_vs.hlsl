struct VSOutput
{
    float4 pos : SV_Position;
    half4 color : TEXCOORD0;
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
    VSOutput output;
    output.pos.x = origin_x + float(vertex * chunk_size) * scale_x;
    output.pos.y = origin_y + (amplitude * scale_y * 0.5) + scale_y * 0.5;
    output.pos.z = 0.0;
    output.pos.w = 1.0;
    output.color = color;
    return output;
}