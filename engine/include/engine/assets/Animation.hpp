#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine {

struct PositionKey {
    float time{};
    glm::vec3 value{};
};

struct RotationKey {
    float time{};
    glm::quat value{};
};

struct ScalingKey {
    float time{};
    glm::vec3 value{};
};

// Keyframes of a single bone inside an animation clip.
struct BoneChannel {
    std::string boneName;
    std::vector<PositionKey> positions;
    std::vector<RotationKey> rotations;
    std::vector<ScalingKey> scalings;
};

// One animation clip (e.g. "Idle", "Walking").
struct Animation {
    std::string name;
    float duration{}; // length in ticks
    float ticksPerSecond{25.0F};
    std::vector<BoneChannel> channels;
};

} // namespace engine
