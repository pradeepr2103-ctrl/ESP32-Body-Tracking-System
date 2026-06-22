// Model.cpp — Assimp GLB loader with GPU skinning upload
// Fixed: safe draw() checks VAO/index count before calling glDrawElements

#define GLFW_INCLUDE_NONE
#include <glad/gl.h>

#include "Model.h"
#include "MathTypes.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace mocap {

// ── Mat4 from aiMatrix4x4 ─────────────────────────────────────────────────────
static Mat4 fromAiMat(const aiMatrix4x4& m) {
    Mat4 r{};
    // Assimp is row-major; our Mat4 is column-major
    r.m[0][0]=m.a1; r.m[1][0]=m.a2; r.m[2][0]=m.a3; r.m[3][0]=m.a4;
    r.m[0][1]=m.b1; r.m[1][1]=m.b2; r.m[2][1]=m.b3; r.m[3][1]=m.b4;
    r.m[0][2]=m.c1; r.m[1][2]=m.c2; r.m[2][2]=m.c3; r.m[3][2]=m.c4;
    r.m[0][3]=m.d1; r.m[1][3]=m.d2; r.m[2][3]=m.d3; r.m[3][3]=m.d4;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
Model::~Model() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
}

bool Model::loadFromFile(const std::string& path) {
    Assimp::Importer imp;
    const aiScene* scene = imp.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals  |
        aiProcess_LimitBoneWeights   // max 4 weights per vertex
    );

    if (!scene || !scene->mRootNode) {
        fprintf(stderr, "Assimp: %s\n", imp.GetErrorString());
        return false;
    }

    // ── first pass: collect all bone names and build index map ───────────────
    for (unsigned int mi = 0; mi < scene->mNumMeshes; mi++) {
        aiMesh* mesh = scene->mMeshes[mi];
        for (unsigned int bi = 0; bi < mesh->mNumBones; bi++) {
            aiBone* bone = mesh->mBones[bi];
            std::string name(bone->mName.C_Str());
            if (boneNameToIndex_.find(name) == boneNameToIndex_.end()) {
                int idx = (int)bones_.size();
                boneNameToIndex_[name] = idx;
                BoneInfo info;
                info.name = name;
                info.inverseBindMatrix = fromAiMat(bone->mOffsetMatrix);
                info.parentBoneIndex = -1;
                bones_.push_back(info);
            }
        }
    }

    // ── second pass: build parent hierarchy from scene node tree ─────────────
    buildBoneHierarchy(scene->mRootNode, scene, -1);

    // ── third pass: collect mesh geometry ─────────────────────────────────────
    processNode(scene->mRootNode, scene);

    if (vertices_.empty()) {
        fprintf(stderr, "Model: no vertices loaded from %s\n", path.c_str());
        return false;
    }

    uploadToGPU();

    printf("Model: loaded '%s' -- %zu vertices, %zu triangles, %d bones.\n",
           path.c_str(), vertices_.size(), indices_.size()/3, (int)bones_.size());
    return true;
}

void Model::buildBoneHierarchy(const aiNode* node, const aiScene*, int parentIdx) {
    if (!node) return;
    std::string name(node->mName.C_Str());

    int myIdx = parentIdx;
    auto it = boneNameToIndex_.find(name);
    if (it != boneNameToIndex_.end()) {
        myIdx = it->second;
        if (bones_[myIdx].parentBoneIndex < 0 && parentIdx >= 0) {
            bones_[myIdx].parentBoneIndex = parentIdx;
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        buildBoneHierarchy(node->mChildren[i], nullptr, myIdx);
    }
}

void Model::processNode(const aiNode* node, const aiScene* scene) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        processMesh(scene->mMeshes[node->mMeshes[i]], scene);
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene);
    }
}

void Model::processMesh(aiMesh* mesh, const aiScene*) {
    unsigned int baseVertex = (unsigned int)vertices_.size();

    // ── per-vertex bone weight accumulators ───────────────────────────────────
    struct WeightAccum {
        int   idx[4]    = {0,0,0,0};
        float weight[4] = {0,0,0,0};
        int   count     = 0;
    };
    std::vector<WeightAccum> accum(mesh->mNumVertices);

    for (unsigned int bi = 0; bi < mesh->mNumBones; bi++) {
        aiBone* bone = mesh->mBones[bi];
        std::string bname(bone->mName.C_Str());
        auto it = boneNameToIndex_.find(bname);
        if (it == boneNameToIndex_.end()) continue;
        int boneIdx = it->second;

        for (unsigned int wi = 0; wi < bone->mNumWeights; wi++) {
            unsigned int vId = bone->mWeights[wi].mVertexId;
            float w = bone->mWeights[wi].mWeight;
            if (vId >= mesh->mNumVertices) continue;
            WeightAccum& a = accum[vId];
            if (a.count < 4) {
                a.idx[a.count]    = boneIdx;
                a.weight[a.count] = w;
                a.count++;
            }
        }
    }

    // ── vertices ──────────────────────────────────────────────────────────────
    for (unsigned int vi = 0; vi < mesh->mNumVertices; vi++) {
        SkinnedVertex sv{};
        sv.position = {mesh->mVertices[vi].x,
                       mesh->mVertices[vi].y,
                       mesh->mVertices[vi].z};
        if (mesh->HasNormals()) {
            sv.normal = {mesh->mNormals[vi].x,
                         mesh->mNormals[vi].y,
                         mesh->mNormals[vi].z};
        }
        if (mesh->mTextureCoords[0]) {
            sv.u = mesh->mTextureCoords[0][vi].x;
            sv.v = mesh->mTextureCoords[0][vi].y;
        }
        const WeightAccum& a = accum[vi];
        float wsum = 0;
        for (int k=0;k<4;k++) {
            sv.boneIndices[k] = a.idx[k];
            sv.boneWeights[k] = a.weight[k];
            wsum += a.weight[k];
        }
        // normalize weights
        if (wsum > 1e-6f) {
            for (int k=0;k<4;k++) sv.boneWeights[k] /= wsum;
        }
        vertices_.push_back(sv);
    }

    // ── indices ───────────────────────────────────────────────────────────────
    for (unsigned int fi = 0; fi < mesh->mNumFaces; fi++) {
        aiFace& face = mesh->mFaces[fi];
        for (unsigned int ii = 0; ii < face.mNumIndices; ii++) {
            indices_.push_back(baseVertex + face.mIndices[ii]);
        }
    }
}

void Model::uploadToGPU() {
    if (vertices_.empty() || indices_.empty()) return;

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

    // location 0: position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, position));
    // location 1: normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, normal));
    // location 2: uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, u));
    // location 3: bone indices (INTEGER attrib)
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(SkinnedVertex),
                           (void*)offsetof(SkinnedVertex, boneIndices));
    // location 4: bone weights
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          (void*)offsetof(SkinnedVertex, boneWeights));

    glBindVertexArray(0);
    indexCount_ = (unsigned int)indices_.size();
}

void Model::draw() const {
    // SAFE: only draw if VAO and indices are valid
    if (vao_ == 0 || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, (GLsizei)indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

int Model::findBoneIndex(const std::string& name) const {
    auto it = boneNameToIndex_.find(name);
    return (it != boneNameToIndex_.end()) ? it->second : -1;
}

} // namespace mocap
