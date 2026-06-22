// MotionRecorder.h
//
// Records the 10-sensor pose stream to CSV and plays it back into the
// same pose buffer the live UdpReceiver feeds -- so the renderer can't
// tell the difference between live capture and a recorded take.
//
// Integration points (see src/main.cpp for the actual wiring):
//   1. Each frame, after pulling latest samples from UdpReceiver, feed
//      them to recorder.onSensorUpdate(id, q) for all 10 sensors.
//   2. Call recorder.update(currentTimeSec) once per frame.
//   3. Read pose for rendering from recorder.pose[boneIndex] (when
//      isPlaying()) -- otherwise read live UdpReceiver samples directly.
//      main.cpp shows the simplest way to merge these into one call site.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "MathTypes.h"
#include "NetworkProtocol.h"

namespace mocap {

struct Frame {
    double timestamp = 0.0;
    std::array<Quat, kNumSensors> q;
};

enum class RecorderMode { Idle, Recording, Playing, Paused };

class MotionRecorder {
public:
    MotionRecorder();

    std::array<Quat, kNumSensors> pose;

    void onSensorUpdate(int sensorId, const Quat& q);
    void update(double currentTimeSec);

    void startRecording();
    bool stopRecording(const std::string& filepath);
    bool isRecording() const { return mode_ == RecorderMode::Recording; }
    double recordingElapsed() const;
    size_t recordedFrameCount() const { return buffer_.size(); }

    bool loadRecording(const std::string& filepath);
    void play();
    void pause();
    void stop();
    void seek(double t);
    void setSpeed(float s) { speed_ = s; }
    void setLooping(bool loop) { looping_ = loop; }

    bool isPlaying() const { return mode_ == RecorderMode::Playing; }
    bool isPaused() const { return mode_ == RecorderMode::Paused; }
    bool hasRecordingLoaded() const { return !buffer_.empty(); }
    double playbackTime() const { return playbackTime_; }
    double playbackDuration() const;

    RecorderMode mode() const { return mode_; }

private:
    RecorderMode mode_ = RecorderMode::Idle;
    std::vector<Frame> buffer_;

    double recordStartTime_ = 0.0;
    double lastUpdateTime_ = 0.0;
    double lastTickTime_ = 0.0;

    double playbackTime_ = 0.0;
    float speed_ = 1.0f;
    bool looping_ = false;

    void sampleAt(double t);
};

} // namespace mocap
