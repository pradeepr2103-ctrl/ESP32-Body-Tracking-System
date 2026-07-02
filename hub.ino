#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps612.h"

const char* SELF_LABEL = "CHEST"; // HIPS
const char* ssid       = "TP-Link_DF6C_Cave";
const char* password   = "Caveiot@123";

const char*  server_ip = "192.168.1.182";   // CHANGE to your PC's IP
const unsigned int port   = 5005;

const int LED_RED   = 1;
const int LED_GREEN = 0;

WiFiUDP udp;
MPU6050 mpu;
uint8_t fifoBuffer[64];

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

static SensorData bufA[8], bufB[8];
static volatile SensorData* writeBuf = bufA;
static SensorData* readBuf = bufB;
static volatile int writeCount = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

uint8_t realChannel = 1;

void onEspNowReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(SensorData)) {
    portENTER_CRITICAL_ISR(&mux);
    if (writeCount < 8) {
      memcpy((void*)&writeBuf[writeCount], incomingData, sizeof(SensorData));
      writeCount++;
    }
    portEXIT_CRITICAL_ISR(&mux);
    return;
  }

  if (len == sizeof(ProbePacket)) {
    ProbePacket probe;
    memcpy(&probe, incomingData, sizeof(probe));
    if (memcmp(probe.magic, "WHO?", 4) != 0) return;

    esp_now_peer_info_t replyPeer = {};
    memcpy(replyPeer.peer_addr, info->src_addr, 6);
    replyPeer.channel = realChannel;
    replyPeer.encrypt = false;
    if (esp_now_add_peer(&replyPeer) == ESP_OK) {
      ProbeReply reply;
      memcpy(reply.magic, "HERE", 4);
      reply.channel = realChannel;
      esp_now_send(info->src_addr, (uint8_t*)&reply, sizeof(reply));
      esp_now_del_peer(info->src_addr);
    }
  }
}

void forwardPacket(const char* label, float qw, float qx, float qy, float qz) {
  char payload[64];
  snprintf(payload, sizeof(payload), "%s,%.4f,%.4f,%.4f,%.4f", label, qw, qx, qy, qz);
  udp.beginPacket(server_ip, port);
  udp.print(payload);
  udp.endPacket();
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println(" Failed!");
      return;
    }
  }
  Serial.println(" Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Sending to server IP: ");
  Serial.println(server_ip);
}

void setup() {
  Serial.begin(115200);
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

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    realChannel = WiFi.channel();
    digitalWrite(LED_GREEN, LOW);
    delay(2000);
    digitalWrite(LED_GREEN, HIGH);

    // TEST PACKET - verify UDP works
    Serial.println("Sending test UDP packet...");
    forwardPacket("TEST", 1.0, 0.0, 0.0, 0.0);
  } else {
    Serial.println("WiFi not connected, check credentials.");
  }

  if (mpu.dmpInitialize() == 0) {
    mpu.setDMPEnabled(true);
  } else {
    while (1) {
      digitalWrite(LED_RED, LOW);   delay(100);
      digitalWrite(LED_RED, HIGH);  delay(100);
    }
  }

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onEspNowReceive);

  // No need to call udp.begin() for sending – it's optional.
  // udp.begin(server_ip, port);   // remove this line
}

void loop() {
  static unsigned long lastOwnSend = 0;
  static unsigned long lastHB = 0;
  static unsigned long lastChannelRefresh = 0;
  static bool hbOn = false;
  static unsigned long hbStart = 0;

  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_GREEN, HIGH);
    connectWiFi();
    realChannel = WiFi.channel();
    return;
  }

  if (now - lastChannelRefresh >= 5000) {
    lastChannelRefresh = now;
    realChannel = WiFi.channel();
  }

  if (!hbOn && now - lastHB >= 1000) {
    digitalWrite(LED_RED, LOW);
    hbOn = true; hbStart = now; lastHB = now;
  }
  if (hbOn && now - hbStart >= 150) {
    digitalWrite(LED_RED, HIGH);
    hbOn = false;
  }

  if (now - lastOwnSend >= 15) {
    if (mpu.getFIFOCount() >= 1024) {
      mpu.resetFIFO();
    } else if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
      lastOwnSend = now;
      Quaternion q;
      mpu.dmpGetQuaternion(&q, fifoBuffer);
      forwardPacket(SELF_LABEL, q.w, q.x, q.y, q.z);
    }
  }

  int count = 0;
  portENTER_CRITICAL(&mux);
  count = writeCount;
  writeCount = 0;
  volatile SensorData* tmp = writeBuf;
  writeBuf = (volatile SensorData*)readBuf;
  readBuf = (SensorData*)tmp;
  portEXIT_CRITICAL(&mux);

  for (int i = 0; i < count; i++) {
    forwardPacket(readBuf[i].label, readBuf[i].qw, readBuf[i].qx, readBuf[i].qy, readBuf[i].qz);
  }

  delay(1);
}
