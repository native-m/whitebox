#version 450 core

layout(location = 0) in vec4 v2point;
layout(location = 1) in vec2 tangent;
layout(location = 2) in float dir;

layout(set = 1, binding = 0, r32i) restrict uniform iimage2D winding_img;

float signed_area(vec2 p, vec2 t) {
    vec2 v0 = v2point.xy;
    vec2 v1 = v2point.zw;
    vec2 v = p - v0;
    float d = t.y*v.x - t.x*v.y;
    float sign_x = sign(t.x);
    float bx0 = (p.x - v0.x) * sign_x;
    float bx1 = (v1.y - p.x) * sign_x;
    d = min(min(bx0, bx1), d) + 0.5;
    return clamp(d, 0.0, 1.0) * dir;
}

void main() {
    vec2 frag_coord = gl_FragCoord.xy;
    int area = int(signed_area(frag_coord, tangent) * 65536.0);
    imageAtomicAdd(winding_img, ivec2(frag_coord), area);
}