#version 450 core

layout(location = 0) in vec2 tangent;
layout(location = 1) in vec2 bound;
layout(location = 2) in float dir;

layout(set = 1, binding = 0, r32i) writeonly image2D winding_img;

float signed_area(vec2 p, vec2 v0, vec2 v1) {
    float s = -1.0;
    if (v1.x < v0.x) {
        vec2 tmp = v0;
        v0 = v1;
        v1 = tmp;
        s = 1.0;
    }
    vec2 n = normalize(v1 - v0);
    vec2 v = p - v0;
    float d = n.y*v.x - n.x*v.y;
    float sign_x = sign(n.x);
    float bx0 = (p.x - v0.x) * sign_x;
    float bx1 = (v1.x - p.x) * sign_x;
    d = min(min(bx0, bx1), d) + 0.5;
    return clamp(d, 0.0, 1.0) * s;
}

void main() {

}