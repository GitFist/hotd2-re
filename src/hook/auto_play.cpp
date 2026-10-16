// Automatic gun input for capture runs (HOTD2_INPUT_AUTO), see auto_play.h. Reads game memory only.
//
// Target selection (notes/agents/autoplay.md):
//   * The hit task 0x004044A0 (body 0x00404570) tests the shot rays of a frame against the targets registered during the
//     previous frame (Hit_RegisterTarget 0x00405160: list 0x0059D8E8, 5 words per entry, count 0x005A4C80; the hit task
//     resets the count after Hit_CopyTargets). Input_Poll runs before any task, so at this point the list holds exactly
//     the targets the shot of this frame will be tested against, with the view-space positions it will use.
//   * Object fields: +0x00 task fn, +0x08 serial, +0x34 flags (0x80 skeleton target, 0x8000 not shootable, 0x10 mesh
//     target, 0x4000000 dead), +0x70 view-space sphere centre, +0x124 sphere radius, +0x1F4 skeleton type (s16).
//     Skeleton targets: the sphere gates the node tests (Hit_TestObjectSphere 0x00404630); nodes are walked from
//     0x004E0430[type] (Node: +0x14 slot s16, +0x16 child count, +0x18 children), node records obj+0x20C+slot*0x90:
//     +0x00 model (0 = inactive), +0x68 view-space hit point, +0x74 flags (0x10 mesh), +0x78 radius.
//   * The ray is v = (cx, cy, -F) with F = 0x009A2D70 (float, Render_BeginWorld) and (cx, cy) the calibrated centred
//     aim of Gun_UpdateRecords 0x0040CBB0 (mode 6: fixed points, else the record's calibration). The nearest entry
//     (key = ftol(-viewZ * 10), low 16 bits, stable) receives the hit (Hit_Deliver 0x00404DB0).
//   * Civilians (actor type 0x10: task fns 0x0048A3E0 / 0x0048A920 / 0x0048B140) are never aimed at, and a point is
//     rejected if a civilian sphere/node might be hit first along its ray.
//   * Feedback: after a shot, 0x009C9010[p] (hit flag) and the nearest hit entry (0x0059ECDC index -> 0x0059F4D0 +
//     idx * 0x3C, +0x28 object) tell what the shot hit; targets that keep absorbing shots without being hit (walls in
//     between) are skipped for a while.

#include "auto_play.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "game/shooting.h"
#include "log.h"

