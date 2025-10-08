#version 450
#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

layout(set=1, binding=0) uniform sampler2D posTex;
layout(set=1, binding=1) uniform sampler2D normalTex;
layout(set=1, binding=2) uniform sampler2D albedoTex;
layout(set=2, binding=0) uniform sampler2D shadowTex;

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float ggx2 = GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
    float ggx1 = GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    return ggx1 * ggx2;
}

void main(){
    vec4 posSample = texture(posTex, inUV);
    if (posSample.w == 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 pos = posSample.xyz;
    vec4 normalSample = texture(normalTex, inUV);
    vec3 N = normalize(normalSample.xyz);
    float roughness = clamp(normalSample.w, 0.04, 1.0);

    vec4 albedoSample = texture(albedoTex, inUV);
    vec3 albedo = albedoSample.rgb;
    float metallic = clamp(albedoSample.a, 0.0, 1.0);

    vec3 camPos = vec3(inverse(sceneData.view)[3]);
    vec3 V = normalize(camPos - pos);
    vec3 L = normalize(-sceneData.sunlightDirection.xyz);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);

    vec3 numerator    = NDF * G * F;
    float denom       = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular     = numerator / max(denom, 0.001);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    // Shadowing (directional, reversed-Z shadow map)
    float visibility = 1.0;
    {
        vec4 lclip = sceneData.lightViewProj * vec4(pos, 1.0);
        vec3 ndc  = lclip.xyz / lclip.w;           // x,y in [-1,1], z in [0,1] (ZO)
        vec2 suv  = ndc.xy * 0.5 + 0.5;

        // Basic bounds check
        if (all(greaterThanEqual(suv, vec2(0.0))) && all(lessThanEqual(suv, vec2(1.0))))
        {
            float current = clamp(ndc.z, 0.0, 1.0);
            float NoL = max(dot(N, L), 0.0);
            float bias = max(0.0005 * (1.0 - NoL), 0.0001);

            // 2x2 PCF (manual, non-compare sampler); tuned for 2048 size
            const float texel = 1.0 / 2048.0;
            float sum = 0.0;
            for (int dy = 0; dy <= 1; ++dy)
            for (int dx = 0; dx <= 1; ++dx)
            {
                vec2 base = (vec2(dx, dy) - vec2(0.5)) * texel;
                float mapD = texture(shadowTex, suv + base).r;

                sum += (current + bias >= mapD) ? 1.0 : 0.0;
            }
            visibility = sum * 0.25;
        }
        else
        {
            visibility = 1.0; // outside light frustum -> lit
        }
    }

    vec3 irradiance = sceneData.sunlightColor.rgb * sceneData.sunlightColor.a * NdotL * visibility;

    vec3 color = (kD * albedo / PI + specular) * irradiance;
    color += albedo * sceneData.ambientColor.rgb;

    outColor = vec4(color, 1.0);
}
