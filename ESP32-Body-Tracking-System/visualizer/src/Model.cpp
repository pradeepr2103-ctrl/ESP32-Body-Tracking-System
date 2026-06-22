// Model.cpp
// Fix: added destroy() for safe GL cleanup before context destruction.
//      indexCount_ stored in uploadToGPU() so draw() works after indices_ is cleared.

#define GLFW_INCLUDE_NONE
#include <glad/gl.h>

#include "Model.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstdio>
#include <algorithm>

namespace mocap {

static Mat4 fromAiMat(const aiMatrix4x4& m) {
    Mat4 r{};
    r.m[0][0]=m.a1; r.m[1][0]=m.a2; r.m[2][0]=m.a3; r.m[3][0]=m.a4;
    r.m[0][1]=m.b1; r.m[1][1]=m.b2; r.m[2][1]=m.b3; r.m[3][1]=m.b4;
    r.m[0][2]=m.c1; r.m[1][2]=m.c2; r.m[2][2]=m.c3; r.m[3][2]=m.c4;
    r.m[0][3]=m.d1; r.m[1][3]=m.d2; r.m[2][3]=m.d3; r.m[3][3]=m.d4;
    return r;
}

// Call before glfwTerminate() — frees GPU resources while context still alive
void Model::destroy() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_);      ebo_ = 0; }
    indexCount_ = 0;
}

bool Model::loadFromFile(const std::string& path) {
    Assimp::Importer imp;
    const aiScene* scene = imp.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals  |
        aiProcess_LimitBoneWeights
    );

    if (!scene || !scene->mRootNode) {
        fprintf(stderr, "Assimp error: %s\n", imp.GetErrorString());
        return false;
    }

    // Collect all bone names first
    for (unsigned int mi = 0; mi < scene->mNumMeshes; mi++) {
        aiMesh* mesh = scene->mMeshes[mi];
        for (unsigned int bi = 0; bi < mesh->mNumBones; bi++) {
            std::string name(mesh->mBones[bi]->mName.C_Str());
            if (boneNameToIndex_.find(name) == boneNameToIndex_.end()) {
                int idx = (int)bones_.size();
                boneNameToIndex_[name] = idx;
                BoneInfo info;
                info.name = name;
                info.inverseBindMatrix = fromAiMat(mesh->mBones[bi]->mOffsetMatrix);
                info.parentBoneIndex = -1;
                bones_.push_back(info);
            }
        }
    }

    buildBoneHierarchy(scene->mRootNode, scene, -1);
    processNode(scene->mRootNode, scene);

    if (vertices_.empty()) {
        fprintf(stderr, "Model: no vertices in %s\n", path.c_str());
        return false;
    }

    uploadToGPU();

    printf("Model: loaded '%s' -- %zu vertices, %zu triangles, %d bones.\n",
           path.c_str(), vertices_.size(), indices_.size()/3, (int)bones_.size());

    // Free CPU-side data after GPU upload (optional, saves RAM)
    vertices_.clear(); vertices_.shrink_to_fit();
    indices_.clear();  indices_.shrink_to_fit();

    return true;
}

void Model::buildBoneHierarchy(const aiNode* node, const aiScene*, int parentIdx) {
    if (!node) return;
    std::string name(node->mName.C_Str());
    int myIdx = parentIdx;
    auto it = boneNameToIndex_.find(name);
    if (it != boneNameToIndex_.end()) {
        myIdx = it->second;
        if (parentIdx >= 0 && bones_[myIdx].parentBoneIndex < 0)
            bones_[myIdx].parentBoneIndex = parentIdx;
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++)
        buildBoneHierarchy(node->mChildren[i], nullptr, myIdx);
}

void Model::processNode(const aiNode* node, const aiScene* scene) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
        processMesh(scene->mMeshes[node->mMeshes[i]], scene);
    for (unsigned int i = 0; i < node->mNumChildren; i++)
        processNode(node->mChildren[i], scene);
}

void Model::processMesh(aiMesh* mesh, const aiScene*) {
    unsigned int base = (unsigned int)vertices_.size();

    struct WA { int idx[4]={0}; float w[4]={0}; int n=0; };
    std::vector<WA> accum(mesh->mNumVertices);

    for (unsigned int bi = 0; bi < mesh->mNumBones; bi++) {
        aiBone* bone = mesh->mBones[bi];
        auto it = boneNameToIndex_.find(std::string(bone->mName.C_Str()));
        if (it == boneNameToIndex_.end()) continue;
        int boneIdx = it->second;
        for (unsigned int wi = 0; wi < bone->mNumWeights; wi++) {
            unsigned int vId = bone->mWeights[wi].mVertexId;
            if (vId >= mesh->mNumVertices) continue;
            WA& a = accum[vId];
            if (a.n < 4) { a.idx[a.n]=boneIdx; a.w[a.n]=bone->mWeights[wi].mWeight; a.n++; }
        }
    }

    for (unsigned int vi = 0; vi < mesh->mNumVertices; vi++) {
        SkinnedVertex sv{};
        sv.position = {mesh->mVertices[vi].x, mesh->mVertices[vi].y, mesh->mVertices[vi].z};
        if (mesh->HasNormals())
            sv.normal = {mesh->mNormals[vi].x, mesh->mNormals[vi].y, mesh->mNormals[vi].z};
        if (mesh->mTextureCoords[0])
            sv.u = mesh->mTextureCoords[0][vi].x, sv.v = mesh->mTextureCoords[0][vi].y;
        float wsum = 0;
        for (int k=0;k<4;k++) { sv.boneIndices[k]=accum[vi].idx[k]; sv.boneWeights[k]=accum[vi].w[k]; wsum+=accum[vi].w[k]; }
        if (wsum > 1e-6f) for (int k=0;k<4;k++) sv.boneWeights[k] /= wsum;
        vertices_.push_back(sv);
    }

    for (unsigned int fi = 0; fi < mesh->mNumFaces; fi++) {
        aiFace& face = mesh->mFaces[fi];
        for (unsigned int ii = 0; ii < face.mNumIndices; ii++)
            indices_.push_back(base + face.mIndices[ii]);
    }
}

void Model::uploadToGPU() {
    if (vertices_.empty() || indices_.empty()) return;

    // *** Store count BEFORE clearing vectors ***
    indexCount_ = (unsigned int)indices_.size();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices_.size() * sizeof(SkinnedVertex)),
                 vertices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices_.size() * sizeof(unsigned int)),
                 indices_.data(), GL_STATIC_DRAW);

    // location 0: position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, position));
    // location 1: normal (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, normal));
    // location 2: uv (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, u));
    // location 3: bone indices (ivec4) — must use IPointer
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(SkinnedVertex),
                           (void*)offsetof(SkinnedVertex, boneIndices));
    // location 4: bone weights (vec4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, boneWeights));

    glBindVertexArray(0);
}

void Model::draw() const {
    if (vao_ == 0 || indexCount_ == 0) return;  // not loaded or destroyed
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, (GLsizei)indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

int Model::findBoneIndex(const std::string& name) const {
    auto it = boneNameToIndex_.find(name);
    return (it != boneNameToIndex_.end()) ? it->second : -1;
}

} // namespace mocap
