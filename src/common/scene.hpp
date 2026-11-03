#pragma once

#include <volk.h>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <stb_image.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/string_cast.hpp>

#include "core/filesystem.hpp"
#include "core/timer.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#define SCENE_ENABLE_NORMALS
#define SCENE_ENABLE_TANGENTS

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::optional<T>& opt) {
    return opt ? (os << *opt) : (os << "--");
}

struct Scene final {
    struct Vertex final {
        glm::vec3 pos;
        glm::vec2 uv;
#ifdef SCENE_ENABLE_NORMALS
        glm::vec3 normal;
#endif
#ifdef SCENE_ENABLE_TANGENTS
        glm::vec3 tangent;
#endif
    };

    struct Node final {
        std::string name;

        glm::mat4 transform;
        std::vector<uint32_t> meshes;
        std::vector<uint32_t> children;
    };

    struct Mesh final {
        std::string name;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIdx;

        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
    };

    struct Material final {
        std::string name;

        glm::vec3 colDiffuse;
        glm::vec3 colSpecular;
        glm::vec3 colAmbient;
        glm::vec3 colEmissive;
        float opacity;
        float metallic;
        float roughness;

        std::optional<uint32_t> texDiffuseIdx;
        std::optional<uint32_t> texMetalRoughIdx;
        std::optional<uint32_t> texNormalIdx;
        bool hasBakedAo;

        enum class AlphaMode {
            Opaque,
            Mask,
            Blend,
        };
        AlphaMode alphaMode;
        float alphaCutoff;

        bool twosided;
    };

    struct Texture final {
        std::string name;

        uint32_t width;
        uint32_t height;
        uint32_t channels;
        std::vector<uint8_t> data;
        bool isSRGB;

        VkFormat format;
        uint32_t mipLevels;
    };

    //--------------------------------------------------------------------------------------------------

    template <typename T>
    struct AnimKey {
        T val;
        double timeSecs;
    };

    struct AnimChannel {
        uint32_t nodeIdx;
        std::vector<AnimKey<glm::vec3>> posKeys;
        std::vector<AnimKey<glm::quat>> rotKeys;
        std::vector<AnimKey<glm::vec3>> sclKeys;
    };

    struct Animation final {
        std::string name;
        double durationSecs;
        std::vector<AnimChannel> channels;
    };

    struct AnimationChannelState final {
        uint32_t stepPos;
        uint32_t stepRot;
        uint32_t stepScl;
    };

    struct AnimationState final {
        double time;
        std::vector<AnimationChannelState> channels;
    };

    //--------------------------------------------------------------------------------------------------

    std::string name;
    float sizeWorld;

    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;

    std::vector<Animation> animations;
    std::vector<AnimationState> animationState;

    //--------------------------------------------------------------------------------------------------

    bool load(const std::string& file, bool verbosePrint = false) {
        std::cerr << "Loading '" << file << "...\n";

        Timer timer;
        timer.start();

        Assimp::Importer importer;
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

        const aiScene* scene = importer.ReadFile(
            Fs::findPath(file).string(),
            aiProcessPreset_TargetRealtime_Fast | aiProcess_RemoveRedundantMaterials | aiProcess_FindInstances |
                aiProcess_OptimizeMeshes | aiProcess_FlipUVs | aiProcess_GenBoundingBoxes
        );

        if (scene == nullptr) {
            std::cerr << "Loading failed: " << importer.GetErrorString() << "\n";
            return false;
        }

        this->name = scene->mName.C_Str();

        loadNodes(scene);
        loadMeshes(scene);
        loadMaterials(scene);
        loadTextures(scene);
        loadAnimations(scene);

        glm::vec3 aabbMin{std::numeric_limits<float>::max()};
        glm::vec3 aabbMax{std::numeric_limits<float>::min()};
        traverseNodes([&](const glm::mat4& transform, uint32_t meshIdx) {
            const auto& mesh = meshes[meshIdx];

            const glm::vec3 center = (mesh.aabbMin + mesh.aabbMax) * 0.5f;
            const glm::vec3 extent = (mesh.aabbMax - mesh.aabbMin) * 0.5f;

            const glm::mat3 R = glm::mat3(transform);
            const glm::vec3 T = glm::vec3(transform[3]);

            const glm::vec3 centerT = R * center + T;
            const glm::vec3 extentT = glm::mat3(glm::abs(R[0]), glm::abs(R[1]), glm::abs(R[2])) * extent;

            const glm::vec3 min = centerT - extentT;
            const glm::vec3 max = centerT + extentT;

            aabbMin = glm::min(aabbMin, min);
            aabbMax = glm::max(aabbMax, max);
        });
        sizeWorld = glm::distance(aabbMin, aabbMax);

        timer.end();
        std::cerr << " => Complete in " << timer.ms() << "ms\n";

        if (verbosePrint) {
            print();
        }

        return true;
    }

