#version 450 core

layout(set = 0, binding = 0) readonly buffer EdgeBuffer {
    vec2 points[];
} vertex_input;

layout(push_constant) uniform DrawCmd {
    vec2 inv_viewport;
    vec2 min_bb;
    vec2 max_bb;
    uint color;
    uint offset;
};

layout(location = 0) out vec4 v2point;
layout(location = 1) out vec2 tangent;
layout(location = 2) out float dir;

void main() {
    uint instance = gl_VertexIndex / 6 + offset;
    uint index = gl_VertexIndex % 6;
    vec2 v0 = vertex_input.points[instance];
    vec2 v1 = vertex_input.points[instance + 1];
    
    if (v1.x < v0.x) {
        float tmp_v0_x = v0.x;
        v0.x = v1.x;
        v1.x = tmp_v0_x;
        dir = -1.0;
    } else {
        dir = 1.0;
    }

    float x = (index & 1) == 1 ? v1.x + 0.5 : v0.x - 0.5;
    float y = ((index >> (index / 3 + 1)) & 1) == 1 ? max(v1.y, v0.y) + 0.5 : min_bb.y - 0.5;

    tangent = normalize(v1 - v0);
    v2point = vec4(v0, v1);

    gl_Position.x = x * inv_viewport.x - 1.0f;
    gl_Position.y = y * inv_viewport.y - 1.0f;
    gl_Position.z = 0.0;
    gl_Position.w = 1.0;
}