#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "inc/camera.ubo"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main()
{
    vWorldPos = aPosition;
    vNormal = aNormal;
    vUV = aUV;
    gl_Position = camera.projection * camera.view * vec4(aPosition, 1.0);
}
