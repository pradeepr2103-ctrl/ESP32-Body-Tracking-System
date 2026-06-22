// main.cpp
//
// Application entry point. Wires together:
//   - UdpReceiver: live quaternion stream from the 10 ESP32 nodes
//   - MotionRecorder: record live stream to CSV / play CSV back
//   - Renderer: OpenGL 3.3 GPU-skinned humanoid driven by either of the
//     above, depending on mode
//
// Two run modes:
//
//   1) Normal (no args): live capture + single-recording record/playback.
//        ./body_tracking_visualizer
//
//   2) Compare mode (two CSV paths as args): loads both recordings and
//      plays them back side by side, in lockstep, for visually comparing
//      two takes (e.g. a reference performance vs a student's attempt).
//        ./body_tracking_visualizer recordings/teacher.csv recordings/student.csv
//
// Controls (focus the GL window first):
//   Normal mode:
//     R        - start/stop recording (saves to recordings/take_<N>.csv)
//     L        - load the most recently saved recording
//     SPACE    - play / pause loaded recording
//     S        - stop playback, return to live view
//     LEFT/RIGHT arrows - seek backward/forward 1 second during playback
//   Compare mode:
//     SPACE    - play / pause both recordings together
//     LEFT/RIGHT arrows - seek both recordings together, -1s/+1s
//   Both modes:
//     ESC      - quit

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>

#include "MotionRecorder.h"
#include "Network.h"
#include "NetworkProtocol.h"
#include "Renderer.h"

namespace {

std::string timestampedFilename() {
    std::time_t t = std::time(nullptr);
    std::tm tmStruct{};
    localtime_r(&t, &tmStruct);
    char buf[64];
    std::strftime(buf, sizeof(buf), "take_%Y%m%d_%H%M%S.csv", &tmStruct);
    return std::string("recordings/") + buf;
}

// --- simple edge-triggered key state so holding a key doesn't spam actions ---
struct KeyEdge {
    bool wasDown = false;
    bool pressedThisFrame(GLFWwindow* w, int key) {
        bool down = glfwGetKey(w, key) == GLFW_PRESS;
        bool edge = down && !wasDown;
        wasDown = down;
        return edge;
    }
};

} // namespace

namespace {

// Runs the side-by-side comparison of two pre-recorded CSV takes. Returns
// the process exit code.
int runCompareMode(mocap::Renderer& renderer, const std::string& pathA,
                    const std::string& pathB) {
    using namespace mocap;

    MotionRecorder takeA, takeB;

    if (!takeA.loadRecording(pathA)) {
        std::fprintf(stderr, "Failed to load '%s' -- check the path and that it's a "
                              "CSV produced by this app (header: t,s0_w,s0_x,...).\n",
                     pathA.c_str());
        return 1;
    }
    if (!takeB.loadRecording(pathB)) {
        std::fprintf(stderr, "Failed to load '%s'.\n", pathB.c_str());
        return 1;
    }

    std::printf("Loaded A: %s (%.2fs, %zu frames)\n", pathA.c_str(),
                takeA.playbackDuration(), takeA.recordedFrameCount());
    std::printf("Loaded B: %s (%.2fs, %zu frames)\n", pathB.c_str(),
                takeB.playbackDuration(), takeB.recordedFrameCount());
    std::printf("A is drawn on the LEFT (warm tone), B on the RIGHT (blue tone).\n");
    std::printf("Controls: SPACE=play/pause both, LEFT/RIGHT=seek both, ESC=quit\n");

    KeyEdge keySpace, keyLeft, keyRight;

    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        double t = renderer.getTime();
        GLFWwindow* win = renderer.window();

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        // Play/pause both takes together so they stay in lockstep --
        // driving both transports from the same key press is what keeps
        // "side by side" actually meaning "at the same point in time".
        if (keySpace.pressedThisFrame(win, GLFW_KEY_SPACE)) {
            bool nowPlaying = !takeA.isPlaying();
            if (nowPlaying) {
                takeA.play();
                takeB.play();
                std::printf("Playing both...\n");
            } else {
                takeA.pause();
                takeB.pause();
                std::printf("Paused at A=%.2fs B=%.2fs\n", takeA.playbackTime(),
                            takeB.playbackTime());
            }
        }

        if (keyLeft.pressedThisFrame(win, GLFW_KEY_LEFT)) {
            takeA.seek(takeA.playbackTime() - 1.0);
            takeB.seek(takeB.playbackTime() - 1.0);
        }
        if (keyRight.pressedThisFrame(win, GLFW_KEY_RIGHT)) {
            takeA.seek(takeA.playbackTime() + 1.0);
            takeB.seek(takeB.playbackTime() + 1.0);
        }

        takeA.update(t);
        takeB.update(t);

        renderer.renderCompare(takeA.pose, takeB.pose);
    }

