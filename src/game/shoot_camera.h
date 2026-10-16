#pragma once

// Shooting (gameplay) camera: engine math mirrors and the per-frame inputs from other subsystems
// (see notes/agents/shoot_camera.md). The state machine itself lives in game::CameraDirector
// (event_runner.h), which calls these.
//
// Precision: the original does all of this with the x87 matrix stack 0x007E7990 (float storage, extended
// intermediates). MtxStack mirrors each stack operation with float storage and double intermediates, in the
// original's operation order, so results match the engine to the float ulp in practice (verified against the
// captured view matrices, tests/event_runner_test.cpp).

#include <cstdint>

#include "game/exe_image.h"

namespace game {

// ---- matrix stack mirror (row vectors, v' = v * M; every op pre-multiplies: M = Op * M) ----
class MtxStack {
public:
    MtxStack();
    const float* Top() const { return m_[top_]; }
    void Push();                                  // 0x004A9880(0): duplicate the top
    void Pop();                                   // 0x004A9840
    void LoadIdentity();                          // 0x004A9E10
    void Load(const float m[16]);                 // 0x004A9230
    void Translate(float x, float y, float z);    // 0x004A9D80
    void RotateX(int angle);                      // 0x004A99F0  Rx = [[1,0,0],[0,c,s],[0,-s,c]]
    void RotateY(int angle);                      // 0x004A9AE0  Ry = [[c,0,-s],[0,1,0],[s,0,c]]
    void RotateZ(int angle);                      // 0x004A9BD0  Rz = [[c,s,0],[-s,c,0],[0,0,1]]
    void TransformPoint(const float in[3], float out[3]) const;  // 0x004A8A80
    void TransformDir(const float in[3], float out[3]) const;    // 0x004A8AF0 (no translation)
    void Store(float out[16]) const;              // 0x004A8CA0

private:
    float m_[8][16];
    int top_ = 0;
};

// ---- angle helpers (engine angles: 0x10000 = 360 degrees) ----
int TruncToAngle(double radians);  // radians * 65536/2pi, __ftol, movsx ax
// 0x004016B0: yaw = atan2(x, z), pitch = -atan2(y, horizontal length) for a direction pointing BACK from the
// camera (pos - target).
void DirToAngles(float x, float y, float z, int* pitch, int* yaw);
// 0x00401790: b = atan2(y, x) (about z), a = -atan2(z, length in xy) (about y).
void DirToAnglesXY(float x, float y, float z, int* a, int* b);
// 0x00401D70: unsigned angle between two vectors, atan2(sqrt(|a|^2 |b|^2 - (a.b)^2), a.b).
int AngleBetween(const float a[3], const float b[3]);
// 0x00401DF0: (a.b)^2 / (|a|^2 |b|^2), negated when a.b < 0.
double SignedCos2(const float a[3], const float b[3]);
// 0x00401E60: (wt * target + wc * cur) / (wt + wc).
float Lerp(float cur, float target, int wt, int wc);
// 0x00403C00: rotates the direction pos->target toward pos->lookAt by num/(num+den) of the angle between them
// (in the plane of the two) and returns the point 100 units from pos along the new direction.
void TurnToward(const float pos[3], const float lookAt[3], const float target[3], float out[3], int num, int den);
// 0x004018E0: re-derives pitch/yaw/roll from the current stack matrix (the camera matrix).
void NormalizeAngles(MtxStack& s, int* pitch, int* yaw, int* roll);

// ---- camera contexts (0x009A6000 + i * 0x1A4) ----
struct CameraContext {
    float view[16] = {};    // +0x00 (built by Cam_BuildViews)
    float camera[16] = {};  // +0x40 (inverse of the view, before angle normalisation)
    float pos[3] = {};      // +0xC0
    int pitch = 0, yaw = 0, roll = 0;  // +0xCC / +0xD0 / +0xD4
    float target[3] = {};   // +0xD8
};
// 0x00403AC0 Cam_LookAngles: pitch/yaw from pos -> target, roll stored as given.
void ContextLookAngles(CameraContext& c, const float target[3], int roll);
// Cam_BuildViews 0x00401F40 (matrix part): the active context first re-aims at the point (0, shakeY, -1000) of
// its own camera frame (0x009CA0E4 = shakeY), then every context gets its camera matrix (+0x40), angles
// normalised (0x004018E0) and the view (+0x00) = T(-pos) Ry(-yaw) Rx(-pitch) Rz(-roll).
void BuildContextViews(CameraContext ctx[4], int active, int shakeY);

// ---- inputs from other subsystems, read by the shooting camera every frame ----
// One lock-on slot 0x009A5EC0 + 8k (k < 4): u8 active, object pointer at +4. The camera reads the object's
// world body point +0x100 and its target player +0x121 (enemy layout, notes/agents/enemy.md).
struct LockOnSlot {
    bool active = false;
    float point[3] = {};
    int targetPlayer = -1;
};

// The parts of an actor the lock-on table reads and writes (enemy offsets).
struct LockOnTarget {
    float pos[3] = {};      // +0x40 world position (registration distance)
    float point[3] = {};    // +0x100 body point (camera look-at)
    int targetPlayer = -1;  // +0x121 (s8; -1 = none)
    int slot = -1;          // +0x120 (s8) written by the table
    uint32_t flags = 0;     // +0x34 (0x10000: not registered)
};

// Lock-on table 0x009A5EC0 (16 x {u8 on, obj*}) and its candidate list 0x005A4DC8 (14 x {key, obj}, count
// 0x009CA93C). Actors register every frame (Actor_UpdateViewPos 0x00409B70 -> Register); the table task
// 0x00408DD0 (created by 0x00408D90 in the stage task list, after the camera task) rebuilds the slots from the
// previous frame's registrations: nearest first, an actor that targets a player takes the first free of slots
// 0/1, the others take slot 2 + rank. The shooting camera reads slots 0..3.
class LockOnTable {
public:
    void Reset();                                               // 0x00408D90 (init part)
    void Register(LockOnTarget* t, const float playerPos[3]);  // 0x00408EC0: key = ftol(|pos - player| * 10)
    void AddDirect(LockOnTarget* t);                            // 0x00408E80: first free slot 2..13
    void Remove(LockOnTarget* t);                               // 0x004092B0: slot off, +0x120 = -1
    void Release(int slot) { if (slot >= 0 && slot < 16) on[slot] = false; }  // on[obj+0x120] = 0 (death code)
    void Build();                                               // task 0x00408DD0 (+ sort 0x00408F30)
    int Count() const { return count_; }                        // 0x009CA93C (op 0x47 waits for 0)
    void Slots(LockOnSlot out[4]) const;                        // camera view of slots 0..3 (live object points)

    bool on[16] = {};
    LockOnTarget* obj[16] = {};

private:
    struct Candidate { uint32_t key; LockOnTarget* t; };
    Candidate list_[14] = {};
    int count_ = 0;
};
// 0x00403050: look-at point from the lock-on slots. Slots 0/1 first (both: the one whose object targets a
// player, or the midpoint if both or neither do), then slots 2/3 (midpoint if both). Returns false (the
// original clears 0x0059C988) when no slot is active; the look-at is then the rail look point.
bool SelectLookAt(const LockOnSlot slots[4], const float railLook[3], float out[3]);
// 0x00403B00: turn divisor 0x009C6F36 from the angle between the view and the look-at: byte table
// *(0x00576C04 + 4 * table)[min(angle, 0x1FFF) >> 7] (table 1 = 0x00576B44: 64 when nearly aligned, 16 when
// far). Needs the exe; returns 12 without it.
int TurnStepFromAngle(const ExeImage* exe, int table, const float pos[3], const float target[3],
                      const float lookAt[3]);
// *(s8*)*(0x00576C0C): turn divisor while the camera looks at the rail (= 12).
int TurnStepRail(const ExeImage* exe);

}  // namespace game
