// game::World - the stage-4 boss's carried objects (carried object type 2, definition 0x005644B0): taken from the boss's
// body by state 18 (0x00494F70), held in its hand (task 0x00443200: node 8), thrown at the target player (0x00442B90
// state 4 -> 0x00443B90), stuck in front of the view after hitting (0x00444160) or deflected away when shot to pieces
// (0x004423F0 type 2 -> 0x00444F70). Verification: notes/agents/stage4b.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_held.h"
#include "game/world_internal.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// 8-byte constants (fld / fcomp qword ptr): 0x00564520 = -15.0, 0x00564528 = -40.0 (F32 read their low half: 0)
double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

uint32_t Def(const World::Impl& w, int type) { return w.exe.U32(0x005644D8 + 4 * static_cast<uint32_t>(type)); }

EnemyTask* HolderOf(World::Impl& w, HeldTask& k) {
    if (!k.holder) return nullptr;
    for (Task* t : w.ts.Live())
        if (t->serial == k.holderSerial) return t->data.get() == k.holder ? k.holder : nullptr;
    return nullptr;
}

// 0x00408EC0 for the object: lock-on candidate with its body point +0x100 and target +0x121
void RegisterLock(World::Impl& w, HeldTask& k) {
    if (!k.lock) k.lock = &w.lockTargets[w.nextLockTarget++];
    memcpy(k.lock->pos, k.pos, 12);
    memcpy(k.lock->point, k.body, 12);
    k.lock->targetPlayer = k.slot121;
    k.lock->flags = k.flags34;
    w.lockOn.Register(k.lock, w.playerPos);
}

void SyncLockOnly(HeldTask& k) {
    if (!k.lock) return;
    k.lock->targetPlayer = k.slot121;
    k.lock->flags = k.flags34;
}

void TaskThrownObj(Task& t);
void TaskStuck(Task& t);
void TaskFlyOff(Task& t);

