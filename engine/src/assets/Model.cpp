#include "engine/assets/Model.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <iterator>
#include <string>
#include <utility>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "engine/core/Log.hpp"

namespace engine {

namespace {

glm::mat4 toGlm(const aiMatrix4x4& m)
{
    return {
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4,
    };
}

glm::vec3 toGlm(const aiVector3D& v) { return {v.x, v.y, v.z}; }

glm::quat toGlm(const aiQuaternion& q) { return {q.w, q.x, q.y, q.z}; }

std::string fileNotFoundMessage(std::string_view path)
{
    return std::format("file not found: {}", path);
}

// Scatters a bone weight into the top-4 slots of the vertex.
void addWeight(Vertex& vertex, int boneId, float weight)
{
    for (int i = 0; i < 4; ++i) {
        if (vertex.boneWeights[i] == 0.0F) {
            vertex.boneIds[i] = boneId;
            vertex.boneWeights[i] = weight;
            return;
        }
    }

    // All four slots taken: replace the least influential weight.
    const auto smallest = std::min_element(vertex.boneWeights.begin(), vertex.boneWeights.end());
    if (weight > *smallest) {
        vertex.boneIds[static_cast<size_t>(std::distance(vertex.boneWeights.begin(), smallest))] = boneId;
        *smallest = weight;
    }
}

} // namespace

std::expected<Model, AssetError> Model::loadFromFile(std::string_view path)
{
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
        return std::unexpected(AssetError{AssetError::Kind::FileNotFound, fileNotFoundMessage(path)});
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(std::string{path},
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace
            | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr) {
        return std::unexpected(AssetError{AssetError::Kind::ParseFailed,
            std::format("assimp failed to parse {}: {}", path, importer.GetErrorString())});
    }

    Model model;
    model.m_sourceDir = fs::path{path}.parent_path().generic_string();
    model.m_root = processNode(*scene->mRootNode, *scene, model);
    for (auto& mesh : model.m_meshes) {
        mesh.computeAabb();
    }
    model.readMaterials(*scene);
    model.readAnimations(*scene);

    Log::info("model '{}' loaded: {} meshes, {} bones, {} animations",
        fs::path{path}.filename().generic_string(), model.m_meshes.size(), model.m_boneCount,
        model.m_animations.size());
    return model;
}

std::expected<void, AssetError> Model::loadAnimations(std::string_view path)
{
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
        return std::unexpected(AssetError{AssetError::Kind::FileNotFound, fileNotFoundMessage(path)});
    }

    // Animation-only read: no geometry post-processing is needed here.
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(std::string{path}, 0);
    if (scene == nullptr) {
        return std::unexpected(AssetError{AssetError::Kind::ParseFailed,
            std::format("assimp failed to read animations from {}: {}", path, importer.GetErrorString())});
    }

    const size_t before = m_animations.size();
    readAnimations(*scene);
    Log::info("{}: +{} animation clip(s)", fs::path{path}.filename().generic_string(),
        m_animations.size() - before);
    return {};
}

ModelNode Model::processNode(const aiNode& node, const aiScene& scene, Model& model)
{
    ModelNode out;
    out.name = node.mName.C_Str();
    out.transformation = toGlm(node.mTransformation);
    out.meshIndices.assign(node.mMeshes, node.mMeshes + node.mNumMeshes);

    model.m_meshes.reserve(model.m_meshes.size() + node.mNumMeshes);
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
        model.m_meshes.push_back(processMesh(*scene.mMeshes[node.mMeshes[i]], scene, model));
    }

    out.children.reserve(node.mNumChildren);
    for (unsigned i = 0; i < node.mNumChildren; ++i) {
        out.children.push_back(processNode(*node.mChildren[i], scene, model));
    }
    return out;
}

