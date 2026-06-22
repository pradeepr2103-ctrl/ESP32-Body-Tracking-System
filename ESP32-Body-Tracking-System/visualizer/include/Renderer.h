#pragma once

#include <string>
#include <vector>

// Forward-declare GLFW handle without pulling in GLFW headers here
struct GLFWwindow;

#include "MathTypes.h"
#include "Model.h"
#include "MotionRecorder.h"   // for Pose / NUM_SENSORS

namespace mocap {

class Renderer {
public:
    Renderer() = default;
    ~Renderer() { shutdown(); }

    bool init(const std::string& title = "ESP32 Body Tracking - Bharatanatyam Capture",
              int w = 900, int h = 700);

    void render(const Model& model, const Pose& pose);
    void renderCompare(const Model& model, const Pose& poseA, const Pose& poseB);

    bool shouldClose() const;
    void pollEvents();
    bool isKeyPressed(int glfwKey) const;

    void shutdown();

private:
    GLFWwindow* window_  = nullptr;
    unsigned int prog_   = 0;   // GLuint

    // Uniform locations (cached after link)
    int locMVP_         = -1;
    int locBones_       = -1;
    int locAlbedo_      = -1;
    int locSkinEnabled_ = -1;

    // Internal helpers
    void drawSkeleton(const Model& model, const Pose& pose,
                      float xOffset, Vec3 albedo);

    // So drawSkeleton can access VAO
    friend class Model;
};

} // namespace mocap
