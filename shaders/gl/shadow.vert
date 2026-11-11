#version 460 core

// Depth-only pass (shadow mapping). Supports skinning + instancing so every
// renderer path can reuse it (non-skinned meshes use an identity palette).
layout(location = 0) in vec3 aPosition;
layout(location = 4) in ivec4 aBoneIds;
layout(location = 5) in vec4 aBoneWeights;

#include "inc/model.ubo"
#include "inc/bones.ubo"
#include "inc/instances.ubo"
#include "inc/shadow.ubo"

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
    const vec4 worldPos = instances.matrix[gl_InstanceID] * world * vec4(aPosition, 1.0);
    gl_Position = shadow.lightViewProjection * worldPos;
}
