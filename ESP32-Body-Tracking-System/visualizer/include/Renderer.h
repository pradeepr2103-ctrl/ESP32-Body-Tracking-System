// Renderer.h
//
// Owns the GL window/context, the loaded Model, and the per-frame forward
// kinematics pass that turns a flat array of 10 sensor quaternions into
// the full bone-matrix array the skinning shader needs (including the 2
// FK-only foot bones that have no physical sensor).
//
// Supports drawing either a single live skeleton (render()) or two
// skeletons side by side at a fixed world-space separation
// (renderCompare()) for comparing a reference take against a student
// take.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "MathTypes.h"
#include "Model.h"
#include "NetworkProtocol.h"
#include "Shader.h"
#include "Skeleton.h"

struct GLFWwindow;

namespace mocap {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    // Creates the GLFW window + OpenGL 3.3 core context, compiles shaders,
    // loads the model. Returns false on any failure (prints why).
    bool init(int width, int height, const std::string& title,
              const std::string& modelPath,
              const std::string& vertShaderPath,
              const std::string& fragShaderPath);

    bool shouldClose() const;
    void pollEvents();
    double getTime() const;

    // pose[i] is the quaternion for kSkeletonBones[i].sensorId (or
    // identity for FK-only bones, which get their matrix purely from
    // their parent in the hierarchy). Single-skeleton live/playback view.
    void render(const std::array<Quat, kNumSensors>& sensorPose);

    // Draws two skeletons side by side: poseA at -separation/2 on X,
    // poseB at +separation/2 on X. Used for comparing two recorded takes.
    // labelA/labelB are accepted for API clarity but only printed to
    // console on first call (no in-GL-window text rendering in this
    // build) -- see main.cpp's console output for take identification.
    void renderCompare(const std::array<Quat, kNumSensors>& poseA,
                        const std::array<Quat, kNumSensors>& poseB,
                        float separation = 1.6f);

    GLFWwindow* window() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    Shader shader_;
    Model model_;

    int width_ = 1280, height_ = 720;

    // Maps kSkeletonBones[] index -> the Model's actual bone index (since
    // the GLB's bone order from Assimp won't match our logical order).
    std::vector<int> logicalToModelBoneIndex_;

    std::vector<Mat4> boneMatrices_; // scratch buffer, reused per skeleton per frame

    // tintColor lets renderCompare() give the two skeletons distinguishable
    // albedo colors (e.g. neutral skin tone vs a blue tint) so it's
    // immediately visually obvious which is which side by side.
    void computeBoneMatrices(const std::array<Quat, kNumSensors>& sensorPose);
    void drawSkeleton(const std::array<Quat, kNumSensors>& sensorPose,
                       const Mat4& view, const Mat4& proj, const Vec3& eye,
                       float xOffset, const Vec3& albedoTint);
    void beginFrame();
    void endFrame();
    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);
};

} // namespace mocap
