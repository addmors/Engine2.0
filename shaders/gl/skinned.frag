#version 460 core

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D uAlbedo;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uSpecular;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vTangent;

#include "inc/lighting.frag"

void main()
{
    vec3 normal = normalize(vNormal);
    if (vUV.x >= 0.0 && textureSize(uNormal, 0).x > 1) {
        normal = mappedNormal(uNormal, normal, vTangent, vUV);
    }

    const vec3 albedo = texture(uAlbedo, vUV).rgb;
    const float specularStrength = texture(uSpecular, vUV).r;

    fragColor = vec4(shade(albedo, normal, specularStrength, vWorldPos, 24.0), 1.0);
}
