#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "waveform.glsli"

layout(set = 0, binding = 0) readonly buffer WaveformBuffer {
    uint minmax;
} vertex_input;

void main() {
    
}