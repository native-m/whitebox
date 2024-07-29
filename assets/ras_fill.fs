#version 450 core

layout(push_constant) uniform DrawCmd {
    vec2 inv_viewport;
    vec2 min_bb;
    vec2 max_bb;
    uint color;
    uint offset;
};

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 1, r32i) restrict coherent uniform iimage2D winding_img;

void main() {
    vec2 frag_coord = gl_FragCoord.xy;
    vec4 fcolor = unpackUnorm4x8(color);
    float winding = float(imageAtomicExchange(winding_img, ivec2(frag_coord), 0)) / 65536.0;
    float alpha = abs(winding);
    out_color = vec4(fcolor.xyz, fcolor.a * alpha);
}