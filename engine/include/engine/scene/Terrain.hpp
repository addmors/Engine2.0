#pragma once

#include <expected>
#include <mdspan>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/AssetError.hpp"

namespace engine {

// Heightmap-driven terrain data (replaces the 2017 `Terrian` class).
// Pure CPU-side: heights, sampling and normals. The renderer builds its mesh
// from this data (M2), physics builds a heightfield collider from it.
class Terrain final {
public:
    // Decodes the heightmap (single channel), maps byte heights linearly onto
    // [minHeight, maxHeight]. World coordinates are centered: x/z in
    // [-worldSize/2, +worldSize/2].
    [[nodiscard]] static std::expected<Terrain, AssetError> loadFromHeightmap(
        std::string_view path, float worldSize = 512.0F, float minHeight = -40.0F, float maxHeight = 40.0F);

    [[nodiscard]] float heightAt(float x, float z) const;      // bilinear sampling
    [[nodiscard]] glm::vec3 normalAt(float x, float z) const;  // central differences

    [[nodiscard]] float worldSize() const { return m_worldSize; }
    [[nodiscard]] float minHeight() const { return m_minHeight; }
    [[nodiscard]] float maxHeight() const { return m_maxHeight; }
    [[nodiscard]] size_t resolution() const { return m_resolution; }

    [[nodiscard]] std::span<const float> heights() const { return m_heights; }

    // 2D view over the height grid: heights2d()[ix, iz].
    [[nodiscard]] auto heights2d() const
    {
        return std::mdspan{m_heights.data(), m_resolution, m_resolution};
    }

private:
    std::vector<float> m_heights;
    size_t m_resolution = 0;
    float m_worldSize = 512.0F;
    float m_minHeight = -40.0F;
    float m_maxHeight = 40.0F;
    std::string m_sourcePath;
};

} // namespace engine
