#ifndef SHADOWS_GLSL
#define SHADOWS_GLSL

// Simple, single shadow map data
layout(set=2, binding=0) uniform ShadowData {
    mat4 lightViewProj;   // light view-projection
    // params: x=mapSize, y=bias, z=visualizeFlag, w=unused
    vec4 params;
} shadow;

layout(set=2, binding=1) uniform sampler2DShadow shadowMap;

float sampleShadowPCF(vec3 worldPos, float NdotL)
{
    float mapSize = max(shadow.params.x, 1.0);
    float bias = shadow.params.y * clamp(1.0 - NdotL, 0.0, 1.0);

    vec4 sc = shadow.lightViewProj * vec4(worldPos, 1.0);
    vec3 uvz = sc.xyz / max(sc.w, 1e-5);
    vec2 uv = uvz.xy * 0.5 + 0.5;
    float depth = uvz.z - bias;

    float texel = 1.0 / mapSize;

    // 3x3 PCF
    float result = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 o = vec2(x, y) * texel;
            result += texture(shadowMap, vec3(uv + o, depth));
        }
    }
    return result / 9.0;
}

#endif // SHADOWS_GLSL

