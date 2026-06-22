// Renderer.cpp
#include "Renderer.h"

#define GLFW_INCLUDE_NONE
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
    glfwSwapInterval(1);

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

    logicalToModelBoneIndex_.resize(kBoneCount, -1);
    for (int i = 0; i < kBoneCount; ++i) {
        int modelIdx = model_.findBoneIndex(kSkeletonBones[i].boneName);
        logicalToModelBoneIndex_[i] = modelIdx;
        if (modelIdx < 0) {
            std::cerr << "Renderer: WARNING -- bone '" << kSkeletonBones[i].boneName
                      << "' not found in loaded GLB.\n";
        }
    }

    // Default: all identity -- renders GLB in its own bind pose perfectly.
    // This is what shows when no ESP32 sensors are connected.
    boneMatrices_.assign(model_.boneCount(), Mat4::identity());

    // Auto-detect model scale from bounding box on first frame
    modelScaleDetected_ = false;

    return true;
}

bool Renderer::shouldClose() const { return glfwWindowShouldClose(window_); }
void Renderer::pollEvents()        { glfwPollEvents(); }
double Renderer::getTime() const   { return glfwGetTime(); }

// Check whether any sensor has received real data (non-identity quaternion).
// If all sensors are identity, no ESP32 is connected yet -- skip FK and
// render the GLB bind pose instead.
static bool anyNonIdentity(const std::array<Quat, kNumSensors>& pose) {
    for (const auto& q : pose) {
        float diff = std::fabs(q.w - 1.0f) + std::fabs(q.x) + std::fabs(q.y) + std::fabs(q.z);
        if (diff > 0.01f) return true;
    }
    return false;
}

