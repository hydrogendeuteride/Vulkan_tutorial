#ifndef SHADOWS_GLSL
#define SHADOWS_GLSL

#define NUM_CASCADES 4

layout(set=2, binding=0) uniform ShadowData {
    mat4 lightViewProj[NUM_CASCADES];
    vec4 cascadeSplits;   // split distances (view-space), ascending
    vec4 params;          // x=cascadeCount, y=mapSize, z=bias, w=unused
} shadow;

layout(set=2, binding=1) uniform sampler2DShadow shadowMaps[NUM_CASCADES];

int selectCascade(float viewZ)
{
    int c = int(shadow.params.x + 0.5);
    // clamp to 1..NUM_CASCADES
    c = clamp(c, 1, NUM_CASCADES);
    // Compare against split boundaries; cascadeSplits.w can store far
    if (c >= 2 && viewZ <= shadow.cascadeSplits.x) return 0;
    if (c >= 3 && viewZ <= shadow.cascadeSplits.y) return 1;
    if (c >= 4 && viewZ <= shadow.cascadeSplits.z) return 2;
    return min(c - 1, NUM_CASCADES - 1);
}

float sampleShadowPCF(int idx, vec3 worldPos, float NdotL)
{
    // Bias can depend on slope; simple normal-based bias term
    float bias = shadow.params.z * clamp(1.0 - NdotL, 0.0, 1.0);
    vec4 sc = shadow.lightViewProj[idx] * vec4(worldPos, 1.0);
    vec3 uvz = sc.xyz / max(sc.w, 1e-5);
    vec2 uv = uvz.xy * 0.5 + 0.5;
    float depth = uvz.z - bias;

    // Outside map => unshadowed (use clamp-to-border white)
    float mapSize = max(shadow.params.y, 1.0);
    float texel = 1.0 / mapSize;

    // 3x3 PCF
    float result = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 o = vec2(x, y) * texel;
            result += texture(shadowMaps[idx], vec3(uv + o, depth));
        }
    }
    result /= 9.0;
    return result;
}

#endif // SHADOWS_GLSL

