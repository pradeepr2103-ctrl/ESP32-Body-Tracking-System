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
    ~Model();

    bool loadFromFile(const std::string& path);

    // Safe draw — does nothing if VAO not ready
    void draw() const;

    int  boneCount() const { return static_cast<int>(bones_.size()); }
    const std::vector<BoneInfo>& bones() const { return bones_; }
    int  findBoneIndex(const std::string& name) const;

private:
    GLuint vao_  = 0;
    GLuint vbo_  = 0;
    GLuint ebo_  = 0;
    unsigned int indexCount_ = 0;   // <-- was missing before, caused the crash

    std::vector<SkinnedVertex>  vertices_;
    std::vector<unsigned int>   indices_;
    std::vector<BoneInfo>       bones_;
    std::map<std::string, int>  boneNameToIndex_;

    void processNode(const aiNode* node, const aiScene* scene);
    void processMesh(aiMesh* mesh, const aiScene* scene);
    void buildBoneHierarchy(const aiNode* node, const aiScene* scene, int parentIdx);
    void uploadToGPU();

    // Renderer needs to call draw() and check vao_
    friend class Renderer;
};

} // namespace mocap
