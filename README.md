# SlimeVR_IMUReconstruction

A minimal motion-capture pipeline using ESP32 + MPU6050 IMUs, the SlimeVR server for skeleton IK, and a Three.js web viewer for a Mixamo-rigged character.

## Architecture

```
┌──────────────────────┐      ESP-NOW         ┌──────────────┐      UDP       ┌───────────────────────┐
│  10× Sensor Nodes    │ ──────────────────▶  │  2× Hubs     │ ────────────▶  │   SlimeVR Server      │
│  (MPU6050 per joint) │  (per-board label)   │ (CHEST/HIPS) │  "CHEST,qw,…"  │  (core/, Java/Kotlin) │
└──────────────────────┘                      └──────────────┘                │                       │
                                                                              │  IK skeleton          │
                                                                 WebSocket    │  Reset/calibration    │
                                                                    :21110    │  Text protocol parser │
                                                                     │        └───────────────────────┘
                                                                     ▼
                                                          ┌──────────────────────┐
                                                          │  Three.js Viewer     │
                                                          │  (gui-mocap/)        │
                                                          │  Mixamo character    │
                                                          └──────────────────────┘
```

## Hardware

| Component | Quantity | Notes |
|-----------|----------|-------|
| ESP32 (any) | 12 | 2 hubs + 10 sensors |
| MPU6050 | 10 | I²C, DMP mode |

### Sensor placement (10 joints)

| Label | Joint | Hub |
|-------|-------|-----|
| `L_FA`  | Left forearm    | CHEST |
| `L_UA`  | Left upper arm  | CHEST |
| `R_FA`  | Right forearm   | CHEST |
| `R_UA`  | Right upper arm | CHEST |
| `CHEST` | Chest (hub)     | —     |
| `L_SH`  | Left shin       | HIPS  |
| `L_TH`  | Left thigh      | HIPS  |
| `R_SH`  | Right shin      | HIPS  |
| `R_TH`  | Right thigh     | HIPS  |
| `HIPS`  | Hips (hub)      | —     |

## Getting Started

### Prerequisites

- Java 17+
- Node.js 18+
- pnpm

### 1. Server

```bash
gradle wrapper

./gradlew build
./gradlew run --no-daemon
```

The server listens on:
- **UDP :5005** — receives IMU data from hubs
- **WebSocket :21110** — serves bone data to the viewer

### 2. Viewer

```bash
cd gui-mocap

npm install
npx vite dev
```

Open `http://localhost:5173` in a browser. The viewer connects to the server at `ws://<server-ip>:21110`.

### 3. Flash ESP32s

#### Hubs

Open `hub.ino` (CHEST) / `hub-hips.ino` (HIPS) in Arduino IDE. Update:
- `ssid` / `password` — your WiFi
- `server_ip` — IP of the machine running the server

#### Sensors

Open `sensor.ino`. For each of the 10 boards, set:
- `SENSOR_LABEL` — the joint label from the table above
- `HUB_MAC` — the MAC address of the hub it reports to

Then flash each board.

## Reset / Calibration

The GUI has three reset buttons:

| Button | Type | Effect |
|--------|------|--------|
| **Reset Yaw** | Yaw | Zeroes horizontal heading only |
| **Reset Full** | Full | Full T-pose calibration (instant) |
| **Reset Mounting** | Mounting | Corrects sensor-to-bone misalignment (instant) |

## Project Layout

```
├── core/                   # SlimeVR server (Kotlin/JVM)
│   └── src/main/java/.../
│       ├── tracking/trackers/udp/TrackersUDPServer.kt   ← UDP text parser
│       ├── protocol/datafeed/DataFeedHandler.kt         ← filtered bone feed
│       └── config/ResetsConfig.kt                       ← reset delays
├── gui-mocap/              # Three.js web viewer
│   ├── src/
│   │   ├── main.ts         # Entry, wires protocol ↔ scene
│   │   ├── protocol.ts     # FlatBuffers WebSocket client
│   │   └── scene.ts        # Three.js GLB renderer + bone mapping
│   ├── public/human.glb    # Mixamo-rigged character
│   └── index.html
├── hub.ino                 # Firmware — CHEST/HIPS hub (ESP-NOW → UDP)
├── sensor.ino              # Firmware — per-joint sensor node
├── solarxr-protocol/       # FlatBuffers protocol schema (submodule)
└── config.json             # Server settings
```

## Key Notes

- Sensors send `LABEL,qw,qx,qy,qz` via ESP-NOW to their hub. The hub forwards to the server via UDP **unicast** (port 5005) — no multicast.
- The server's text parser auto-registers trackers by label. Labels map to `TrackerPosition` then `BodyPart` for IK.
- The viewer receives **only bones with an active tracker** attached (filtered server-side), keeping the T-pose in the GLB intact.
- The GLB rig uses `mixamorig`-prefixed bone names without colons (e.g. `mixamorigHips`, not `mixamorig:Hips`).
