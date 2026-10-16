#pragma once

// HOTD2 props ("komono") and other non-enemy actors placed by event spawn records. See notes/agents/props.md.
// Ported (per-frame logic + world matrices; verified against the stage-1 demo capture by tests/props_test.cpp):
//   HangingProp     task 0x00473CF0, created by type-0x44 states 1/2/4 (0x00472BD0 ...): sways when a flag is set
//   BreakableProp   task 0x00465FB0, created by type-0x44 state 15 (0x00473770) and komono kind 4 (0x00462E10)
//   DecorKind45     task 0x0046DAB0 (komono kind 45 via 0x00461CF0): fixed decoration models + vertex wave phase
//   AnimatedProp    task 0x00474120, created by type-0x44 state 3 (0x00472E00): motion-driven door/prop
//   PropPose        prop skeleton draw 0x0040DDC0 / 0x0040DE50 / 0x0040D9D0 (skeleton table 0x004D5390)
// Matrices are WORLD matrices (row vectors); the engine draws them premultiplied onto the view (M = W * V).

#include <cstdint>
#include <string>
#include <vector>

#include "anim.h"  // Mat4, Motion
#include "event.h"
#include "exe_image.h"
#include "task.h"

namespace game {

// Spawn record of event ops 0x09 / 0x0B / 0x0C (notes/formats.md "Actors spawned by events").
struct SpawnRecord {
    uint32_t addr = 0;
    uint32_t type = 0, flags = 0;
    float pos[3] = {};
    int32_t rot[3] = {};
    uint16_t lo = 0, state = 0;
    uint8_t params[0x50] = {};  // bytes from record + 0x24 (the stage-4 boss reads s16 0x40 / 0x42) (op 0x0B/0x0C params pointer; op 0x09: byte 0 = item
                                // slot (+0x1F4), byte 1 = komono kind (+0x130C))
    bool Read(const EventMemory& mem, uint32_t addr);
    int16_t ParamS16(int off) const;
    int32_t ParamS32(int off) const;
    int8_t ParamS8(int off) const { return static_cast<int8_t>(params[off]); }
};

// Game state the props read (original globals).
struct PropWorld {
    uint8_t flags[256] = {};  // 0x009C7200 event flags (op 0x48 sets them)
    int stage = 0;            // 0x009A1A08
    int stageSub = 0;         // 0x009A2BC0
    int area = 0;             // 0x009A2BB0 (prop lifetimes count changes of it)
    int gameMode = 0;         // 0x009CA08C
    int altModels = 0;        // 0x009A2BB4 (non-zero: some props use Mdl_SelectAlt 0x004185E0)
    int camPath = 0;          // 0x009A2D78
    int camTime = 0;          // 0x009A6110
    int camYaw = 0;           // 0x009A60D0[cam * 0x69]
    float groundY = 0;        // 0x009C8E58 (shadow height)
    int motionState = 2;      // 0x009A4694 (2 = komono motions resident; AnimatedProp draws only then)
    uint8_t itemCounter[16] = {};  // 0x009C7010[slot]
    int (*rand)() = nullptr;  // CRT rand (0x004ABE60); required where the original calls it
    std::vector<uint32_t> sounds;  // 0x0041CFD0 requests issued (id), in order
};

struct PropDraw {
    int model = -1;
    Mat4 world;
    bool alt = false;     // drawn through Mdl_SelectAlt 0x004185E0 instead of 0x00418560
    bool approx = false;  // node used the unported big-angle interpolation (0x00412750); matrix approximate
};

enum class PropResult { Alive, Killed /*Task_Kill*/, Removed /*Actor_Remove 0x00409CC0, then Task_Kill*/ };

// ---- prop skeletons (0x004D5390) and their motions ----

struct PropSkeleton {
    struct Node {
        int model = 0;  // global model id (0 for the root)
        int index = 0;  // 0 = root (no transform, not drawn); else frame slot index + 1
        std::vector<int> children;
    };
    int type = -1;
    int count = 0;   // 0x004D5404[type]: nodes incl. root; a frame holds count-1 positions + rotations
    int interp = 0;  // 0x004D5440[type]: 0 = one frame per tick, 1/2 = 30 Hz frames, odd ticks interpolated
    std::vector<Node> nodes;  // depth first, nodes[0] = root
    bool Load(const ExeImage& exe, int type);
    size_t FrameStride() const { return (18 * static_cast<size_t>(count) - 15) & ~size_t(3); }
};

// Loads one motion block by global id from game\mot (ListMotionFiles/SplitMotionFile).
bool LoadMotionById(const ExeImage& exe, const std::string& gameDir, int motionId, Motion& out);

// Motion-state struct of the original at task +0x324: {skeleton, motion, frame, prevFrame, ..., +0x14 matrix}.
struct PropAnim {
    int skeleton = 0, motion = 0, frame = 0, prevFrame = 0;
};

// 0x0040DDC0: clamps anim.frame (>= ticks-1 -> 0, < 0 -> ticks-2), walks the nodes (0x0040DE50, node transform
// 0x0040D9D0) and sets prevFrame = frame. parent: matrix in effect at the call (world part only).
// modelOverride >= 0: every node draws that model (0x0040DFD0); alt: Mdl_SelectAlt (0x0040DFA0).
// storeIndex > 0: *stored receives the matrix of the node with index storeIndex - 1 (0x0040DFD0's store).
std::vector<PropDraw> PropPose(const PropSkeleton& s, const Motion& m, PropAnim& anim, const Mat4& parent,
                               int modelOverride = -1, bool alt = false, int storeIndex = 0, Mat4* stored = nullptr,
                               int (*rnd)() = nullptr);

// ---- hanging prop (0x00473CF0) ----

struct HangingTables {
    // 0x005960B4[kind]: 3 s16 (rx, ry, rz) per frame for kinds 0, 2, 3; 0x005960C8[kind]: u16 ry per frame
    std::vector<int16_t> triple[16];
    std::vector<uint16_t> single[16];
    bool Load(const ExeImage& exe);
};

struct HangingProp : TaskData {
    float pos[3] = {};         // +0x19C
    float scale[3] = {1, 1, 1};  // +0x1A8
    int baseRot[3] = {};       // +0x1CC / +0x1D0 (record rot[1]) / +0x1D4
    int rot[3] = {};           // +0x64 / +0x68 / +0x6C
    float ryScale = 1.0f;      // +0x2C0
    uint32_t flags = 0;        // +0x34
    int hitId = -1;            // +0x14C (-1: not shootable, no hit target)
    int16_t model = 0;         // +0x28C
    int16_t kind = 0;          // +0x290
    int amp = 0;               // +0x1DC
    int phase = 0;             // +0x1E8
    int swingBase = 0;         // +0x1F4
    int triggerFlag = -1;      // +0x2A0
    int killFlag = -1;         // +0x2A4
    int frame = 0;             // +0x2A8
    int once = 0;              // +0x2AC

