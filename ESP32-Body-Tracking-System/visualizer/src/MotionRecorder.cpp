// MotionRecorder.cpp
#include "MotionRecorder.h"
#include "MiniCsv.h"

#include <algorithm>
#include <cmath>

namespace mocap {

namespace {

Quat nlerp(const Quat& a, const Quat& b, float t) {
    float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    Quat bb = b;
    if (dot < 0.0f) {
        bb.w = -bb.w; bb.x = -bb.x; bb.y = -bb.y; bb.z = -bb.z;
    }

    Quat result;
    result.w = a.w + (bb.w - a.w) * t;
    result.x = a.x + (bb.x - a.x) * t;
    result.y = a.y + (bb.y - a.y) * t;
    result.z = a.z + (bb.z - a.z) * t;

    return result.normalized();
}

} // namespace

MotionRecorder::MotionRecorder() {
    for (auto& q : pose) q = Quat{};
}

void MotionRecorder::onSensorUpdate(int sensorId, const Quat& q) {
    if (sensorId < 0 || sensorId >= kNumSensors) return;

    if (mode_ != RecorderMode::Playing && mode_ != RecorderMode::Paused) {
        pose[sensorId] = q;
    }

    if (mode_ == RecorderMode::Recording) {
        if (buffer_.empty() || lastTickTime_ - buffer_.back().timestamp > 1e-6) {
            Frame f;
            f.timestamp = lastTickTime_ - recordStartTime_;
            f.q = pose;
            buffer_.push_back(f);
        }
        buffer_.back().q[sensorId] = q;
    }
}

void MotionRecorder::update(double currentTimeSec) {
    lastTickTime_ = currentTimeSec;

    if (mode_ == RecorderMode::Playing) {
        double dt = (currentTimeSec - lastUpdateTime_) * speed_;
        if (lastUpdateTime_ <= 0.0) dt = 0.0; // first tick after play(): no jump
        playbackTime_ += dt;

        double duration = playbackDuration();
        if (duration > 0.0 && playbackTime_ > duration) {
            if (looping_) {
                playbackTime_ = std::fmod(playbackTime_, duration);
            } else {
                playbackTime_ = duration;
                mode_ = RecorderMode::Paused;
            }
        }
        sampleAt(playbackTime_);
    }

    lastUpdateTime_ = currentTimeSec;
}

void MotionRecorder::startRecording() {
    buffer_.clear();
    recordStartTime_ = lastTickTime_;
    mode_ = RecorderMode::Recording;
}

bool MotionRecorder::stopRecording(const std::string& filepath) {
    if (mode_ != RecorderMode::Recording) return false;
    mode_ = RecorderMode::Idle;
    if (buffer_.empty()) return false;
    return csv::writeRecording(filepath, buffer_, kNumSensors);
}

double MotionRecorder::recordingElapsed() const {
    if (mode_ != RecorderMode::Recording || buffer_.empty()) return 0.0;
    return lastTickTime_ - recordStartTime_;
}

bool MotionRecorder::loadRecording(const std::string& filepath) {
    std::vector<Frame> loaded;
    if (!csv::readRecording(filepath, loaded, kNumSensors)) return false;

    buffer_ = std::move(loaded);
    std::sort(buffer_.begin(), buffer_.end(),
              [](const Frame& a, const Frame& b) { return a.timestamp < b.timestamp; });

    playbackTime_ = 0.0;
    lastUpdateTime_ = 0.0;
    mode_ = RecorderMode::Paused;
    sampleAt(0.0);
    return true;
}

void MotionRecorder::play() {
    if (buffer_.empty()) return;
    if (mode_ == RecorderMode::Idle) return;
    lastUpdateTime_ = 0.0; // force no-jump on next update()
    mode_ = RecorderMode::Playing;
}

void MotionRecorder::pause() {
    if (mode_ == RecorderMode::Playing) mode_ = RecorderMode::Paused;
}

void MotionRecorder::stop() {
    mode_ = RecorderMode::Idle;
    playbackTime_ = 0.0;
}

void MotionRecorder::seek(double t) {
    double duration = playbackDuration();
    playbackTime_ = std::clamp(t, 0.0, duration > 0.0 ? duration : 0.0);
    sampleAt(playbackTime_);
}

double MotionRecorder::playbackDuration() const {
    if (buffer_.empty()) return 0.0;
    return buffer_.back().timestamp - buffer_.front().timestamp;
}

void MotionRecorder::sampleAt(double t) {
    if (buffer_.empty()) return;

    if (t <= buffer_.front().timestamp) {
        pose = buffer_.front().q;
        return;
    }
    if (t >= buffer_.back().timestamp) {
        pose = buffer_.back().q;
        return;
    }

    auto it = std::lower_bound(
        buffer_.begin(), buffer_.end(), t,
        [](const Frame& f, double val) { return f.timestamp < val; });

    size_t idx1 = static_cast<size_t>(std::distance(buffer_.begin(), it));
    size_t idx0 = idx1 > 0 ? idx1 - 1 : 0;

    const Frame& f0 = buffer_[idx0];
    const Frame& f1 = buffer_[idx1];

    double span = f1.timestamp - f0.timestamp;
    float alpha = span > 1e-9 ? static_cast<float>((t - f0.timestamp) / span) : 0.0f;

    for (int s = 0; s < kNumSensors; ++s) {
        pose[s] = nlerp(f0.q[s], f1.q[s], alpha);
    }
}

} // namespace mocap
