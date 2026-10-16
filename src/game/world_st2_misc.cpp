// game::World - small tasks first met in stage 2 that are not actors. Verification: notes/agents/stage2b.md.

#include "game/world_internal.h"

#include "game/mstack.h"
#include "game/shoot_camera.h"

namespace game {

namespace {

// 0x0040E3B0 (created by 0x0040E370 for event op 0x17, 0x0045F2A0): the light direction 0x009A3558 / 0x009A355C turns
// from its current angles to (word 1, word 2) over word 3 frames (rotation blend 0x00412750, renderer state only); at
// the last frame 0x0040E140 sets the target light and the task ends
struct LightTask : WTask {
    int frames = 0;         // +0x34
    int counter = 0;        // +0x38
    int from[2] = {};       // +0x3C / +0x40
    int to[2] = {};         // +0x44 / +0x48
};

void TaskLight(Task& t) {
    World::Impl& w = *CurrentWorld();
    LightTask& l = t.As<LightTask>();
    if (++l.counter == l.frames) {
        w.env.lightPitch = l.to[0];  // 0x0040E140(&0x009A3540, to)
        w.env.lightYaw = l.to[1];
        w.ts.KillCurrent();
        return;
    }
    // renderer state: A = Rz(0) Rx(from pitch) Ry(from yaw), B likewise for the target, blended by counter / frames
    // (0x00412750), then the angles re-derived (0x004018E0: pitch / yaw of the matrix's z row) into 0x009A3558 / 5C
    Mat4 a = Mat4::Identity(), b = Mat4::Identity();
    mx::RotY(a, l.from[1]);
    mx::RotX(a, l.from[0]);
    mx::RotZ(a, 0);
    mx::RotY(b, l.to[1]);
    mx::RotX(b, l.to[0]);
    mx::RotZ(b, 0);
    Mat4 top = Mat4::Identity();
    mx::BlendRotation(top, a, b, static_cast<float>(l.counter) / static_cast<float>(l.frames));
    int pitch = 0, yaw = 0;
    DirToAngles(top.m[8], top.m[9], top.m[10], &pitch, &yaw);
    w.env.lightPitch = pitch;
    w.env.lightYaw = yaw;
}

}  // namespace

// Event op 0x17 (0x0045F2A0 -> 0x0040E370)
void World::Impl::StartLightTask(const EventOp& op) {
    LightTask* l = NewTask<LightTask>(TaskLight, 0x0040E3B0);
    l->counter = 0;
    l->frames = op.args.size() > 2 ? static_cast<int>(op.args[2]) : 0;
    l->to[0] = op.args.size() > 0 ? static_cast<int>(op.args[0]) : 0;
    l->to[1] = op.args.size() > 1 ? static_cast<int>(op.args[1]) : 0;
    l->from[0] = env.lightPitch;  // 0x009A3558 / 0x009A355C
    l->from[1] = env.lightYaw;
}

// Water wakes 0x00408740 / 0x004083D0: World::Impl::SpawnWake (world_st3_enemy.cpp); actor 0x20 variant 2 box walk
// 0x00449020: World::Impl::Actor20BoxWalk (world_st3.cpp).

}  // namespace game
