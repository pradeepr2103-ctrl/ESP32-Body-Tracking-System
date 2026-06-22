#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

#include "MathTypes.h"
#include "Model.h"
#include "MotionRecorder.h"

namespace mocap {

class Renderer {
public:
    Renderer() = default;

    // Do NOT call GL in destructor — context may be gone.
    // Call shutdown() explicitly.
    ~Renderer() = default;

    bool init(const std::string& title = "ESP32 Body Tracking - Bharatanatyam Capture",
              int w = 900, int h = 700);

    void render(const Model& model, const Pose& pose);
    void renderCompare(const Model& model, const Pose& poseA, const Pose& poseB);

    bool shouldClose() const;
    void pollEvents();
    bool isKeyPressed(int glfwKey) const;

    // Call this BEFORE model.destroy() and BEFORE going out of scope
    void shutdown();

private:
    GLFWwindow*  window_        = nullptr;
    unsigned int prog_          = 0;
    int          locMVP_        = -1;
    int          locBones_      = -1;
    int          locAlbedo_     = -1;
    int          locSkinEnabled_= -1;

    void drawSkeleton(const Model& model, const Pose& pose,
                      float xOffset, Vec3 albedo);
};

} // namespace mocap
