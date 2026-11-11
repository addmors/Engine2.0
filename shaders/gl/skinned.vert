#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in ivec4 aBoneIds;
layout(location = 5) in vec4 aBoneWeights;

#include "inc/camera.ubo"
#include "inc/model.ubo"
#include "inc/bones.ubo"
#include "inc/instances.ubo"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;

mat4 skinMatrix()
{
    if (aBoneWeights.x == 0.0 && aBoneWeights.y == 0.0 && aBoneWeights.z == 0.0 && aBoneWeights.w == 0.0) {
        return mat4(1.0);
    }
    return aBoneWeights.x * bones.palette[aBoneIds.x]
         + aBoneWeights.y * bones.palette[aBoneIds.y]
         + aBoneWeights.z * bones.palette[aBoneIds.z]
         + aBoneWeights.w * bones.palette[aBoneIds.w];
}

void main()
{
    const mat4 skin = skinMatrix();
    const mat4 world = model.matrix * skin;
    const mat4 instance = instances.matrix[gl_InstanceID];
    const vec4 worldPos = instance * world * vec4(aPosition, 1.0);

    // Instance transforms are rigid (rotation + translation, no non-uniform
    // scale) for the instanced physics bodies, so their upper-left 3x3 is a
    // valid normal matrix. Without it the normals keep the LOCAL orientation
    // while the body rotates — lighting appears glued to the object even as
    // it tumbles. For non-instanced draws instances.matrix[0] is identity.
    const mat3 normalMat = mat3(instance) * mat3(model.normalMatrix);
    vWorldPos = worldPos.xyz;
    vNormal = normalMat * (mat3(skin) * aNormal);
    vTangent = normalMat * (mat3(skin) * aTangent);
    vUV = aUV;

    gl_Position = camera.projection * camera.view * worldPos;
}
