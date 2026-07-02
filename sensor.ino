#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps612.h"

// ============================================================
// PER-BOARD CONFIG — only these two lines change across the 10 sensors.
//
//   L_FA, L_UA, R_FA, R_UA  ->  HUB_MAC = CHEST  (E8:3D:C1:9C:50:14)
//   L_SH, L_TH, R_SH, R_TH  ->  HUB_MAC = HIPS   (A4:CB:8F:1E:A5:B8)
// ============================================================
const char* SENSOR_LABEL = "L_FA";
uint8_t HUB_MAC[] = {0xE8, 0x3D, 0xC1, 0x9C, 0x50, 0x14};   // CHEST
// ============================================================

const int LED_RED   = 1;
const int LED_GREEN = 0;

MPU6050 mpu;
uint8_t fifoBuffer[64];
esp_now_peer_info_t peerInfo;

#pragma pack(1)
struct SensorData {
  char label[8];
  float qw, qx, qy, qz;
};
struct ProbePacket {
  char magic[4];   // "WHO?"
};
struct ProbeReply {
  char magic[4];   // "HERE"
  uint8_t channel;
};
#pragma pack()

SensorData data;

// Discovery dwell time — conservative, per requirement that this must work
// reliably even if it's slow. 300ms/channel * 13 channels ~= 3.9s per sweep.
const unsigned long DWELL_MS = 300;

volatile bool replyReceived = false;
volatile uint8_t replyChannel = 0;

void onSend(const wifi_tx_info_t *info, esp_now_send_status_t status) {}

// Only ProbeReply packets (5 bytes) are expected during discovery.
// Ignore anything else that might land here (shouldn't happen, but
// the size check makes this safe regardless).
void onDiscoveryReply(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len != sizeof(ProbeReply)) return;
  ProbeReply reply;
  memcpy(&reply, incomingData, sizeof(reply));
  if (memcmp(reply.magic, "HERE", 4) != 0) return;
  // Confirm it's actually our hub replying, not some other sensor's hub
  // chatter leaking onto this channel.
  if (memcmp(info->src_addr, HUB_MAC, 6) != 0) return;
  replyChannel = reply.channel;
  replyReceived = true;
}

// LED pattern distinct from the existing error states: alternating
// red/green fast blink, so "still searching for hub" is visually
// distinguishable from "DMP init failed" (solid red blink) at a glance.
void blinkSearching() {
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, HIGH);
  delay(60);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, LOW);
  delay(60);
  digitalWrite(LED_GREEN, HIGH);
}

// Sweeps channels 1..13 indefinitely until the hub answers. Never falls
// back to a guessed channel — a wrong guess is worse than a slow correct
// answer, since a silently-wrong channel looks identical to "working" on
// the sensor side but produces nothing on the hub/UDP side.
uint8_t discoverHubChannel() {
  esp_now_register_recv_cb(onDiscoveryReply);

  while (true) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

      memcpy(peerInfo.peer_addr, HUB_MAC, 6);
      peerInfo.channel = ch;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);   // re-added each attempt; channel differs each time

      replyReceived = false;
      ProbePacket probe;
      memcpy(probe.magic, "WHO?", 4);
      esp_now_send(HUB_MAC, (uint8_t*)&probe, sizeof(probe));

      unsigned long start = millis();
      while (millis() - start < DWELL_MS) {
        if (replyReceived) {
          uint8_t found = replyChannel;
          esp_now_del_peer(HUB_MAC);
          return found;
        }
        blinkSearching();
      }
      esp_now_del_peer(HUB_MAC);
    }
    // Completed a full 1..13 sweep with no reply — hub may not be booted
    // yet, or still associating to the AP. Loop and try the whole sweep
    // again rather than giving up.
  }
}

void setup() {
  Wire.begin(8, 9);
  pinMode(LED_RED,   OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED,   HIGH);
  digitalWrite(LED_GREEN, HIGH);

  for (int i = 0; i < 11; i++) {
    digitalWrite(LED_RED,   (i % 2) ? HIGH : LOW);
    digitalWrite(LED_GREEN, (i % 2) ? LOW  : HIGH);
    delay(500);
  }
  digitalWrite(LED_RED,   HIGH);
  digitalWrite(LED_GREEN, HIGH);

  mpu.initialize();
  mpu.CalibrateGyro(6);
  mpu.CalibrateAccel(6);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    while (1) { digitalWrite(LED_RED, LOW); delay(100); digitalWrite(LED_RED, HIGH); delay(100); }
  }

  // --- Discovery phase: find which channel the hub is actually on ---
  uint8_t ch = discoverHubChannel();

  // Lock onto the discovered channel and switch into normal send mode.
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_now_register_send_cb(onSend);

  memcpy(peerInfo.peer_addr, HUB_MAC, 6);
  peerInfo.channel = ch;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    while (1) { digitalWrite(LED_RED, LOW); delay(100); digitalWrite(LED_RED, HIGH); delay(100); }
  }

  if (mpu.dmpInitialize() == 0) {
    mpu.setDMPEnabled(true);
  } else {
    while (1) { digitalWrite(LED_RED, LOW); delay(100); digitalWrite(LED_RED, HIGH); delay(100); }
  }

  strncpy(data.label, SENSOR_LABEL, 7);
  data.label[7] = 0;

  // Discovery success confirmation: solid green for 2s (same as before).
  digitalWrite(LED_GREEN, LOW);
  delay(2000);
  digitalWrite(LED_GREEN, HIGH);
}

void loop() {
  static unsigned long lastSend  = 0;
  static unsigned long lastHB    = 0;
  static bool          hbOn      = false;
  static unsigned long hbStart   = 0;

  unsigned long now = millis();

  if (!hbOn && now - lastHB >= 1000) {
    digitalWrite(LED_RED, LOW);
    hbOn = true; hbStart = now; lastHB = now;
  }
  if (hbOn && now - hbStart >= 150) {
    digitalWrite(LED_RED, HIGH);
    hbOn = false;
  }

  if (now - lastSend < 15) return;
  if (!mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) return;
  lastSend = now;

  Quaternion q;
  mpu.dmpGetQuaternion(&q, fifoBuffer);
  data.qw = q.w; data.qx = q.x; data.qy = q.y; data.qz = q.z;
  esp_now_send(HUB_MAC, (uint8_t*)&data, sizeof(data));
}
