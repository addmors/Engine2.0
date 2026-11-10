#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/Animation.hpp"
#include "engine/assets/AssetError.hpp"
#include "engine/assets/Mesh.hpp"

struct aiNode;
struct aiScene;
struct aiMesh;

namespace engine {

// Hierarchy node of a loaded model (mirrors the assimp node tree).
struct ModelNode {
    std::string name;
    glm::mat4 transformation{1.0F};
    std::vector<unsigned> meshIndices;
    std::vector<ModelNode> children;
};

// Skeleton bone bookkeeping: flat id + inverse bind matrix.
struct BoneInfo {
    int id = -1;
    glm::mat4 offsetMatrix{1.0F};
};

// Static asset data: geometry + skeleton + animation clips.
// GPU resources are created later by the renderer (RHI, M2).
class Model final {
public:
    // Texture references of one material (paths relative to the model file).
    struct Material {
        std::string name;
        std::string diffuse;
        std::string normal;
        std::string specular;
    };

    // Parses the file; reports failures via std::expected instead of exceptions.
    [[nodiscard]] static std::expected<Model, AssetError> loadFromFile(std::string_view path);

    // Appends all animation clips found in the given file (idle.dae, Walking.dae, ...).
    std::expected<void, AssetError> loadAnimations(std::string_view path);

    [[nodiscard]] const ModelNode& root() const { return m_root; }
    [[nodiscard]] const std::vector<Mesh>& meshes() const { return m_meshes; }
    [[nodiscard]] const std::vector<Animation>& animations() const { return m_animations; }
    [[nodiscard]] const std::unordered_map<std::string, BoneInfo>& bones() const { return m_bones; }
    [[nodiscard]] const std::vector<Material>& materials() const { return m_materials; }
    [[nodiscard]] int boneCount() const { return m_boneCount; }
    [[nodiscard]] const std::string& sourceDirectory() const { return m_sourceDir; }

private:
    static ModelNode processNode(const aiNode& node, const aiScene& scene, Model& model);
    [[nodiscard]] static Mesh processMesh(const aiMesh& mesh, const aiScene& scene, Model& model);
    void readAnimations(const aiScene& scene);
    void readMaterials(const aiScene& scene);

    std::vector<Mesh> m_meshes;
    std::vector<Animation> m_animations;
    std::vector<Material> m_materials;
    std::unordered_map<std::string, BoneInfo> m_bones;
    ModelNode m_root;
    std::string m_sourceDir;
    int m_boneCount = 0;
};

} // namespace engine