// 0x00442B90(obj) for state 4 (top = the object's model-view): world position / angles, then the arc at a point 15 in
// front of the view (beside the target with two players) in c+0x4C frames with gravity c+0x50
void Throw(World::Impl& w, HeldTask& k, const Mat4& world) {
    k.flags34 &= ~0x100u;
    mx::GetTranslation(world, k.pos);
    memcpy(k.lastView, k.viewPos, 12);  // c+0x14 = +0x70 (last frame's view position)
    mx::MatrixToAngles(world, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
    memcpy(k.body, k.pos, 12);
    RegisterLock(w, k);  // 0x00408EC0
    if (k.state != 4) {
        w.Note("carried object throw state " + std::to_string(k.state) + " (0x00442B90 for type 2) not ported");
        return;
    }
    float aimL[3] = { 0.0f, 0.0f, -15.0f };
    if (w.route != 1) {  // two players: beside the target (0x0055CB00 is a double, 3.0)
        double side = 0;
        w.exe.Read(0x0055CB00, &side, 8);
        aimL[0] = static_cast<float>(static_cast<double>(k.slot121 * 2 - 1) * side);
    }
    float aim[3];
    mx::Transform(w.cam.inverse, aimL, aim);
    int pitch = 0, yaw = 0;
    mx::DirToAngles(aim[0] - k.pos[0], aim[1] - k.pos[1], aim[2] - k.pos[2], &pitch, &yaw);  // 0x004016B0
    const float dz = k.pos[2] - aim[2], dx = k.pos[0] - aim[0];
    const float frames = k.vel[0];
    k.acc[1] = k.vel[1];
    const float dy = aim[1] - k.pos[1];
    Mat4 r = Mat4::Identity();
    mx::RotY(r, yaw);
    const float l[3] = { 0.0f, 0.0f, std::sqrt(dz * dz + dx * dx) / frames };
    float v[3];
    mx::Transform(r, l, v);
    k.vel[0] = v[0];
    k.vel[2] = v[2];
    k.vel[1] = dy / frames - frames * k.acc[1] * F32(w.exe, 0x004C43AC);
    w.ts.Current()->fn = TaskThrownObj;  // 0x005926A8[4]
    k.fn = 0x00443B90;
}

// 0x00443200: in the holder's hand: node 8 x T(-0.5, -2, 0) x T(pos) RotX RotZ RotY; the holder's throw tick sets the
// state (0x00442B90)
void TaskCarried(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    EnemyTask* h = HolderOf(w, k);
    if (!h) {
        w.Note("carried object without its holder (0x00443200)");
        HeldDestroyExt(w, k);
        return;
    }
    Mat4 mv = h->anim.nodes[8].mv;  // 0x004A9230(holder +0x6B4): the node's model-view
    mx::Translate(mv, -0.5f, -2.0f, 0.0f);
    mx::Translate(mv, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotX(mv, k.rot[0]);
    mx::RotZ(mv, k.rot[2]);
    mx::RotY(mv, k.rot[1]);
    w.Draw(k.model, mv, 8, -1.0f, false, -1, true);
    if (k.state != 10) {  // 0x00442B90: world = model-view x the camera matrix 0x009A6040
        Mat4 world = w.cam.inverse;
        mx::MulLeft(world, mv);
        Throw(w, k, world);
    }
    mx::GetTranslation(mv, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
    HeldShotsExt(w, k, mv);  // 0x004423F0
    SyncLockOnly(k);
}

// 0x00443B90: the arc; close in front of the view (+0x78 >= 0x00564520) it sticks there (view space, 0x00444160) and
// hits the target (Player_TakeHit kind 1 for type 2, else 7)
void TaskThrownObj(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    k.pos[0] = k.vel[0] + k.pos[0];
    k.pos[1] = k.pos[1] + k.vel[1];
    k.pos[2] = k.vel[2] + k.pos[2];
    k.vel[1] = k.acc[1] + k.vel[1];
    k.rot[1] = k.rot[1] + k.spin[1];
    k.rot[0] = k.rot[0] + k.spin[0];
    k.rot[2] = k.rot[2] + k.spin[2];
    memcpy(k.lastView, k.pos, 12);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m);
    memcpy(k.body, k.pos, 12);
    RegisterLock(w, k);  // 0x00408EC0
    const Mat4 mv = m * w.cam.view;
    mx::GetTranslation(mv, k.viewPos);
    w.RegisterTarget(&k);
    HeldShotsExt(w, k, mv);
    if (0 < k.hp && static_cast<float>(F64(w.exe, 0x00564520)) <= k.viewPos[2]) {
        Mat4 v = w.cam.view;
        mx::Translate(v, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(v, k.rot[2]);
        mx::RotY(v, k.rot[1]);
        mx::RotX(v, k.rot[0]);
        mx::GetTranslation(v, k.pos);
        mx::MatrixToAngles(v, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
        k.stuck = 0x5A;
        w.ts.Current()->fn = TaskStuck;
        k.fn = 0x00444160;
        int kind = 7;
        if (k.type == 2) {
            k.rot[0] = -0x800;
            kind = 1;
        }
        w.PlayerTakeHit(k.slot121, 1, kind);  // 0x00415300
    }
    SyncLockOnly(k);
}

// 0x00444160: in front of the view (view space) for 0x5A frames, blinking over the last 0x3C; then the target's
// 0x009A2BA0 flag is released
void TaskStuck(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    const int v = k.stuck--;
    if (v == 0) {
        if (k.slot121 >= 0) w.targetTaken[k.slot121 & 1] = 0;
        HeldDestroyExt(w, k);
        return;
    }
    if (k.stuck < 0x3D && (k.stuck & 1) == 0) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m, 8, -1.0f, false, -1, true);  // view space
    mx::Transform(w.cam.inverse, k.pos, k.body);
    RegisterLock(w, k);
}

// 0x00444F70: deflected fly-off; removed once off screen (0x004459C0)
void TaskFlyOff(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    k.pos[0] = k.vel[0] + k.pos[0];
    k.pos[1] = k.pos[1] + k.vel[1];
    k.pos[2] = k.vel[2] + k.pos[2];
    k.vel[1] = k.acc[1] + k.vel[1];
    k.rot[1] = k.rot[1] + k.spin[1];
    k.rot[0] = k.rot[0] + k.spin[0];
    k.rot[2] = k.rot[2] + k.spin[2];
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m);
    const Mat4 mv = m * w.cam.view;
    mx::GetTranslation(mv, k.viewPos);
    if (!HeldOnScreenExt(w, k)) HeldDestroyExt(w, k);
    SyncLockOnly(k);
}

}  // namespace

bool HeldAlive(const HeldTask* k) { return k && HeldTask::Live().count(k) != 0; }
void HeldRegisterLock(World::Impl& w, HeldTask& k) { RegisterLock(w, k); }
void HeldSyncLock(HeldTask& k) { SyncLockOnly(k); }
void TaskHeldStuck(Task& t) { TaskStuck(t); }

// 0x004423F0, type 2 at hp 0: the velocity turned (about y) by the angle between it and the direction from the view
// position to a point beside the view (0x004C4398 / 0x004C4C78), level flight (acc -0.0), task 0x00444F70
void HeldType2Break(World::Impl& w, HeldTask& k) {
    const float side = 0.0f < k.viewPos[0] ? F32(w.exe, 0x004C4398) : F32(w.exe, 0x004C4C78);
    const float l[3] = { side - k.viewPos[0], 0.0f, F32(w.exe, 0x004C4C78) - k.viewPos[2] };
    float d[3];
    mx::TransformDir(w.cam.inverse, l, d);  // 0x004A8AF0 on the camera matrix
    const float dot = d[2] * k.vel[2] + d[0] * k.vel[0];
    const float s = (k.vel[0] * k.vel[0] + k.vel[2] * k.vel[2]);
    const float q = (d[2] * d[2] + d[0] * d[0]) * s - dot * dot;
    const float a = static_cast<float>(std::atan2(static_cast<double>(std::sqrt(q)), static_cast<double>(dot)) * mx::kToAngle);
    float f = static_cast<float>(static_cast<int16_t>(static_cast<int>(a)));
    if (!(0.0f < k.viewPos[0])) f = -f;
    Mat4 r = Mat4::Identity();
    mx::RotY(r, static_cast<int>(f));
    float v[3];
    mx::Transform(r, k.vel, v);
    k.vel[0] = v[0];
    k.vel[2] = v[2];
    k.vel[1] = 0.0f;
    k.acc[1] = Bits(0x80000000);
    w.ts.Current()->fn = TaskFlyOff;
    k.fn = 0x00444F70;
}

// 0x00494F70(boss): carried object type 2 in the boss's hand: (0, 0, 2.8) RotX 0x4000, 50 frames of flight with gravity
// -0.055 (phase 3) / -0.04, spin 0x1C00 about x, not shootable (+0x34 0x100) until thrown
HeldTask* SpawnCarriedType2(World::Impl& w, EnemyTask& holder, bool phase3) {
    HeldTask* k = w.NewTask<HeldTask>(TaskCarried, 0x00443200);  // 0x005926A8[2]
    k->flags34 |= 0x100;
    k->holder = &holder;
    k->holderSerial = w.CurrentSerial();
    k->civ = nullptr;
    k->type = 2;
    const uint32_t def = Def(w, 2);
    const int hp = w.exe.S16(def + 0x20);
    k->model = w.exe.S16(def + 0x20 + 2 * static_cast<uint32_t>(hp));
    k->state = 10;
    k->pos[0] = 0.0f;
    k->pos[1] = 0.0f;
    k->pos[2] = Bits(0x40333333);
    k->rot[0] = 0x4000;
    k->rot[1] = 0;
    k->rot[2] = 0;
    k->vel[0] = Bits(0x42480000);
    k->vel[1] = phase3 ? Bits(0xBD6147AE) : Bits(0xBD23D70A);
    k->acc[1] = Bits(0xBCDF0123);
    k->spin[0] = 0x1C00;
    k->spin[1] = 0;
    k->spin[2] = 0;
    k->radius = F32(w.exe, def + 4);
    k->radius128 = F32(w.exe, def + 8);
    k->hp = static_cast<int16_t>(hp);
    k->slot121 = -1;
    return k;
}

}  // namespace game
