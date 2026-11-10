#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/assets/Model.hpp"

namespace engine {

// Skeletal animation runtime: evaluates animation clips over time and produces
// the final bone palette for GPU skinning. Replaces the 2017
// Skeleton/AnimNode/Bone/MenegerAnimation stack with one linear-time pass.
//
// Supports cross-fading between clips (idle <-> walk blending like the old
// `ourModel.PlayAnimation` + movement-driven switches).
class Animator final {
public:
    // Position-rotation-scale triple shared by the pose pipeline.
    struct Trs {
        glm::vec3 translation{};
        glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec3 scaling{1.0F};
    };

    explicit Animator(const Model& model);

    // Hard switch to the clip at clipIndex (time resets).
    void play(size_t clipIndex, float speed = 1.0F);

    // Smooth cross-fade to another clip over fadeDuration seconds.
    void crossFadeTo(size_t clipIndex, float fadeDuration = 0.25F, float speed = 1.0F);

    // Advances time and recomputes the bone palette. Call once per frame.
    void update(float deltaSeconds);

    // Final skinning matrices, one per bone (index = BoneInfo::id).
    [[nodiscard]] std::span<const glm::mat4> bonePalette() const { return m_palette; }

    [[nodiscard]] size_t clipCount() const { return m_model.animations().size(); }
    [[nodiscard]] size_t currentClip() const { return m_currentClip; }
    [[nodiscard]] float currentTime() const { return m_time; }
    [[nodiscard]] float normalizedTime() const;

private:
    // Flattened model node tree: parents always precede children.
    struct FlatNode {
        std::string name;
        int parentIndex = -1;
        Trs staticTransform{};
        int boneId = -1; // -1 when the node is not a skinning bone
    };

    struct Pose {
        std::vector<Trs> nodeTransforms; // index-aligned with m_nodes
    };

    void flattenTree();
    void buildChannelMap(size_t clipIndex);
    void samplePose(const Animation& clip, const std::unordered_map<std::string, size_t>& channels,
        float timeSeconds, Pose& out) const;
    void blendPoses(const Pose& from, const Pose& to, float blendFactor, Pose& out) const;
    void composePalette(const Pose& pose);

    const Model& m_model;

    std::vector<FlatNode> m_nodes;
    std::vector<glm::mat4> m_inverseBind; // by bone id
    std::vector<glm::mat4> m_palette;     // by bone id
    std::vector<glm::mat4> m_globals;     // scratch: per flat node world matrices

    std::vector<std::unordered_map<std::string, size_t>> m_channelMaps; // per clip

    Pose m_currentPose;
    Pose m_fadePose;
    const Animation* m_fadingFrom = nullptr;
    size_t m_fadingFromIndex = 0;
    float m_fadeTime = 0.0F;
    float m_fadeDuration = 0.0F;

    size_t m_currentClip = 0;
    float m_time = 0.0F;
    float m_speed = 1.0F;
};

} // namespace engine
