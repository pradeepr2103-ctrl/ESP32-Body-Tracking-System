// compare_test.cpp
//
// Exercises the "two CSV recordings played side by side" scenario used by
// main.cpp's runCompareMode(), minus the actual OpenGL drawing -- verifies
// that two independently-recorded MotionRecorder instances can be loaded
// and played in lockstep, each producing correct, normalized, distinct
// poses, exactly like Renderer::renderCompare() would consume.
//
// Build:
//   g++ -std=c++17 -O2 -Ivisualizer/include visualizer/src/MotionRecorder.cpp
//   visualizer/test/compare_test.cpp -o compare_test
// Run:
//   ./compare_test

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "MotionRecorder.h"

using namespace mocap;

namespace {

double nowSec() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Synthesizes a recording directly (bypassing UDP) so this test has zero
// network dependency -- just exercises record -> save -> load -> playback,
// twice, with two different motion patterns standing in for "teacher take"
// vs "student take".
void synthesizeRecording(const std::string& path, float speedMultiplier, double durationSec) {
    MotionRecorder rec;
    double start = nowSec();
    rec.update(start);
    rec.startRecording();

    while (nowSec() - start < durationSec) {
        double t = (nowSec() - start) * speedMultiplier;
        for (int s = 0; s < kNumSensors; ++s) {
            float angle = static_cast<float>(t * (0.5 + 0.1 * s));
            Quat q;
            q.w = std::cos(angle * 0.5f);
            q.x = std::sin(angle * 0.5f) * 0.2f;
            q.y = std::sin(angle * 0.5f) * 0.9f;
            q.z = 0.0f;
            // Real MPU-6050 DMP output is always unit-length; normalize
            // here so this synthetic test data matches that guarantee
            // (the raw w/x/y/z above aren't unit-length before this).
            q = q.normalized();
            rec.onSensorUpdate(s, q);
        }
        rec.update(nowSec());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    bool saved = rec.stopRecording(path);
    assert(saved);
}

} // namespace

int main() {
    printf("=== Compare-mode test: two independent CSV recordings, played in lockstep ===\n");

    const std::string pathA = "/tmp/compare_test_teacher.csv";
    const std::string pathB = "/tmp/compare_test_student.csv";

    // "Teacher" take: baseline speed. "Student" take: slightly different
    // speed multiplier so the two poses visibly diverge over time --
    // standing in for a real student being a little ahead/behind/off in
    // their movement.
    synthesizeRecording(pathA, 1.0f, 0.8);
    synthesizeRecording(pathB, 1.3f, 0.8);
    printf("[ok] Synthesized two distinct recordings (teacher.csv, student.csv)\n");

    // --- Load both into independent MotionRecorder instances, exactly as
    //     runCompareMode() does. ---
    MotionRecorder takeA, takeB;
    bool loadedA = takeA.loadRecording(pathA);
    bool loadedB = takeB.loadRecording(pathB);
    printf("[%s] takeA loaded: %.2fs, %zu frames\n", loadedA ? "ok" : "FAIL",
           takeA.playbackDuration(), takeA.recordedFrameCount());
    printf("[%s] takeB loaded: %.2fs, %zu frames\n", loadedB ? "ok" : "FAIL",
           takeB.playbackDuration(), takeB.recordedFrameCount());
    assert(loadedA && loadedB);

    // --- Play both together, in lockstep, exactly as the SPACE key does
    //     in runCompareMode(). ---
    takeA.play();
    takeB.play();

    int frames = 0;
    bool sawDivergence = false;
    double playStart = nowSec();

    while ((takeA.isPlaying() || takeA.isPaused()) &&
           (takeB.isPlaying() || takeB.isPaused())) {
        double t = nowSec();
        takeA.update(t);
        takeB.update(t);

        // Both poses must stay normalized.
        for (int s = 0; s < kNumSensors; ++s) {
            const Quat& qa = takeA.pose[s];
            const Quat& qb = takeB.pose[s];
            float lenA = qa.w*qa.w + qa.x*qa.x + qa.y*qa.y + qa.z*qa.z;
            float lenB = qb.w*qb.w + qb.x*qb.x + qb.y*qb.y + qb.z*qb.z;
            assert(std::fabs(lenA - 1.0f) < 0.01f);
            assert(std::fabs(lenB - 1.0f) < 0.01f);
        }

        // Confirm the two takes are NOT identical (since they were
        // recorded with different speed multipliers) -- this is the
        // actual point of the comparison feature: the skeletons should
        // visibly differ.
        float diff = std::fabs(takeA.pose[3].y - takeB.pose[3].y);
        if (diff > 0.02f) sawDivergence = true;

        frames++;
        if (nowSec() - playStart > 3.0) break; // safety timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[%s] Played %d lockstep frames, both quaternion streams stayed normalized\n",
           frames > 0 ? "ok" : "FAIL", frames);
    printf("[%s] Take A and Take B poses visibly diverged at some point (expected, "
           "since they're different takes)\n", sawDivergence ? "ok" : "FAIL");

    assert(frames > 0);
    assert(sawDivergence);

    printf("\n=== ALL COMPARE-MODE CHECKS PASSED ===\n");
    return 0;
}