namespace autoplay {
namespace {

template <typename T>
T Get(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr uintptr_t kTargets = 0x0059D8E8, kTargetCount = 0x005A4C80;
constexpr uintptr_t kFocal = 0x009A2D70;
constexpr uintptr_t kSkelRoot = 0x004E0430;
constexpr uintptr_t kPlayer = 0x009A5C60;  // + p * 0x130: +2 state, +6 lives, +0x1C ammo, +0x26 hits
constexpr uintptr_t kGameMode = 0x009C8E98;
constexpr uintptr_t kCanShoot = 0x009C8E00;
constexpr uintptr_t kHitFlag = 0x009C9010;  // int[p]
constexpr uintptr_t kHitKeys = 0x0059ECD8, kHitEntries = 0x0059F4D0;
constexpr uintptr_t kGunRecord = 0x009C8FC0;  // + p * 0x28: +4/+6 centred aim, +0x16 / +0x1E calibration
constexpr uintptr_t kDeviceMode[2] = { 0x00588E24, 0x007DC698 };

bool IsCivilianFn(uint32_t fn) { return fn == 0x0048A3E0 || fn == 0x0048A920 || fn == 0x0048B140; }

// Stage-3 boss heads (task 0x004209B0, notes/agents/stage4.md section 1): a shot only damages a head when it hits the
// weak node block+0x598 (block = obj+0x1390) while the jaw (nodes block+0x599 / +0x59A, angle +0x0C of the node record
// plus block+0xD8[node]) is open by more than 0x1700 and block+0x5A4 (u16) is not in 0x1701..0xBFFF; head 2 is never
// damaged unless 0x007DC6F1 == 2. Anything else spawns a spark 0x004247D0 (no damage). A damaging hit on a head in the
// attack state 5 interrupts the attack (state 6) before its bite tick block+0x763C (Player_TakeHit 0x00415300).
constexpr uint32_t kBossHeadFn = 0x004209B0;
constexpr uint32_t kBoss2Fn = 0x00476150;
constexpr int kSkelTypes = 86;  // 0x004E0430 has 86 skeleton pointers (types 0x48 / 0x49 are the boss heads)

constexpr uint32_t kBoss5Fn = 0x0047C960;

// "head" = a target with weak nodes (boss heads, and the stage-5 / stage-2 bosses below)
struct HeadInfo {
    bool head = false;
    int index = 0, state = 0, weak = -1, tick = 0, biteTick = 0;
    uint32_t weakMask = 0;  // node slots that take damage (weak, or the boss tables)
    bool damageable = false;
    int jaw = 0, yaw = 0;
    bool havePoint = false;  // the hit must also pass within pointR of this view-space point (final boss 0x00428F00)
    float point[3] = {};
    float pointR = 0;
};
constexpr uint32_t kBoss6Fn = 0x00426C30;  // final boss
constexpr uint32_t kOrbFn = 0x00429D30;    // its orbs (shot first: they hit the player)

HeadInfo ReadHead(uintptr_t a) {
    HeadInfo h;
    h.head = true;
    h.index = Get<int8_t>(a + 0x131B);
    h.state = Get<int16_t>(a + 0x1310);
    h.tick = Get<int>(a + 0x19C);
    const uintptr_t blk = Get<uintptr_t>(a + 0x1390);
    if (blk < 0x10000) return h;
    h.weak = Get<int8_t>(blk + 0x598);
    h.biteTick = Get<int16_t>(blk + 0x763C);
    const int j1 = Get<int8_t>(blk + 0x599), j2 = Get<int8_t>(blk + 0x59A);
    if (h.weak < 0 || h.weak >= 32 || j1 < 0 || j1 >= 32 || j2 < 0 || j2 >= 32) return h;
    int d = (Get<int>(a + 0x218 + j1 * 0x90) - Get<int>(a + 0x218 + j2 * 0x90)) - Get<int>(blk + 0xD8 + j2 * 4) +
            Get<int>(blk + 0xD8 + j1 * 4);
    if (d < 0) d = -d;
    h.jaw = d;
    h.weakMask = 1u << h.weak;
    const uint16_t yaw = Get<uint16_t>(blk + 0x5A4);
    h.yaw = yaw;
    const bool variant2 = Get<int8_t>(0x007DC6F1) == 2;
    h.damageable = d >= 0x1701 && !(yaw > 0x1700 && yaw < 0xC000) && (variant2 || h.index != 2) && h.state != 7 &&
                   h.state != 6;
    return h;
}

struct Stats {
    int shots = 0, hits = 0, misses = 0, civilianSkips = 0, reloads = 0, starts = 0, aimChecks = 0, aimBad = 0;
};

struct PlayerCtl {
    int startHold = 0;           // frames of start still to hold
    uint32_t nextStartTry = 700; // like build\script_p1_start.txt (start at boot frame 700)
    bool played = false;         // a game (mode 6) has been seen: no more title starts (credits)
    bool shotPending = false;    // feedback due (previous frame pressed the trigger at a target)
    uint32_t shotSerial = 0;
    uint32_t shotKey = 0;        // shotSerial, or serial * 64 + node + 1 (HOTD2_INPUT_AUTO_BLOCKNODES)
    uintptr_t shotObj = 0;
    bool shotPrefer = false;     // the shot was at a PREFER target
    int predX = 0, predY = 0;    // centred aim we expect the game to compute
    bool predValid = false;
    uint32_t lastPress = 0;
    int prevLives = 0;
    std::map<uint32_t, int> misses;          // serial -> consecutive shots that hit nothing / something else
    std::map<uint32_t, uint32_t> blockedTo;  // serial -> frame until which the target is skipped
    std::map<uint32_t, int> preferHits;      // serial -> hits on a PREFER target (HOTD2_INPUT_AUTO_PREFER_HITS: then no longer preferred)
    std::map<uint32_t, int> preferMisses;    // serial -> consecutive PREFER shots without a hit (40: given up, no longer preferred)
    Stats st;
};

int g_players = 0;
int g_preferHits = 0;  // HOTD2_INPUT_AUTO_PREFER_HITS=n: a PREFER target hit n times is no longer preferred (0: always)
int g_period = 2;  // frames per trigger cycle (press 1 frame, release period-1 frames)
PlayerCtl g_ctl[2];
// Rules added for the stage-3 boss (HOTD2_INPUT_AUTO_LEGACY=1 turns both off = the script_auto_full.txt behaviour):
bool g_skelLimit = true;  // skeleton types 64..85 are skeleton targets too (was < 64: the boss heads were aimed at as spheres)
bool g_bossHeads = true;  // boss heads: weak node only, only while damageable, attacking heads first
bool g_debug = false;     // HOTD2_INPUT_AUTO_DEBUG=1: boss-head details every 4 frames in the hook log
// HOTD2_INPUT_AUTO_SKIP / HOTD2_INPUT_AUTO_PREFER (route steering): groups "fn[,fn...][@from-to | @S<stage>[:<scene>]]"
// separated by ';' (boot frames, or the event mode 0x009A1A08 / scene 0x009A2BC0 at the time of the shot). An entry
// "fn/off=val" (hex offset, decimal value) also needs the s16 at task block + off to equal val (e.g. 43F720/130C=4: actor
// 0x52 variant 4 only). Civilian functions (0x0048A920, ...) in a PREFER group are aimed at (route steering: a civilian
// killed).
struct FnFilter {
    struct Entry {
        uint32_t fn = 0, off = 0;
        int val = 0;
        bool cond = false;
    };
    struct Group {
        std::vector<Entry> fns;
        uint32_t from = 0, to = 0xFFFFFFFFu;
        int stage = -1, scene = -1;
    };
    std::vector<Group> groups;
    bool Has(uint32_t fn, uintptr_t obj, uint32_t frame) const {
        for (const Group& g : groups) {
            if (frame < g.from || frame > g.to) continue;
            if (g.stage >= 0 && Get<int16_t>(0x009A1A08) != g.stage) continue;
            if (g.scene >= 0 && Get<int16_t>(0x009A2BC0) != g.scene) continue;
            for (const Entry& f : g.fns)
                if (f.fn == fn && (!f.cond || (obj && Get<int16_t>(obj + f.off) == f.val))) return true;
        }
        return false;
    }
};
FnFilter g_skip, g_prefer;
// HOTD2_INPUT_AUTO_NOHEAD=1 or =<from>-<to> (boot frames): aim at the head node (slot 2) last. Body hits score 10 and reset the
// head-shot streak (0x00409430: head +0x78 + streak, streak += 10), so a whole game scores far less (ending branch 0x00436140)
uint32_t g_noHeadFrom = 1, g_noHeadTo = 0;
// HOTD2_INPUT_AUTO_CIVSLACK=<factor>[@from-to]: civilian spheres are widened by this factor (default 1.3) when checking that no
// civilian comes first along the ray (a smaller factor reaches attackers standing just behind a civilian)
double g_civSlack = 1.3;
// HOTD2_INPUT_AUTO_BLOCKNODES=1: a node (not the whole target) that keeps absorbing shots is skipped for 90 frames, so the
// other nodes of a target partly behind level geometry are tried
bool g_blockNodes = false;
uint32_t g_civSlackFrom = 0, g_civSlackTo = 0xFFFFFFFFu;
// HOTD2_INPUT_AUTO_ONLY=from-to[,from-to...]: in these boot frames only the PREFER targets are shot. PREFER targets (sphere targets) are aimed at over a grid of points on their sphere, and a
// point whose ray might hit a civilian node first is skipped (the plain auto player tries the centre only).
struct Windows {
    std::vector<std::pair<uint32_t, uint32_t>> w;
    bool Has(uint32_t frame) const {
        for (const auto& p : w)
            if (frame >= p.first && frame <= p.second) return true;
        return false;
    }
};
Windows g_onlyWin;
// HOTD2_INPUT_AUTO_IDLE=p:from-to[,p:from-to...] (two-player continue / game-over captures, notes/agents/twoplayer2.md):
// player p (1 or 2) presses nothing in those boot frames (no shots, reloads, starts): it is hit until it is out, its
// continue countdown runs out, it does not join.
struct IdleWindow {
    int player;
    uint32_t from, to;
};
std::vector<IdleWindow> g_idle;
bool Idle(int p, uint32_t frame) {
    for (const IdleWindow& w : g_idle)
        if (w.player == p && frame >= w.from && frame <= w.to) return true;
    return false;
}
bool g_p2Title = true;  // =2: P2 joins on the title (mode-4 trigger with P1); HOTD2_INPUT_AUTO_P2=game: start in the game
struct HeadStats {
    int attacks = 0, interrupted = 0, bites = 0, headShots = 0, kills = 0;
} g_head;
std::map<uint32_t, int> g_headPrevState;  // serial -> state last seen

// Raw -> centred tables for the current device mode / calibration (Gun_UpdateRecords 0x0040CBB0).
struct CalTable {
    int cx[640];
    int cy[480];
    bool okx[640], oky[480];
};

void BuildCal(int p, CalTable& t) {
    const uint32_t mode = Get<uint32_t>(kDeviceMode[p]) & 0xFFFF;
    static const int16_t outX[3] = { -264, 0, 264 }, outY[3] = { 192, 0, -192 };
    int16_t calX[4], calY[4];
    for (int i = 0; i < 4; ++i) {
        calX[i] = Get<int16_t>(kGunRecord + p * 0x28 + 0x16 + 2 * i);
        calY[i] = Get<int16_t>(kGunRecord + p * 0x28 + 0x1E + 2 * i);
    }
    for (int x = 0; x < 640; ++x) {
        int v;
        if (mode == 6) {
            static const int16_t c[4] = { -265, 0, 265, 320 };  // 0x0040CD37 (GunCalibrateMode6)
            v = game::GunCalibrate(x, c, outX);
        } else {
            v = game::GunCalibrate(x, calX, outX, 640);
        }
        t.cx[x] = v;
        t.okx[x] = v >= -320 && v <= 319;
    }
    for (int y = 0; y < 480; ++y) {
        int v;
        if (mode == 6) {
            static const int16_t c[4] = { 50, 240, 430, 0 };
            v = game::GunCalibrate(y, c, outY);
        } else {
            v = game::GunCalibrate(y, calY, outY, 640);
        }
        t.cy[y] = v;
        t.oky[y] = v >= -240 && v <= 239;
    }
}

// Raw pixel whose centred value is closest to want (valid only).
int BestRaw(const int* tab, const bool* ok, int n, double want) {
    int best = -1;
    double bd = 1e30;
    for (int i = 0; i < n; ++i) {
        if (!ok[i]) continue;
        const double d = std::fabs(tab[i] - want);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

struct Ray {
    double d[3];
};

Ray MakeRay(int cx, int cy, float focal) {
    Ray r;
    const double v[3] = { static_cast<double>(cx), static_cast<double>(cy), -static_cast<double>(focal) };
    const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) r.d[i] = v[i] / len;
    return r;
}

// Distance of p from the ray's line through the eye; along = projection on the ray (> 0: in front).
double RayDist(const Ray& r, const float* p, double* along = nullptr) {
    const double t = p[0] * r.d[0] + p[1] * r.d[1] + p[2] * r.d[2];
    if (along) *along = t;
    double s = 0;
    for (int i = 0; i < 3; ++i) {
        const double e = p[i] - t * r.d[i];
        s += e * e;
    }
    return std::sqrt(s);
}

bool Finite(const float* p) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }

struct Point {
    float pos[3];
    float radius;
    int node;  // slot, -1 = object sphere
};

struct Obj {
    uintptr_t a;
    uint32_t fn, serial, flags;
    float view[3];
    float radius;
    bool civilian;
    bool skeleton;           // node tests apply
    bool mesh = false;       // a collision-mesh target (PREFER only): pts = view-space centres of its front faces
    std::vector<Point> pts;  // hit points (nodes, or the object sphere)
    HeadInfo head;
};

void CollectNodes(uintptr_t obj, uintptr_t node, std::vector<Point>& out, int depth) {
    if (!node || depth > 64) return;  // (the big boss head 0x48 is a chain of 27 nodes)
    const int16_t children = Get<int16_t>(node + 0x16);
    for (int i = 0; i < children && i < 32; ++i) {
        const uintptr_t c = Get<uintptr_t>(node + 0x18 + 4 * i);
        if (!c) continue;
        const int slot = Get<int16_t>(c + 0x14);
        if (slot >= 0 && slot < 32) {
            const uintptr_t rec = obj + 0x20C + static_cast<uintptr_t>(slot) * 0x90;
            if (Get<uint32_t>(rec) != 0 && !(Get<uint32_t>(rec + 0x74) & 0x10)) {
                Point p;
                for (int k = 0; k < 3; ++k) p.pos[k] = Get<float>(rec + 0x68 + 4 * k);
                p.radius = Get<float>(rec + 0x78);
                p.node = slot;
                if (p.radius > 0 && Finite(p.pos)) out.push_back(p);
            }
        }
        CollectNodes(obj, c, out, depth + 1);
    }
}

// Aim points of a collision-mesh target: the centres of the mesh faces that face the eye, in view space. The mesh
// (obj+0x14C, the collision format of 0x004AAA40: s32 groups, per group {s32 n, box[6], n faces of 72 bytes {a, b, c, d,
// s32 axis, v[4][3], material}}) is in model space; obj+0x150 is its world matrix once Hit_Register 0x00405160 has run
// (camera matrix x model-view); the view matrix is the active camera context's (0x009A6000 + ctx * 0x1A4, ctx
// 0x009C6F00). Row vectors (p' = p M, translation m[12..14]). Largest faces first.
bool MeshPoints(uintptr_t a, std::vector<Point>& out) {
    const uint32_t mesh = Get<uint32_t>(a + 0x14C);
    if (mesh < 0x10000 || mesh == 0xFFFFFFFFu) return false;
    const float* M = reinterpret_cast<const float*>(a + 0x150);
    const int ctx = Get<int>(0x009C6F00) & 3;
    const float* V = reinterpret_cast<const float*>(0x009A6000 + ctx * 0x1A4);
    auto xf = [](const float* m, const float* p, float* o, bool dir) {
        for (int k = 0; k < 3; ++k) o[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + (dir ? 0.0f : m[12 + k]);
    };
    const int groups = Get<int>(mesh);
    if (groups <= 0 || groups > 4096) return false;
    uintptr_t addr = mesh + 4;
    std::vector<std::pair<float, Point>> pts;
    for (int g = 0; g < groups; ++g) {
        const int n = Get<int>(addr);
        if (n < 0 || n > 100000) return false;
        const uintptr_t faces = addr + 28;
        addr = faces + static_cast<uintptr_t>(n) * 72;
        for (int i = 0; i < n; ++i) {
            const float* f = reinterpret_cast<const float*>(faces + static_cast<uintptr_t>(i) * 72);
            float c[3] = { 0, 0, 0 };
            for (int k = 0; k < 4; ++k)
                for (int j = 0; j < 3; ++j) c[j] += f[5 + 3 * k + j] * 0.25f;
            float w[3], vc[3], nw[3], nv[3];
            xf(M, c, w, false);
            xf(V, w, vc, false);
            xf(M, f, nw, true);
            xf(V, nw, nv, true);
            if (!Finite(vc) || vc[2] > -1.0f) continue;
            if (nv[0] * -vc[0] + nv[1] * -vc[1] + nv[2] * -vc[2] <= 0) continue;  // facing away from the eye
            // size: half the shorter diagonal, in view units (approx.)
            float d1 = 0, d2 = 0;
            for (int j = 0; j < 3; ++j) {
                const float e1 = f[5 + 6 + j] - f[5 + j], e2 = f[5 + 9 + j] - f[5 + 3 + j];
                d1 += e1 * e1;
                d2 += e2 * e2;
            }
            const float r = 0.25f * std::sqrt(std::min(d1, d2));
            if (!(r > 0.05f)) continue;
            Point p;
            for (int j = 0; j < 3; ++j) p.pos[j] = vc[j];
            p.radius = r;
            p.node = -1;
            pts.push_back({ r, p });
        }
    }
    std::stable_sort(pts.begin(), pts.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    for (size_t i = 0; i < pts.size() && i < 64; ++i) out.push_back(pts[i].second);
    return !out.empty();
}

std::vector<Obj> ReadTargets(uint32_t frame) {
    std::vector<Obj> v;
    const int n = Get<int>(kTargetCount);
    for (int i = 0; i < n && i < 512; ++i) {
        const uintptr_t a = Get<uintptr_t>(kTargets + i * 20);
        if (a < 0x10000) continue;
        Obj o;
        o.a = a;
        o.fn = Get<uint32_t>(a);
        o.serial = Get<uint32_t>(a + 8);
        o.flags = Get<uint32_t>(a + 0x34);
        for (int k = 0; k < 3; ++k) o.view[k] = Get<float>(a + 0x70 + 4 * k);
        o.radius = Get<float>(a + 0x124);
        o.civilian = IsCivilianFn(o.fn);
        o.skeleton = false;
        o.mesh = false;
        if (o.flags & 0x10) {  // collision-mesh targets (doors, ...): only PREFER ones are aimed at (face centres)
            if (!(o.flags & 0x8000) && g_prefer.Has(o.fn, a, frame) && MeshPoints(a, o.pts)) {
                o.mesh = true;
                for (int k = 0; k < 3; ++k) o.view[k] = o.pts[0].pos[k];
                o.radius = 0;
                v.push_back(std::move(o));
            }
            continue;
        }
        if (!Finite(o.view)) continue;
        if ((o.flags & 0x80) && !(o.flags & 0x8000)) {
            const int kind = Get<int16_t>(a + 0x1F4);
            if (kind >= 0 && kind < (g_skelLimit ? kSkelTypes : 64)) {
                const uintptr_t root = Get<uintptr_t>(kSkelRoot + kind * 4);
                if (root && Get<int16_t>(root + 0x16) > 0) {
                    o.skeleton = true;
                    CollectNodes(a, root, o.pts, 0);
                }
            }
        }
        if (!o.skeleton) o.pts.push_back({ { o.view[0], o.view[1], o.view[2] }, o.radius, -1 });
        if (o.fn == kBossHeadFn && g_bossHeads) o.head = ReadHead(a);
        if (o.fn == kBoss2Fn && g_bossHeads) {
            // stage-2 / stage-5 boss 0x00476150: its hit check 0x00476270 only damages on node slot 1 (else a spark
            // 0x00407340 type 2); flag 0x100 = no more damage
            o.head.head = true;
            o.head.weak = 1;
            o.head.weakMask = 1u << 1;
            o.head.damageable = !(o.flags & 0x100);
            // flag 0x80000 (the walk-in states 1 / 2 and the death states): a damaging hit during the stage-5 rematch's
            // walk-in leaves it away from its start point; phase 9 later sends it home along the arena wall forever
            // (state 6 0x00478C00 with +0x34 & 0x100: no damage, the game stalls; runs game3 / game4, routes.md section 5)
            if (o.flags & 0x80000) o.head.damageable = false;
        }
        if (o.fn == kBoss5Fn && g_bossHeads) {
            // stage-5 boss 0x0047C960: 0x0047CE10 damages on node slots 4, 6, 11, 13 only (else a spark); flag 0x100
            // (a phase) or 0x4000000 (dead): sparks only
            o.head.head = true;
            o.head.weak = 4;
            o.head.weakMask = (1u << 4) | (1u << 6) | (1u << 11) | (1u << 13);
            o.head.damageable = !(o.flags & 0x4000100);
            // flag 0x4000 (animation frozen; set from the start until the first volley 0x0047D410 or the final phase
            // clears it): a damaging hit then starts the stagger (behaviour 5, 0x0047CA50), which waits for the frozen
            // motion to end - forever. The original soft-locks (2p_auto run, frame 66343: hit during the first move,
            // behaviour 6). Never shoot it then.
            if (o.flags & 0x4000) o.head.damageable = false;
        }
        if (o.fn == kBoss6Fn && g_bossHeads) {
            // final boss 0x00426C30 (states 0x005898A8), hit check 0x00428DA0: damage only on node slot 1, sub-state +0x1312
            // in 1..5, flags without 0x100 / 0x40000000, and the ray within +0x1374 of node 1's matrix (obj+0x2C4) applied
            // to (2.3121, 0.1097, 0) (0x00428F00). A damaging hit during the charge (sub-pattern 3) stops it.
            o.head.head = true;
            o.head.weak = 1;
            o.head.weakMask = 1u << 1;
            const int sub = Get<int16_t>(a + 0x1312);
            o.head.state = sub;
            o.head.damageable = !(o.flags & 0x40000100) && sub > 0 && sub < 6;
            const float* m = reinterpret_cast<const float*>(a + 0x2C4);
            const float lx = 2.3121f, ly = 0.1097f;  // 0x4013F972, 0x3DE0AA65
            for (int k = 0; k < 3; ++k) o.head.point[k] = lx * m[k] + ly * m[4 + k] + m[12 + k];
            o.head.pointR = Get<float>(a + 0x1374);
            o.head.havePoint = Finite(o.head.point) && o.head.pointR > 0;
            if (!o.head.havePoint) o.head.damageable = false;
        }
        v.push_back(std::move(o));
    }
    return v;
}

int Key(float viewZ) { return static_cast<int>(-viewZ * 10.0f) & 0xFFFF; }

// Can the ray hit obj at point index pi? Returns the entry key, or -1.
// slack > 1 widens the spheres (conservative "might hit" test), < 1 narrows them ("surely hits").
int TestEntry(const Ray& r, const Obj& o, const Point& p, double slack) {
    double along;
    if (o.skeleton) {
        if (RayDist(r, o.view, &along) > o.radius * slack || along <= 0) return -1;
    }
    if (RayDist(r, p.pos, &along) > p.radius * slack || along <= 0) return -1;
    return Key(p.pos[2]);
}

}  // namespace

void Init(int players) {
    g_players = players;
    char buf[16] = {};
    if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_PERIOD", buf, sizeof(buf)) > 0) g_period = std::max(2, atoi(buf));
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_PREFER_HITS", buf, sizeof(buf)) > 0) g_preferHits = atoi(buf);
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_LEGACY", buf, sizeof(buf)) > 0 && buf[0] == '1') {
        g_skelLimit = g_bossHeads = false;
        Log("auto input: legacy target rules (script_auto_full.txt behaviour)");
    }
    buf[0] = 0;
    g_debug = GetEnvironmentVariableA("HOTD2_INPUT_AUTO_DEBUG", buf, sizeof(buf)) > 0 && buf[0] == '1';
    // Route steering (notes/agents/routes.md): HOTD2_INPUT_AUTO_SKIP / _PREFER = fn[,fn...][@from-to] (task functions in
    // hex, boot frames decimal or 0x):
    // targets with these task functions are never / always first aimed at in that window
    for (FnFilter* f : { &g_skip, &g_prefer }) {
        char all[2048] = {};
        if (GetEnvironmentVariableA(f == &g_skip ? "HOTD2_INPUT_AUTO_SKIP" : "HOTD2_INPUT_AUTO_PREFER", all, sizeof(all)) == 0) continue;
        for (char* v = all; v && *v;) {
            char* next = strchr(v, ';');
            if (next) *next++ = 0;
            FnFilter::Group g;
            char* at = strchr(v, '@');
            if (at) {
                *at = 0;
                if (at[1] == 'S' || at[1] == 's') {
                    char* e = nullptr;
                    g.stage = static_cast<int>(strtol(at + 2, &e, 10));
                    if (e && *e == ':') g.scene = static_cast<int>(strtol(e + 1, nullptr, 10));
                } else {
                    g.from = static_cast<uint32_t>(strtoul(at + 1, nullptr, 0));
                    if (const char* dash = strchr(at + 1, '-')) g.to = static_cast<uint32_t>(strtoul(dash + 1, nullptr, 0));
                }
            }
            for (char* s = v; *s;) {
                char* e = s;
                FnFilter::Entry en;
                en.fn = static_cast<uint32_t>(strtoul(s, &e, 16));
                if (e == s) break;
                if (*e == '/') {  // fn/off=val
                    en.off = static_cast<uint32_t>(strtoul(e + 1, &e, 16));
                    if (*e == '=') en.val = static_cast<int>(strtol(e + 1, &e, 10));
                    en.cond = true;
                }
                g.fns.push_back(en);
                s = e;
                while (*s == ',' || *s == ' ') ++s;
            }
            Log("auto input: %s %zu task function(s), frames %u..%u, stage %d scene %d", f == &g_skip ? "skip" : "prefer", g.fns.size(), g.from, g.to, g.stage, g.scene);
            f->groups.push_back(g);
            v = next;
        }
    }
    {
        char v[512] = {};
        if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_ONLY", v, sizeof(v)) > 0) {
            for (char* s = v; *s;) {
                const uint32_t a = static_cast<uint32_t>(strtoul(s, &s, 0));
                uint32_t b = a;
                if (*s == '-') b = static_cast<uint32_t>(strtoul(s + 1, &s, 0));
                g_onlyWin.w.push_back({ a, b });
                Log("auto input: only PREFER targets in frames %u..%u", a, b);
                while (*s == ',' || *s == ' ') ++s;
                if (*s && !isdigit(static_cast<unsigned char>(*s))) break;
            }
        }
    }
    {
        {
        char v[64] = {};
        if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_NOHEAD", v, sizeof(v)) > 0) {
            if (const char* dash = strchr(v, '-')) {
                g_noHeadFrom = static_cast<uint32_t>(strtoul(v, nullptr, 0));
                g_noHeadTo = static_cast<uint32_t>(strtoul(dash + 1, nullptr, 0));
            } else if (v[0] == '1') {
                g_noHeadFrom = 0;
                g_noHeadTo = 0xFFFFFFFFu;
            }
            Log("auto input: head node last, frames %u..%u", g_noHeadFrom, g_noHeadTo);
        }
        v[0] = 0;
        g_blockNodes = GetEnvironmentVariableA("HOTD2_INPUT_AUTO_BLOCKNODES", v, sizeof(v)) > 0 && v[0] == '1';
        if (g_blockNodes) Log("auto input: blocked shots skip the node, not the target");
        v[0] = 0;
        if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_CIVSLACK", v, sizeof(v)) > 0) {
            g_civSlack = atof(v);
            if (char* at = strchr(v, '@')) {
                g_civSlackFrom = static_cast<uint32_t>(strtoul(at + 1, nullptr, 0));
                if (const char* dash = strchr(at + 1, '-')) g_civSlackTo = static_cast<uint32_t>(strtoul(dash + 1, nullptr, 0));
            }
            Log("auto input: civilian slack %.2f, frames %u..%u", g_civSlack, g_civSlackFrom, g_civSlackTo);
        }
        }
        {
        char v[512] = {};
        if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_IDLE", v, sizeof(v)) > 0) {
            for (char* s = v; *s;) {
                IdleWindow w{};
                w.player = static_cast<int>(strtol(s, &s, 10)) - 1;
                if (*s == ':') ++s;
                w.from = static_cast<uint32_t>(strtoul(s, &s, 0));
                w.to = w.from;
                if (*s == '-') w.to = static_cast<uint32_t>(strtoul(s + 1, &s, 0));
                if (w.player >= 0 && w.player < 2) {
                    g_idle.push_back(w);
                    Log("auto input: P%d idle in frames %u..%u", w.player + 1, w.from, w.to);
                }
                while (*s == ',' || *s == ' ') ++s;
            }
        }
        buf[0] = 0;
        if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_P2", buf, sizeof(buf)) > 0 && strcmp(buf, "game") == 0) g_p2Title = false;
        if (players == 2) Log("auto input: P2 joins %s", g_p2Title ? "on the title (mode-4 trigger with P1)" : "in the game (start)");
        }
    }
    Log("auto input: %d player(s), trigger period %d frames (AUTOMATIC input from game memory, not device input)", players,
        g_period);
}

