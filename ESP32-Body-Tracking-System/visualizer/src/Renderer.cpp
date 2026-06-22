// Renderer.cpp
#include "Renderer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <iostream>

namespace mocap {

Renderer::~Renderer() {
    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

void Renderer::framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

bool Renderer::init(int width, int height, const std::string& title,
                     const std::string& modelPath,
                     const std::string& vertShaderPath,
                     const std::string& fragShaderPath) {
    width_ = width;
    height_ = height;

    if (!glfwInit()) {
        std::cerr << "Renderer: glfwInit() failed.\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window_ = glfwCreateWindow(width_, height_, title.c_str(), nullptr, nullptr);
    if (!window_) {
        std::cerr << "Renderer: glfwCreateWindow() failed.\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSwapInterval(1); // vsync

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::cerr << "Renderer: gladLoadGL() failed.\n";
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    if (!shader_.loadFromFiles(vertShaderPath, fragShaderPath)) {
        std::cerr << "Renderer: shader load failed.\n";
        return false;
    }

    if (!model_.loadFromFile(modelPath)) {
        std::cerr << "Renderer: model load failed for '" << modelPath << "'.\n";
        return false;
    }

    // Resolve our logical Skeleton.h bone order against whatever bone
    // indices Assimp assigned this specific GLB.
    logicalToModelBoneIndex_.resize(kBoneCount, -1);
    for (int i = 0; i < kBoneCount; ++i) {
        int modelIdx = model_.findBoneIndex(kSkeletonBones[i].boneName);
        logicalToModelBoneIndex_[i] = modelIdx;
        if (modelIdx < 0) {
            std::cerr << "Renderer: WARNING -- bone '" << kSkeletonBones[i].boneName
                      << "' not found in loaded GLB. Check Skeleton.h names against "
                      << "your model's actual joint names.\n";
        }
    }

    boneMatrices_.assign(model_.boneCount(), Mat4::identity());

    return true;
}

bool Renderer::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

void Renderer::pollEvents() {
    glfwPollEvents();
}

double Renderer::getTime() const {
    return glfwGetTime();
}

void Renderer::computeBoneMatrices(const std::array<Quat, kNumSensors>& sensorPose) {
    // World-space (well, model-root-space) transform for each logical
    // bone, computed via forward kinematics: each bone's world rotation
    // is its sensor's quaternion composed with its parent's world
    // rotation. This intentionally keeps bone *lengths* fixed (from the
    // bind pose) and only drives *rotation* from the sensors -- exactly
    // matching what 10 orientation-only IMUs can actually give you (no
    // positional/translation data).
    std::vector<Quat> worldRotation(kBoneCount);
    std::vector<Vec3> worldPosition(kBoneCount);

    // Bind-pose local offsets are baked into the GLB; we approximate them
    // here as unit-length bone vectors along Y, scaled per segment. For a
    // production-accurate rig, replace boneLocalOffset() with the actual
    // bind-pose translation extracted from the GLB's node transforms.
    auto boneLocalOffset = [](int logicalIdx) -> Vec3 {
        switch (logicalIdx) {
            case 1: return {0, 0.25f, 0};   // spine
            case 2: return {0, 0.20f, 0};   // head
            case 3: return {-0.18f, 0.05f, 0}; // L upper arm
            case 4: return {-0.25f, 0, 0};     // L forearm
            case 5: return {0.18f, 0.05f, 0};  // R upper arm
            case 6: return {0.25f, 0, 0};      // R forearm
            case 7: return {-0.10f, -0.05f, 0}; // L thigh
            case 8: return {0.10f, -0.05f, 0};  // R thigh
            case 9: return {0, -0.45f, 0};      // L foot (FK-only)
            case 10: return {0, -0.45f, 0};     // R foot (FK-only)
            default: return {0, 0, 0};          // hip/root
        }
    };

    for (int i = 0; i < kBoneCount; ++i) {
        const BoneMapping& bm = kSkeletonBones[i];

        Quat localRot = (bm.sensorId >= 0) ? sensorPose[bm.sensorId] : Quat{};

        if (bm.parentIndex < 0) {
            worldRotation[i] = localRot;
            worldPosition[i] = {0, 0, 0};
        } else {
            worldRotation[i] = worldRotation[bm.parentIndex] * localRot;
            Vec3 offset = worldRotation[bm.parentIndex].rotate(boneLocalOffset(i));
            worldPosition[i] = worldPosition[bm.parentIndex] + offset;
        }
    }

    // Convert logical (world rotation, world position) pairs into the
    // model-bone-indexed matrix array the shader actually consumes,
    // composed with each bone's inverse bind matrix from the GLB.
    for (int i = 0; i < kBoneCount; ++i) {
        int modelIdx = logicalToModelBoneIndex_[i];
        if (modelIdx < 0) continue;

        Mat4 worldTransform = Mat4::fromQuatTranslation(worldRotation[i], worldPosition[i]);
        const Mat4& inverseBind = model_.bones()[modelIdx].inverseBindMatrix;
        boneMatrices_[modelIdx] = worldTransform * inverseBind;
    }
}

void Renderer::beginFrame() {
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader_.use();
}

void Renderer::endFrame() {
    glfwSwapBuffers(window_);
}

void Renderer::drawSkeleton(const std::array<Quat, kNumSensors>& sensorPose,
                             const Mat4& view, const Mat4& proj, const Vec3& eye,
                             float xOffset, const Vec3& albedoTint) {
    computeBoneMatrices(sensorPose);

    // Shift the whole skeleton sideways by translating the model matrix --
    // bone matrices themselves stay root-relative, so this is a single
    // cheap translation rather than re-running FK with an offset baked in.
    Mat4 model = Mat4::fromQuatTranslation(Quat{1, 0, 0, 0}, Vec3{xOffset, 0, 0});

    shader_.setMat4("uModel", model);
    shader_.setMat4("uView", view);
    shader_.setMat4("uProjection", proj);
    shader_.setMat4Array("uBoneMatrices", boneMatrices_.data(),
                          static_cast<int>(boneMatrices_.size()));

    shader_.setVec3("uViewPos", eye);
    shader_.setVec3("uLightDir", Vec3{-0.4f, -1.0f, -0.3f});
    shader_.setVec3("uLightColor", Vec3{1.0f, 0.98f, 0.92f});
    shader_.setVec3("uAlbedo", albedoTint);
    shader_.setFloat("uRoughness", 0.6f);
    shader_.setFloat("uMetalness", 0.0f);
    shader_.setFloat("uAmbientStrength", 0.15f);

    model_.draw();
}

void Renderer::render(const std::array<Quat, kNumSensors>& sensorPose) {
    beginFrame();

    Vec3 eye{0.0f, 1.0f, 3.2f};
    Vec3 target{0.0f, 0.9f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};

    Mat4 view = Mat4::lookAt(eye, target, up);
    Mat4 proj = Mat4::perspective(45.0f * 3.14159265f / 180.0f,
                                   static_cast<float>(width_) / static_cast<float>(height_),
                                   0.1f, 100.0f);

    // Skin-ish neutral tone for the single-skeleton live/playback view.
    drawSkeleton(sensorPose, view, proj, eye, 0.0f, Vec3{0.75f, 0.55f, 0.45f});

    endFrame();
}

void Renderer::renderCompare(const std::array<Quat, kNumSensors>& poseA,
                              const std::array<Quat, kNumSensors>& poseB,
                              float separation) {
    beginFrame();

    // Pull the camera back and recenter so both skeletons fit in frame --
    // wider FOV-equivalent than the single-skeleton view since we're now
    // framing two figures plus the gap between them.
    Vec3 eye{0.0f, 1.1f, 4.4f};
    Vec3 target{0.0f, 0.9f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};

    Mat4 view = Mat4::lookAt(eye, target, up);
    Mat4 proj = Mat4::perspective(50.0f * 3.14159265f / 180.0f,
                                   static_cast<float>(width_) / static_cast<float>(height_),
                                   0.1f, 100.0f);

    float half = separation * 0.5f;

    // Distinct albedo tints so the two skeletons are immediately
    // distinguishable at a glance, independent of position:
    //   A (left)  -- warm neutral skin tone, same as single-view default
    //   B (right) -- cool blue tint
    drawSkeleton(poseA, view, proj, eye, -half, Vec3{0.75f, 0.55f, 0.45f});
    drawSkeleton(poseB, view, proj, eye, half, Vec3{0.45f, 0.60f, 0.85f});

    endFrame();
}

} // namespace mocap