    void traverseNodes(std::function<void(const glm::mat4&, uint32_t)> cb) {
        traverseNodesRecurse(cb, 0, glm::identity<glm::mat4>());
    }

  private:
    void traverseNodesRecurse(
        const std::function<void(const glm::mat4&, uint32_t)>& cb, uint32_t nodeIdx, const glm::mat4& parentTransform
    ) const {
        const Node& node = nodes[nodeIdx];
        const glm::mat4 transform = parentTransform * node.transform;

        for (const uint32_t meshIdx : node.meshes) {
            cb(transform, meshIdx);
        }

        for (const uint32_t child : node.children) {
            traverseNodesRecurse(cb, child, transform);
        }
    }

    //--------------------------------------------------------------------------------------------------

  private:
    void loadNodes(const aiScene* scene) {
        assert(scene->mRootNode);

        uint32_t rootNode = buildNode(scene->mRootNode);
        assert(rootNode == 0);
    }

    uint32_t buildNode(const aiNode* node) {
        const uint32_t nodeIdx = nodes.size();

        const glm::mat4 m = glm::transpose(glm::make_mat4(&node->mTransformation.a1));
        nodes.emplace_back(node->mName.C_Str(), m);

        std::vector<uint32_t> meshes(node->mNumMeshes);
        for (uint32_t i = 0; i < node->mNumMeshes; i++) {
            meshes[i] = node->mMeshes[i];
        }

        std::vector<uint32_t> children(node->mNumChildren);
        for (uint32_t i = 0; i < node->mNumChildren; i++) {
            children[i] = buildNode(node->mChildren[i]);
        }

        nodes[nodeIdx].meshes = std::move(meshes);
        nodes[nodeIdx].children = std::move(children);

        return nodeIdx;
    }

    void loadMeshes(const aiScene* scene) {
        std::vector<Mesh> outMeshes;
        outMeshes.resize(scene->mNumMeshes);

        for (uint32_t i = 0; i < scene->mNumMeshes; i++) {
            const aiMesh* mesh = scene->mMeshes[i];

            if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
                std::cerr << "Error: Mesh " << mesh->mName.C_Str() << " is of type " << mesh->mPrimitiveTypes << "\n";
                assert(mesh->mPrimitiveTypes == aiPrimitiveType_TRIANGLE);
            }

            Mesh& outMesh = outMeshes[i];
            outMesh.name = mesh->mName.C_Str();
            outMesh.materialIdx = mesh->mMaterialIndex;
            outMesh.vertices.resize(mesh->mNumVertices);
            outMesh.indices.resize(mesh->mNumFaces * 3);

            for (uint32_t v = 0; v < mesh->mNumVertices; v++) {
                const auto vtx = mesh->mVertices[v];
                outMesh.vertices[v].pos = {vtx.x, vtx.y, vtx.z};

                const auto uv = mesh->mTextureCoords[0][v];
                outMesh.vertices[v].uv = {uv.x, uv.y};

#ifdef SCENE_ENABLE_NORMALS
                if (mesh->mNormals) {
                    const auto nrm = mesh->mNormals[v];
                    outMesh.vertices[v].normal = {nrm.x, nrm.y, nrm.z};
                }
#endif

#ifdef SCENE_ENABLE_TANGENTS
                if (mesh->mTangents) {
                    const auto tangent = mesh->mTangents[v];
                    outMesh.vertices[v].tangent = {tangent.x, tangent.y, tangent.z};
                }
#endif
            }

            for (uint32_t f = 0; f < mesh->mNumFaces; f++) {
                const auto face = mesh->mFaces[f];
                assert(face.mNumIndices == 3);
                outMesh.indices[f * 3 + 0] = face.mIndices[0];
                outMesh.indices[f * 3 + 1] = face.mIndices[1];
                outMesh.indices[f * 3 + 2] = face.mIndices[2];
            }

            outMesh.aabbMin = {mesh->mAABB.mMin.x, mesh->mAABB.mMin.y, mesh->mAABB.mMin.z};
            outMesh.aabbMax = {mesh->mAABB.mMax.x, mesh->mAABB.mMax.y, mesh->mAABB.mMax.z};
        }

