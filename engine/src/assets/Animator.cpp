#include "engine/assets/Animator.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace engine {

// Nested type shared with the header's pose pipeline.
using Trs = Animator::Trs;

// Position/scale-like key interpolation (works for PositionKey and ScalingKey).
template <typename Key>
glm::vec3 interpolateVec3Key(const std::vector<Key>& keys, float tick)
{
    if (keys.empty()) {
        return {0.0F, 0.0F, 0.0F};
    }
    if (tick <= keys.front().time || keys.size() == 1) {
        return keys.front().value;
    }
    if (tick >= keys.back().time) {
        return keys.back().value;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (tick < keys[i + 1].time) {
            const float span = keys[i + 1].time - keys[i].time;
            const float t = span > 0.0F ? (tick - keys[i].time) / span : 0.0F;
            return keys[i].value + (keys[i + 1].value - keys[i].value) * t;
        }
    }
    return keys.back().value;
}

// Rotation key interpolation (spherical).
glm::quat interpolateQuat(const std::vector<RotationKey>& keys, float tick)
{
    if (keys.empty()) {
        return glm::quat{1.0F, 0.0F, 0.0F, 0.0F};
    }
    if (tick <= keys.front().time || keys.size() == 1) {
        return keys.front().value;
    }
    if (tick >= keys.back().time) {
        return keys.back().value;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (tick < keys[i + 1].time) {
            const float span = keys[i + 1].time - keys[i].time;
            const float t = span > 0.0F ? (tick - keys[i].time) / span : 0.0F;
            return glm::slerp(keys[i].value, keys[i + 1].value, t);
        }
    }
    return keys.back().value;
}

// Decomposes a pure TRS matrix (assimp node transforms are TRS).
Trs decomposeTrs(const glm::mat4& m)
{
    Trs out;
    out.translation = glm::vec3{m[3]};

    const glm::vec3 x{m[0]};
    const glm::vec3 y{m[1]};
    const glm::vec3 z{m[2]};
    out.scaling = {glm::length(x), glm::length(y), glm::length(z)};

    glm::mat3 rotation;
    rotation[0] = x / (out.scaling.x != 0.0F ? out.scaling.x : 1.0F);
    rotation[1] = y / (out.scaling.y != 0.0F ? out.scaling.y : 1.0F);
    rotation[2] = z / (out.scaling.z != 0.0F ? out.scaling.z : 1.0F);
    out.rotation = glm::quat_cast(rotation);
    return out;
}

glm::mat4 composeTrs(const Trs& trs)
{
    glm::mat4 result = glm::translate(glm::mat4{1.0F}, trs.translation);
    result *= glm::mat4_cast(trs.rotation);
    result = glm::scale(result, trs.scaling);
    return result;
}

Animator::Animator(const Model& model)
    : m_model(model)
{
    flattenTree();

    m_inverseBind.resize(static_cast<size_t>(model.boneCount()), glm::mat4{1.0F});
    for (const auto& [name, info] : model.bones()) {
        m_inverseBind[static_cast<size_t>(info.id)] = info.offsetMatrix;
    }
    m_channelMaps.resize(model.animations().size());
    m_palette.resize(static_cast<size_t>(model.boneCount()), glm::mat4{1.0F});

    m_currentPose.nodeTransforms.resize(m_nodes.size());
    m_fadePose.nodeTransforms.resize(m_nodes.size());
}

void Animator::flattenTree()
{
    // DFS pre-order: parents always precede children in m_nodes.
    std::function<void(const ModelNode&, int)> visit = [&](const ModelNode& node, int parentIndex) {
        FlatNode flat;
        flat.name = node.name;
        flat.parentIndex = parentIndex;
        flat.staticTransform = decomposeTrs(node.transformation);
        if (const auto it = m_model.bones().find(node.name); it != m_model.bones().end()) {
            flat.boneId = it->second.id;
        }
        const int index = static_cast<int>(m_nodes.size());
        m_nodes.push_back(std::move(flat));

        for (const auto& child : node.children) {
            visit(child, index);
        }
    };
    visit(m_model.root(), -1);
}

void Animator::buildChannelMap(size_t clipIndex)
{
    if (clipIndex >= m_channelMaps.size() || !m_channelMaps[clipIndex].empty()) {
        return;
    }
    const Animation& clip = m_model.animations()[clipIndex];
    auto& map = m_channelMaps[clipIndex];
    map.reserve(clip.channels.size());
    for (size_t c = 0; c < clip.channels.size(); ++c) {
        map.emplace(clip.channels[c].boneName, c);
    }
}