int Players() { return g_players; }

void LogStats() {
    for (int p = 0; p < g_players; ++p) {
        const Stats& s = g_ctl[p].st;
        Log("auto input P%d: shots %d (hit %d, no/other hit %d), civilian-blocked points %d, reloads %d, starts %d, "
            "aim checks %d (mismatch %d)",
            p + 1, s.shots, s.hits, s.misses, s.civilianSkips, s.reloads, s.starts, s.aimChecks, s.aimBad);
    }
    if (g_head.attacks)
        Log("auto input boss heads: attacks %d (interrupted %d, completed %d), weak-node shots %d, heads down %d", g_head.attacks,
            g_head.interrupted, g_head.bites, g_head.headShots, g_head.kills);
}

Decision Decide(int p, uint32_t frame, const Decision& prev) {
    PlayerCtl& c = g_ctl[p];
    Decision d = prev;
    d.buttons = 0;

    // Feedback for the shot of the previous frame (its hit task has run).
    if (c.shotPending) {
        c.shotPending = false;
        if (c.predValid) {
            ++c.st.aimChecks;
            const int gx = Get<int16_t>(kGunRecord + p * 0x28 + 4), gy = Get<int16_t>(kGunRecord + p * 0x28 + 6);
            if (gx != c.predX || gy != c.predY) {
                if (++c.st.aimBad <= 10) Log("auto input P%d frame %u: aim (%d,%d) predicted (%d,%d)", p + 1, frame, gx, gy, c.predX, c.predY);
            }
        }
        uintptr_t hitObj = 0;
        if (Get<int>(kHitFlag + 4 * p) == 1) {
            const int idx = Get<int>(kHitKeys + 4);
            if (idx >= 0 && idx < 4096) hitObj = Get<uintptr_t>(kHitEntries + idx * 0x3C + 0x28);
        }
        if (hitObj && hitObj == c.shotObj) {
            ++c.st.hits;
            if (c.shotPrefer) {
                ++c.preferHits[c.shotSerial];
                Log("auto input P%d frame %u: PREFER target %u hit", p + 1, frame, c.shotSerial);
            }
            c.misses[c.shotKey] = 0;
            c.preferMisses[c.shotSerial] = 0;
        } else {
            ++c.st.misses;
            if (c.shotPrefer && ++c.preferMisses[c.shotSerial] == 40)
                Log("auto input P%d frame %u: PREFER target %u given up after 40 shots without a hit", p + 1, frame, c.shotSerial);
            if (++c.misses[c.shotSerial] >= 3) {
                c.blockedTo[c.shotSerial] = frame + 90;
                c.misses[c.shotKey] = 0;
            }
        }
    }

    const uintptr_t P = kPlayer + p * 0x130;
    const int state = Get<int16_t>(P + 2), lives = Get<int16_t>(P + 6), ammo = Get<int16_t>(P + 0x1C);
    const int mode = Get<int16_t>(kGameMode);

    // Boss-head bookkeeping (statistics / log only).
    if (p == 0 && g_bossHeads) {
        const int n = Get<int>(kTargetCount);
        for (int i = 0; i < n && i < 512; ++i) {
            const uintptr_t a = Get<uintptr_t>(kTargets + i * 20);
            if (a < 0x10000 || Get<uint32_t>(a) != kBossHeadFn) continue;
            const uint32_t serial = Get<uint32_t>(a + 8);
            const int hs = Get<int16_t>(a + 0x1310);
            if (g_debug && frame % 4 == 0) {
                const HeadInfo h = ReadHead(a);
                const uintptr_t rec = a + 0x20C + static_cast<uintptr_t>(h.weak & 31) * 0x90;
                Log("head %u f%u idx %d st %d tick %d bite %d jaw %X yaw %X dmg %d fl %X | weak %d model %X nfl %X r %.2f z %.1f "
                    "| fight %d var %d hp %d",
                    serial, frame, h.index, h.state, h.tick, h.biteTick, h.jaw, h.yaw, h.damageable ? 1 : 0,
                    Get<uint32_t>(a + 0x34), h.weak, Get<uint32_t>(rec), Get<uint32_t>(rec + 0x74), Get<float>(rec + 0x78),
                    Get<float>(rec + 0x70), Get<int8_t>(0x007DC738), Get<int8_t>(0x007DC6F1), Get<int16_t>(a + 0x11C));
            }
            auto it = g_headPrevState.find(serial);
            const int prevState = it == g_headPrevState.end() ? -1 : it->second;
            if (hs != prevState) {
                if (hs == 5) ++g_head.attacks;
                if (prevState == 5 && hs == 6) ++g_head.interrupted;
                if (prevState == 5 && hs == 4) ++g_head.bites;
                if (hs == 7 && prevState != -1) {
                    ++g_head.kills;
                    Log("auto input: boss head %u (index %d) down at frame %u", serial, Get<int8_t>(a + 0x131B), frame);
                }
                g_headPrevState[serial] = hs;
            }
        }
    }
    if (lives < c.prevLives && mode == 6) {
        Log("auto input P%d frame %u: life lost (%d -> %d); last shot at serial %u (frame %u, %d shots / %d hits so far)", p + 1,
            frame, c.prevLives, lives, c.shotSerial, c.lastPress, c.st.shots, c.st.hits);
        const int n = Get<int>(kTargetCount);
        for (int i = 0; i < n && i < 64; ++i) {
            const uintptr_t a = Get<uintptr_t>(kTargets + i * 20);
            if (a < 0x10000) continue;
            Log("  target %X serial %u flags %X skel %d view %.1f %.1f %.1f r %.2f", Get<uint32_t>(a), Get<uint32_t>(a + 8),
                Get<uint32_t>(a + 0x34), Get<int16_t>(a + 0x1F4), Get<float>(a + 0x70), Get<float>(a + 0x74), Get<float>(a + 0x78),
                Get<float>(a + 0x124));
        }
    }
    c.prevLives = lives;

    if (Idle(p, frame)) {  // HOTD2_INPUT_AUTO_IDLE
        c.startHold = 0;
        if (mode == 6) c.played = true;
        return d;
    }
    // Start: hold it like the scripts (12 frames); the trigger on the screen after the title (game mode 4) 3 frames.
    if (c.startHold > 0) {
        --c.startHold;
        d.buttons = prev.buttons;
        return d;
    }
    // Game mode 4 (after start on the title) waits for a trigger pull: like build\script_p1_start.txt (start 700..711,
    // trigger at the centre 900..902), pull it 200 frames after the start press. Two players (default): P2 pulls its
    // trigger on the same frame: the idle player tasks of mode 4 join on start or trigger (0x00414FC0, 0x00413230), so
    // both start the game (P2 state 0).
    if (mode == 4 && (p == 0 || (g_players == 2 && g_p2Title)) && frame >= c.nextStartTry - 400 && prev.buttons == 0) {
        c.startHold = 2;
        c.nextStartTry = frame + 700;
        Log("auto input P%d frame %u: trigger (game mode 4)", p + 1, frame);
        d.x = d.y = 0.5f;
        d.buttons = 1;
        return d;
    }
    if (mode == 6) c.played = true;
    const bool title = mode != 6 && mode != 7 && mode != 4 && !c.played;
    const bool cont = mode == 6 && state == 4;               // continue countdown
    const bool join = mode == 6 && state == 9 && p == 1;     // P2 idle in a game (two-player auto)
    if ((title || cont || join) && frame >= c.nextStartTry && prev.buttons == 0) {
        c.startHold = 11;
        c.nextStartTry = frame + (title ? 600 : 120);
        ++c.st.starts;
        Log("auto input P%d frame %u: start (game mode %d, player state %d)", p + 1, frame, mode, state);
        d.x = d.y = 0.5f;
        d.buttons = 4;
        return d;
    }

    if (mode != 6 || state != 5 || lives <= 0 || Get<int>(kCanShoot) == 0) return d;
    if (prev.buttons != 0 || frame - c.lastPress < static_cast<uint32_t>(g_period)) return d;
    if (g_players == 2 && static_cast<int>(frame & 1) != p) return d;  // one shooter per frame (feedback entries)

    if (ammo <= 0) {
        if (g_debug) Log("auto f%u: reload (empty)", frame);
        c.lastPress = frame;
        ++c.st.reloads;
        d.buttons = 2;
        return d;
    }

    // ---- target choice
    const float focal = Get<float>(kFocal);
    if (!(focal > 1.0f && focal < 100000.0f)) return d;
    std::vector<Obj> objs = ReadTargets(frame);
    CalTable cal;
    BuildCal(p, cal);

    struct Cand {
        double score;
        size_t obj;
    };
    std::vector<Cand> cands;
    const bool only = g_onlyWin.Has(frame);
    auto preferred = [&](const Obj& o) {
        if (!g_prefer.Has(o.fn, o.a, frame)) return false;
        auto m = c.preferMisses.find(o.serial);
        if (m != c.preferMisses.end() && m->second >= 40) return false;
        auto h = c.preferHits.find(o.serial);
        // (a PREFER civilian is shot until it dies: one hit does not kill it)
        return g_preferHits <= 0 || o.civilian || h == c.preferHits.end() || h->second < g_preferHits;
    };
    for (size_t i = 0; i < objs.size(); ++i) {
        const Obj& o = objs[i];
        if ((o.civilian && !preferred(o)) || (o.flags & 0x4000000) || (o.flags & 0x8000)) continue;
        if (o.view[2] > -1.0f) continue;
        if (g_skip.Has(o.fn, o.a, frame)) continue;
        if (only && !preferred(o)) continue;
        auto bl = c.blockedTo.find(o.serial);
        if (!g_blockNodes && !o.head.head && !preferred(o) && bl != c.blockedTo.end() && frame < bl->second) continue;  // (weak-node targets: never)
        const double dist = -o.view[2];
        double score = o.skeleton ? dist : dist * 3.0 + 100.0;  // skeleton actors (enemies) first
        if (o.head.head) {
            if (!o.head.damageable) continue;  // a shot would only spark
            // attacking heads first (nearest to the bite tick), then the other damageable heads
            score = o.head.state == 5 && o.fn == kBossHeadFn ? -100000.0 + (o.head.biteTick - o.head.tick) : -50000.0 + dist;
        }
        if (o.fn == kOrbFn && g_bossHeads) score = -200000.0 + dist;  // the final boss's orbs: nearest first
        if (preferred(o)) score = -300000.0 + dist;
        cands.push_back({ score, i });
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });

    for (const Cand& cd : cands) {
        const Obj& o = objs[cd.obj];
        // points: head (slot 2) first, then by radius; boss heads: the weak node only, its centre then 4 offsets
        std::vector<Point> pts;
        std::vector<Point> real;  // heads: the node sphere each aim point belongs to
        if (o.head.head) {
            std::vector<const Point*> weak;
            for (const Point& pt : o.pts)
                if (pt.node >= 0 && pt.node < 32 && (o.head.weakMask >> pt.node & 1)) weak.push_back(&pt);
            std::stable_sort(weak.begin(), weak.end(), [](const Point* a, const Point* b) { return a->radius > b->radius; });
            for (const Point* pt : weak) {
                Point c = *pt;
                float rr = pt->radius;
                if (o.head.havePoint) {  // aim at the extra test point instead (it must be passed too)
                    for (int k = 0; k < 3; ++k) c.pos[k] = o.head.point[k];
                    rr = std::min(rr, o.head.pointR);
                }
                pts.push_back(c);
                real.push_back(*pt);
                static const float off[4][2] = { { 0.5f, 0 }, { -0.5f, 0 }, { 0, 0.5f }, { 0, -0.5f } };
                for (const auto& f : off) {
                    Point q = c;
                    q.pos[0] += f[0] * rr;
                    q.pos[1] += f[1] * rr;
                    pts.push_back(q);
                    real.push_back(*pt);
                }
            }
        } else {
            pts = o.pts;
            // (not the stage-4 boss 0x004919D0: it takes damage on the head; the other bosses use the weak-node rules)
            const bool noHead = frame >= g_noHeadFrom && frame <= g_noHeadTo && o.fn != 0x004919D0;
            std::stable_sort(pts.begin(), pts.end(), [noHead](const Point& a, const Point& b) {
                const int ha = (a.node == 2) != noHead ? 0 : 1, hb = (b.node == 2) != noHead ? 0 : 1;
                if (ha != hb) return ha < hb;
                return a.radius > b.radius;
            });
            if (preferred(o) && !o.skeleton && !o.mesh && !pts.empty()) {
                // a grid over the sphere (view x / y offsets up to 0.7 r), nearest to the centre first
                const Point c0 = pts[0];
                std::vector<std::pair<float, Point>> grid;
                for (int iy = -7; iy <= 7; ++iy)
                    for (int ix = -7; ix <= 7; ++ix) {
                        const float fx = ix * 0.1f, fy = iy * 0.1f;
                        if (fx * fx + fy * fy > 0.49f + 1e-4f || (ix == 0 && iy == 0)) continue;
                        Point q = c0;
                        q.pos[0] += fx * c0.radius;
                        q.pos[1] += fy * c0.radius;
                        grid.push_back({ fx * fx + fy * fy, q });
                    }
                std::stable_sort(grid.begin(), grid.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                real = pts;
                for (const auto& g : grid) {
                    pts.push_back(g.second);
                    real.push_back(c0);
                }
            }
        }
        int why[4] = {};  // debug (heads): behind, off screen, node sphere missed, occluded
        uint32_t occluder = 0;
        int occNode = 0;
        if (o.head.head && pts.empty() && g_debug) Log("auto f%u: head %u no weak node point", frame, o.serial);
        for (size_t pi = 0; pi < pts.size(); ++pi) {
            const Point& aimPt = pts[pi];
            const Point* pt = &aimPt;
            const uint32_t bkey = g_blockNodes ? o.serial * 64u + static_cast<uint32_t>(aimPt.node + 1) : o.serial;
            if (g_blockNodes && !o.head.head) {
                auto nb = c.blockedTo.find(bkey);
                if (nb != c.blockedTo.end() && frame < nb->second) continue;
            }
            if (pt->pos[2] > -1.0f) {
                ++why[0];
                continue;
            }
            const double wantX = focal * pt->pos[0] / -pt->pos[2], wantY = focal * pt->pos[1] / -pt->pos[2];
            const int rx = BestRaw(cal.cx, cal.okx, 640, wantX), ry = BestRaw(cal.cy, cal.oky, 480, wantY);
            if (rx < 0 || ry < 0) {
                ++why[1];
                continue;
            }
            const Ray r = MakeRay(cal.cx[rx], cal.cy[ry], focal);
            int key;
            if (o.head.head) {
                // the true node sphere must surely be hit, and no other node / sphere may come first along the ray
                const Point& node = real[pi];
                key = TestEntry(r, o, node, 0.85);
                double along = 0;
                if (key >= 0 && o.head.havePoint &&
                    (RayDist(r, o.head.point, &along) > o.head.pointR * 0.85 || along <= 0))
                    key = -1;
                if (key < 0) {
                    ++why[2];
                    continue;
                }
                bool first = true;
                for (const Obj& q : objs) {
                    if (q.flags & 0x8000) continue;
                    for (const Point& qp : q.pts) {
                        if (&q == &o && qp.node >= 0 && qp.node < 32 && (o.head.weakMask >> qp.node & 1)) continue;
                        const int k = TestEntry(r, q, qp, 1.05);
                        if (k >= 0 && k <= key) {
                            first = false;
                            occluder = q.serial;
                            occNode = qp.node;
                        }
                    }
                }
                if (!first) {
                    ++why[3];
                    continue;
                }
                ++g_head.headShots;
                if (g_debug) Log("auto f%u: shoot head %u node %d (point %zu) ammo %d", frame, o.serial, node.node, pi, ammo);
            } else {
                key = real.empty() ? TestEntry(r, o, *pt, 0.8) : TestEntry(r, o, real[pi], 0.9);
                if (key >= 0 && !real.empty())
                    Log("auto f%u: P%d prefer shot %X serial %u point %zu z %.1f", frame, p + 1, o.fn, o.serial, pi, pt->pos[2]);
                if (key >= 0 && g_debug)
                    Log("auto f%u: shoot %X %u node %d z %.1f ammo %d", frame, o.fn, o.serial, pt->node, pt->pos[2], ammo);
            }
            if (key < 0) continue;
            // nothing civilian may come first (or tie) along this ray
            bool blocked = false;
            for (const Obj& q : objs) {
                if (!q.civilian || (&q == &o)) continue;  // (a PREFER civilian: the target itself does not block)
                for (const Point& qp : q.pts) {
                    const double slack = frame >= g_civSlackFrom && frame <= g_civSlackTo ? g_civSlack : 1.3;
                    const int k = TestEntry(r, q, qp, slack);
                    if (k >= 0 && k <= key) blocked = true;
                }
            }
            if (blocked) {
                ++c.st.civilianSkips;
                continue;
            }
            d.x = static_cast<float>((rx + 0.5) / 640.0);
            d.y = static_cast<float>((ry + 0.5) / 480.0);
            d.buttons = 1;
            c.lastPress = frame;
            c.shotPending = true;
            c.shotObj = o.a;
            c.shotSerial = o.serial;
            c.shotKey = bkey;
            c.shotPrefer = preferred(o);
            c.predX = cal.cx[rx];
            c.predY = cal.cy[ry];
            c.predValid = true;
            ++c.st.shots;
            return d;
        }
        if (o.head.head && g_debug)
            Log("auto f%u: head %u (state %d) not shot: behind %d off %d sphere %d occluded %d (by %u node %d)", frame, o.serial,
                o.head.state, why[0], why[1], why[2], why[3], occluder, occNode);
    }

    if (ammo < 6) {
        c.lastPress = frame;
        ++c.st.reloads;
        d.buttons = 2;
    }
    return d;
}

}  // namespace autoplay
