# Firmware notes

## Flashing all 10 nodes

1. Open `esp32_node.ino` in Arduino IDE (or PlatformIO).
2. Install libraries: `MPU6050` (Electronic Cats or jrowberg i2cdevlib fork)
   and its `I2Cdevlib-MPU6050` dependency.
3. Select board: **ESP32C3 Dev Module**.
4. For each of the 10 boards:
   - Set `#define SENSOR_ID` to that board's index (0-9).
   - Set `WIFI_SSID` / `WIFI_PASSWORD` / `PC_IP_ADDRESS` (same for all boards
     unless your network setup differs).
   - Flash.
   - Label the physical board with its sensor ID (tape + marker is fine) so
     you don't mix them up when strapping the suit on.

## Per-sensor calibration

Each MPU-6050 chip has slightly different manufacturing offsets. For best
drift behavior, run the `IMU_Zero` example sketch (ships with the MPU6050
library) on each board once, with the sensor flat and still, and copy its
six offset values into the `setXAccelOffset` / `setYAccelOffset` / ...
calls in `esp32_node.ino` for that board before final flashing.

Skipping this step still works -- the DMP's runtime `CalibrateAccel` /
`CalibrateGyro` calls in `setupMPU()` do a coarse auto-calibration on every
boot -- but a proper one-time per-chip calibration noticeably reduces
residual drift during stillness, which is the issue called out as an open
challenge for this project.

## Sensor ID -> body part mapping

Defined in the comment block at the top of `esp32_node.ino`. Keep this
mapping consistent with `SENSOR_BONE_MAP` in
`visualizer/include/Skeleton.h` on the PC side -- if you change one, change
the other.

## Network

All 10 nodes are UDP *senders only* in this design -- they don't need to
receive anything back, which keeps the firmware simple and avoids needing
sequence-numbered acks. The PC-side receiver in `visualizer/src/Network.cpp`
listens on a single UDP port and distinguishes nodes by the `sensorId`
field in each packet, so all 10 boards can safely target the same
`PC_IP_ADDRESS:PC_UDP_PORT`.

If you see choppy motion only on specific limbs, check that board's WiFi
signal strength first -- weak RSSI causes UDP packet loss before it causes
anything else.
