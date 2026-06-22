// Skeleton.h
//
// Maps the 10 physical IMU sensors to bones in the GLB humanoid rig, and
// defines the parent-child hierarchy used for forward kinematics before
// GPU skinning.
//
// IMPORTANT: bone name strings here must exactly match the joint/node
// names baked into your human.glb's skeleton (check in Blender, or by
// dumping node names from the GLB with a glTF inspector) -- Mixamo rigs
// typically use names like "mixamorig:Hips", "mixamorig:Spine", etc.
// Adjust BoneName values below to match your specific asset.

#pragma once

#include <array>
#include <string>

#include "NetworkProtocol.h"

namespace mocap {

struct BoneMapping {
    int sensorId;            // which physical IMU drives this bone (-1 = none, FK-only)
    const char* boneName;    // must match a joint name in human.glb
    int parentIndex;         // index into kSkeletonBones, -1 = root
};

// Index in this array IS the bone index used elsewhere (Pose.h, Renderer).
// Order matters for the parentIndex references below.
//
// Sensor ID -> body part mapping (must match firmware/esp32_node/README.md):
//   0 = hip/root   1 = chest/spine   2 = head
//   3 = L upper arm  4 = L forearm   5 = R upper arm   6 = R forearm
//   7 = L thigh      8 = R thigh     9 = spare (e.g. pelvis-relative foot)
inline const std::array<BoneMapping, 11> kSkeletonBones = {{
    /* 0 */  { 0, "mixamorig:Hips",          -1 },
    /* 1 */  { 1, "mixamorig:Spine1",         0 },
    /* 2 */  { 2, "mixamorig:Head",           1 },
    /* 3 */  { 3, "mixamorig:LeftArm",        1 },
    /* 4 */  { 4, "mixamorig:LeftForeArm",    3 },
    /* 5 */  { 5, "mixamorig:RightArm",       1 },
    /* 6 */  { 6, "mixamorig:RightForeArm",   5 },
    /* 7 */  { 7, "mixamorig:LeftUpLeg",      0 },
    /* 8 */  { 8, "mixamorig:RightUpLeg",     0 },
    /* 9 */  {-1, "mixamorig:LeftFoot",       7 },  // FK-only, no sensor
    /* 10 */ {-1, "mixamorig:RightFoot",      8 },  // FK-only, no sensor
}};

constexpr int kBoneCount = static_cast<int>(std::tuple_size<decltype(kSkeletonBones)>::value);

// Convenience: find the bone index that a given sensor drives.
// Returns -1 if no bone is mapped to that sensor.
inline int boneIndexForSensor(int sensorId) {
    for (int i = 0; i < kBoneCount; ++i) {
        if (kSkeletonBones[i].sensorId == sensorId) return i;
    }
    return -1;
}

} // namespace mocap