    // 0x00472BD0 (type 0x44 state 1)
    void Init(const SpawnRecord& r);
    // 0x00473CF0 up to the draw. A shot is the hit system setting flags |= 8 before the call.
    PropResult Update(PropWorld& w, const HangingTables& t);
    Mat4 World() const;  // S * Rx(rot0) * Ry(rot1) * Rz(rot2) * Ry(baseRot1) * T(pos)
    PropDraw Draw(const PropWorld& w) const;
    bool HitTarget() const { return hitId != -1; }  // 0x00405160 called after the draw
};

// ---- breakable prop group (0x00465FB0) ----

// 0x00593DB8: 12-byte rows per sub-kind (11 rows): prop skeleton, break motion, break sound, hit radius
// (+0x124), hit point height above the base (0x00593DC2).
struct BreakableTables {
    struct Row {
        int16_t skeleton, motion;
        uint32_t sound;
        int16_t radius, height;
    };
    std::vector<Row> rows;
    bool Load(const ExeImage& exe);
};

struct BreakableProp : TaskData {
    float pos[3] = {};        // +0x19C
    int rotY = 0;             // +0x1D0
    int16_t subKind = 0;      // +0x290 (row of 0x00593DB8)
    int16_t model = -1;       // +0x28C (-1: drawn as a prop skeleton)
    int16_t lifetime = 0;     // +0x11C (area changes survived)
    int8_t itemSlot = 0;      // +0x194
    uint8_t areaSeen = 0;     // +0x196
    uint8_t areaChanges = 0;  // +0x197
    float hitRadius = 0;      // +0x124
    float shake = 0;          // +0x2C0
    int field2A0 = -1;        // +0x2A0
    uint32_t flags = 0;       // +0x34
    PropAnim anim;            // +0x324
    float hitPoint[3] = {};   // +0x70 (view space; set when registered)
    bool shakeValid = false;
    float shakeOffset[2] = {};

