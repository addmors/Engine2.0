#version 460 core

// Procedural sky: gradient + sun glow, one fullscreen triangle.
layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 vNdc;

#include "inc/camera.ubo"
#include "inc/light.ubo"

layout(location = 0) uniform mat4 uInverseViewProjection;

void main()
{
    const vec4 farPoint = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    const vec3 ray = normalize(farPoint.xyz / farPoint.w - camera.position.xyz);

    const vec3 zenith = vec3(0.16, 0.34, 0.68);
    const vec3 horizon = vec3(0.78, 0.87, 0.96);
    const vec3 ground = vec3(0.24, 0.26, 0.30);

    const float up = clamp(ray.y, -1.0, 1.0);
    vec3 sky = up > 0.0 ? mix(horizon, zenith, pow(up, 0.55)) : mix(horizon, ground, pow(-up, 0.4));

    // Sun disc + haze towards the sun.
    const vec3 sunDir = normalize(light.sunDirection.xyz);
    const float sunAmount = max(dot(ray, -sunDir), 0.0);
    sky += light.sunColor.rgb * (pow(sunAmount, 1200.0) * 4.0 + pow(sunAmount, 12.0) * 0.22);

    fragColor = vec4(sky, 1.0);
}
