#include "engine/scene/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

#include "engine/assets/Image.hpp"

namespace engine {

std::expected<Terrain, AssetError> Terrain::loadFromHeightmap(
    std::string_view path, float worldSize, float minHeight, float maxHeight)
{
    auto image = loadImage(path, 1); // single channel (grayscale)
    if (!image) {
        return std::unexpected(image.error());
    }

    Terrain terrain;
    terrain.m_resolution = static_cast<size_t>(image->width);
    terrain.m_worldSize = worldSize;
    terrain.m_minHeight = minHeight;
    terrain.m_maxHeight = maxHeight;
    terrain.m_sourcePath = std::string{path};

    if (image->width != image->height || image->width <= 0) {
        return std::unexpected(AssetError{AssetError::Kind::Unsupported,
            std::format("heightmap must be square, got {}x{}", image->width, image->height)});
    }

    const size_t resolution = terrain.m_resolution;
    terrain.m_heights.resize(resolution * resolution);
    for (size_t i = 0; i < resolution * resolution; ++i) {
        const float normalized = static_cast<float>(image->pixels[i]) / 255.0F;
        terrain.m_heights[i] = minHeight + normalized * (maxHeight - minHeight);
    }

    return terrain;
}

Terrain Terrain::makePlainWithRamp(size_t resolution, float worldSize, float plateauHeight,
    float rampStartZ, float rampEndZ)
{
    Terrain terrain;
    terrain.m_resolution = resolution;
    terrain.m_worldSize = worldSize;
    terrain.m_sourcePath = "<procedural: plain with ramp>";

    const float step = worldSize / static_cast<float>(resolution - 1);
    const float half = worldSize / 2.0F;
    terrain.m_heights.resize(resolution * resolution);

    // Flat plain for z > rampStartZ, one linear ramp up to the plateau. The
    // heights are exact (no image quantization), so heightAt(), the rendered
    // mesh and the physics heightfield agree to the last bit.
    float minH = std::numeric_limits<float>::max();
    float maxH = std::numeric_limits<float>::lowest();
    for (size_t iz = 0; iz < resolution; ++iz) {
        const float z = static_cast<float>(iz) * step - half;
        float height = 0.0F; // plain
        if (z <= rampEndZ) {
            height = plateauHeight; // far plateau
        } else if (z < rampStartZ) {
            const float t = (rampStartZ - z) / (rampStartZ - rampEndZ);
            height = plateauHeight * t; // ramp, 0 -> plateauHeight
        }
        minH = std::min(minH, height);
        maxH = std::max(maxH, height);
        for (size_t ix = 0; ix < resolution; ++ix) {
            terrain.m_heights[iz * resolution + ix] = height;
        }
    }
    // Real data bounds: the physics heightfield body centers on (min+max)/2.
    terrain.m_minHeight = minH;
    terrain.m_maxHeight = maxH;
    return terrain;
}

float Terrain::heightAt(float x, float z) const
{
    const float half = m_worldSize / 2.0F;
    // Grid space: [0, resolution-1] over the world extent.
    const float fx = (x + half) * (static_cast<float>(m_resolution) - 1.0F) / m_worldSize;
    const float fz = (z + half) * (static_cast<float>(m_resolution) - 1.0F) / m_worldSize;

    const float maxX = static_cast<float>(m_resolution) - 1.0F;
    const float cx = std::clamp(fx, 0.0F, maxX);
    const float cz = std::clamp(fz, 0.0F, maxX);

    const size_t ix0 = static_cast<size_t>(cx);
    const size_t iz0 = static_cast<size_t>(cz);
    const size_t ix1 = std::min(ix0 + 1, m_resolution - 1);
    const size_t iz1 = std::min(iz0 + 1, m_resolution - 1);
    const float tx = cx - static_cast<float>(ix0);
    const float tz = cz - static_cast<float>(iz0);

    const float h00 = m_heights[iz0 * m_resolution + ix0];
    const float h10 = m_heights[iz0 * m_resolution + ix1];
    const float h01 = m_heights[iz1 * m_resolution + ix0];
    const float h11 = m_heights[iz1 * m_resolution + ix1];

    const float h0 = h00 + (h10 - h00) * tx;
    const float h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * tz;
}

glm::vec3 Terrain::normalAt(float x, float z) const
{
    const float step = m_worldSize / static_cast<float>(m_resolution - 1);
    const float heightL = heightAt(x - step, z);
    const float heightR = heightAt(x + step, z);
    const float heightD = heightAt(x, z - step);
    const float heightU = heightAt(x, z + step);
    return glm::normalize(glm::vec3{heightL - heightR, 2.0F * step, heightD - heightU});
}

glm::vec3 Terrain::findScenicSpot() const
{
    const size_t low = m_resolution / 4;
    const size_t high = m_resolution * 3 / 4;
    size_t bestX = m_resolution / 2;
    size_t bestZ = m_resolution / 2;
    float bestHeight = -std::numeric_limits<float>::max();

    for (size_t ix = low; ix < high; ++ix) {
        for (size_t iz = low; iz < high; ++iz) {
            const float height = m_heights[iz * m_resolution + ix];
            if (height > bestHeight) {
                bestHeight = height;
                bestX = ix;
                bestZ = iz;
            }
        }
    }

    const float step = m_worldSize / static_cast<float>(m_resolution - 1);
    const float half = m_worldSize / 2.0F;
    return {static_cast<float>(bestX) * step - half, bestHeight, static_cast<float>(bestZ) * step - half};
}

} // namespace engine
