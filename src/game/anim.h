#pragma once

// HOTD2 skeletons and motions (see notes/formats.md). From Anim_DrawSkeleton 0x0040F520, Anim_NodeTransform
// 0x0040ED30, Anim_RootTransform 0x0040EFB0, Anim_GetFrame 0x00412F50, Res_SetupMotionSlots 0x00412D90 and the
// matrix stack ops 0x004A9D80 / 0x004A99F0 / 0x004A9AE0 / 0x004A9BD0.

#include <cstdint>
#include <string>
#include <vector>

#include "exe_image.h"

namespace game {

struct SkeletonNode {
    uint32_t modelId = 0;     // global model id drawn at this node (0 = none)
    float offset[3] = {};     // translation relative to the parent
    uint32_t flags = 0;
    int16_t index = 0;        // rotation slot in the motion frame
    int parent = -1;          // index into Skeleton::nodes
    std::vector<int> children;
};

struct Skeleton {
    int type = -1;
    int nodeCount = 0;               // from 0x004E0724 (rotation slots per frame)
    std::vector<SkeletonNode> nodes;  // depth-first; nodes[0] is the root
};

// Skeleton type t (0..63): root pointer 0x004E0430[t], node count 0x004E0724[t].
bool LoadSkeleton(const ExeImage& exe, int type, Skeleton& out);

struct MotionFileEntry {
    int index;
    std::string name;            // file in mot\  (0x004D1B00[n])
    std::vector<int16_t> motionIds;  // 0x004E2B14[n], count 0x004E2BDC[n]
};
std::vector<MotionFileEntry> ListMotionFiles(const ExeImage& exe);

// Length of a motion in 60 Hz ticks (0x004E07D0[id]).
int MotionTicks(const ExeImage& exe, int motionId);

// One motion's block from a mot file: u32 header + frames stored at 30 Hz.
struct Motion {
    int id = -1;
    int ticks = 0;
    int nodes = 0;
    std::vector<uint8_t> data;

    int StoredFrames() const { return (ticks - 1) / 2 + 2; }
    size_t FrameStride() const { return (12 + 6 * static_cast<size_t>(nodes) + 3) & ~size_t(3); }
    bool SizeConsistent() const { return data.size() == 4 + StoredFrames() * FrameStride(); }
    void RootPosition(int frame, float out[3]) const;
    void Rotation(int frame, int slot, int16_t out[3]) const;  // x, y, z angles (0x10000 = 360 degrees)
};

// Splits a mot file into its motions (offsets table + exe ids/ticks). nodes: node count to assume.
std::vector<Motion> SplitMotionFile(const ExeImage& exe, const MotionFileEntry& f, const std::vector<uint8_t>& file,
                                    int nodes);

// Row-vector 4x4 matrix (D3D convention, v' = v * M).
struct Mat4 {
    float m[16];
    static Mat4 Identity();
    static Mat4 Translate(float x, float y, float z);
    static Mat4 RotateX(int angle);  // engine angle units
    static Mat4 RotateY(int angle);
    static Mat4 RotateZ(int angle);
    Mat4 operator*(const Mat4& b) const;
};

// World matrix per skeleton node (indexed like Skeleton::nodes) at a tick. Even ticks use the stored frame
// tick/2 exactly; odd ticks interpolate halfway to the next frame like Anim_RootTransform/NodeTransform.
// root:  M = Rx Ry Rz(rot slot of root) * T(rootPos);   node: M = Rx Ry Rz(slot) * T(offset) * M_parent
std::vector<Mat4> EvaluatePose(const Skeleton& s, const Motion& m, int tick);

}  // namespace game
