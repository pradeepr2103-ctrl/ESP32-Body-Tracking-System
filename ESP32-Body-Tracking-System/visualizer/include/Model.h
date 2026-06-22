#pragma once

#include <map>
#include <string>
#include <vector>

#include "MathTypes.h"

// Forward declarations at global scope (NOT inside namespace)
struct aiNode;
struct aiScene;
struct aiMesh;

namespace mocap {

struct SkinnedVertex {
    Vec3  position  = {0,0,0};
    Vec3  normal    = {0,1,0};
    float u = 0, v = 0;
    int   boneIndices[4]  = {0, 0, 0, 0};
    float boneWeights[4]  = {0, 0, 0, 0};
};

struct BoneInfo {
    std::string name;
    Mat4 inverseBindMatrix;
    int  parentBoneIndex = -1;
};

class Model {
public:
    Model() = default;

    // Do NOT put GL calls in destructor — GL context may already be gone.
    // Call destroy() explicitly before glfwTerminate().
    ~Model() = default;

    bool loadFromFile(const std::string& path);

    // Call this BEFORE renderer.shutdown() to free GPU resources safely
    void destroy();

    // Safe draw — does nothing if not loaded
    void draw() const;

    int  boneCount() const { return static_cast<int>(bones_.size()); }
    const std::vector<BoneInfo>& bones() const { return bones_; }
    int  findBoneIndex(const std::string& name) const;

private:
    unsigned int vao_        = 0;
    unsigned int vbo_        = 0;
    unsigned int ebo_        = 0;
    unsigned int indexCount_ = 0;   // stored separately after upload

    std::vector<SkinnedVertex>  vertices_;
    std::vector<unsigned int>   indices_;
    std::vector<BoneInfo>       bones_;
    std::map<std::string, int>  boneNameToIndex_;

    void processNode(const aiNode* node, const aiScene* scene);
    void processMesh(aiMesh* mesh, const aiScene* scene);
    void buildBoneHierarchy(const aiNode* node, const aiScene* scene, int parentIdx);
    void uploadToGPU();

    friend class Renderer;
};

} // namespace mocap