void Renderer::computeBoneMatrices(const std::array<Quat, kNumSensors>& sensorPose) {
    // If no real sensor data yet, leave boneMatrices_ as identity so the
    // GLB renders in its bind pose. This is correct because:
    //   skinned_position = sum(boneMatrix[i] * inverseBindMatrix[i] * vertex * weight[i])
    // When boneMatrix == identity, result == inverseBindMatrix^-1 * vertex == bind pose.
    // Actually for identity skinning we want boneMatrix = bindMatrix = inv(inverseBindMatrix).
    // Simplest correct approach for no-sensor case: don't touch matrices, let shader
    // use identity (vertex shader handles zero-weight vertices as unskinned).
    if (!anyNonIdentity(sensorPose)) {
        // Reset to identity -- show bind pose
        std::fill(boneMatrices_.begin(), boneMatrices_.end(), Mat4::identity());
        return;
    }

    // --- Real sensor data received: apply FK ---
    std::vector<Quat> worldRotation(kBoneCount);
    std::vector<Vec3> worldPosition(kBoneCount);

    // Bone offsets in centimetres (model is Mixamo centimetre scale,
    // confirmed from GLB node translations: head at Y~9.3dm, top at Y~21dm)
    auto boneLocalOffset = [](int idx) -> Vec3 {
        switch (idx) {
            case 1:  return {  0.0f,  25.0f,  0.0f };  // hip  -> spine
            case 2:  return {  0.0f,  55.0f,  0.0f };  // spine -> head
            case 3:  return {-20.0f,  12.0f,  0.0f };  // spine -> L upper arm
            case 4:  return {-28.0f,   0.0f,  0.0f };  // L upper arm -> L forearm
            case 5:  return { 20.0f,  12.0f,  0.0f };  // spine -> R upper arm
            case 6:  return { 28.0f,   0.0f,  0.0f };  // R upper arm -> R forearm
            case 7:  return {-10.0f,  -8.0f,  0.0f };  // hip  -> L thigh
            case 8:  return { 10.0f,  -8.0f,  0.0f };  // hip  -> R thigh
            case 9:  return {  0.0f, -48.0f,  0.0f };  // L thigh -> L foot
            case 10: return {  0.0f, -48.0f,  0.0f };  // R thigh -> R foot
            default: return {  0.0f,   0.0f,  0.0f };
        }
    };

    for (int i = 0; i < kBoneCount; ++i) {
        const BoneMapping& bm = kSkeletonBones[i];
        Quat localRot = (bm.sensorId >= 0) ? sensorPose[bm.sensorId] : Quat{};

        if (bm.parentIndex < 0) {
            worldRotation[i] = localRot;
            worldPosition[i] = {0.0f, 95.0f, 0.0f};
        } else {
            worldRotation[i] = worldRotation[bm.parentIndex] * localRot;
            Vec3 offset = worldRotation[bm.parentIndex].rotate(boneLocalOffset(i));
            worldPosition[i] = worldPosition[bm.parentIndex] + offset;
        }
    }

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

// Build camera matrices that frame a Mixamo centimetre-scale model.
// The model's bounding box is roughly:
//   X: -30 to +30 cm (shoulders)
//   Y:   0 to 170 cm (feet to head)
//   Z: -15 to +15 cm (depth)
// We position the camera to frame this box comfortably.
static void buildCamera(float windowW, float windowH,
                         float camZ, float targetY,
                         Mat4& outView, Mat4& outProj) {
    Vec3 eye   { 0.0f, targetY, camZ };
    Vec3 target{ 0.0f, targetY,  0.0f };
    Vec3 up    { 0.0f,   1.0f,  0.0f };
    outView = Mat4::lookAt(eye, target, up);
    outProj = Mat4::perspective(
        45.0f * 3.14159265f / 180.0f,
        windowW / windowH,
        0.5f, 5000.0f);  // near/far in cm
}

void Renderer::drawSkeleton(const std::array<Quat, kNumSensors>& sensorPose,
                             const Mat4& view, const Mat4& proj, const Vec3& eye,
                             float xOffset, const Vec3& albedoTint) {
    computeBoneMatrices(sensorPose);

    Mat4 model = Mat4::fromQuatTranslation(Quat{1,0,0,0}, Vec3{xOffset, 0.0f, 0.0f});

    shader_.setMat4("uModel", model);
    shader_.setMat4("uView", view);
    shader_.setMat4("uProjection", proj);
    shader_.setMat4Array("uBoneMatrices", boneMatrices_.data(),
                          static_cast<int>(boneMatrices_.size()));
    shader_.setVec3("uViewPos", eye);
    shader_.setVec3("uLightDir",   Vec3{-0.4f, -1.0f, -0.3f});
    shader_.setVec3("uLightColor", Vec3{ 1.0f,  0.98f, 0.92f});
    shader_.setVec3("uAlbedo",     albedoTint);
    shader_.setFloat("uRoughness",       0.6f);
    shader_.setFloat("uMetalness",       0.0f);
    shader_.setFloat("uAmbientStrength", 0.20f);
    model_.draw();
}

void Renderer::render(const std::array<Quat, kNumSensors>& sensorPose) {
    beginFrame();

    // Camera sits 350 cm back, aimed at mid-body (Y=85 cm = roughly chest).
    // At 45 deg FOV and 350 cm distance, vertical frustum height at Y=0..170
    // fits comfortably with room to spare.
    Mat4 view, proj;
    Vec3 eye{0.0f, 85.0f, 350.0f};
    buildCamera(static_cast<float>(width_), static_cast<float>(height_),
                350.0f, 85.0f, view, proj);

    drawSkeleton(sensorPose, view, proj, eye, 0.0f, Vec3{0.75f, 0.55f, 0.45f});
    endFrame();
}

void Renderer::renderCompare(const std::array<Quat, kNumSensors>& poseA,
                              const std::array<Quat, kNumSensors>& poseB,
                              float separation) {
    beginFrame();

    Mat4 view, proj;
    Vec3 eye{0.0f, 85.0f, 500.0f};
    buildCamera(static_cast<float>(width_), static_cast<float>(height_),
                500.0f, 85.0f, view, proj);

    float half = separation * 0.5f;  // separation should be ~160 cm for side-by-side
    drawSkeleton(poseA, view, proj, eye, -half, Vec3{0.75f, 0.55f, 0.45f});
    drawSkeleton(poseB, view, proj, eye,  half, Vec3{0.45f, 0.60f, 0.85f});
    endFrame();
}

} // namespace mocap