void Animator::play(size_t clipIndex, float speed)
{
    if (clipIndex >= m_model.animations().size()) {
        return;
    }
    m_fadingFrom = nullptr;
    m_currentClip = clipIndex;
    m_speed = speed;
    m_time = 0.0F;
}

void Animator::crossFadeTo(size_t clipIndex, float fadeDuration, float speed)
{
    if (clipIndex >= m_model.animations().size() || clipIndex == m_currentClip) {
        return;
    }
    m_fadingFrom = &m_model.animations()[m_currentClip];
    m_fadingFromIndex = m_currentClip;
    m_fadeTime = m_time; // the old clip keeps playing from its current phase
    m_fadeDuration = fadeDuration;
    m_currentClip = clipIndex;
    m_speed = speed;
    m_time = 0.0F; // the new clip starts from its beginning
}

void Animator::update(float deltaSeconds)
{
    if (m_model.animations().empty() || m_nodes.empty()) {
        return;
    }

    m_time += deltaSeconds * m_speed;
    buildChannelMap(m_currentClip);
    const Animation& current = m_model.animations()[m_currentClip];
    samplePose(current, m_channelMaps[m_currentClip], m_time, m_currentPose);

    if (m_fadingFrom != nullptr) {
        m_fadeTime += deltaSeconds * m_speed;
        const float blend = m_fadeDuration > 0.0F ? std::clamp(m_fadeTime / m_fadeDuration, 0.0F, 1.0F) : 1.0F;

        buildChannelMap(m_fadingFromIndex);
        samplePose(*m_fadingFrom, m_channelMaps[m_fadingFromIndex], m_fadeTime, m_fadePose);
        blendPoses(m_fadePose, m_currentPose, blend, m_currentPose);

        if (blend >= 1.0F) {
            m_fadingFrom = nullptr;
        }
    }

    composePalette(m_currentPose);
}

void Animator::samplePose(const Animation& clip, const std::unordered_map<std::string, size_t>& channels,
    float timeSeconds, Pose& out) const
{
    const float ticksPerSecond = clip.ticksPerSecond > 0.0F ? clip.ticksPerSecond : 25.0F;
    const float duration = std::max(clip.duration, 0.001F);
    float tick = std::fmod(timeSeconds * ticksPerSecond, duration);
    if (tick < 0.0F) {
        tick += duration;
    }

    for (size_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex) {
        const FlatNode& node = m_nodes[nodeIndex];
        Trs trs = node.staticTransform;

        if (const auto it = channels.find(node.name); it != channels.end()) {
            const BoneChannel& channel = clip.channels[it->second];
            trs.translation = interpolateVec3Key(channel.positions, tick);
            trs.rotation = interpolateQuat(channel.rotations, tick);
            trs.scaling = interpolateVec3Key(channel.scalings, tick);
        }
        out.nodeTransforms[nodeIndex] = trs;
    }
}

void Animator::blendPoses(const Pose& from, const Pose& to, float blendFactor, Pose& out) const
{
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const Trs& a = from.nodeTransforms[i];
        const Trs& b = to.nodeTransforms[i];
        Trs blended;
        blended.translation = a.translation + (b.translation - a.translation) * blendFactor;
        blended.rotation = glm::slerp(a.rotation, b.rotation, blendFactor);
        blended.scaling = a.scaling + (b.scaling - a.scaling) * blendFactor;
        out.nodeTransforms[i] = blended;
    }
}

void Animator::composePalette(const Pose& pose)
{
    m_globals.resize(m_nodes.size(), glm::mat4{1.0F});
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const FlatNode& node = m_nodes[i];
        const glm::mat4 parent = node.parentIndex >= 0 ? m_globals[static_cast<size_t>(node.parentIndex)]
                                                       : glm::mat4{1.0F};
        m_globals[i] = parent * composeTrs(pose.nodeTransforms[i]);

        if (node.boneId >= 0) {
            m_palette[static_cast<size_t>(node.boneId)]
                = m_globals[i] * m_inverseBind[static_cast<size_t>(node.boneId)];
        }
    }
}

float Animator::normalizedTime() const
{
    if (m_currentClip >= m_model.animations().size()) {
        return 0.0F;
    }
    const Animation& clip = m_model.animations()[m_currentClip];
    const float seconds = clip.duration / (clip.ticksPerSecond > 0.0F ? clip.ticksPerSecond : 25.0F);
    return seconds > 0.0F ? std::fmod(m_time, seconds) / seconds : 0.0F;
}

} // namespace engine
