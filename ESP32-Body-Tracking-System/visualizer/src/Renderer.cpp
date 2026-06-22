// Renderer.cpp
#include "Renderer.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>

namespace mocap {

// ---------------------------------------------------------------------------
// Helper – check if any sensor has a non‑identity quaternion
// ---------------------------------------------------------------------------
static bool anyNonIdentity(const Quat* q, int count) {
    for (int i = 0; i < count; ++i) {
        if (q[i].w != 1.0f || q[i].x != 0.0f || q[i].y != 0.0f || q[i].z != 0.0f)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
Renderer::Renderer(const std::string& glbPath) {
    // GLFW init
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window_ = glfwCreateWindow(1280, 720, "ESP32 Body Tracking - Bharatanatyam Capture", nullptr, nullptr);
    glfwMakeContextCurrent(window_);
    gladLoadGL((GLADloadfunc)glfwGetProcAddress);
    glfwSwapInterval(0);

    // Compile shaders
    skinnedShader_ = new Shader("shaders/skinned.vert", "shaders/skinned.frag");
    lineShader_    = new Shader("shaders/line.vert",    "shaders/line.frag");

    // Load the GLB model
    model_.loadFromFile(glbPath);
    std::cout << "Model loaded: " << glbPath
              << "  (" << model_.vertices().size() << " vertices, "
              << model_.indices().size() / 3 << " triangles, "
              << model_.boneCount() << " bones)" << std::endl;

    // GPU buffers for the skinned mesh
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 model_.vertices().size() * sizeof(SkinnedVertex),
                 model_.vertices().data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 model_.indices().size() * sizeof(unsigned int),
                 model_.indices().data(), GL_STATIC_DRAW);
    // Vertex attributes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)0);                  // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)(sizeof(Vec3)));     // normal
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(2, 4, GL_INT, sizeof(SkinnedVertex), (void*)(sizeof(Vec3)*2));             // bone ids
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)(sizeof(Vec3)*2 + 4*sizeof(int))); // weights
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    // GPU buffers for the skeleton lines
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);

    // Prepare bone offsets (bind pose identity – the shader will apply skinning directly)
    int boneCnt = model_.boneCount();
    boneMatrices_.resize(boneCnt);
    for (int i = 0; i < boneCnt; ++i) boneMatrices_[i].identity();

    // Enable depth test, backface culling
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    modelScaleDetected_ = true;    // we already know the model is in cm
}

