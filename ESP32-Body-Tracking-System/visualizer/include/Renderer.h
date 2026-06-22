#pragma once
#include <string>
#include <vector>

struct GLFWwindow;
#include "MathTypes.h"
#include "Model.h"
#include "MotionRecorder.h"   // for Pose

namespace mocap {

class Renderer {
public:
    Renderer()  = default;
    ~Renderer() = default;   // NO GL calls in destructor

    bool init(const std::string& title =
                  "ESP32 Body Tracking - Bharatanatyam Capture",
              int w = 900, int h = 700);

    void render(const Model& model, const Pose& pose);
    void renderCompare(const Model& model,
                       const Pose& poseA, const Pose& poseB);

    bool shouldClose()    const;
    void pollEvents();
    bool isKeyPressed(int glfwKey) const;

    // Call before going out of scope — frees GL resources safely
    void shutdown();

private:
    GLFWwindow*  window_  = nullptr;
    unsigned int prog_    = 0;
    int locMVP_   = -1;
    int locBones_ = -1;
    int locColor_ = -1;
    int locSkin_  = -1;

    void drawSkeleton(const Model& model, const Pose& pose,
                      float xOffset, float r, float g, float b);
};

} // namespace mocap
