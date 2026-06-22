// integration_test.cpp
//
// Exercises Network.cpp + MotionRecorder.cpp together exactly as main.cpp
// does, but with simulated UDP traffic instead of real ESP32 boards and
// zero OpenGL/GLFW/Assimp dependency -- so this can be compiled and run
// anywhere to sanity-check the non-graphics core before touching the
// renderer.
//
// Build (run from project root):
//   g++ -std=c++17 -O2 -Ivisualizer/include visualizer/src/Network.cpp
//   visualizer/src/MotionRecorder.cpp visualizer/test/integration_test.cpp
//   -o integration_test -lpthread
// Run:
//   ./integration_test

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "MotionRecorder.h"
#include "Network.h"
#include "NetworkProtocol.h"

using namespace mocap;

namespace {

double nowSec() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Fires one fake UDP packet at 127.0.0.1:port, as if an ESP32 node sent it.
void sendFakePacket(int sockFd, sockaddr_in& dest, uint8_t sensorId,
                     uint32_t seq, float t) {
    SensorPacket pkt;
    pkt.sensorId = sensorId;
    pkt.seq = seq;
    float angle = t * (0.5f + 0.1f * sensorId);
    pkt.qw = std::cos(angle * 0.5f);
    pkt.qx = std::sin(angle * 0.5f) * 0.2f;
    pkt.qy = std::sin(angle * 0.5f) * 0.9f;
    pkt.qz = 0.0f;
    sendto(sockFd, &pkt, sizeof(pkt), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

} // namespace

int main() {
    const uint16_t testPort = 15005; // distinct from production 5005

    printf("=== Integration test: UDP receive -> record -> save -> load -> play ===\n");

    // --- Set up receiver (this is the real production class) ---
    UdpReceiver receiver(testPort);
    bool started = receiver.start();
    assert(started && "UdpReceiver failed to bind/start");
    printf("[ok] UdpReceiver started on port %u\n", testPort);

    // --- Set up a raw sender socket to simulate the 10 ESP32 nodes ---
    int senderFd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(senderFd >= 0);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(testPort);
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    // --- Phase 1: blast packets from all 10 "nodes" for ~1 second, while
    //     simultaneously polling getAllSamples() and feeding MotionRecorder,
    //     exactly like main.cpp's loop does. ---
    MotionRecorder recorder;
    double startT = nowSec();
    recorder.update(startT);
    recorder.startRecording();

    uint32_t seq[kNumSensors] = {0};
    int loopIterations = 0;
    while (nowSec() - startT < 1.0) {
        double t = nowSec() - startT;
        for (int s = 0; s < kNumSensors; ++s) {
            sendFakePacket(senderFd, dest, static_cast<uint8_t>(s), seq[s]++, static_cast<float>(t));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // let UDP land
        double tickT = nowSec();
        auto samples = receiver.getAllSamples();
        for (int s = 0; s < kNumSensors; ++s) {
            recorder.onSensorUpdate(s, Quat{samples[s].qw, samples[s].qx,
                                             samples[s].qy, samples[s].qz});
        }
        recorder.update(tickT);
        loopIterations++;
    }

    printf("[ok] Sent/received traffic for ~1s across %d loop iterations\n", loopIterations);

    auto finalSamples = receiver.getAllSamples();
    int receivedCount = 0;
    for (int s = 0; s < kNumSensors; ++s) {
        if (finalSamples[s].everReceived) receivedCount++;
    }
    printf("[%s] %d/%d sensors received at least one packet\n",
           receivedCount == kNumSensors ? "ok" : "FAIL", receivedCount, kNumSensors);
    assert(receivedCount == kNumSensors);

    std::string testFile = "/tmp/integration_test_recording.json";
    bool saved = recorder.stopRecording(testFile);
    printf("[%s] Recording saved (%zu frames) to %s\n",
           saved ? "ok" : "FAIL", recorder.recordedFrameCount(), testFile.c_str());
    assert(saved);
    assert(recorder.recordedFrameCount() > 50); // ~1s at decent rate should give plenty

    // --- Phase 2: load it back into a FRESH recorder instance and verify
    //     playback produces sane interpolated values. ---
    MotionRecorder playback;
    bool loaded = playback.loadRecording(testFile);
    printf("[%s] Recording loaded back, duration=%.2fs\n",
           loaded ? "ok" : "FAIL", playback.playbackDuration());
    assert(loaded);
    assert(playback.playbackDuration() > 0.5); // recorded ~1s of motion

    playback.play();
    double playStart = nowSec();
    int frames = 0;
    Quat lastSensor0Pose{};
    bool sawMovement = false;

    while (playback.isPlaying() || playback.isPaused()) {
        double t = nowSec() - playStart;
        // Feed the recorder's own clock -- update() needs monotonic time.
        playback.update(nowSec());

        Quat cur = playback.pose[0];
        // Confirm the quaternion stays normalized (sanity check on nlerp).
        float lenSq = cur.w*cur.w + cur.x*cur.x + cur.y*cur.y + cur.z*cur.z;
        assert(std::fabs(lenSq - 1.0f) < 0.01f);

        if (frames > 0 &&
            (std::fabs(cur.x - lastSensor0Pose.x) > 1e-4f ||
             std::fabs(cur.y - lastSensor0Pose.y) > 1e-4f)) {
            sawMovement = true;
        }
        lastSensor0Pose = cur;
        frames++;

        if (playback.playbackTime() >= playback.playbackDuration()) break;
        if (t > 5.0) break; // safety timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[%s] Played back %d frames, observed motion change: %s\n",
           (frames > 0 && sawMovement) ? "ok" : "FAIL", frames,
           sawMovement ? "yes" : "no");
    assert(frames > 0);
    assert(sawMovement);

    receiver.stop();
    close(senderFd);

    printf("\n=== ALL INTEGRATION CHECKS PASSED ===\n");
    return 0;
}