        this->meshes = std::move(outMeshes);
    }

    static glm::vec3 getMaterialColor(
        const aiMaterial* material, const char* pKey, unsigned int type, unsigned int idx
    ) {
        aiColor3D color{};
        material->Get(pKey, type, idx, color);
        return {color.r, color.g, color.b};
    }

    static float getMaterialFloat(const aiMaterial* material, const char* pKey, unsigned int type, unsigned int idx) {
        float value{};
        material->Get(pKey, type, idx, value);
        return value;
    }

    static std::optional<uint32_t> getTextureIndex(
        const aiScene* scene, const aiMaterial* material, aiTextureType texType
    ) {
        aiString texPath{};
        if (material->GetTexture(texType, 0, &texPath) != AI_SUCCESS) {
            return std::nullopt;
        }

        const auto [texture, index] = scene->GetEmbeddedTextureAndIndex(texPath.C_Str());
        if (texture == nullptr || index < 0) {
            return std::nullopt;
        }

        return index;
    }

    static Material::AlphaMode stringToAlphaMode(const aiString& alphaMode) {
        if (std::strcmp(alphaMode.C_Str(), "OPAQUE") == 0) {
            return Material::AlphaMode::Opaque;
        }

        if (std::strcmp(alphaMode.C_Str(), "MASK") == 0) {
            return Material::AlphaMode::Mask;
        }

        if (std::strcmp(alphaMode.C_Str(), "BLEND") == 0) {
            return Material::AlphaMode::Blend;
        }

        std::cerr << "Warning: Invalid alpha mode '" << alphaMode.C_Str() << "'!\n";

        return Material::AlphaMode::Opaque;
    }

    void loadMaterials(const aiScene* scene) {
        std::vector<Material> outMaterials;
        outMaterials.resize(scene->mNumMaterials);

        for (uint32_t i = 0; i < scene->mNumMaterials; i++) {
            const aiMaterial* material = scene->mMaterials[i];

            Material& outMat = outMaterials[i];
            outMat.name = material->GetName().C_Str();

            outMat.colDiffuse = getMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE);
            outMat.colSpecular = getMaterialColor(material, AI_MATKEY_COLOR_SPECULAR);
            outMat.colAmbient = getMaterialColor(material, AI_MATKEY_COLOR_AMBIENT);
            outMat.colEmissive = getMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE);
            outMat.opacity = getMaterialFloat(material, AI_MATKEY_OPACITY);
            outMat.metallic = getMaterialFloat(material, AI_MATKEY_METALLIC_FACTOR);
            outMat.roughness = getMaterialFloat(material, AI_MATKEY_ROUGHNESS_FACTOR);

            outMat.texDiffuseIdx = getTextureIndex(scene, material, aiTextureType_DIFFUSE);
            outMat.texMetalRoughIdx = getTextureIndex(scene, material, aiTextureType_GLTF_METALLIC_ROUGHNESS);
            outMat.texNormalIdx = getTextureIndex(scene, material, aiTextureType_NORMALS);

            outMat.hasBakedAo = outMat.texMetalRoughIdx.has_value() &&
                                getTextureIndex(scene, material, aiTextureType_LIGHTMAP) == outMat.texMetalRoughIdx;

            bool usesTextureAlpha = false;
            outMat.alphaMode = Material::AlphaMode::Opaque;
            outMat.alphaCutoff = 0.f;

            aiString alphaMode{};
            material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode);
            outMat.alphaMode = stringToAlphaMode(alphaMode);

            if (outMat.alphaMode == Material::AlphaMode::Mask) {
                outMat.alphaCutoff = 0.5f;
                material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, outMat.alphaCutoff);
            }

            int twosided = 0;
            material->Get(AI_MATKEY_TWOSIDED, twosided);
            outMat.twosided = (twosided > 0);
        }

        this->materials = std::move(outMaterials);
    }

    void loadTextures(const aiScene* scene) {
        if (scene->mNumTextures == 0) {
            this->textures = {Texture{
                .name = "<dummy texture>",
                .width = 1,
                .height = 1,
                .data = {0, 0, 0, 0},
            }};
            return;
        }

        std::vector<Texture> outTextures;
        outTextures.resize(scene->mNumTextures);

        bool error = false;

#pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < scene->mNumTextures; i++) {
            const aiTexture* texture = scene->mTextures[i];
            assert(texture->pcData != nullptr && texture->mHeight == 0);

            const uint8_t* textureData = reinterpret_cast<const uint8_t*>(texture->pcData);
            const uint32_t textureDataLen = texture->mWidth;

            Texture& outTex = outTextures[i];
            outTex.name = texture->mFilename.C_Str();

            int x, y, c;
            uint8_t* data = stbi_load_from_memory(textureData, textureDataLen, &x, &y, &c, 4);
            if (data == nullptr) {
                error = true;
                continue;
            }

            outTex.width = x;
            outTex.height = y;
            outTex.channels = 4;
            outTex.data.assign(data, data + x * y * 4);
            outTex.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(x, y)))) + 1;
            outTex.isSRGB = true;

            stbi_image_free(data);
        }

        if (error) {
            std::cerr << "Loading texture in failed\n";
            exit(1);
        }

        for (const auto& material : this->materials) {
            if (material.texNormalIdx.has_value()) {
                outTextures[material.texNormalIdx.value()].isSRGB = false;
            }

            if (material.texMetalRoughIdx.has_value()) {
                outTextures[material.texMetalRoughIdx.value()].isSRGB = false;
            }
        }

        for (const auto& material : this->materials) {
            if (material.texDiffuseIdx.has_value() && !outTextures[material.texDiffuseIdx.value()].isSRGB) {
                std::cerr << "Warning: Albedo texture is also used as non-SRGB texture!\n";
            }
        }

        for (auto& texture : outTextures) {
            texture.format = texture.isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        }

        this->textures = std::move(outTextures);
    }

    void loadAnimations(const aiScene* scene) {
        std::vector<Animation> outAnimations;
        outAnimations.resize(scene->mNumAnimations);

        std::vector<AnimationState> outAnimationState;
        outAnimationState.resize(scene->mNumAnimations);

        std::unordered_map<std::string_view, uint32_t> nodeMap;
        for (uint32_t i = 0; i < nodes.size(); i++) {
            nodeMap.emplace(nodes[i].name, i);
        }

        for (uint32_t i = 0; i < scene->mNumAnimations; i++) {
            const aiAnimation* anim = scene->mAnimations[i];

            Animation& outAnim = outAnimations[i];
            outAnim.name = anim->mName.C_Str();
            outAnim.durationSecs = anim->mDuration / anim->mTicksPerSecond;

            AnimationState& outAnimState = outAnimationState[i];
            outAnimState = {.time = 0.0};
            outAnimState.channels.resize(anim->mNumChannels, {.stepPos = 0});

            outAnim.channels.resize(anim->mNumChannels);
            for (uint32_t j = 0; j < anim->mNumChannels; j++) {
                const aiNodeAnim* nodeAnim = anim->mChannels[j];

                AnimChannel& outChannel = outAnim.channels[j];

                // find index of targeted node
                if (auto nodeIdx = nodeMap.find(nodeAnim->mNodeName.C_Str()); nodeIdx != nodeMap.end()) {
                    outChannel.nodeIdx = nodeIdx->second;
                } else {
                    std::cerr << "Node '" << nodeAnim->mNodeName.C_Str() << "' in animation '" << outAnim.name
                              << "' does not exist!\n";
                    exit(1);
                }

                outChannel.posKeys.resize(nodeAnim->mNumPositionKeys);
                for (uint32_t k = 0; k < nodeAnim->mNumPositionKeys; k++) {
                    const aiVectorKey& key = nodeAnim->mPositionKeys[k];
                    outChannel.posKeys[k] = {
                        .val = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z),
                        .timeSecs = key.mTime / anim->mTicksPerSecond,
                    };
                }

                outChannel.rotKeys.resize(nodeAnim->mNumRotationKeys);
                for (uint32_t k = 0; k < nodeAnim->mNumRotationKeys; k++) {
                    const aiQuatKey& key = nodeAnim->mRotationKeys[k];
                    outChannel.rotKeys[k] = {
                        .val = glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z),
                        .timeSecs = key.mTime / anim->mTicksPerSecond,
                    };
                }

                outChannel.sclKeys.resize(nodeAnim->mNumScalingKeys);
                for (uint32_t k = 0; k < nodeAnim->mNumScalingKeys; k++) {
                    const aiVectorKey& key = nodeAnim->mScalingKeys[k];
                    outChannel.sclKeys[k] = {
                        .val = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z),
                        .timeSecs = key.mTime / anim->mTicksPerSecond,
                    };
                }
            }
        }

        this->animations = std::move(outAnimations);
        this->animationState = std::move(outAnimationState);
    }

    //--------------------------------------------------------------------------------------------------

    template <typename T>
    static inline std::tuple<uint32_t, T> animateChannel(
        double time, uint32_t step, const std::vector<AnimKey<T>>& keys
    ) {
        if (time < keys[step].timeSecs) {
            step = 0;
        }

        while ((time > keys[step + 1].timeSecs) && (step + 1 < keys.size())) {
            step += 1;
        }

        const auto& keyCurr = keys[step];
        const auto& keyNext = keys[step + 1];

        const float delta = (time - keyCurr.timeSecs) / (keyNext.timeSecs - keyCurr.timeSecs);

        T res;
        if constexpr (std::is_same<T, glm::quat>::value) {
            res = glm::slerp(keyCurr.val, keyNext.val, delta);
        } else {
            res = glm::mix(keyCurr.val, keyNext.val, delta);
        }

        return {step, res};
    }

  public:
    void updateAnimationState(double dt) {
        for (uint32_t i = 0; i < animations.size(); i++) {
            const auto& anim = animations[i];
            auto& state = animationState[i];

            double time = state.time + dt;
            while (time >= anim.durationSecs) {
                time -= anim.durationSecs;
            }

            for (uint32_t j = 0; j < anim.channels.size(); j++) {
                const auto& channel = anim.channels[j];
                auto& chState = state.channels[j];

                glm::vec3 pos;
                if (channel.posKeys.size() > 1) {
                    std::tie(chState.stepPos, pos) = animateChannel(time, chState.stepPos, channel.posKeys);
                } else {
                    pos = channel.posKeys[0].val;
                }

                glm::quat rot;
                if (channel.rotKeys.size() > 1) {
                    std::tie(chState.stepRot, rot) = animateChannel(time, chState.stepRot, channel.rotKeys);
                } else {
                    rot = channel.rotKeys[0].val;
                }

                glm::vec3 scl;
                if (channel.sclKeys.size() > 1) {
                    std::tie(chState.stepScl, scl) = animateChannel(time, chState.stepScl, channel.sclKeys);
                } else {
                    scl = channel.sclKeys[0].val;
                }

                glm::mat4 m(1.f);
                m *= glm::translate(pos);
                m *= glm::mat4_cast(rot);
                m *= glm::scale(scl);

                nodes[channel.nodeIdx].transform = m;
            }

            state.time = time;
        }
    }

    //--------------------------------------------------------------------------------------------------
  private:
    void print() {
        std::cerr << "_________________________________________________\n"
                  << "Scene '" << name << "':\n"
                  << "   size: " << sizeWorld << '\n';

        printNodes();
        printMeshes();
        printMaterials();
        printTextures();
        printAnimations();
    }

    void printNodes() {
        std::cerr << "Nodes (" << nodes.size() << ")\n";
        printNode(0, 0);
    }

    void printNode(uint32_t idx, uint32_t indent) {
        const auto& node = nodes[idx];

        for (uint32_t i = 0; i < indent; ++i) {
            std::cerr << ' ';
        }

        std::cerr << "└ " << node.name << " \t(meshes:";

        if (node.meshes.empty()) {
            std::cerr << " <none>";
        }

        for (uint32_t meshIdx : node.meshes) {
            std::cerr << " #" << meshIdx;
        }

        std::cerr << ")\n";

        for (uint32_t childIdx : node.children) {
            printNode(childIdx, indent + 1);
        }
    }

    void printMeshes() {
        std::cerr << "Meshes (" << meshes.size() << "):\n";

        uint32_t idx = 0;
        for (const auto& mesh : meshes) {
            std::cerr << " - mesh " << idx << ": '" << mesh.name << "'\n"
                      << "   " << mesh.vertices.size() << " vertices, " << mesh.indices.size() << " indices\n"
                      << "   material: #" << mesh.materialIdx << '\n';

            idx += 1;
        }
    }

    void printMaterials() {
        std::cerr << "Materials (" << materials.size() << "):\n";
        std::cerr << std::boolalpha;

        const auto alphaModeToStr = [](Material::AlphaMode alphaMode) {
            switch (alphaMode) {
            case Material::AlphaMode::Opaque:
                return "Opaque";
            case Material::AlphaMode::Mask:
                return "Mask";
            case Material::AlphaMode::Blend:
                return "Blend";
            }
            return "";
        };

        uint32_t idx = 0;
        for (const auto& material : materials) {

            std::cerr << " - mat " << idx << ": '" << material.name << "'\n"
                      << "   diffuse     : " << glm::to_string(material.colDiffuse) << '\n'
                      << "   specular    : " << glm::to_string(material.colSpecular) << '\n'
                      << "   ambient     : " << glm::to_string(material.colAmbient) << '\n'
                      << "   emissive    : " << glm::to_string(material.colEmissive) << '\n'
                      << "   opacity     : " << material.opacity << '\n'
                      << "   metallic    : " << material.metallic << '\n'
                      << "   roughness   : " << material.roughness << '\n'
                      << "   alphaMode   : " << alphaModeToStr(material.alphaMode) << '\n'
                      << "   alphaCutoff : " << material.alphaCutoff << '\n'
                      << "   tex diffuse     : " << material.texDiffuseIdx << "\n"
                      << "   tex metal/rough : " << material.texMetalRoughIdx << "\n"
                      << "   tex normal      : " << material.texNormalIdx << "\n";

            idx += 1;
        }
    }

    void printTextures() {
        std::cerr << "Textures (" << textures.size() << "):\n";

        uint32_t idx = 0;
        for (const auto& texture : textures) {
            std::cerr << " - tex " << idx << ": '" << texture.name << "'\n"
                      << "    " << texture.width << 'x' << texture.height << ", " << texture.mipLevels << " mip, "
                      << (texture.isSRGB ? "SRGB" : "Linear") << "\n";

            idx += 1;
        }
    }

    void printAnimations() {
        std::cerr << "Animations (" << animations.size() << ")\n";

        uint32_t idx = 0;
        for (const auto& animation : animations) {
            std::cerr << " - anim " << idx << ": '" << animation.name << "' (" << animation.durationSecs << " secs, "
                      << animation.channels.size() << " channels)\n";

            for (const auto& channel : animation.channels) {
                std::cerr << "   > #" << channel.nodeIdx << " '" << nodes[channel.nodeIdx].name
                          << "' : " << channel.posKeys.size() << " pos, " << channel.rotKeys.size() << " rot, "
                          << channel.sclKeys.size() << " scl\n";
            }

            idx += 1;
        }
    }

  public:
    static bool loadHDRI(const std::string& path, std::vector<float>& outData, uint32_t& outW, uint32_t& outH) {
        int x, y, n;
        float* data = stbi_loadf(Fs::findPath(path).string().c_str(), &x, &y, &n, 4);
        if (data == nullptr) {
            return false;
        }

        outData.assign(data, data + x * y * 4);
        outW = x;
        outH = y;

        stbi_image_free(data);

        return true;
    }
};
