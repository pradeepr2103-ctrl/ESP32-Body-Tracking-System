// Network.h
//
// Non-blocking UDP receiver for the 10-node sensor suit. Runs the actual
// socket recv() on a background thread (UDP receive can block; we don't
// want that anywhere near the render loop) and exposes a thread-safe
// "give me the latest packet for sensor N" interface to the rest of the
// app.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "NetworkProtocol.h"

namespace mocap {

struct LatestSample {
    float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
    uint32_t lastSeq = 0;
    double lastReceivedTime = 0.0; // seconds, app clock
    uint32_t packetsReceived = 0;
    uint32_t packetsDropped = 0;   // inferred from sequence gaps
    bool everReceived = false;
};

class UdpReceiver {
public:
    explicit UdpReceiver(uint16_t port = kDefaultUdpPort);
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    // Opens the socket and starts the background receive thread.
    // Returns false if the socket couldn't be bound (port in use, etc).
    bool start();
    void stop();

    // Thread-safe snapshot of the latest sample for one sensor.
    LatestSample getSample(int sensorId) const;

    // Thread-safe snapshot of all 10 sensors at once -- prefer this over
    // calling getSample() in a loop from the render thread, since it only
    // takes the lock once.
    std::array<LatestSample, kNumSensors> getAllSamples() const;

    bool isRunning() const { return running_.load(); }

private:
    void receiveLoop();

    uint16_t port_;
    int sockFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread recvThread_;

    mutable std::mutex mutex_;
    std::array<LatestSample, kNumSensors> samples_;
};

} // namespace mocap
