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

    // Procedural test level: a perfectly flat plain (y = 0) with one long ramp
    // up to a plateau along -Z. No assets and no heightmap quantization — the
    // exact heights make character grounding verifiable numerically (see the
    // app's testPlayer / testPhysics and the game scene).
    [[nodiscard]] static Terrain makePlainWithRamp(size_t resolution = 256, float worldSize = 512.0F,
        float plateauHeight = 25.0F, float rampStartZ = -60.0F, float rampEndZ = -200.0F);

    [[nodiscard]] float heightAt(float x, float z) const;      // bilinear sampling
    [[nodiscard]] glm::vec3 normalAt(float x, float z) const;  // central differences

    // World position of the highest grid point within the central quarter of
    // the map — a scenic spawn vantage (used by the demo scene).
    [[nodiscard]] glm::vec3 findScenicSpot() const;

    [[nodiscard]] float worldSize() const { return m_worldSize; }
    [[nodiscard]] float minHeight() const { return m_minHeight; }
    [[nodiscard]] float maxHeight() const { return m_maxHeight; }
    [[nodiscard]] size_t resolution() const { return m_resolution; }

    [[nodiscard]] std::span<const float> heights() const { return m_heights; }

    // 2D view over the height grid: heights2d()[ix, iz] (x is the fast axis,
    // matching both our linear sampling and bullet's heightfield layout).
    using HeightGrid = std::mdspan<const float, std::dextents<size_t, 2>, std::layout_left>;
    [[nodiscard]] HeightGrid heights2d() const
    {
        return HeightGrid{m_heights.data(), m_resolution, m_resolution};
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
