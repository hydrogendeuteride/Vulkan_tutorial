#version 450
#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec4 outNorm;
layout(location = 2) out vec4 outAlbedo;

void main() {
    vec3 albedo = inColor * texture(colorTex, inUV).rgb * materialData.colorFactors.rgb;
    outPos = vec4(inWorldPos, 1.0);
    outNorm = vec4(normalize(inNormal), 0.0);
    outAlbedo = vec4(albedo, 1.0);
}