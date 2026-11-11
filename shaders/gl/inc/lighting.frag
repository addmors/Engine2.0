// Shared lighting: directional sun + ambient + blinn-phong specular,
// with PCF 3x3 shadow mapping. Pulls in the camera/light/shadow UBO blocks.

#include "inc/camera.ubo"
#include "inc/light.ubo"
#include "inc/shadow.ubo"

layout(binding = 8) uniform sampler2D uShadowMap;

// Tangent-space normal mapping (Gram-Schmidt re-orthogonalized TBN).
vec3 mappedNormal(sampler2D normalMap, vec3 vertexNormal, vec3 tangent, vec2 uv)
{
    const vec3 sampled = texture(normalMap, uv).xyz * 2.0 - 1.0;
    const vec3 n = normalize(vertexNormal);
    const vec3 t = normalize(tangent - n * dot(tangent, n));
    const vec3 b = cross(n, t);
    return normalize(t * sampled.x + b * sampled.y + n * sampled.z);
}

vec3 shade(vec3 albedo, vec3 normal, float specularStrength, vec3 worldPos, float shininess)
{
    vec3 lightDir = normalize(-light.sunDirection.xyz); // towards the sun
    float diffuse = max(dot(normal, lightDir), 0.0);

    // PCF 3x3 shadow sampling.
    float shadowFactor = 1.0;
    if (shadow.params.x > 0.5) {
        vec4 lightSpace = shadow.lightViewProjection * vec4(worldPos, 1.0);
        vec3 proj = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
        if (proj.z <= 1.0) {
            float texel = shadow.params.y;
            float sum = 0.0;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    float depth = texture(uShadowMap, proj.xy + vec2(float(dx), float(dy)) * texel).r;
                    sum += (proj.z - 0.0015 > depth) ? 0.0 : 1.0;
                }
            }
            shadowFactor = sum / 9.0;
        }
    }

    const vec3 viewDir = normalize(camera.position.xyz - worldPos);
    const vec3 halfway = normalize(lightDir + viewDir);
    const float specular = pow(max(dot(normal, halfway), 0.0), shininess) * specularStrength;

    const vec3 color = albedo * (light.ambient.rgb + light.sunColor.rgb * diffuse * shadowFactor)
        + light.sunColor.rgb * specular * shadowFactor;
    return color;
}
