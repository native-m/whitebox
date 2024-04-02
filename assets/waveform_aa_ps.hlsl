#include "waveform.hlsli"

float4 main(PSInput input) : SV_Target0
{
    float4 c = color;
    c.a *= input.width;
    return c;
}