    std::printf("Shutting down.\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    using namespace mocap;

    std::printf("=== ESP32 Body Tracking Visualizer ===\n");

    // Compare mode: two CSV paths given on the command line. This mode
    // doesn't need the UDP receiver at all -- it's purely two pre-recorded
    // takes played back together -- so we skip starting it.
    bool compareMode = (argc == 3);

    Renderer renderer;
    bool rendererOk = renderer.init(
        compareMode ? 1600 : 1280, 720,
        compareMode ? "ESP32 Body Tracking - Compare Takes"
                    : "ESP32 Body Tracking - Bharatanatyam Capture",
        "visualizer/assets/models/human.glb",
        "visualizer/shaders/skinned.vert",
        "visualizer/shaders/skinned.frag");

    if (!rendererOk) {
        std::fprintf(stderr, "Renderer init failed -- check the messages above. "
                              "Most likely human.glb is missing from "
                              "visualizer/assets/models/, or a bone name in "
                              "Skeleton.h doesn't match your GLB's joint names.\n");
        return 1;
    }

    if (compareMode) {
        return runCompareMode(renderer, argv[1], argv[2]);
    }

    UdpReceiver receiver(kDefaultUdpPort);
    if (!receiver.start()) {
        std::fprintf(stderr, "Failed to start UDP receiver on port %u. "
                              "Is something else using it?\n", kDefaultUdpPort);
        return 1;
    }

    MotionRecorder recorder;
    std::string lastSavedRecording;

    KeyEdge keyR, keySpace, keyS, keyL, keyLeft, keyRight;

    std::printf("Live view running. Controls: R=record S=stop L=load SPACE=play/pause "
                "Arrows=seek ESC=quit\n");
    std::printf("Tip: run with two CSV file paths as arguments to compare two takes "
                "side by side instead, e.g.:\n"
                "  ./body_tracking_visualizer recordings/teacher.csv recordings/student.csv\n");

    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        double t = renderer.getTime();

        GLFWwindow* win = renderer.window();

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        // ---- Recording toggle ----
        if (keyR.pressedThisFrame(win, GLFW_KEY_R)) {
            if (!recorder.isRecording()) {
                recorder.startRecording();
                std::printf("Recording started...\n");
            } else {
                lastSavedRecording = timestampedFilename();
                bool saved = recorder.stopRecording(lastSavedRecording);
                std::printf(saved ? "Recording saved to %s (%zu frames)\n"
                                   : "Recording stop failed (no frames captured?)\n",
                            lastSavedRecording.c_str(), recorder.recordedFrameCount());
            }
        }

        // ---- Load most recent recording ----
        if (keyL.pressedThisFrame(win, GLFW_KEY_L)) {
            if (!lastSavedRecording.empty()) {
                if (recorder.loadRecording(lastSavedRecording)) {
                    std::printf("Loaded %s (%.2fs)\n", lastSavedRecording.c_str(),
                                recorder.playbackDuration());
                } else {
                    std::printf("Failed to load %s\n", lastSavedRecording.c_str());
                }
            } else {
                std::printf("No recording saved yet this session -- press R twice to "
                            "record one first.\n");
            }
        }

        // ---- Play / pause ----
        if (keySpace.pressedThisFrame(win, GLFW_KEY_SPACE)) {
            if (recorder.hasRecordingLoaded()) {
                if (recorder.isPlaying()) {
                    recorder.pause();
                    std::printf("Paused at %.2fs\n", recorder.playbackTime());
                } else {
                    recorder.play();
                    std::printf("Playing...\n");
                }
            }
        }

        // ---- Stop playback, return to live ----
        if (keyS.pressedThisFrame(win, GLFW_KEY_S)) {
            if (recorder.isPlaying() || recorder.isPaused()) {
                recorder.stop();
                std::printf("Stopped playback. Back to live view.\n");
            }
        }

        // ---- Seek ----
        if (keyLeft.pressedThisFrame(win, GLFW_KEY_LEFT)) {
            recorder.seek(recorder.playbackTime() - 1.0);
        }
        if (keyRight.pressedThisFrame(win, GLFW_KEY_RIGHT)) {
            recorder.seek(recorder.playbackTime() + 1.0);
        }

        // ---- Feed live sensor data into the recorder every frame ----
        // (onSensorUpdate no-ops the live-pose write internally while
        // playback is active, so this is always safe to call.)
        auto samples = receiver.getAllSamples();
        for (int s = 0; s < kNumSensors; ++s) {
            const LatestSample& sample = samples[s];
            recorder.onSensorUpdate(s, Quat{sample.qw, sample.qx, sample.qy, sample.qz});
        }
        recorder.update(t);

        // ---- Render whatever recorder.pose currently holds ----
        // (live values when idle/recording, recorded+interpolated values
        // when playing/paused -- Renderer doesn't need to know which.)
        renderer.render(recorder.pose);
    }

    receiver.stop();
    std::printf("Shutting down.\n");
    return 0;
}