    // 0x00473770 (type 0x44 state 15). Returns false when the stage rules skip the prop (no task).
    // Calls w.rand once when the item slot is > 0 and record rot[0] > 1 (item counter).
    bool InitState15(const SpawnRecord& r, PropWorld& w, const BreakableTables& t);
    // 0x00462E10 (komono kind 4, from an op 0x09 record; item slot = params byte 0)
    void InitKind4(const SpawnRecord& r, PropWorld& w, const BreakableTables& t);
    // 0x00465FB0 up to the draw. shotBits: the +0x34 bits the hit system set since the last frame
    // (8 = hit, 2/4 = which gun). motionTicks: 0x004E07D0[anim.motion].
    // Item drops / hit sparks / score (0x00471BD0, 0x004723F0, 0x004722A0, 0x00467B90, 0x004666B0, 0x004650F0)
    // are not ported: their requests are recorded in `events`.
    PropResult Update(PropWorld& w, const BreakableTables& t, uint32_t shotBits, int motionTicks);
    // Draw part (motionLoaded: 0x009A37E4[motion*4] == 2). Appends the shadow (model 0x10D0/0x10D1) when the
    // sub-kind has one. view: needed for the hit point (+0x70, view space).
    std::vector<PropDraw> Draw(const PropWorld& w, const BreakableTables& t, const PropSkeleton& s, const Motion& m,
                               bool motionLoaded, const Mat4& view);
    bool HitTarget() const { return anim.frame == 0; }  // registered (0x00405160) only while intact
    std::vector<std::string> events;

private:
    void InitCommon(const SpawnRecord& r, PropWorld& w, const BreakableTables& t, int16_t sub);
};

// ---- komono kind 45 decoration (0x0046DAB0) ----

struct DecorKind45 : TaskData {
    int16_t lifetime = 0;     // +0x11C
    uint8_t areaSeen = 0;     // +0x196
    uint8_t areaChanges = 0;  // +0x197
    int extra = 0;            // +0x2A0: also draw the first 5 table entries
    int phase = 0;            // +0x1D0: vertex wave phase, +0x200 per waved model per frame
    void Init(const SpawnRecord& r, const PropWorld& w);  // 0x00461CF0 common part
    PropResult Update(PropWorld& w);
    std::vector<PropDraw> Draw(const ExeImage& exe) const;
    // Wave phase applied to model 0x1731 + k this frame (the phase before this frame's Update + k * 0x200)
    int WavePhase(int k) const { return (phase - 4 * 0x200 + k * 0x200) & 0xFFFF; }
};

struct Model;
// The vertex wave of 0x0046DAB0 on a copy of model 0x1731 + k (k = 0..3), as the original edits the loaded model:
// every inline vertex (x bit 0 set; the strips of these models are all reference strips) with t = trunc(y) != 0 gets
// z = y * -0.1 (0x0056470C) * sin(((5k - t) * 0x200 + phase) & 0xFFFF). Returns false if a strip is not a reference
// strip (the original's walk would then read it differently; not seen).
bool DecorWave(Model& m, int k, int phase);

// ---- type 0x44 state 3: motion-driven prop (0x00474120) ----

struct AnimatedProp : TaskData {
    float pos[3] = {};   // +0x40
    uint32_t flags = 0;  // +0x34
    int hitId = -1;      // +0x14C (params word 2)
    int16_t model = 0;   // +0x28C override model (0x17EE, or 0x197C outside stage 0)
    int triggerFlag = 0, killFlag = 0;
    PropAnim anim;       // +0x324: skeleton 0xB, motion 0x1D6
    void Init(const SpawnRecord& r, const PropWorld& w);  // 0x00472E00
    PropResult Update(PropWorld& w, int motionTicks);
    std::vector<PropDraw> Draw(const PropWorld& w, const PropSkeleton& s, const Motion& m, Mat4* stored);
};

}  // namespace game