Renderer::~Renderer() {
    delete skinnedShader_;
    delete lineShader_;
    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

// ---------------------------------------------------------------------------
// Frame loop helpers
// ---------------------------------------------------------------------------
bool Renderer::shouldClose() const { return glfwWindowShouldClose(window_); }
void Renderer::beginFrame() {
    glClearColor(0.15f, 0.15f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void Renderer::endFrame() {
    glfwSwapBuffers(window_);
    glfwPollEvents();
}

// ---------------------------------------------------------------------------
// FK: convert 10 sensor quaternions → bone matrices
// ---------------------------------------------------------------------------
void Renderer::computeBoneMatrices(const Quat* sensorQuats) {
    auto& bones = model_.bones();
    int boneCnt = (int)bones.size();

    // Map sensor ID → bone index (example – adjust to your actual placement)
    static const int sensorToBone[10] = {
        0, 1, 2, 3, 4,   // Hips, Spine, Spine1, Spine2, Neck
        6, 7, 8,         // LeftShoulder, LeftArm, LeftForeArm
        10, 11, 12       // RightShoulder, RightArm, RightForeArm
    };

    // If no real sensor data, upload identity → bind pose
    if (!anyNonIdentity(sensorQuats, 10)) {
        for (int i = 0; i < boneCnt; ++i)
            boneMatrices_[i].identity();
        return;
    }

    // Step 1 – set local rotations from sensors
    std::vector<Mat4> local(boneCnt);
    for (int i = 0; i < boneCnt; ++i) local[i].identity();
    for (int s = 0; s < 10; ++s) {
        int b = sensorToBone[s];
        if (b >= 0 && b < boneCnt)
            local[b] = Mat4::fromQuat(sensorQuats[s]);
    }

    // Step 2 – forward kinematics
    boneMatrices_[0] = local[0];   // root (Hips) is at world origin + its rotation
    // apply a world translation upward for cm-scale model: hip ≈ Y=95
    boneMatrices_[0].m[13] = 95.0f;  // column‑major, index 13 is the Y translation

    for (int i = 1; i < boneCnt; ++i) {
        int p = bones[i].parentBoneIndex;
        if (p >= 0)
            boneMatrices_[i] = boneMatrices_[p] * local[i];
        else
            boneMatrices_[i] = local[i];
    }
}

// ---------------------------------------------------------------------------
// Draw single skeleton (model + coloured skeleton lines)
// ---------------------------------------------------------------------------
void Renderer::drawSkeleton(const Mat4& modelMatrix, const Vec3& tint, const Vec3& lineColor) {
    // Model
    skinnedShader_->use();
    // view/projection matrix for cm-scale model
    Mat4 view = Mat4::lookAt({0, 85, 300}, {0, 80, 0}, {0, 1, 0});
    Mat4 proj = Mat4::perspective(50.0f * 3.14159f / 180.0f, 1280.0f/720.0f, 1.0f, 2000.0f);
    Mat4 mvp = proj * view * modelMatrix;
    skinnedShader_->setMat4("uModelViewProj", mvp.data());
    skinnedShader_->setVec3("uTint", tint.x, tint.y, tint.z);
    // Upload bone matrices
    for (size_t i = 0; i < boneMatrices_.size(); ++i)
        skinnedShader_->setMat4("uBoneMatrices[" + std::to_string(i) + "]", boneMatrices_[i].data());
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, model_.indices().size(), GL_UNSIGNED_INT, 0);

    // Skeleton lines
    auto& bones = model_.bones();
    std::vector<float> lineVerts;
    for (size_t i = 0; i < bones.size(); ++i) {
        int p = bones[i].parentBoneIndex;
        if (p >= 0) {
            // Get world position of parent and child from bone matrices
            float px = boneMatrices_[p].m[12], py = boneMatrices_[p].m[13], pz = boneMatrices_[p].m[14];
            float cx = boneMatrices_[i].m[12], cy = boneMatrices_[i].m[13], cz = boneMatrices_[i].m[14];
            lineVerts.insert(lineVerts.end(), {px, py, pz, cx, cy, cz});
        }
    }
    lineShader_->use();
    lineShader_->setMat4("uModelViewProj", mvp.data());
    lineShader_->setVec3("uColor", lineColor.x, lineColor.y, lineColor.z);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(float), lineVerts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_LINES, 0, lineVerts.size() / 3);
}

// ---------------------------------------------------------------------------
// Public rendering entry points
// ---------------------------------------------------------------------------
void Renderer::render(const Quat* sensorQuats) {
    beginFrame();
    computeBoneMatrices(sensorQuats);
    Mat4 identity;
    identity.identity();
    // Tint the model white, skeleton cyan
    drawSkeleton(identity, {1,1,1}, {0,1,1});
    endFrame();
}

void Renderer::renderCompare(const Quat* teacherPose, const Quat* studentPose) {
    beginFrame();

    // Teacher (blue tint, cyan lines) – shifted left by 80 cm
    computeBoneMatrices(teacherPose);
    Mat4 teacherModel = Mat4::fromTranslation({-80, 0, 0});
    drawSkeleton(teacherModel, {0.2f, 0.4f, 1.0f}, {0,1,1});

    // Student (red tint, error‑coloured lines) – shifted right by 80 cm
    computeBoneMatrices(studentPose);
    Mat4 studentModel = Mat4::fromTranslation({80, 0, 0});
    drawSkeleton(studentModel, {1.0f, 0.2f, 0.2f}, {1,0,0});

    endFrame();
}

} // namespace mocap
