#version 460 core

// Instanced untextured primitives (physics bodies). The skinned.vert handles
// all vertex transforms; this fragment shades a flat color with lighting.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vTangent;

layout(location = 0) out vec4 fragColor;

uniform vec3 uColor;

#include "inc/lighting.frag"

void main()
{
    fragColor = vec4(shade(uColor, normalize(vNormal), 0.15, vWorldPos, 32.0), 1.0);
}
