# ESP32 Body Tracking System

Real-time 3D human motion capture using 10 ESP32-C3 + MPU-6050 IMU nodes,
streaming quaternion orientation data over UDP to an Ubuntu PC running an
OpenGL 3.3 GPU-skinned visualizer with Blinn-Phong lighting.

Recordings are saved as **CSV files** (open directly in Excel / LibreOffice
Calc / Google Sheets, hand-editable). Two recordings can be played back
**side-by-side** in the same window for visually comparing two takes --
e.g. a reference Bharatanatyam performance on the left vs a student attempt
on the right.

---

## Project structure

```
ESP32-Body-Tracking-System/
├── CMakeLists.txt
├── README.md
├── firmware/
│   └── esp32_node/
│       ├── esp32_node.ino       # Flash to all 10 boards (set SENSOR_ID each time)
│       └── README.md            # Wiring, libraries, calibration
├── visualizer/
│   ├── include/
│   │   ├── NetworkProtocol.h   # UDP packet layout (must match firmware)
│   │   ├── Network.h/.cpp      # Threaded UDP receiver, one per node
│   │   ├── MotionRecorder.h    # Record / playback engine
│   │   ├── MiniCsv.h           # CSV read / write (no external libs)
│   │   ├── MathTypes.h         # Vec3, Quat, Mat4
│   │   ├── Skeleton.h          # Sensor → bone → hierarchy mapping
│   │   ├── Model.h/.cpp        # Assimp GLB loader + skinning data
│   │   ├── Shader.h/.cpp       # GLSL compile/link wrapper
│   │   └── Renderer.h/.cpp     # FK pass, single + dual skeleton draw
│   ├── src/
│   │   └── main.cpp            # Entry point, keyboard controls
│   ├── shaders/
│   │   ├── skinned.vert        # GPU skinning (4 bone influences/vertex)
│   │   └── skinned.frag        # Blinn-Phong + roughness/metalness
│   ├── assets/models/          # ← put human.glb here
│   └── test/
│       ├── integration_test.cpp # UDP + record + CSV + playback (headless)
│       └── compare_test.cpp     # Dual-CSV lockstep playback (headless)
├── third_party/glad/            # See its README — one-time GLAD setup
└── recordings/                  # Saved CSV takes land here at runtime
```

---

## 1. Firmware setup

See `firmware/esp32_node/README.md`.

Flash `esp32_node.ino` to each of the 10 ESP32-C3 boards. The only thing
that differs per board is `#define SENSOR_ID` (0–9). Update
`WIFI_SSID`, `WIFI_PASSWORD`, and `PC_IP_ADDRESS` once and they apply to
all boards.

**Sensor ID → body part mapping** (matches `Skeleton.h`):

| ID | Body part       |
|----|-----------------|
| 0  | Hip / root      |
| 1  | Chest / spine   |
| 2  | Head            |
| 3  | Left upper arm  |
| 4  | Left forearm    |
| 5  | Right upper arm |
| 6  | Right forearm   |
| 7  | Left thigh      |
| 8  | Right thigh     |
| 9  | Spare (foot)    |

---

## 2. PC dependencies

```bash
sudo apt update
sudo apt install build-essential cmake \
    libglfw3-dev libassimp-dev \
    libgl1-mesa-dev libx11-dev libxrandr-dev \
    libxi-dev libxinerama-dev libxcursor-dev
```

---

## 3. GLAD (one-time, ~1 minute)

GLAD is the OpenGL function loader. It must be generated from the official
Khronos spec rather than hand-written. Follow `third_party/glad/README.md`.

**Quick version:**
```bash
pip install glad2 --break-system-packages
python -m glad --api gl:core=3.3 --out-path third_party/glad c
```
This produces `third_party/glad/include/glad/glad.h`,
`third_party/glad/include/KHR/khrplatform.h`, and
`third_party/glad/src/glad.c` — exactly what `CMakeLists.txt` expects.

---

## 4. Human model

Place a rigged Mixamo humanoid GLB at:
```
visualizer/assets/models/human.glb
```
Check that the joint names in your GLB (inspect in Blender or a glTF
viewer) match the `boneName` strings in `visualizer/include/Skeleton.h`.
Mixamo exports typically use `mixamorig:Hips`, `mixamorig:Spine1`, etc.
Edit `Skeleton.h` to match your actual export if they differ.

---

## 5. Build

