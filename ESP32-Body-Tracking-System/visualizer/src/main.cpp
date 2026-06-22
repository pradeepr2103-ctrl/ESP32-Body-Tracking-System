// main.cpp — ESP32 Bharatanatyam Body Tracking Visualizer
// Fix: removed #include <glad/gl.h> (Renderer.h handles GL includes)
//      shutdown order: renderer.shutdown() BEFORE model goes out of scope

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "Network.h"
#include "MotionRecorder.h"
#include "Model.h"
#include "Renderer.h"
#include "Skeleton.h"

using namespace mocap;

static std::string makeFilename(const char* prefix) {
    time_t t = time(nullptr);
    struct tm* tm_s = localtime(&t);
    char buf[128];
    snprintf(buf, sizeof(buf), "recordings/%s_%04d%02d%02d_%02d%02d%02d.csv",
             prefix,
             tm_s->tm_year+1900, tm_s->tm_mon+1, tm_s->tm_mday,
             tm_s->tm_hour, tm_s->tm_min, tm_s->tm_sec);
    return buf;
}

struct KeyState {
    bool prev = false, curr = false;
    void update(bool p) { prev = curr; curr = p; }
    bool justPressed() const { return curr && !prev; }
};

int main(int argc, char** argv) {
    system("mkdir -p recordings");

    bool compareMode = (argc == 3);
    std::string csvA, csvB;
    if (compareMode) {
        csvA = argv[1];
        csvB = argv[2];
        printf("Compare mode: %s  vs  %s\n", csvA.c_str(), csvB.c_str());
    }

    // ── Init renderer FIRST (creates GL context) ──────────────────────────────
    Renderer renderer;
    if (!renderer.init("ESP32 Body Tracking - Bharatanatyam Capture", 900, 700)) {
        fprintf(stderr, "Renderer init failed\n");
        return 1;
    }

    // ── Load model AFTER GL context exists ───────────────────────────────────
    // Model uploads to GPU in loadFromFile(), needs active GL context
    Model model;
    const char* GLB_PATH = "visualizer/assets/models/human.glb";
    if (!model.loadFromFile(GLB_PATH)) {
        fprintf(stderr, "Warning: could not load %s\n", GLB_PATH);
    } else {
        printf("Model: loaded '%s' -- %d bones\n", GLB_PATH, model.boneCount());
    }

    // ── Networking ────────────────────────────────────────────────────────────
    UdpReceiver udp;
    if (!compareMode) {
        udp.start(5005);
        printf("UdpReceiver: listening on UDP port 5005\n");
    }

    printf("Live view running. Controls: R=record S=stop L=load SPACE=play/pause Arrows=seek ESC=quit\n");

    // ── Recorders ─────────────────────────────────────────────────────────────
    MotionRecorder recLive, recA, recB;
    std::string lastSavedFile;

    if (compareMode) {
        if (!recA.loadRecording(csvA) || !recB.loadRecording(csvB)) {
            fprintf(stderr, "Failed to load compare CSVs\n");
            renderer.shutdown();
            return 1;
        }
        recA.play(); recB.play();
    }

    // ── Key state ─────────────────────────────────────────────────────────────
    KeyState kR, kS, kL, kSpace, kLeft, kRight;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        double now = glfwGetTime();

        kR.update(renderer.isKeyPressed(GLFW_KEY_R));
        kS.update(renderer.isKeyPressed(GLFW_KEY_S));
        kL.update(renderer.isKeyPressed(GLFW_KEY_L));
        kSpace.update(renderer.isKeyPressed(GLFW_KEY_SPACE));
        kLeft.update(renderer.isKeyPressed(GLFW_KEY_LEFT));
        kRight.update(renderer.isKeyPressed(GLFW_KEY_RIGHT));

        if (!compareMode) {
            // Ingest UDP
            for (auto& s : udp.getAllSamples()) {
                if (s.sensorId >= 0 && s.sensorId < NUM_SENSORS)
                    recLive.onSensorUpdate(s.sensorId, s.qw, s.qx, s.qy, s.qz);
            }

            if (kR.justPressed()) {
                if (!recLive.isRecording()) {
                    recLive.startRecording();
                    printf("Recording started...\n");
                } else {
                    auto fn = makeFilename("take");
                    int n = recLive.stopRecording(fn);
                    lastSavedFile = fn;
                    printf("Recording saved to %s (%d frames)\n", fn.c_str(), n);
                }
            }
            if (kS.justPressed() && recLive.isRecording()) {
                auto fn = makeFilename("take");
                recLive.stopRecording(fn);
                lastSavedFile = fn;
                printf("Stopped.\n");
            }
            if (kL.justPressed()) {
                if (lastSavedFile.empty())
                    printf("No recording yet — press R twice first.\n");
                else if (recLive.loadRecording(lastSavedFile))
                    printf("Loaded %s\n", lastSavedFile.c_str());
            }
            if (kSpace.justPressed()) {
                if (recLive.isPlaying()) { recLive.pause(); printf("Paused.\n"); }
                else                     { recLive.play();  printf("Playing...\n"); }
            }
            if (kLeft.justPressed())  recLive.seek(recLive.currentTime() - 1.0f);
            if (kRight.justPressed()) recLive.seek(recLive.currentTime() + 1.0f);

            recLive.update(now);
            renderer.render(model, recLive.pose);

        } else {
            if (kSpace.justPressed()) {
                if (recA.isPlaying()) { recA.pause(); recB.pause(); printf("Paused.\n"); }
                else                  { recA.play();  recB.play();  printf("Playing...\n"); }
            }
            if (kLeft.justPressed())  { recA.seek(recA.currentTime()-1); recB.seek(recB.currentTime()-1); }
            if (kRight.justPressed()) { recA.seek(recA.currentTime()+1); recB.seek(recB.currentTime()+1); }
            recA.update(now); recB.update(now);
            renderer.renderCompare(model, recA.pose, recB.pose);
        }
    }

    // ── CRITICAL shutdown order ───────────────────────────────────────────────
    // 1. Stop UDP (background thread)
    udp.stop();
    // 2. Free GPU resources while GL context still alive
    model.destroy();
    // 3. THEN destroy GL context (glfwTerminate inside shutdown())
    renderer.shutdown();

    printf("Shutting down.\n");
    return 0;
}
