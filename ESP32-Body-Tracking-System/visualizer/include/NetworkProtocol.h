// NetworkProtocol.h
//
// Wire format shared between the ESP32 firmware (firmware/esp32_node/
// esp32_node.ino) and this PC-side receiver. The struct layout here MUST
// match the firmware's SensorPacket byte-for-byte: same field order, same
// types, same packing. If you change one side, change the other.

#pragma once

#include <cstdint>

namespace mocap {

constexpr int kNumSensors = 10;
constexpr uint16_t kDefaultUdpPort = 5005;

#pragma pack(push, 1)
struct SensorPacket {
    uint8_t  sensorId;       // 0-9
    uint32_t seq;            // per-node sequence number, detects drops
    float    qw, qx, qy, qz; // unit quaternion, (w,x,y,z) order, DMP output
};
#pragma pack(pop)

static_assert(sizeof(SensorPacket) == 21,
              "SensorPacket layout changed -- update firmware to match!");

} // namespace mocap
