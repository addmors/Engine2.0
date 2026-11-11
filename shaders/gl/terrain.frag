#version 460 core

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D uBlendMap;   // rgb = rock/grass/... weights
layout(binding = 1) uniform sampler2D uBase;       // background layer
layout(binding = 2) uniform sampler2D uLayerR;
layout(binding = 3) uniform sampler2D uLayerG;
layout(binding = 4) uniform sampler2D uLayerB;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#include "inc/lighting.frag"

const float TILING = 48.0;

void main()
{
    const vec3 blend = texture(uBlendMap, vUV).rgb;
    const float baseWeight = clamp(1.0 - (blend.r + blend.g + blend.b), 0.0, 1.0);

    const vec2 tiled = vUV * TILING;
    const vec3 base = texture(uBase, vUV).rgb * baseWeight;
    const vec3 r = texture(uLayerR, tiled).rgb * blend.r;
    const vec3 g = texture(uLayerG, tiled).rgb * blend.g;
    const vec3 b = texture(uLayerB, tiled).rgb * blend.b;

    const vec3 albedo = base + r + g + b;
    fragColor = vec4(shade(albedo, normalize(vNormal), 0.02, vWorldPos, 16.0), 1.0);
}
