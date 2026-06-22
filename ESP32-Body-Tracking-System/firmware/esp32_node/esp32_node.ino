// esp32_node.ino
//
// Firmware for ONE ESP32-C3 + MPU-6050 node in the 10-node body tracking
// suit. Flash this same file to all 10 boards, changing only SENSOR_ID
// (and WIFI_SSID/PASSWORD/PC_IP if they differ per board, though normally
// they won't).
//
// Pipeline: MPU-6050 onboard DMP computes sensor fusion (accel + gyro +
// temp compensation) directly on the IMU chip and outputs a stable
// quaternion. We read that quaternion over I2C and fire it at the PC over
// UDP as a compact fixed-size binary packet -- no JSON/text parsing
// overhead on a 50-100Hz loop.
//
// Libraries required (Arduino IDE Library Manager):
//   - "MPU6050" by Electronic Cats (or jrowberg's i2cdevlib MPU6050,
//     either fork's DMP6 example API matches what's used below)
//   - "I2Cdevlib-MPU6050" (dependency of the above)
//   Board package: esp32 by Espressif Systems (select "ESP32C3 Dev Module")
//
// Wiring (per node):
//   MPU-6050 VCC -> 3.3V
//   MPU-6050 GND -> GND
//   MPU-6050 SCL -> GPIO9  (default I2C SCL on most ESP32-C3 dev boards)
//   MPU-6050 SDA -> GPIO8  (default I2C SDA on most ESP32-C3 dev boards)
//   MPU-6050 INT -> GPIO2  (DMP data-ready interrupt)
//   Adjust the pins below if your board's silkscreen differs.

#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "MPU6050_6Axis_MotionApps20.h"

// ============================================================
// PER-NODE CONFIG -- change SENSOR_ID for each of the 10 boards
// ============================================================
// Suggested mapping (adjust to your suit's actual sensor placement):
//   0 = hip/root        1 = chest/spine     2 = head
//   3 = left upper arm   4 = left forearm    5 = right upper arm
//   6 = right forearm    7 = left thigh      8 = right thigh
//   9 = (spare / left or right foot, depending on your build)
#define SENSOR_ID 0

// ============================================================
// WiFi + network config -- same for all nodes
// ============================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* PC_IP_ADDRESS = "192.168.1.100";  // Ubuntu PC's IP on the same LAN
const uint16_t PC_UDP_PORT = 5005;

// ============================================================
// I2C pins (ESP32-C3) -- adjust if your dev board differs
// ============================================================
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define MPU_INT_PIN 2

// Target send rate. DMP itself runs faster internally; we throttle
// outgoing UDP packets to this rate to avoid flooding the network with
// 10 nodes all transmitting at once.
#define SEND_RATE_HZ 50
const uint32_t SEND_INTERVAL_MS = 1000 / SEND_RATE_HZ;

// ============================================================
// Wire-format packet -- MUST match struct SensorPacket on the PC side
// (visualizer/include/NetworkProtocol.h). Keep this packed and in this
// exact field order on both ends.
// ============================================================
#pragma pack(push, 1)
struct SensorPacket {
    uint8_t  sensorId;     // which of the 10 nodes this is (0-9)
    uint32_t seq;          // packet sequence number (detects drops on PC side)
    float    qw, qx, qy, qz; // unit quaternion from DMP
};
#pragma pack(pop)

MPU6050 mpu;
WiFiUDP udp;

bool dmpReady = false;
uint8_t fifoBuffer[64];
Quaternion q;

uint32_t seqCounter = 0;
uint32_t lastSendMs = 0;

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
        // Don't hang forever if WiFi is briefly unavailable -- ESP32 will
        // keep retrying in the background after WiFi.begin() regardless.
        if (millis() - startAttempt > 15000) {
            Serial.println("\nWiFi taking a while... still trying in background.");
            startAttempt = millis();
        }
    }
    Serial.println("\nWiFi connected.");
    Serial.print("Node IP: ");
    Serial.println(WiFi.localIP());
}

void setupMPU() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz I2C, DMP needs reasonably fast bus

    mpu.initialize();
    pinMode(MPU_INT_PIN, INPUT);

    if (!mpu.testConnection()) {
        Serial.println("MPU6050 connection FAILED. Check wiring.");
        while (true) delay(1000); // halt -- no point sending garbage data
    }

    uint8_t devStatus = mpu.dmpInitialize();

    // ---- IMPORTANT: per-sensor calibration ----
    // These offsets are specific to each individual MPU-6050 chip. Run the
    // IMU_Zero calibration sketch (included with the MPU6050 library
    // examples) once per board and paste its output here. Leaving these
    // at 0 will work but will drift more than a calibrated unit.
    mpu.setXAccelOffset(0);
    mpu.setYAccelOffset(0);
    mpu.setZAccelOffset(0);
    mpu.setXGyroOffset(0);
    mpu.setYGyroOffset(0);
    mpu.setZGyroOffset(0);

    if (devStatus == 0) {
        mpu.CalibrateAccel(6);
        mpu.CalibrateGyro(6);
        mpu.setDMPEnabled(true);
        dmpReady = true;
        Serial.println("DMP ready.");
    } else {
        Serial.print("DMP init failed, code: ");
        Serial.println(devStatus);
        while (true) delay(1000);
    }
}

void sendPacket() {
    SensorPacket pkt;
    pkt.sensorId = SENSOR_ID;
    pkt.seq = seqCounter++;
    // DMP quaternion is already unit-length and in (w, x, y, z) order.
    pkt.qw = q.w;
    pkt.qx = q.x;
    pkt.qy = q.y;
    pkt.qz = q.z;

    udp.beginPacket(PC_IP_ADDRESS, PC_UDP_PORT);
    udp.write(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
    udp.endPacket();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("=== ESP32 Body Tracking Node %d ===\n", SENSOR_ID);

    connectWiFi();
    setupMPU();
    udp.begin(PC_UDP_PORT); // not strictly needed for send-only, but harmless
}

void loop() {
    if (!dmpReady) return;

    // Pull the latest quaternion out of the DMP FIFO if one is available.
    if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
        mpu.dmpGetQuaternion(&q, fifoBuffer);
    }

    uint32_t now = millis();
    if (now - lastSendMs >= SEND_INTERVAL_MS) {
        lastSendMs = now;
        sendPacket();
    }
}
