// Model.cpp
#include "Model.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstddef>
#include <iostream>
#include <unordered_map>

namespace mocap {

namespace {
Mat4 fromAssimp(const aiMatrix4x4& m) {
    // Assimp matrices are row-major; our Mat4 is column-major, so transpose
    // while copying.
    Mat4 r;
    r.m[0] = m.a1; r.m[4] = m.a2; r.m[8]  = m.a3; r.m[12] = m.a4;
    r.m[1] = m.b1; r.m[5] = m.b2; r.m[9]  = m.b3; r.m[13] = m.b4;
    r.m[2] = m.c1; r.m[6] = m.c2; r.m[10] = m.c3; r.m[14] = m.c4;
    r.m[3] = m.d1; r.m[7] = m.d2; r.m[11] = m.d3; r.m[15] = m.d4;
    return r;
}
}

Model::~Model() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Model::loadFromFile(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs |
        aiProcess_LimitBoneWeights | // caps influences at 4/vertex, matches shader
        aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Model: Assimp failed to load '" << path << "': "
                  << importer.GetErrorString() << "\n";
        return false;
    }

    if (scene->mNumMeshes == 0) {
        std::cerr << "Model: '" << path << "' contains no meshes.\n";
        return false;
    }

    // First pass: walk all meshes, collect vertices + bone weights.
    processNode(scene->mRootNode, scene);

    if (bones_.empty()) {
        std::cerr << "Model: '" << path << "' loaded but has no skinning/bone data. "
                  << "GPU skinning requires a rigged GLB (e.g. Mixamo export).\n";
        return false;
    }

    // Second pass: build parent-child relationships between bones by
    // walking the node tree and matching node names to bone names.
    buildBoneHierarchy(scene->mRootNode, scene, -1);

    uploadToGPU();

    std::cout << "Model: loaded '" << path << "' -- "
              << vertices_.size() << " vertices, "
              << indices_.size() / 3 << " triangles, "
              << bones_.size() << " bones.\n";

    return true;
}

void Model::processNode(const aiNode* node, const aiScene* scene) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh, scene);
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        processNode(node->mChildren[i], scene);
    }
}

void Model::processMesh(aiMesh* mesh, const aiScene* scene) {
    (void)scene;
    unsigned int vertexBase = static_cast<unsigned int>(vertices_.size());

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        SkinnedVertex v;
        v.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
        if (mesh->HasNormals()) {
            v.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
        }
        if (mesh->mTextureCoords[0]) {
            v.u = mesh->mTextureCoords[0][i].x;
            v.v = mesh->mTextureCoords[0][i].y;
        }
        vertices_.push_back(v);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices_.push_back(vertexBase + face.mIndices[j]);
        }
    }

    // Bone weights: Assimp exposes these per-mesh as a list of bones, each
    // with a list of (vertexId, weight) pairs -- the inverse of the layout
    // our vertex struct wants, so we scatter into per-vertex slots here.
    std::vector<int> influenceCount(mesh->mNumVertices, 0);

    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
        aiBone* bone = mesh->mBones[b];
        std::string boneName = bone->mName.C_Str();

        int boneIndex;
        auto it = boneNameToIndex_.find(boneName);
        if (it == boneNameToIndex_.end()) {
            boneIndex = static_cast<int>(bones_.size());
            BoneInfo info;
            info.name = boneName;
            info.inverseBindMatrix = fromAssimp(bone->mOffsetMatrix);
            bones_.push_back(info);
            boneNameToIndex_[boneName] = boneIndex;
        } else {
            boneIndex = it->second;
        }

        for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
            unsigned int vId = vertexBase + bone->mWeights[w].mVertexId;
            float weight = bone->mWeights[w].mWeight;

            int& slot = influenceCount[bone->mWeights[w].mVertexId];
            if (slot < 4) {
                vertices_[vId].boneIndices[slot] = boneIndex;
                vertices_[vId].boneWeights[slot] = weight;
                slot++;
            }
            // 5th+ influence on a vertex is silently dropped --
            // aiProcess_LimitBoneWeights should prevent this in practice.
        }
    }
}

void Model::buildBoneHierarchy(const aiNode* node, const aiScene* scene, int parentIdx) {
    (void)scene;
    std::string nodeName = node->mName.C_Str();

    int myBoneIdx = parentIdx;
    auto it = boneNameToIndex_.find(nodeName);
    if (it != boneNameToIndex_.end()) {
        myBoneIdx = it->second;
        bones_[myBoneIdx].parentBoneIndex = parentIdx;
    }

    int childParent = (it != boneNameToIndex_.end()) ? myBoneIdx : parentIdx;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        buildBoneHierarchy(node->mChildren[i], scene, childParent);
    }
}

int Model::findBoneIndex(const std::string& name) const {
    auto it = boneNameToIndex_.find(name);
    return it != boneNameToIndex_.end() ? it->second : -1;
}

void Model::uploadToGPU() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices_.size() * sizeof(SkinnedVertex)),
                 vertices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices_.size() * sizeof(unsigned int)),
                 indices_.data(), GL_STATIC_DRAW);

    // layout(location=0) position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                           reinterpret_cast<void*>(offsetof(SkinnedVertex, position)));

    // layout(location=1) normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                           reinterpret_cast<void*>(offsetof(SkinnedVertex, normal)));

    // layout(location=2) texcoord
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                           reinterpret_cast<void*>(offsetof(SkinnedVertex, u)));

    // layout(location=3) bone indices (integer attribute!)
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(SkinnedVertex),
                            reinterpret_cast<void*>(offsetof(SkinnedVertex, boneIndices)));

    // layout(location=4) bone weights
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                           reinterpret_cast<void*>(offsetof(SkinnedVertex, boneWeights)));

    glBindVertexArray(0);
}

void Model::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace mocap
