#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "waveform.glsli"

layout(location = 0) in float coverage;
layout(location = 0) out vec4 color;

void main() {
    color = draw_cmd.color;
    color.a *= coverage;
}
