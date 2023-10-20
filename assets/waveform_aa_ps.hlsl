struct PSInput
{
    float4 pos : SV_Position;
    float2 uv_pos : TEXCOORD1;
    half4 color : TEXCOORD0;
};

half4 main(PSInput input) : SV_Target0
{
    input.color.a = 1.0 - clamp(abs(input.uv_pos.y), 0.0, 1.0);
    return input.color;
}