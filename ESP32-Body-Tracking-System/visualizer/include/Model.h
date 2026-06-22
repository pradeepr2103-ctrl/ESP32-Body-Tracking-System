// Model.h
//
// Loads a skinned humanoid GLB (e.g. a Mixamo export) via Assimp,
// extracts vertex skinning data (bone indices/weights), uploads to a
// single VAO/VBO/EBO, and exposes a bone name -> node hierarchy so
// Renderer.cpp can drive it with live/recorded quaternions.

#pragma once

#include <glad/gl.h>

#include <map>
#include <string>
#include <vector>

#include "MathTypes.h"

struct aiNode;
struct aiScene;
struct aiMesh;

namespace mocap {

struct SkinnedVertex {
    Vec3 position;
    Vec3 normal;
    float u = 0, v = 0;
    int boneIndices[4] = {0, 0, 0, 0};
    float boneWeights[4] = {0, 0, 0, 0};
};

struct BoneInfo {
    std::string name;
    Mat4 inverseBindMatrix;  // offset matrix from Assimp (mesh-space -> bone-space)
    int parentBoneIndex = -1; // index into Model::bones_, -1 = root
};

class Model {
public:
    Model() = default;
    ~Model();

    // Loads geometry + skeleton from a .glb/.gltf file. Returns false on
    // failure (bad path, unsupported format, no skinning data found).
    bool loadFromFile(const std::string& path);

    void draw() const;

    int boneCount() const { return static_cast<int>(bones_.size()); }
    const std::vector<BoneInfo>& bones() const { return bones_; }

    // Returns the bone index for a given joint name, or -1 if not found.
    // Used by Renderer to match Skeleton.h's BoneMapping::boneName against
    // whatever this specific GLB actually calls its joints.
    int findBoneIndex(const std::string& name) const;

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    std::vector<SkinnedVertex> vertices_;
    std::vector<unsigned int> indices_;
    std::vector<BoneInfo> bones_;
    std::map<std::string, int> boneNameToIndex_;

    void processNode(const aiNode* node, const aiScene* scene);
    void processMesh(aiMesh* mesh, const aiScene* scene);
    void buildBoneHierarchy(const aiNode* node, const aiScene* scene, int parentIdx);
    void uploadToGPU();
};

} // namespace mocap
