# SlimeVR_IMUReconstruction

## Overview

This is a full-body motion capture pipeline. Ten ESP32 sensor nodes with MPU6050 IMUs measure orientation and send data via ESP-NOW to two ESP32 hub nodes. The hubs forward this data over UDP to a Kotlin/JVM SlimeVR server, which computes a skeleton using inverse kinematics and streams the pose over WebSockets to web-based viewers. The system includes an Electron desktop GUI, a lightweight Three.js viewer, and a FlatBuffers-based protocol for all server-client communication.

---

## Modules

### 1. Firmware (`hub.ino`, `sensor.ino`)

ESP32 Arduino firmware for the two node types:

- **`sensor.ino`** — Per-joint sensor node. Runs on 10 ESP32 boards placed at body joints. Each board configures its `SENSOR_LABEL` and `HUB_MAC`. During discovery it scans WiFi channels and sends `"WHO?"` ESP-NOW probes until it finds its hub, then locks onto the channel and streams orientation data (quaternions) as binary `SensorData` packets via ESP-NOW at ~66 Hz.

- **`hub.ino`** — Central relay node. Two hubs (CHEST and HIPS positions) each have their own MPU6050. They connect to WiFi, send their own IMU data as UDP text packets (`LABEL,qw,qx,qy,qz`) to the server, and relay ESP-NOW packets received from up to 8 remote sensors via UDP unicast. They also respond to `"WHO?"` probes with `"HERE"` for channel discovery.

### 2. Application Entry Point (`src/`)

- **`ServerLauncher.kt`** — The JVM entry point. Reads `config.json`, creates a `ConfigManager` and `VRServer`, then starts the server thread.
- **`ReceiveTest.kt`** — A diagnostic utility that listens on UDP port 5005 and prints incoming text packets for debugging sensor data flow.

### 3. Core Server Library (`core/`)

The main SlimeVR server library. Handles all server-side logic:

- **`dev/slimevr/VRServer.kt`** — The main server orchestrator that starts all subsystems (UDP receiver, WebSocket API, tracking engine, etc.).
- **`dev/slimevr/tracking/trackers/udp/TrackersUDPServer.kt`** — The UDP receiver on port 5005. Parses the custom text packet format (`LABEL,qw,qx,qy,qz`) from the hubs, creates/updates `Tracker` objects mapped to body positions, and feeds them into the tracking engine.
- **`dev/slimevr/tracking/processor/`** — The skeleton IK engine. `HumanPoseManager.kt` computes the full body pose from tracker orientations, building a `HumanSkeleton` from `Bone`/`BoneType` definitions with constraints.
- **`dev/slimevr/config/`** — Configuration model classes. `ConfigManager.java` loads/saves the YAML config (server ports, filters, skeleton offsets, OSC, AutoBone, resets, etc.).
- **`dev/slimevr/protocol/`** — The FlatBuffers-based protocol layer. `ProtocolAPI.kt` routes incoming messages, `DataFeedHandler.kt` streams bone/tracker data to connected clients, and `RPCHandler.kt` handles ~30+ RPC commands (reset, calibrate, autobone, serial, firmware, settings, etc.).
- **`dev/slimevr/websocketapi/`** — WebSocket servers. `WebsocketAPI.java` serves the main protocol API on port 21110. `WebSocketVRBridge.kt` handles VR bridge connections.
- **`dev/slimevr/autobone/`** — AutoBone body proportion calibration. Automatically estimates skeleton dimensions from movement data.
- **`dev/slimevr/osc/`** — OSC/VMC protocol handlers for integrations with VRChat and other VMC-compatible applications.
- **`dev/slimevr/reset/`** — Tracker reset and calibration (yaw reset, full reset, mounting reset).
- **`dev/slimevr/serial/`** — Serial port communication for firmware flashing and provisioning.
- **`dev/slimevr/filtering/`** — IMU data prediction and smoothing filters.
- **`dev/slimevr/posestreamer/`** — BVH recording and streaming of pose data.

### 4. Minimal Web Viewer (`gui-mocap/`)

A lightweight Three.js motion capture viewer. Connects to the server WebSocket at `ws://<host>:21110`, receives FlatBuffers `DataFeedUpdate` messages with bone rotations, and renders a Mixamo-rigged 3D character (`human.glb`) in real-time. Includes buttons for reset commands (Yaw, Full, Mounting) and AutoBone controls (Record, Process, Apply). Built with Vite and TypeScript.

### 5. SolarXR Protocol (`solarxr-protocol/`)

A git submodule containing the hardware-agnostic FlatBuffers schema and generated language bindings (Java, Kotlin, TypeScript, C++, Rust). Defines the wire format for all server-client communication:
- **`MessageBundle`** — Top-level container wrapping data feed, RPC, and pub/sub messages.
- **Data Feed** — Continuous streaming of bone/tracker/device orientation data.
- **RPC** — Request/response commands for reset, assign, settings, skeleton config, serial, autobone, firmware, and more.
- **Pub/Sub** — Event-based messaging for server status updates.