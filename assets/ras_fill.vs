#version 450 core

layout(push_constant) uniform DrawCmd {
    vec2 inv_viewport;
    vec2 min_bb;
    vec2 max_bb;
    uint color;
    uint offset;
};

void main() {
    float x = (gl_VertexIndex & 1) == 1 ? max_bb.x + 0.55 : min_bb.x - 0.55;
    float y = (gl_VertexIndex & 2) == 2 ? max_bb.y + 0.55 : min_bb.y - 0.55;
    gl_Position = vec4(vec2(x, y) * inv_viewport - 1.0, 0.0, 1.0);
}
