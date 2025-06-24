#version 450
#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

layout(set=1, binding=0) uniform sampler2D posTex;
layout(set=1, binding=1) uniform sampler2D normalTex;
layout(set=1, binding=2) uniform sampler2D albedoTex;

void main(){
    vec4 posSample = texture(posTex, inUV);
    if (posSample.w == 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 pos = posSample.xyz;
    vec3 normal = normalize(texture(normalTex, inUV).xyz);
    vec3 albedo = texture(albedoTex, inUV).rgb;
    vec3 L = normalize(-sceneData.sunlightDirection.xyz);
    float NdotL = max(dot(normal, L), 0.0);
    vec3 color = albedo * sceneData.ambientColor.rgb;
    color += albedo * sceneData.sunlightColor.rgb * sceneData.sunlightColor.a * NdotL;
    outColor = vec4(color, 1.0);
}