// main.cpp — ESP32 Bharatanatyam Body Tracking Visualizer
// Fixed: safe playback loop, no segfault, bounds-checked pose access

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>

#include "Network.h"
#include "MotionRecorder.h"
#include "Model.h"
#include "Renderer.h"
#include "Skeleton.h"

using namespace mocap;

// ── generate timestamped filename ────────────────────────────────────────────
static std::string makeFilename(const char* prefix) {
    time_t t = time(nullptr);
    struct tm* tm = localtime(&t);
    char buf[128];
    snprintf(buf, sizeof(buf), "recordings/%s_%04d%02d%02d_%02d%02d%02d.csv",
             prefix,
             tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

// ── key edge-detection helper ─────────────────────────────────────────────────
struct KeyState {
    bool prev = false;
    bool curr = false;
    void update(bool pressed) { prev = curr; curr = pressed; }
    bool justPressed() const  { return curr && !prev; }
};

// ── make sure recordings directory exists ─────────────────────────────────────
static void ensureDir() {
    // Simple: try to create it; ignore if exists
    system("mkdir -p recordings");
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    ensureDir();

    // ── compare mode: two CSV files as args ──────────────────────────────────
    bool compareMode = (argc == 3);
    std::string csvA, csvB;
    if (compareMode) {
        csvA = argv[1];
        csvB = argv[2];
        printf("Compare mode: %s  vs  %s\n", csvA.c_str(), csvB.c_str());
    }

    // ── networking (only in live mode) ───────────────────────────────────────
    UdpReceiver udp;
    if (!compareMode) {
        if (!udp.start(5005)) {
            fprintf(stderr, "Warning: UDP receiver failed to start\n");
        } else {
            printf("UdpReceiver: listening on UDP port 5005\n");
        }
    }

    // ── model ─────────────────────────────────────────────────────────────────
    Model model;
    const char* GLB_PATH = "visualizer/assets/models/human.glb";
    if (!model.loadFromFile(GLB_PATH)) {
        fprintf(stderr, "Warning: could not load %s — will render without mesh\n", GLB_PATH);
    } else {
        printf("Model: loaded '%s' -- %d bones\n", GLB_PATH, model.boneCount());
    }

    // ── renderer ──────────────────────────────────────────────────────────────
    Renderer renderer;
    if (!renderer.init("ESP32 Body Tracking - Bharatanatyam Capture", 900, 700)) {
        fprintf(stderr, "Renderer init failed\n");
        return 1;
    }
    printf("Live view running. Controls: R=record S=stop L=load SPACE=play/pause Arrows=seek ESC=quit\n");

    // ── motion recorders ─────────────────────────────────────────────────────
    MotionRecorder recLive;   // live capture + single-file playback
    MotionRecorder recA, recB; // compare mode

    if (compareMode) {
        if (!recA.loadRecording(csvA)) {
            fprintf(stderr, "Could not load %s\n", csvA.c_str());
            return 1;
        }
        if (!recB.loadRecording(csvB)) {
            fprintf(stderr, "Could not load %s\n", csvB.c_str());
            return 1;
        }
        recA.play();
        recB.play();
        printf("Compare playback started. SPACE=pause/resume ESC=quit\n");
    }

    // ── key state ─────────────────────────────────────────────────────────────
    KeyState kR, kS, kL, kSpace, kLeft, kRight;
    std::string lastSavedFile;
    bool wasRecording = false;

    // ── main loop ─────────────────────────────────────────────────────────────
    while (!renderer.shouldClose()) {
        renderer.pollEvents();

        double now = glfwGetTime();

        // ── key updates ───────────────────────────────────────────────────────
        kR.update(renderer.isKeyPressed(GLFW_KEY_R));
        kS.update(renderer.isKeyPressed(GLFW_KEY_S));
        kL.update(renderer.isKeyPressed(GLFW_KEY_L));
        kSpace.update(renderer.isKeyPressed(GLFW_KEY_SPACE));
        kLeft.update(renderer.isKeyPressed(GLFW_KEY_LEFT));
        kRight.update(renderer.isKeyPressed(GLFW_KEY_RIGHT));

        if (!compareMode) {
            // ── ingest UDP packets into live recorder ─────────────────────────
            auto samples = udp.getAllSamples();
            for (auto& s : samples) {
                if (s.sensorId >= 0 && s.sensorId < NUM_SENSORS) {
                    recLive.onSensorUpdate(s.sensorId, s.qw, s.qx, s.qy, s.qz);
                }
            }

            // ── R: toggle recording ───────────────────────────────────────────
            if (kR.justPressed()) {
                if (!recLive.isRecording()) {
                    recLive.startRecording();
                    printf("Recording started...\n");
                } else {
                    std::string fn = makeFilename("take");
                    int frames = recLive.stopRecording(fn);
                    lastSavedFile = fn;
                    printf("Recording saved to %s (%d frames)\n", fn.c_str(), frames);
                }
            }

            // ── S: force stop recording ───────────────────────────────────────
            if (kS.justPressed() && recLive.isRecording()) {
                std::string fn = makeFilename("take");
                int frames = recLive.stopRecording(fn);
                lastSavedFile = fn;
                printf("Recording saved to %s (%d frames)\n", fn.c_str(), frames);
            }

            // ── L: load last saved recording ──────────────────────────────────
            if (kL.justPressed()) {
                if (lastSavedFile.empty()) {
                    printf("No recording saved yet this session -- press R twice to record one first.\n");
                } else {
                    if (recLive.loadRecording(lastSavedFile)) {
                        printf("Loaded %s\n", lastSavedFile.c_str());
                    } else {
                        printf("Failed to load %s\n", lastSavedFile.c_str());
                    }
                }
            }

            // ── SPACE: play / pause ───────────────────────────────────────────
            if (kSpace.justPressed()) {
                if (recLive.isPlaying()) {
                    recLive.pause();
                    printf("Paused.\n");
                } else {
                    recLive.play();
                    printf("Playing...\n");
                }
            }

            // ── Arrow keys: seek ──────────────────────────────────────────────
            if (kLeft.justPressed())  recLive.seek(recLive.currentTime() - 1.0f);
            if (kRight.justPressed()) recLive.seek(recLive.currentTime() + 1.0f);

            // ── tick recorder ────────────────────────────────────────────────
            recLive.update(now);

        } else {
            // compare mode controls
            if (kSpace.justPressed()) {
                if (recA.isPlaying()) {
                    recA.pause(); recB.pause();
                    printf("Paused.\n");
                } else {
                    recA.play(); recB.play();
                    printf("Playing...\n");
                }
            }
            if (kLeft.justPressed()) {
                float t = recA.currentTime() - 1.0f;
                recA.seek(t); recB.seek(t);
            }
            if (kRight.justPressed()) {
                float t = recA.currentTime() + 1.0f;
                recA.seek(t); recB.seek(t);
            }

            recA.update(now);
            recB.update(now);
        }

        // ── render ────────────────────────────────────────────────────────────
        if (compareMode) {
            renderer.renderCompare(model, recA.pose, recB.pose);
        } else {
            renderer.render(model, recLive.pose);
        }
    }

    // ── cleanup ───────────────────────────────────────────────────────────────
    if (!compareMode && recLive.isRecording()) {
        std::string fn = makeFilename("take_autosave");
        recLive.stopRecording(fn);
        printf("Auto-saved recording to %s\n", fn.c_str());
    }
    udp.stop();
    renderer.shutdown();
    printf("Shutting down.\n");
    return 0;
}