Mesh Model::processMesh(const aiMesh& mesh, const aiScene& /*scene*/, Model& model)
{
    Mesh out;
    out.name = mesh.mName.C_Str();
    out.materialIndex = mesh.mMaterialIndex;

    out.vertices.reserve(mesh.mNumVertices);
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        Vertex v;
        v.position = toGlm(mesh.mVertices[i]);
        v.normal = mesh.HasNormals() ? toGlm(mesh.mNormals[i]) : glm::vec3{0.0F, 1.0F, 0.0F};
        if (mesh.HasTextureCoords(0)) {
            v.uv = {mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y};
        }
        if (mesh.HasTangentsAndBitangents()) {
            v.tangent = toGlm(mesh.mTangents[i]);
        }
        out.vertices.push_back(v);
    }

    out.indices.reserve(static_cast<size_t>(mesh.mNumFaces) * 3);
    for (unsigned i = 0; i < mesh.mNumFaces; ++i) {
        const aiFace& face = mesh.mFaces[i];
        out.indices.insert(out.indices.end(), face.mIndices, face.mIndices + face.mNumIndices);
    }

    for (unsigned b = 0; b < mesh.mNumBones; ++b) {
        const aiBone& bone = *mesh.mBones[b];

        const std::string boneName = bone.mName.C_Str();
        auto [it, inserted] = model.m_bones.try_emplace(boneName,
            BoneInfo{static_cast<int>(model.m_boneCount), toGlm(bone.mOffsetMatrix)});
        if (inserted) {
            ++model.m_boneCount;
        }
        const int boneId = it->second.id;

        for (unsigned w = 0; w < bone.mNumWeights; ++w) {
            const aiVertexWeight& weight = bone.mWeights[w];
            addWeight(out.vertices[weight.mVertexId], boneId, weight.mWeight);
        }
    }

    return out;
}

void Model::readMaterials(const aiScene& scene)
{
    m_materials.reserve(scene.mNumMaterials);
    for (unsigned i = 0; i < scene.mNumMaterials; ++i) {
        const aiMaterial& material = *scene.mMaterials[i];

        const auto pickTexture = [&material](aiTextureType type) {
            aiString path;
            if (material.GetTexture(type, 0, &path) == aiReturn_SUCCESS) {
                std::string value = path.C_Str();
                // Drop URI/relative prefixes that collada/fbx sometimes carry.
                const size_t scheme = value.find("://");
                if (scheme != std::string::npos) {
                    value = value.substr(scheme + 3);
                }
                return std::filesystem::path{value}.generic_string();
            }
            return std::string{};
        };

        Material out;
        out.name = material.GetName().C_Str();
        out.diffuse = pickTexture(aiTextureType_DIFFUSE);
        out.normal = pickTexture(aiTextureType_NORMALS);
        out.specular = pickTexture(aiTextureType_SPECULAR);
        m_materials.push_back(std::move(out));
    }
}

void Model::readAnimations(const aiScene& scene)
{
    m_animations.reserve(m_animations.size() + scene.mNumAnimations);
    for (unsigned a = 0; a < scene.mNumAnimations; ++a) {
        const aiAnimation& anim = *scene.mAnimations[a];

        Animation out;
        out.name = anim.mName.C_Str();
        out.duration = static_cast<float>(anim.mDuration);
        out.ticksPerSecond = anim.mTicksPerSecond != 0.0 ? static_cast<float>(anim.mTicksPerSecond) : 25.0F;
        out.channels.reserve(anim.mNumChannels);

        for (unsigned c = 0; c < anim.mNumChannels; ++c) {
            const aiNodeAnim& channel = *anim.mChannels[c];

            BoneChannel boneChannel;
            boneChannel.boneName = channel.mNodeName.C_Str();

            boneChannel.positions.reserve(channel.mNumPositionKeys);
            for (unsigned k = 0; k < channel.mNumPositionKeys; ++k) {
                boneChannel.positions.push_back({static_cast<float>(channel.mPositionKeys[k].mTime),
                    toGlm(channel.mPositionKeys[k].mValue)});
            }

            boneChannel.rotations.reserve(channel.mNumRotationKeys);
            for (unsigned k = 0; k < channel.mNumRotationKeys; ++k) {
                boneChannel.rotations.push_back({static_cast<float>(channel.mRotationKeys[k].mTime),
                    toGlm(channel.mRotationKeys[k].mValue)});
            }

            boneChannel.scalings.reserve(channel.mNumScalingKeys);
            for (unsigned k = 0; k < channel.mNumScalingKeys; ++k) {
                boneChannel.scalings.push_back({static_cast<float>(channel.mScalingKeys[k].mTime),
                    toGlm(channel.mScalingKeys[k].mValue)});
            }

            out.channels.push_back(std::move(boneChannel));
        }

        m_animations.push_back(std::move(out));
    }
}

} // namespace engine
