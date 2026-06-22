// Network.cpp
#include "Network.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace mocap {

UdpReceiver::UdpReceiver(uint16_t port) : port_(port) {}

UdpReceiver::~UdpReceiver() {
    stop();
}

bool UdpReceiver::start() {
    sockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd_ < 0) {
        std::perror("UdpReceiver: socket() failed");
        return false;
    }

    // Allow quick restart after a crash/Ctrl-C without "address already in use".
    int reuse = 1;
    setsockopt(sockFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(sockFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("UdpReceiver: bind() failed");
        close(sockFd_);
        sockFd_ = -1;
        return false;
    }

    // Receive timeout so the thread can periodically check running_ and
    // exit cleanly instead of blocking forever in recvfrom().
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms
    setsockopt(sockFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    running_.store(true);
    recvThread_ = std::thread(&UdpReceiver::receiveLoop, this);

    std::printf("UdpReceiver: listening on UDP port %u\n", port_);
    return true;
}

void UdpReceiver::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (recvThread_.joinable()) recvThread_.join();
    if (sockFd_ >= 0) {
        close(sockFd_);
        sockFd_ = -1;
    }
}

void UdpReceiver::receiveLoop() {
    SensorPacket pkt;

    while (running_.load()) {
        ssize_t n = recvfrom(sockFd_, &pkt, sizeof(pkt), 0, nullptr, nullptr);

        if (n != static_cast<ssize_t>(sizeof(pkt))) {
            // Either a timeout (n < 0, expected -- lets us recheck
            // running_) or a malformed/partial packet (wrong size). Both
            // cases: just loop again.
            continue;
        }

        if (pkt.sensorId >= kNumSensors) {
            continue; // ignore packets from misconfigured/unexpected nodes
        }

        double now = std::chrono::duration<double>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

        std::lock_guard<std::mutex> lock(mutex_);
        LatestSample& s = samples_[pkt.sensorId];

        if (s.everReceived && pkt.seq > s.lastSeq + 1) {
            s.packetsDropped += (pkt.seq - s.lastSeq - 1);
        }

        s.qw = pkt.qw;
        s.qx = pkt.qx;
        s.qy = pkt.qy;
        s.qz = pkt.qz;
        s.lastSeq = pkt.seq;
        s.lastReceivedTime = now;
        s.packetsReceived++;
        s.everReceived = true;
    }
}

LatestSample UdpReceiver::getSample(int sensorId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sensorId < 0 || sensorId >= kNumSensors) return LatestSample{};
    return samples_[sensorId];
}

std::array<LatestSample, kNumSensors> UdpReceiver::getAllSamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

} // namespace mocap