```bash
# Normal build (main visualizer)
mkdir build && cd build
cmake ..
make -j$(nproc)

# Build + run headless tests only (no GL/GLFW/Assimp required)
cmake .. -DBUILD_TESTS=ON -DBUILD_VISUALIZER=OFF
make -j$(nproc)
./integration_test
./compare_test
```

---

## 6. Run

Always run from the **project root** (not `build/`), so shader and model
paths resolve correctly.

### Normal mode — live capture + single recording playback

```bash
./build/body_tracking_visualizer
```

Power on the suit. The skeleton starts moving as UDP packets arrive from
the ESP32 nodes. Controls:

| Key | Action |
|-----|--------|
| `R` | Start recording / stop & save (→ `recordings/take_YYYYMMDD_HHMMSS.csv`) |
| `L` | Load the most recently saved recording |
| `SPACE` | Play / pause loaded recording |
| `S` | Stop playback, return to live view |
| `← →` | Seek −1 s / +1 s during playback |
| `ESC` | Quit |

### Compare mode — two CSV recordings side by side

```bash
./build/body_tracking_visualizer recordings/teacher.csv recordings/student.csv
```

Loads both CSVs and plays them back in a **1600 × 720 window** with the
two skeletons side by side:

- **Left skeleton** (warm skin tone) = first file (A)
- **Right skeleton** (cool blue tone) = second file (B)

Both transports are driven together so they stay in lockstep.

| Key | Action |
|-----|--------|
| `SPACE` | Play / pause **both** |
| `← →` | Seek **both** −1 s / +1 s |
| `ESC` | Quit |

**Typical workflow for Bharatanatyam practice review:**
1. Teacher performs the adavu/sequence → `R` to record → `R` again to save
   (`take_..._teacher.csv`).
2. Student performs the same sequence → `R` → `R` → save
   (`take_..._student.csv`).
3. Open compare mode with both files. Both skeletons animate together;
   differences in posture, timing, and limb angles are immediately visible
   side by side.

---

## 7. CSV recording format

Each `.csv` file saved by this app has this layout and can be opened
directly in Excel / Google Sheets:

```
t,s0_w,s0_x,s0_y,s0_z,s1_w,s1_x,...,s9_w,s9_x,s9_y,s9_z
0.000000,1.000000,0.000000,0.000000,0.000000,...
0.020012,0.999847,0.012345,0.007891,0.000000,...
```

- **Column 1**: timestamp in seconds from recording start
- **Columns 2–41**: quaternion (w, x, y, z) for each of the 10 sensors
- ~50 Hz capture → ~50 rows/second → ~3 000 rows/minute
- A 2-minute take is around 200 KB

**Hand-editing in Excel**: values are plain floats. If you edit a
quaternion manually, the loader re-normalizes each row on read so the
playback won't glitch even if your edited values aren't exactly unit-length.

---

## 8. Headless tests

Two tests cover the non-GL core logic (useful for CI or quick sanity
checks before touching the renderer):

```bash
# Build
g++ -std=c++17 -O2 -Ivisualizer/include \
    visualizer/src/Network.cpp visualizer/src/MotionRecorder.cpp \
    visualizer/test/integration_test.cpp -o integration_test -lpthread

g++ -std=c++17 -O2 -Ivisualizer/include \
    visualizer/src/MotionRecorder.cpp \
    visualizer/test/compare_test.cpp -o compare_test -lpthread

./integration_test   # 10-sensor UDP receive → record → CSV → load → play
./compare_test        # two independent CSVs loaded + played in lockstep
```

Both tests have been verified passing in this build.

---

## Known open issues

- **IMU drift**: per-chip MPU-6050 calibration offsets reduce residual
  drift during stillness. Run the `IMU_Zero` example per board and paste
  its 6 offsets into `esp32_node.ino` before final flashing.
- **Bone offset approximation**: `Renderer::computeBoneMatrices()`
  uses hardcoded approximate bone lengths rather than the actual
  bind-pose translations from the GLB. Works visually but won't match
  real model proportions exactly -- replace `boneLocalOffset()` with
  bind-pose data extracted from the GLB's node transforms for a
  production-accurate rig.
- **Mesh segmentation**: if joint areas look segmented, check that your
  Mixamo export uses smooth multi-bone weight blending
  (`aiProcess_LimitBoneWeights` caps at 4 influences). Some Mixamo
  exporter versions also need `aiProcess_PopulateArmatureData` added to
  the Assimp import flags in `Model.cpp`.
- **Bone names**: if the skeleton doesn't move in the visualizer (bind
  pose only), `Skeleton.h` bone names don't match your GLB's joint
  names. Check with Blender's Armature panel and update `Skeleton.h`.
