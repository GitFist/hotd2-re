// game::World - scripted actors of the stage-1 intro in game mode 6 (the attract demo starts after them): voice lines
// / subtitles, the cutscene skip watcher, the stage title card, the players' car and its model patch task, the intro
// characters (actor 0x22) and the far background characters (actor 0x20), komono kinds 40 / 41.
// CONFIRMED = code read + frame-exact against build\scene_capture_play1full.txt; CODE = read, not exercised;
// PLACEHOLDER = task with the original function id and lifetime only (no draws), marked in World::Notes().

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"
#include "game/world_st5.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

struct VoiceTask : WTask {
    int16_t line = 0, time = 0, sub = 0;  // +0x34 / +0x36 / +0x38
};
struct TitleTask : WTask {
    int16_t timer = 0, sub = 0;  // obj +0x11C / +0x1312
};
struct CarTask : WTask {
    float pos[3] = {};      // +0x40
    int rot[3] = {};        // +0x64 / +0x68 / +0x6C
    int active = 0;         // +0x1320 wheels turning
    int twoPlayers = 0;     // +0x1324
    int spin = 0;           // +0x1330 wheel angle
    int doorYaw = 0;        // +0x1334
};
struct CarLightTask : WTask {
    int loaded = 0;  // +0x84 car models patched
};
struct Actor20Task : EnemyTask {  // 0x00448ED0: distant character (skeleton P[0], variant P[1])
    int dying = 0;  // +0x1330 frames left while sinking (0x00449430)
    int hold = 0;   // +0x1334 training lesson 6 (scene 0xD): held after the lesson's end
};
struct PlaceholderTask : WTask {
    SpawnRecord rec;
};
// Komono kind 40 piece set (task 0x0046C570, block 0xD14): a small breakable that shatters into 40 pieces
struct Komono40Task : ActorTask {
    int16_t life = 0;            // +0x11C areas it survives
    float scale = 1.0f;          // +0x118
    float pos[3] = {};           // +0x194
    int rot[3] = {};             // +0x1AC / +0x1B0 / +0x1B4
    int8_t index = 0;            // +0x1B8
    int8_t broken = 0;           // +0x1B9
    int8_t kind = 0;             // +0x1BA (komono P0)
    int8_t area = 0, areaCount = 0;  // +0x1BB / +0x1BC
    int frames = 0;              // +0x1C0 frames since broken
    int16_t model = 0;           // +0x1E0
    float piece[40][3] = {};     // +0x238
    int16_t pieceAng[40][3] = {};  // +0x90E
    float pieceVel[40][3] = {};  // +0x5A0
    int16_t pieceSpin[40][3] = {};  // +0xA92
};
// Komono kind 41 (task 0x0046CC50, block 0x378): two shutters that swing open when shot
struct Komono41Task : ActorTask {
    int16_t life = 0;           // +0x11C
    float pos[3] = {};          // +0x19C
    int rot[3] = {};            // +0x1CC / +0x1D0 / +0x1D4
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int8_t shot = 0;            // +0x192
    int16_t ang[2] = {};        // +0x200 / +0x206
    float part[2][3] = {};      // +0x22C / +0x238
    int speed0 = 0, speed1 = 0, braked = 0;  // +0x2A0 / +0x2A4 / +0x2AC
};

}  // namespace

// Player choice + score of a prop hit (0x004650F0): the shooter (rand when both), +10 points, hits + 1
static int PropHitPlayer(World::Impl& w, uint32_t flags, bool score) {
    int p;
    if ((flags & 2) && (flags & 4)) p = w.Rand(0) & 1;
    else p = (flags & 2) ? 0 : 1;
    if (score) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

// Camera path evaluation 0x004042D0(id, t): position (float) and the three other channels __ftol'd (+0x64..+0x6C)
static void EvalPath(const CameraBank& bank, int id, float t, float pos[3], int ang[3]) {
    float p[3] = {}, tg[3] = {};
    int roll = 0;
    if (!bank.Evaluate(id, t, p, tg, &roll, false)) return;
    memcpy(pos, p, 12);
    for (int k = 0; k < 3; ++k) ang[k] = static_cast<int>(tg[k]);
}

// Snd_PlayVoiceLine 0x00435B80 (op 0x2D): line 0x0058B6B8[id * 3 + players in game] (modes 10 / 11: id * 3), its sound
// 0x00589DA8[line] + 0xC and the subtitle task 0x00435AA0 (duration +2); nothing while a cutscene is skipped
void RandomizerClearSubtitles(World::Impl& w) {
    // The voice channel plays one file. A longer randomized line must lose its caption when
    // the next voice interrupts it, without changing the event script's camera/action timing.
    auto voiced = [&](int line) { return (w.exe.U32(0x00589DA8 + line * 16 + 0xC) >> 28) == 2; };
    for (auto* task : w.ts.Live())
        if (auto* voice = dynamic_cast<VoiceTask*>(task->data.get()); voice && voiced(voice->line)) voice->time = 1;
    auto& subtitles = w.hudSubtitles;
    subtitles.erase(std::remove_if(subtitles.begin(), subtitles.end(), [&](const auto& sub) { return voiced(sub.group); }), subtitles.end());
}

void World::Impl::Voice(int id) {
    const int idx = id * 3 + ((gameMode == 10 || gameMode == 11) ? 0 : playersInGame);
    int line = exe.S16(0x0058B6B8 + static_cast<uint32_t>(idx) * 2);
    if (line == 0 || runner->skipping || skip2230 != 0) return;
    // SoundCall remaps the original sound using the same key as the subtitle group.
    const uint32_t sound = exe.U32(0x00589DA8 + static_cast<uint32_t>(line) * 16 + 0xC);
    const int replacement = RandomizerVoiceLine(*this, line);
    if (replacement != line) Note("randomizer: dialogue " + std::to_string(line) + " -> " + std::to_string(replacement));
    line = replacement;
    const uint32_t r = 0x00589DA8 + static_cast<uint32_t>(line) * 16;
    if (sound != 0) Sound(sound);
    VoiceTask* t = NewTask<VoiceTask>(WorldTramp<&Impl::TaskVoice>, 0x00435AA0);
    t->line = static_cast<int16_t>(line);
    t->time = exe.S16(r + 2);
    t->sub = 0;
}

// 0x00435AA0: subtitle sprites (0x00436850, HUD) until the line's time runs out or the cutscene is skipped
void World::Impl::TaskVoice(Task& t) {
    VoiceTask& v = t.As<VoiceTask>();
    if (--v.time != 0 && !runner->skipping && skip2230 == 0) {
        const int idx = v.line * 4;
        const int seg = exe.S16(0x005919A8 + static_cast<uint32_t>(v.sub + idx) * 2);
        if (v.time < exe.S16(0x0058BCA6 + static_cast<uint32_t>(seg) * 0x40)) ++v.sub;
        hudSubtitles.push_back({ v.line, v.sub });  // drawn by the HUD (0x00436850)
        return;
    }
    ts.KillCurrent();
}

// Actor 0x63 (0x00435F20 -> 0x00435F40): cutscene skip watcher. While the scene is skippable (0x009A2D7C) a skip
// request (reload while the letterbox is closed, 0x00414940 -> 0x009A1A18) ends the camera segment, sets skipping
// (0x009A2D74) and 0x009A2230, flushes the resource queue and becomes 0x00435FA0 (clears 0x009A2230, ends).
void World::Impl::TaskSkipWatch(Task& t) {
    WTask& w = t.As<WTask>();
    if (w.fn == fn::kSkipWatch) {
        skipRequest = false;
        w.fn = fn::kSkipWatch2;
    } else if (w.fn == fn::kSkipWatch3) {
        skip2230 = 0;
        runner->globals[0x009A2230] = 0;
        ts.KillCurrent();
    }
    if (!runner->skippable) ts.KillCurrent();
    if (!skipRequest) return;
    if (runner->camera.endTick != runner->camera.playTick) runner->camera.endTick = runner->camera.playTick;  // 0x009A6148
    skipRequest = false;
    runner->skipping = true;
    skip2230 = 1;
    runner->globals[0x009A2230] = 1;  // civilian actors read this through GlobalInt (e.g. Goldman's speech task)
    Resource(0x58, 0);  // Res_FlushRequests 0x0041D970
    w.fn = fn::kSkipWatch3;
}

// Actor 0x60 (0x004342E0): stage title card (sprites, HUD), 0xB4 frames or until reload; hides the letterbox bars and
// the car (0x009A5900 | 0x20) and sets event flag 0xF8 when done
void World::Impl::TaskTitle(Task& t) {
    TitleTask& a = t.As<TitleTask>();
    if (a.sub == 0) {
        flags5900 |= 0x20;
        // 0x0040E140(&0x009A3540, active context pitch / yaw) and 0x0040C2C0(0.7): light from the camera, intensity 0.7
        // (renderer state, world_light.cpp)
        env.lightPitch = cam.pitch;
        env.lightYaw = cam.yaw;
        env.lightIntensity = 0.7f;      // 0x009A378C
        env.chan[0][8].value = 0.7f;    // 0x009C8A64
        LightIntensity(0.7f);           // 0x004AA070
        a.sub = 1;
        a.timer = 0xB4;
    }
    if (a.sub == 1 && cfg.eventMode == 5) {  // stage 6 title draws model 0x1730
        Mat4 m = Mat4::Identity();
        mx::Translate(m, 0.0f, 0.0f, -30.0f);
        mx::RotY(m, 0x8000);
        mx::Scale(m, 2.0f, 2.0f, 2.0f);
        Draw(0x1730, m, 8, -1.0f, false, -1, true);
    }
    hudTitleSerial = CurrentSerial();  // stage name / chapter sprites 0x00436AD0 / 0x00499F00: HUD (game::ChapterCard)
    if (cfg.portTyping) {
        if (a.sub > 0 && a.timer < 0xA0 && (pressed & 0x80008u)) a.timer = 1;
    } else if ((a.sub > 0 && a.timer < 0xA0 && (pressed & 2)) || (pressed & 0x20000)) a.timer = 1;
    if (--a.timer < 1) {
        // 0x0041D710(0x18C + stage) / 0x0041D970: title resources released (not modelled)
        runner->flags[0xF8] = 1;  // 0x009C72F8
        flags5900 &= ~0x20u;
        ts.KillCurrent();
    }
}

// Actor 0x26 state 1 (0x0048E290 -> 0x0048E600): the players' car, placed on camera paths 0xFD / 0xFE / 0xFF while the
// intro cameras 0x20..0x22 run; removed by event flag 0xE0
void World::Impl::TaskCar(Task& t) {
    CarTask& c = t.As<CarTask>();
    if (runner->flags[0xE0] == 1) ts.KillCurrent();
    // Source4849AC uses cameras30..32 and paths11F..121. Prepared event
    // cameras have their own namespace; raw native20..22 never match them.
    const int camId = runner->camera.camId - (typingReferenceActive ? 0x4010 : 0), tick = runner->camera.tick;
    auto pathId = [&](int native) { return native + (typingReferenceActive ? 0x4022 : 0); };
    if (getenv("WORLD_TRACE_SPAWN")) fprintf(stderr, "%d car cam %X tick %d\n", frame, camId, tick);
    int path = -1;
    if (camId == 0x20) {
        path = 0xFD;
    } else if (camId == 0x21) {
        if (tick > 0x15D) c.active = 0;
        else path = 0xFE;
    } else if (camId == 0x22) {
        int ang[3] = { c.rot[0], c.rot[1], c.rot[2] };
        EvalPath(cameraBank, pathId(0xFE), 350.0f, c.pos, ang);  // 0x43AF0000
        c.rot[0] = ang[0];
        c.rot[1] = ang[1];
        c.rot[2] = ang[2];
        c.active = 0;
        const float t2 = (tick < 0 || tick > 0x31) ? 150.0f : static_cast<float>(tick) + F32(exe, 0x004C43B0);
        float p2[3];
        int a2[3] = {};
        EvalPath(cameraBank, pathId(0xFF), t2, p2, a2);
        c.doorYaw = a2[1] - 0x4000;
        if (players == 2) c.twoPlayers = 1;
    }
    if (path >= 0) {
        c.active = 1;
        int lim = static_cast<int>(exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4));
        if (tick <= lim) lim = tick;
        int ang[3] = { c.rot[0], c.rot[1], c.rot[2] };
        EvalPath(cameraBank, pathId(path), static_cast<float>(lim), c.pos, ang);
        c.rot[0] = ang[0];
        c.rot[1] = ang[1];
        c.rot[2] = ang[2];
    }
    if (c.active != 0) c.spin += 0x2000;
    if (flags5900 & 0x20) return;
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    Mat4 base = Mat4::Identity();
    mx::Translate(base, c.pos[0], c.pos[1], c.pos[2]);
    mx::RotZ(base, c.rot[2]);
    mx::RotY(base, c.rot[1]);
    mx::RotX(base, c.rot[0]);
    Draw(0x1579, base);
    Draw(0x157E, base);
    {
        Mat4 m = base;
        mx::Translate(m, bits(0xC0959DB2), 0.0f, bits(0x3E74BC6A));
        mx::Translate(m, 0.0f, 9.0f, 6.0f);
        mx::RotX(m, 0xD000);
        Draw(0x898, m);
    }
    {
        Mat4 m = base;
        mx::Translate(m, 0.0f, bits(0x412BD2F2), bits(0x41366F00));
        mx::RotX(m, static_cast<int>(0xFFFFE400));
        double k2pi = 0;
        exe.Read(0x004C4370, &k2pi, 8);
        // roll = ftol(sin(int(0x009A32A0 << 9) * 2pi/65536) * float 0x005703B8) (24-bit products)
        const float a = static_cast<float>(static_cast<double>(static_cast<int>(seedCounter << 9)) * k2pi);
        const float roll = static_cast<float>(std::sin(static_cast<double>(a)) * static_cast<double>(F32(exe, 0x005703B8)));
        mx::RotZ(m, static_cast<int>(roll));
        mx::RotX(m, 0x1C00);
        Draw(0x8CC, m);
    }
    {
        Mat4 m = base;
        mx::Translate(m, bits(0xC1211F8A), bits(0x40C5AC71), bits(0x40DB35A8));
        mx::RotY(m, c.doorYaw);
        Draw(0x157C, m, 0xC);
        Draw(0x157D, m, 0xC);
    }
    {
        Mat4 m = base;
        mx::Translate(m, bits(0x41213B64), bits(0x40C6C083), bits(0x40DA9100));
        if (c.twoPlayers != 0) mx::RotY(m, -c.doorYaw);
        Draw(0x157A, m);
        Draw(0x157B, m);
    }
    {
        Mat4 m = base;
        mx::Translate(m, 0.0f, bits(0x4076617C), bits(0x4181CA8C));
        mx::RotX(m, c.spin);
        Draw(0x157F, m);
    }
    {
        Mat4 m = base;
        mx::Translate(m, 0.0f, bits(0x4076617C), bits(0xC14AC56D));
        mx::RotX(m, c.spin);
        Draw(0x1580, m);
    }
    if (c.active != 0) {
        const int w = static_cast<int>(seedCounter % 0xC) + 0x8CE;
        const float wx[2] = { bits(0xC11A147B), bits(0x411A147B) };
        const float wz[2] = { bits(0x417FD2F2), bits(0xC14E9C78) };
        for (int zi = 0; zi < 2; ++zi)
            for (int xi = 0; xi < 2; ++xi) {
                Mat4 m = base;
                mx::Translate(m, wx[xi], 0.0f, wz[zi]);
                if (xi == 1) mx::Scale(m, -1.0f, 1.0f, 1.0f);
                Draw(w, m);
            }
    }
}

// Komono kind 3 (0x00462DF0 -> task 0x004659D0 -> 0x00465BC0): patches the car models' geometry once they are loaded,
// then (0x00465BC0) updates the car lights from the camera; ends with event flag 0xE0 or when camera 0x21 is skipped.
// The model patch / light values are renderer state (not modelled).
void World::Impl::TaskCarLight(Task& t) {
    CarLightTask& c = t.As<CarLightTask>();
    if (c.fn == 0x004659D0) {
        c.loaded = 3;  // models 0x157E / 0x157D / 0x157B resident (resource residency not modelled)
        c.fn = 0x00465BC0;
        return;
    }
    if (runner->flags[0xE0] == 1) ts.KillCurrent();
    if (runner->camera.camId == (typingReferenceActive ? 0x4031 : 0x21) && skip2230 != 0) ts.KillCurrent();
}

// Actor 0x22 (0x0049B0D0 -> 0x0049B260 = behaviour table 0x00598000[+0x1310]): the intro characters (skeleton 0x45,
// with a second skeleton-0x46 object carried on its node 1). Variant 0: behaviour 0 (0x0049B280, scripted on camera
// ticks, waits for event flag 0xF8), behaviour 1 (0x0049B3F0, placed on paths 0x101 / 0x102 while cameras 0x21 / 0x22
// run; ends at camera P+6 tick P+8).
void World::Impl::TaskActor22(Task& t) {
    Actor22Task& a = t.As<Actor22Task>();
    if (a.variant == 3) {  // 0x0049B5B0: drawn (counter + 1) until camera P s16 6 reaches tick P s16 8 (story mode 10)
        const CameraDirector& c = runner->camera;
        if (c.camId == a.record.ParamS16(6) && c.tick >= a.record.ParamS16(8)) {
            if (a.actorSlot >= 0 && a.actorSlot < 14) actorSlots[a.actorSlot] = nullptr;  // 0x004092D0 (+0x120 is -1)
            ts.KillCurrent();
            return;
        }
        Actor22Draw(a);  // 0x0049D770
        ++a.anim.counter[0];
        return;
    }
    if (a.variant != 0) {
        Note("actor 0x22 variant " + std::to_string(a.variant) + " not ported");
        return;
    }
    const CameraDirector& c = runner->camera;
    const int introCamera = c.camId - (typingReferenceActive ? 0x4010 : 0);
    const float tick = static_cast<float>(c.tick);
    auto place = [&](int path) {  // 0x0049DB40
        int ang[3] = { a.rot[0], a.rot[1], a.rot[2] };
        EvalPath(cameraBank, path + (typingReferenceActive ? 0x4022 : 0), tick, a.pos, ang);
        memcpy(a.rot, ang, 12);
    };
    auto draw = [&] {
        Actor22Draw(a);  // 0x0049D770
        ++a.anim.counter[0];
    };
    if (a.behaviour == 0) {  // 0x0049B280
        switch (a.sub) {
        case 0:
            a.pos[0] = -406.0f;  // 0xC3CB0000
            a.pos[1] = 152.5f;   // 0x43188000
            a.pos[2] = 142.4f;   // 0x430E6666
            a.rot[1] = 0x717F;
            a.rot[2] = 0;
            a.rot[0] = 0;
            a.talk = 0;
            a.secondWant = 2;
            ++a.sub;
            [[fallthrough]];
        case 1:
            if (tick != F32(exe, 0x00570488)) break;
            AnimBlendTo(a, 0x40F, 0, 5);
            Sound(0x004317A9);
            a.talk = 1;
            ++a.sub;
            break;
        case 2:
            a.pos[0] = -406.0f;
            a.pos[2] = 142.4f;
            if (a.anim.tick[0] != MotionTicks(a.anim.motion[0]) - 1) break;
            AnimSetMotion(a, exe.S16(0x00570BE0));
            ++a.sub;
            break;
        case 3:
            if (tick != F32(exe, 0x00570F1C)) return;
            Sound(0x004517A9);
            ++a.sub;
            break;
        case 4:
            place(0x100);
            if (runner->flags[0xF8] == 1) {
                a.secondWant = 1;
                ++a.behaviour;
                a.sub = 0;
                return;
            }
            break;
        default:
            break;
        }
        if (!(flags5900 & 0x20)) draw();
        return;
    }
    if (a.behaviour == 1) {  // 0x0049B3F0
        if (c.camId == a.record.ParamS16(6) && c.tick >= a.record.ParamS16(8)) {
            // 0x004092D0: actor slot released (the second object's slot stays taken, like the original);
            // 0x004092B0: lock-on slot (+0x120 is -1)
            if (a.actorSlot >= 0 && a.actorSlot < 14) actorSlots[a.actorSlot] = nullptr;
            ts.KillCurrent();
        }
        if (introCamera == 0x22) {
            place(0x102);
            if (tick == F32(exe, 0x00570A5C)) {
                a.mouthMode = 0;
                Sound(0x004317A9);
            }
            if (tick == F32(exe, 0x004C49C8)) AnimBlendTo(a, 0x412, 0, 3);
        } else if (introCamera == 0x21) {
            place(0x101);
            if (tick == F32(exe, 0x00570F20)) AnimBlendTo(a, exe.S16(0x00570BE2), 0, 8);
            if (tick == F32(exe, 0x005691CC)) {
                a.mouthMode = 1;
                a.mouthTick = 0;
                AnimBlendTo(a, exe.S16(0x00570BF0), 0, 3);
            }
        } else {
            return;
        }
        if (a.anim.motion[0] == exe.S16(0x00570BF0) && a.anim.tick[0] == MotionTicks(a.anim.motion[0]) - 1)
            AnimBlendTo(a, exe.S16(0x00570BE2), 0, 3);
        if (a.anim.motion[0] == 0x412 && a.anim.tick[0] == exe.S16(0x004E0FF4) - 1)
            AnimBlendTo(a, exe.S16(0x00570BE2), 0, 3);
        draw();
        return;
    }
    Note("actor 0x22 behaviour " + std::to_string(a.behaviour) + " not ported");
}

// 0x0049D770: both skeletons; the second object sits on node 1 of the first (camera x model-view)
void World::Impl::Actor22Draw(EnemyTask& e) {
    Actor22Task& a = static_cast<Actor22Task&>(e);
    AnimDraw(a);
    EnemyShadow(a);  // 0x00411090 -> 0x0040A590
    EnemyTask& s = *a.second;
    // camera matrix x node 1's model-view (the original's node matrices are model-view)
    Mat4 n1 = cam.inverse;  // 0x004A9230 (camera matrix), 0x004A92A0 (node +0x28)
    mx::MulLeft(n1, a.anim.nodes[1].mv);
    s.pos[0] = n1.m[12];
    s.pos[1] = n1.m[13];
    s.pos[2] = n1.m[14];
    mx::MatrixToAngles(n1, &s.rot[0], &s.rot[1], &s.rot[2]);  // 0x004019E0
    AnimDraw(s);
    EnemyShadow(s);
    if (a.variant == 3) {  // switch case 3: the second object's counter always advances
        ++s.anim.counter[0];
        return;
    }
    if (a.secondHave != a.secondWant) AnimSetMotion(s, exe.S16(0x00570C00 + static_cast<uint32_t>(a.secondWant) * 2));
    a.secondHave = a.secondWant;
    if (a.talk != 0) ++s.anim.counter[0];
}

// 0x0049D980: node model; node 2 cycles mouth models while +0x1338 is set; model 699 gets two eye parts
void World::Impl::Actor22Node(EnemyTask& e, int idx, const Mat4& m) {
    Actor22Task& a = static_cast<Actor22Task&>(e);
    int model = a.anim.nodes[idx].model;
    if (idx == 2) {
        ++a.mouthTick;
        if (a.mouthMode == 1) model = static_cast<int8_t>(exe.S16(0x00570F08 + static_cast<uint32_t>(a.mouthTick % 10)) & 0xFF) + 0x2A5;
        else if (a.mouthMode == 2) model = static_cast<int8_t>(exe.S16(0x00570F14 + static_cast<uint32_t>(a.mouthTick % 6)) & 0xFF) + 0x2A5;
    }
    Draw(model, m, 8, -1.0f, false, idx);
    if (a.anim.nodes[idx].model != 699) return;
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    auto eye = [&](float x, int nodeIdx, int part) {
        const EnemyNode& n = a.anim.nodes[nodeIdx];
        Mat4 q = m;
        mx::Translate(q, x, bits(0x3FBEAB36), 0.0f);
        mx::RotZ(q, (n.cur[2] * 0x238E) / 0x671C);
        const int v = n.cur[0] * 0x5555;
        mx::RotX(q, (v + ((v >> 31) & 0x7FFF)) >> 15);
        Draw(part, q, 8, -1.0f, false, idx);
    };
    eye(bits(0xBF47381D), 3, 0x2B5);
    eye(bits(0x3F47381D), 6, 0x2B4);
}

// Actor 0x20 (0x00448ED0 -> 0x00449020): distant characters with a looping motion (skeleton P[0], variant P[1]);
// removed when camera P+2 reaches tick P+4; shootable on their nodes: any hit kills (score like an enemy, then the
// death motion 0x3DC, 0x00449380 / 0x00449430 sink and remove)
void World::Impl::TaskActor20(Task& t) {
    Actor20Task& a = t.As<Actor20Task>();
    auto drawSkeleton = [&] {  // 0x00411090 (light 0x0041DC70 / 0x0041DCC0: renderer)
        AnimDraw(a);
        EnemyShadow(a);
    };
    if (a.fn == 0x004494D0) {  // game type 2, scene 0xD: drawn, not shootable, until the lesson runs (flag 0xF1, not 0xF2)
        drawSkeleton();
        if (runner->flags[0xF2] == 0 && runner->flags[0xF1] == 1) {
            a.flags34 &= ~8u;
            RegisterTarget(&a);  // 0x00405160
            a.fn = 0x00449020;
        }
        return;
    }
    if (a.fn == 0x00449020 && runner->cfg.gameType == 2 && runner->scene == 0xD) {  // the lesson's end holds it again
        if (a.hold != 0) {
            drawSkeleton();
            a.fn = 0x004494D0;
            return;
        }
        if (runner->flags[0xF2] == 1) a.hold = 1;
    }
    if (a.fn == 0x00449380) {  // death motion, then hold its last frame and sink
        drawSkeleton();
        ++a.anim.counter[0];
        if (a.anim.tick[0] == MotionTicks(a.anim.motion[0])) {
            --a.anim.counter[0];
            a.dying = 0x78;
            SpawnBloodPool(a);  // 0x00407DA0
            a.fn = 0x00449430;
        }
        return;
    }
    if (a.fn == 0x00449430) {
        drawSkeleton();
        a.pos[1] = a.pos[1] - F32(exe, 0x004C4D04);
        if (--a.dying == 0) {
            if (a.actorSlot >= 0 && a.actorSlot < 14) actorSlots[a.actorSlot] = nullptr;
            a.flags34 = (a.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0 (and off the target copy, 0x00405220)
            for (Target& tg : targetCopy)
                if (tg.obj == &a) tg.obj = nullptr;
            ts.KillCurrent();
        }
        return;
    }
    if (!(a.flags34 & 8)) {
        if (runner->camera.camId == a.record.ParamS16(2) && runner->camera.tick >= a.record.ParamS16(4)) {
            if (a.actorSlot >= 0 && a.actorSlot < 14) actorSlots[a.actorSlot] = nullptr;
            a.flags34 = (a.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0 (and off the target copy, 0x00405220)
            for (Target& tg : targetCopy)
                if (tg.obj == &a) tg.obj = nullptr;
            ts.KillCurrent();
        }
    } else {
        const uint32_t zones = exe.U32(0x004C7160 + static_cast<uint32_t>(a.anim.kind) * 4);
        for (int i = 0; i < 16; ++i) {
            EnemyNode& n = a.anim.nodes[i];
            if (!(n.flags & 8)) continue;
            const int w = exe.S16(zones + static_cast<uint32_t>(i) * 0xC) & 0xFFFF;
            if (w > 2) n.model = w;
            n.flags &= ~8u;
            SpawnNodeBlood(a, i);  // 0x00407200 (task 0x00407120)
            if (i == 2) {
                for (int p = 0; p < 2; ++p)
                    if (n.flags & (2u << p)) {
                        AddScore(p, 0x78);
                        AddScore(p, player[p].streak);
                        player[p].streak = static_cast<int16_t>(player[p].streak + 10);
                        ++player[p].hits;
                    }
            } else {
                for (int p = 0; p < 2; ++p)
                    if (n.flags & (2u << p)) {
                        AddScore(p, 10);
                        ++player[p].hits;
                        player[p].streak = 0;
                    }
            }
        }
        int p;
        if ((a.flags34 & 6) == 2) p = 0;
        else if ((a.flags34 & 6) == 4) p = 1;
        else p = Rand(0x004491C6) & 1;
        AddScore(p, 0x50);
        a.flags34 &= ~1u;
        // 0x004491FD: inc word [0x009A244A], the kill counter (reset / read by the training lesson 0x00497640:
        // world_modes_train.cpp; the other writer is 0x00452644 in 0x004525C0)
        runner->globals[0x009A244A] = (GlobalInt(0x009A244A, 0) + 1) & 0xFFFFu;
        AnimBlendTo(a, 0x3DC, 0, 5);
        a.fn = 0x00449380;
    }
    if (a.variant == 1) a.rot[1] += (a.hp != 0 ? 0x80 : 0) - 0x40;
    if (a.variant == 2) Actor20BoxWalk(a);  // world_st3.cpp
    drawSkeleton();
    ++a.anim.counter[0];
    const uint32_t f = a.flags34;
    a.flags34 = f & ~8u;
    if (f & 1) RegisterTarget(&a);  // view position +0x70 is never written for this actor (stays 0)
}
// 0x0046C570: komono kind 40 piece (bottle / can): shatters into 40 tumbling pieces when shot
void World::Impl::TaskKomono40(Task& t) {
    Komono40Task& k = t.As<Komono40Task>();
    if (cfg.eventMode == 1 && runner->flags[0x77] != 0) ts.KillCurrent();  // Actor_Destroy 0x00409CC0
    if (runner->variant != k.area) {
        if (k.life < ++k.areaCount) ts.KillCurrent();
        k.area = static_cast<int8_t>(runner->variant);
    }
    const int camId = runner->camera.camId, tick = runner->camera.tick;
    if ((camId == 100 && tick == 0x50) || (camId == 0x67 && tick == 10)) ts.KillCurrent();
    // game type 1, kind 9 (stage 4 scene 0): two of them broken while event flag 0x11 is set -> route branch 2
    if (runner->cfg.gameType == 1 && k.kind == 9 && komono40Count == 2 && runner->flags[0x11] == 1) {
        runner->camera.routeBranch = 2;  // 0x009C88A4
        komono40Count = -1;
    }
    const float f464 = F32(exe, 0x004D5464);
    double k2pi = 0;
    exe.Read(0x004C4370, &k2pi, 8);
    if ((k.flags34 & 8) && k.broken == 0 && (k.kind != 1 || komono40Intact[k.index % 12] != 0)) {
        int a = 0;
        for (int i = 0; i < 40; ++i, a += 0x666) {
            k.piece[i][0] = k.pos[0];
            k.piece[i][1] = k.pos[1] + F32(exe, 0x004E1FE4);
            if (k.kind == 0) k.piece[i][1] = k.piece[i][1] - F32(exe, 0x004C4CB8);
            k.piece[i][2] = k.pos[2];
            SetSites({ 0x0046C6E1, 0x0046C6F8, 0x0046C70E, 0x0046C725, 0x0046C748, 0x0046C76D, 0x0046C78E, 0x0046C7B1,
                       0x0046C7D6, 0x0046C7F0, 0x0046C80A });
            for (int c = 0; c < 3; ++c) k.pieceAng[i][c] = static_cast<int16_t>(rng.Next());
            const float as = static_cast<float>(static_cast<double>(rng.Next() % 0x201 + a) * k2pi);
            const double s = std::sin(static_cast<double>(as));
            k.pieceVel[i][0] = static_cast<float>(rng.Next() % 0x1A) * f464 * static_cast<float>(s);
            k.pieceVel[i][1] = static_cast<float>(rng.Next() % 0x51) * f464;
            const float ac = static_cast<float>(static_cast<double>(rng.Next() % 0x201 + a) * k2pi);
            const double c = std::cos(static_cast<double>(ac));
            k.pieceVel[i][2] = static_cast<float>(rng.Next() % 0x1A) * f464 * static_cast<float>(c);
            for (int j = 0; j < 3; ++j) k.pieceSpin[i][j] = static_cast<int16_t>(rng.Next() % 0x401 - 0x200);
        }
        PropHitPlayer(*this, k.flags34, true);  // 0x004650F0
        Sound((Rand(0x0046C84A) & 1) == 0 ? 0x2D16A9u : 0x2C16A9u);
        k.broken = 1;
        k.flags34 &= ~8u;
        if (k.kind == 0) k.model = 0x123F;
        else ++k.model;
        if (k.kind == 1) komono40Intact[k.index % 12] = 0;  // 0x0046C8AC (game type != 2)
        if (runner->cfg.gameType == 1 && k.kind == 9) ++komono40Count;  // 0x007DCF18
    }
    if (k.broken == 1 && k.frames < 100) {
        // 0x004AA0A0: piece light (renderer state)
        const float grav = F32(exe, 0x0055CB10), bounce = F32(exe, 0x0056474C);
        const float s = static_cast<float>(static_cast<int8_t>(exe.S16(0x0059469C + static_cast<uint32_t>(k.kind)) & 0xFF)) * f464;
        for (int i = 0; i < 40; ++i) {
            k.pieceVel[i][1] = k.pieceVel[i][1] - grav;
            for (int c = 0; c < 3; ++c) k.piece[i][c] = k.pieceVel[i][c] + k.piece[i][c];
            for (int c = 0; c < 3; ++c) k.pieceAng[i][c] = static_cast<int16_t>(k.pieceAng[i][c] + k.pieceSpin[i][c]);
            if (k.piece[i][1] < ground + 1.0f && (cfg.eventMode != 1 || runner->scene != 0x10)) {
                k.piece[i][1] = ground + 1.0f;
                k.pieceVel[i][1] = k.pieceVel[i][1] * bounce;
            }
            Mat4 m = Mat4::Identity();
            mx::Translate(m, k.piece[i][0], k.piece[i][1], k.piece[i][2]);
            mx::RotZ(m, k.pieceAng[i][2]);
            mx::RotY(m, k.pieceAng[i][1]);
            mx::RotX(m, k.pieceAng[i][0]);
            mx::Scale(m, s, s, s);
            Draw(i + 0xCA5, m);
        }
        ++k.frames;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    if (k.kind == 13) mx::Scale(m, bits(0x3F4E48E9), bits(0x3FA41893), 1.0f);
    else if (k.kind == 14) mx::Scale(m, bits(0x3F990FF9), bits(0x3F588CE7), bits(0x3F4CCCCD));
    else mx::Scale(m, k.scale, k.scale, k.scale);
    Draw(k.model, m);
    if (k.kind == 9) {  // 0x0046C570: model + 0x96 at a fixed world point (by +0x1B8)
        Mat4 x = Mat4::Identity();
        mx::Translate(x, bits(k.index == 0 ? 0x42AC891D : 0x42FFAA16), bits(0xC1379CE0), bits(0xC3683810));
        Draw(k.model + 0x96, x);
    }
    if (k.broken == 0) {
        float h = 0.0f;  // 0x004C436C
        if (k.kind == 0) h = F32(exe, 0x004C4CBC);
        if (k.model == 0x17C6) h = 8.0f;  // 0x004C43A0
        if (k.model == 0x1786 || k.model == 0x16B1) h = 5.0f;  // 0x0055D2B4
        const float p[3] = { k.pos[0], h + k.pos[1], k.pos[2] };
        mx::Transform(cam.view, p, k.viewPos);
        RegisterTarget(&k);
    }
}

// Komono kind 48 (ctor 0x00463B20 -> task 0x0046DDE0, block 0xD14; stage 2 scene 26): a hanging lamp (model 0x17AC,
// scale 3) with a point light (slot 0x00480A60 of 0x009A1A20: diffuse (20, 20, 15), range 1024, attenuation 0.15 / 0 /
// flicker); shot (0x004650F0 with score, sound 0x2B16A9): 30 pieces (models 0xCA5.., scale 0.5) for 0x5A frames, the
// broken lamp 0x17AD (scale 2), the light fading; the light slot freed when it leaves its areas
struct Komono48Task : ActorTask {
    int16_t life = 0;                 // +0x11C
    float pos[3] = {};                // +0x194
    int phase = 0;                    // +0x1AC flicker phase
    int yaw = 0;                      // +0x1B0
    int8_t area = 0, areaCount = 0;   // +0x1BB / +0x1BC
    int count = 0;                    // +0x1C0 frames since shot
    int slot = 0;                     // +0x1C4 light slot
    float piece[30][3] = {};          // +0x238
    float vel[30][3] = {};            // +0x5A0
    int16_t ang[30][3] = {};          // +0x910
    int16_t spin[30][3] = {};         // +0xAC4
};

namespace {
double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

void Komono48Light(World::Impl& w, Komono48Task& k, float att2) {
    if (k.slot < 0 || k.slot >= 16) return;
    World::Impl::LightSlot& s = w.lightSlots[k.slot];
    s.enabled = true;  // +0x68
    s.l.type = 1;      // point
    const float d[3] = { 20.0f, 20.0f, 15.0f };
    for (int c = 0; c < 3; ++c) {
        s.l.pos[c] = k.pos[c];
        s.l.diffuse[c] = d[c];
    }
    s.l.att[0] = 0.15f;  // 0x3E19999A
    s.l.att[1] = 0.0f;
    s.l.att[2] = att2;
    s.l.range = 1024.0f;
}

void TaskKomono48(Task& t) {  // 0x0046DDE0
    World::Impl& w = *CurrentWorld();
    Komono48Task& k = t.As<Komono48Task>();
    const ExeImage& exe = w.exe;
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            if (k.slot >= 0 && k.slot < 16) w.lightSlots[k.slot].enabled = false;  // +0x68 = 0
            st5::State(w).lightUsed[k.slot & 15] = false;                            // 0x00480AA0
            w.ts.KillCurrent();
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    double k2pi = 0;
    exe.Read(0x004C4370, &k2pi, 8);
    const float f464 = F32(exe, 0x004D5464);
    if ((k.flags34 & 8) && !(k.flags34 & 0x40000000)) {
        PropHitPlayer(w, k.flags34, true);  // 0x004650F0(flags, 1)
        w.Sound(0x2B16A9);
        k.flags34 |= 0x40000000;
        k.count = 0;
        for (int i = 0, a = 0; i < 30; ++i, a += 0x888) {
            k.piece[i][0] = k.pos[0];
            k.piece[i][1] = k.pos[1] - F32(exe, 0x004C4CB8);
            k.piece[i][2] = k.pos[2];
            w.SetSites({ 0x0046DED1, 0x0046DEE8, 0x0046DEFE, 0x0046DF15, 0x0046DF3A, 0x0046DF5F, 0x0046DF86, 0x0046DFAB,
                         0x0046DFD0, 0x0046DFEA, 0x0046E004 });
            for (int c = 0; c < 3; ++c) k.ang[i][c] = static_cast<int16_t>(w.rng.Next());
            const float as = static_cast<float>(static_cast<double>(w.rng.Next() % 0x201 + a) * k2pi);
            const double s = std::sin(static_cast<double>(as));
            k.vel[i][0] = static_cast<float>(w.rng.Next() % 0x1A) * f464 * static_cast<float>(s);
            k.vel[i][1] = static_cast<float>(w.rng.Next() % 0x1F) * f464 + F32(exe, 0x004D1D24);
            const float ac = static_cast<float>(static_cast<double>(w.rng.Next() % 0x201 + a) * k2pi);
            const double c = std::cos(static_cast<double>(ac));
            k.vel[i][2] = static_cast<float>(w.rng.Next() % 0x1A) * f464 * static_cast<float>(c);
            for (int j = 0; j < 3; ++j) k.spin[i][j] = static_cast<int16_t>(w.rng.Next() % 0x401 - 0x200);
        }
    }
    if (!(k.flags34 & 0x40000000)) {
        const double sn = std::sin(static_cast<double>(static_cast<uint32_t>(k.phase) & 0x7FFF) * k2pi);
        Komono48Light(w, k, static_cast<float>(sn * F64(exe, 0x0055D190) + F64(exe, 0x0055D188)));
        k.phase += 0x800;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1] + F32(exe, 0x004E30F0), k.pos[2]);
        mx::RotY(m, k.yaw);
        mx::Scale(m, 3.0f, 3.0f, 3.0f);
        w.Draw(0x17AC, m);
        const float p[3] = { k.pos[0], k.pos[1] - F32(exe, 0x00564708), k.pos[2] };
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);  // 0x00405160
        return;
    }
    Komono48Light(w, k, static_cast<float>(static_cast<float>(k.count) * F32(exe, 0x004C4CC0) + F64(exe, 0x0055D188)));
    if (++k.count < 0x5A) {
        const float g = F32(exe, 0x0055CB10);
        for (int i = 0; i < 30; ++i) {
            k.vel[i][1] = k.vel[i][1] - g;
            for (int c = 0; c < 3; ++c) k.piece[i][c] = k.vel[i][c] + k.piece[i][c];
            for (int c = 0; c < 3; ++c) k.ang[i][c] = static_cast<int16_t>(k.ang[i][c] + k.spin[i][c]);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, k.piece[i][0], k.piece[i][1], k.piece[i][2]);
            mx::RotZ(m, k.ang[i][2]);
            mx::RotY(m, k.ang[i][1]);
            mx::RotX(m, k.ang[i][0]);
            mx::Scale(m, 0.5f, 0.5f, 0.5f);
            w.Draw(i + 0xCA5, m);
        }
    } else if (k.slot >= 0 && k.slot < 16) {
        w.lightSlots[k.slot].enabled = false;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.yaw);
    mx::Scale(m, 2.0f, 2.0f, 2.0f);
    w.Draw(0x17AD, m);
}
}  // namespace

// 0x0046CC50: komono kind 41 shutters: swing open once shot (spark, +10, a hit)
void World::Impl::TaskKomono41(Task& t) {
    Komono41Task& k = t.As<Komono41Task>();
    if (runner->variant != k.area) {
        if (k.life < ++k.areaCount) ts.KillCurrent();  // Actor_Destroy 0x00409CC0
        k.area = static_cast<int8_t>(runner->variant);
    }
    if ((k.flags34 & 8) && k.shot == 0) {
        SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.2f, k.pos[2]);  // 0x004666B0(obj, p, 1.2)
        PropHitPlayer(*this, k.flags34, true);                                       // 0x004650F0
        k.shot = 1;
        Sound(0xE16A9);
        k.speed0 = 0x200;
    }
    if (k.shot == 1) {
        if (k.ang[0] < 0x3800) {
            k.speed0 += 0x30;
            k.ang[0] = static_cast<int16_t>(k.ang[0] + static_cast<int16_t>(k.speed0));
            if (k.ang[0] > 0x37FF) {
                Sound(0x1416A9);
                k.ang[0] = 0x3800;
            }
        }
        if (k.ang[1] < 0x3F00) {
            if (k.ang[0] > 0x800) {
                if (k.braked == 0) {
                    k.braked = 1;
                    k.speed0 -= 0x200;
                }
                k.speed1 += 0x20;
                k.ang[1] = static_cast<int16_t>(k.ang[1] + static_cast<int16_t>(k.speed1));
            }
            if (k.ang[1] > 0x3EFF) {
                Sound(0x1416A9);
                k.ang[1] = 0x3F00;
            }
        }
    }
    for (int i = 0; i < 2; ++i) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.part[i][0], k.part[i][1], k.part[i][2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.ang[i]);
        mx::RotX(m, k.rot[0]);
        Draw(0x930, m);
    }
    const float p[3] = { k.pos[0], k.pos[1] + 5.0f, k.pos[2] };  // + (float)double 0x00565E10
    mx::Transform(cam.view, p, k.viewPos);
    RegisterTarget(&k);
}

namespace {
struct BloodPoolTask : WTask {  // 0x00407E30 / 0x00408100 / 0x00408220 (block 0x13F4)
    float pos[3] = {};  // +0x40
    int yaw = 0;        // +0x68
    float scale = 1.0f; // +0x118
    int counter = 0;    // +0x1330
    int fade = 0;       // +0x1320
    float alpha = 1.0f; // +0x1370
};
}  // namespace

// 0x00407DA0 (game mode 6): blood pool under a corpse: grows 120 frames (0x00407E30), stays 30 (0x00408100), fades 40
// (0x00408220)
void World::Impl::SpawnBloodPool(EnemyTask& e) {
    if (cfg.portTyping && !cfg.typingOptions.bloodEffects) return;
    if (gameMode != 6) return;
    BloodPoolTask* b = NewTask<BloodPoolTask>(WorldTramp<&Impl::TaskBloodPool>, 0x00407E30);
    float y = e.pos[1];
    if (e.anim.drawFlags & 4) {
        int mat = 0;
        y = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);  // 0x004C4C8C
    }
    b->pos[0] = e.body[0];
    b->pos[1] = y + 0.05f;  // 0x004C4C88
    b->pos[2] = e.body[2];
    b->yaw = e.rot[1];
    b->counter = 0x78;
    b->scale = 1.0f;
}

// 0x00408370(block {x, y, z, -, yaw}, scale) (game mode 6): blood pool at y + 0.05
void World::Impl::SpawnBloodPoolAt(const float pos[3], int yaw, float scale) {
    if (cfg.portTyping && !cfg.typingOptions.bloodEffects) return;
    if (gameMode != 6) return;
    BloodPoolTask* b = NewTask<BloodPoolTask>(WorldTramp<&Impl::TaskBloodPool>, 0x00407E30);
    b->pos[0] = pos[0];
    b->pos[1] = pos[1] + 0.05f;  // 0x004C4C88
    b->pos[2] = pos[2];
    b->yaw = yaw;
    b->counter = 0x78;
    b->scale = scale;
}

void World::Impl::TaskBloodPool(Task& t) {
    BloodPoolTask& b = t.As<BloodPoolTask>();
    double k2pi = 0;
    exe.Read(0x004C4370, &k2pi, 8);
    auto ang = [&](int n, float f) {  // fild n * float (24-bit) -> __ftol -> * 2pi/65536 (24-bit)
        const int k = static_cast<int>(static_cast<float>(n) * f);
        return static_cast<float>(static_cast<double>(k) * k2pi);
    };
    const float s = b.scale;
    Mat4 base = cam.view;  // the matrix-stack top: drawn from the model-view chain
    mx::Translate(base, b.pos[0], b.pos[1], b.pos[2]);
    mx::RotY(base, b.yaw);
    const int ripple = frameCounter5C50;
    if (b.fn == 0x00407E30) {
        if (--b.counter == 0) b.fn = 0x00408100;
        const float c = static_cast<float>(static_cast<float>(std::cos(static_cast<double>(ang(b.counter, F32(exe, 0x004C4CA4)))) * s) * 4.0f);
        Mat4 m = base;
        mx::Scale(m, c, s, c);
        Draw(0x1A38, m, 0xC, -1.0f, false, -1, true);
        const float sn = static_cast<float>(std::sin(static_cast<double>(ang(b.counter, F32(exe, 0x004C4CA4)))) * s);
        const float d = sn + sn, z = d + d, z2 = d * -2.0f;
        const float ss = static_cast<float>(s * 0.5);
        const float xs[4] = { d, -d, d, -d }, zs[4] = { z, z, z2, z2 };
        const int add[4] = { 0, 8, 0x18, 0x10 };
        for (int i = 0; i < 4; ++i) {
            Mat4 q = base;
            mx::Translate(q, xs[i], 0.0f, zs[i]);
            mx::Scale(q, ss, ss, ss);
            Draw(static_cast<int>(static_cast<uint32_t>(ripple + add[i]) % 0x1E) + 0x15E4, q, 0xC, -1.0f, false, -1, true);
        }
        return;
    }
    if (b.fn == 0x00408100) {
        if (++b.counter == 0x1E) {
            b.fn = 0x00408220;
            b.counter = 0;
            b.alpha = 1.0f;
        }
        Mat4 m = base;
        const float w = static_cast<float>(s * 4.0);
        mx::Scale(m, w, s, w);
        Draw(0x1A38, m, 0xC, -1.0f, false, -1, true);
        Mat4 q = base;
        const float ss = static_cast<float>(s * 0.3);
        mx::Scale(q, ss, ss, ss);
        Draw(static_cast<int>(static_cast<uint32_t>(ripple) % 0x1E) + 0x15E4, q, 0xC, -1.0f, false, -1, true);
        return;
    }
    if (++b.fade == 0x28) ts.KillCurrent();
    const float c = static_cast<float>(static_cast<float>(std::cos(static_cast<double>(ang(b.fade, F32(exe, 0x004C4CB4)))) * s) * 4.0f);
    b.alpha = b.alpha - 0.025f;
    Mat4 m = base;
    mx::Scale(m, c, s, c);
    Draw(0x1A38, m, 0xC, b.alpha, false, -1, true);
    Mat4 q = base;
    const float ss = static_cast<float>(s * 0.3);
    mx::Scale(q, ss, ss, ss);
    Draw(static_cast<int>(static_cast<uint32_t>(ripple) % 0x1E) + 0x15E4, q, 0xC, b.alpha, false, -1, true);
}

void World::Impl::TaskScriptedPlaceholder(Task& t) {
    Note("scripted actor task 0x" + std::to_string(t.As<WTask>().fn) + " placeholder (no draws)");
}

// The actor type functions handled here (first run of a spawned actor). Returns false for other types.
bool World::Impl::SpawnScripted(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    if (getenv("WORLD_TRACE_SPAWN")) fprintf(stderr, "%d spawn type %X kind %d state %d\n", frame, r.type, r.ParamS8(1), r.state);
    if (SpawnScene1(t, s)) return true;  // world_scene1.cpp
    switch (r.type) {
    case 0x63: {  // skip watcher
        s.fn = fn::kSkipWatch;
        t.fn = WorldTramp<&Impl::TaskSkipWatch>;
        TaskSkipWatch(t);
        return true;
    }
    case 0x60: {  // title card: the spawn task becomes the title task (same block)
        auto d = std::make_unique<TitleTask>();
        d->fn = 0x004342E0;
        t.data = std::move(d);
        t.fn = WorldTramp<&Impl::TaskTitle>;
        TaskTitle(t);
        return true;
    }
    case 0x26: {
        if (r.state != 1) {
            Note("actor 0x26 state " + std::to_string(r.state) + " not ported");
            t.fn = WorldTramp<&Impl::TaskPlaceholder>;
            return true;
        }
        auto d = std::make_unique<CarTask>();
        d->fn = 0x0048E600;
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        t.data = std::move(d);
        t.fn = WorldTramp<&Impl::TaskCar>;
        TaskCar(t);
        return true;
    }
    case 0x22: {  // 0x0049B0D0
        if (r.ParamS8(1) == 1 || r.ParamS8(1) == 2) return SpawnBoss22(t, r);  // stage-1 boss / stage-5 rematch (world_st1b_boss.cpp)
        auto d = std::make_unique<Actor22Task>();
        d->record = r;
        d->flags34 = r.flags | 1;  // 0x00408A20
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->hp = d->hpMax = static_cast<int16_t>(r.state);
        d->behaviour = 0;
        d->sub = 0;
        d->variant = r.ParamS8(1);
        d->anim.kind = 0x45;
        d->anim.motion[0] = r.ParamS16(2);
        EnemyInitPose(*d);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
        d->anim.rotOrder = 5;
        d->nodeCb = 0x0049D980;
        d->anim.drawFlags |= 4;
        d->anim.counter[0] = r.ParamS16(4);
        d->radius = F32(exe, 0x004C4D28 + 0x45 * 4);
        d->anim.nodes[2].model = 0x2A9;
        d->mouthMode = d->mouthTick = 0;
        auto s = std::make_unique<EnemyTask>();  // 0x004A74E0(0x13F4): skeleton 0x46, motion 0x10
        s->anim.kind = 0x46;
        s->anim.motion[0] = 0x10;
        EnemyInitPose(*s);
        s->anim.rotOrder = 5;
        s->nodeCb = 0x00411050;
        s->flags34 |= 0x88000;
        d->secondWant = d->secondHave = 1;
        d->second = std::move(s);
        d->fn = d->variant == 3 ? 0x0049B5B0 : 0x0049B260;
        t.data = std::move(d);
        t.fn = WorldTramp<&Impl::TaskActor22>;
        if (r.ParamS8(1) != 0 && r.ParamS8(1) != 3) Note("actor 0x22 variant " + std::to_string(r.ParamS8(1)) + " setup not ported");
        TaskActor22(t);
        return true;
    }
    case 0x20: {  // 0x00448ED0
        auto d = std::make_unique<Actor20Task>();
        d->record = r;
        d->flags34 = r.flags | 1;  // 0x00408A20
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->hp = d->hpMax = static_cast<int16_t>(r.state);
        d->variant = r.ParamS8(1);
        d->anim.kind = r.ParamS8(0);
        if (r.ParamS16(6) == 0) {
            const int v = Rand(0x00448F1B) & 3;
            d->anim.motion[0] = static_cast<int>(exe.U32(0x005647A4 + static_cast<uint32_t>(v) * 4));
        } else {
            d->anim.motion[0] = r.ParamS16(6);
        }
        EnemyInitPose(*d);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
        d->anim.drawFlags |= 2;
        d->anim.rotOrder = 5;
        d->anim.counter[0] = Rand(0x00448F5D);
        d->nodeCb = 0x00449530;
        for (int n : { 5, 8, 12, 15 }) {  // hands / feet: drawn by their parents' node callback instead
            d->anim.nodes[n].model = 0;
            d->anim.nodes[n].radius = 0;
        }
        if (gameMode == 10) d->flags34 |= 0x8000;
        else d->radius = F32(exe, 0x004C4D28 + static_cast<uint32_t>(d->anim.kind) * 4);
        // game type 1 with item 0x15 (0x009C88A8 == 1): the head's hit radius (node 2 +0x78) doubled
        if (runner->cfg.gameType == 1 && ItemFlag(*this, 0)) d->anim.nodes[2].radius = d->anim.nodes[2].radius + d->anim.nodes[2].radius;
        d->fn = 0x00449020;
        if (runner->cfg.gameType == 2 && runner->scene == 0xD) d->fn = 0x004494D0;  // training lesson 6: held until the start
        t.data = std::move(d);
        t.fn = WorldTramp<&Impl::TaskActor20>;
        return true;
    }    case 0x41: {
        const int kind = r.ParamS8(1);
        if (kind == 3) {  // 0x00462DF0: Task_Create(0x004659D0, 0x88)
            CarLightTask* c = NewTask<CarLightTask>(WorldTramp<&Impl::TaskCarLight>, 0x004659D0);
            (void)c;
            ts.KillCurrent();
        }
        if (kind == 48) {  // 0x00463B20
            Komono48Task* k = NewTask<Komono48Task>(TaskKomono48, 0x0046DDE0);
            k->areaCount = 0;
            k->area = static_cast<int8_t>(runner->variant);
            k->life = static_cast<int16_t>(r.state);
            memcpy(k->pos, r.pos, 12);
            k->yaw = r.rot[1];
            k->radius = 3.0f;  // 0x40400000
            k->flags34 = 0;
            k->slot = 0;  // 0x00480A60: the first free slot of 3..15 (0 when none)
            for (int i = 3; i < 16; ++i)
                if (!st5::State(*this).lightUsed[i]) {
                    st5::State(*this).lightUsed[i] = true;
                    k->slot = i;
                    break;
                }
            ts.KillCurrent();
            return true;
        }
        if (kind == 40) {  // 0x004636A0: 0x005945D8[P0] tasks 0x0046C570
            const int k = r.ParamS8(0);
            const int n = static_cast<int8_t>(exe.S16(0x005945D8 + static_cast<uint32_t>(k)) & 0xFF);
            for (int i = 0; i < n; ++i) {
                Komono40Task* p = NewTask<Komono40Task>(WorldTramp<&Impl::TaskKomono40>, 0x0046C570);
                p->life = static_cast<int16_t>(r.state);
                p->area = static_cast<int8_t>(runner->variant);
                p->areaCount = 0;
                p->index = static_cast<int8_t>(i);
                p->kind = static_cast<int8_t>(k);
                p->radius = 5.5f;  // 0x40B00000
                p->flags34 = 0x80000001u;
                p->scale = 1.0f;
                if (k >= 2) {  // k 2..5 index the same pointer table (0x005945EC[k] = 0x00594604[k - 6])
                    const uint32_t tab = exe.U32(0x00594604 + static_cast<uint32_t>(k - 6) * 4) + static_cast<uint32_t>(i) * 12;
                    for (int c = 0; c < 3; ++c) p->pos[c] = F32(exe, tab + 4 * c);
                    p->model = exe.S16(0x00594648 + static_cast<uint32_t>(k - 6) * 2);
                    const uint32_t ui = static_cast<uint32_t>(i);
                    switch (k - 6) {  // 0x004636A0 per-variant extras (+0x118 scale, +0x1AC..+0x1B4 angles)
                    case 0:
                        p->rot[1] = i < 3 ? 0x4000 : 0xC000;
                        p->scale = 1.5f;
                        break;
                    case 3:
                        p->scale = 0.8f;  // 0x3F4CCCCD
                        komono40Count = 0;  // 0x007DCF18
                        break;
                    case 5:
                        p->scale = F32(exe, 0x00594664 + ui * 4);
                        p->rot[1] = static_cast<int>(exe.U32(0x00594680 + ui * 4));
                        break;
                    case 6:
                        p->scale = F32(exe, 0x00594670 + ui * 4);
                        p->rot[1] = static_cast<int>(exe.U32(0x0059468C + ui * 4));
                        break;
                    case 7: p->rot[1] = static_cast<int>(0xFFFFA9A0u); break;
                    case 8:
                        p->rot[0] = static_cast<int>(0xFFFFEC23u);
                        p->rot[1] = static_cast<int>(0xFFFFBED9u);
                        p->rot[2] = 0x13DE;
                        break;
                    case 0xB: case 0xC: p->rot[1] = static_cast<int>(0xFFFFC000u); break;
                    case 0xD: p->rot[1] = i != 0 ? static_cast<int>(0xFFFFC000u) : 0x298E; break;
                    default: break;
                    }
                } else if (k == 0) {
                    // identity; T(16.473, 16.821, 4.749); RotY(0x278D); T(0x00594158[i]); translation (0x004A8CC0)
                    auto bits = [](uint32_t u) {
                        float f;
                        memcpy(&f, &u, 4);
                        return f;
                    };
                    Mat4 m = Mat4::Identity();
                    mx::Translate(m, bits(0x4183C8B4), bits(0x41869168), bits(0x4097F7CF));
                    mx::RotY(m, 0x278D);
                    mx::Translate(m, F32(exe, 0x00594158 + static_cast<uint32_t>(i) * 8), 0.0f,
                                  F32(exe, 0x0059415C + static_cast<uint32_t>(i) * 8));
                    mx::GetTranslation(m, p->pos);
                    p->model = 0x123E;
                } else if (k == 1) {  // 0x00594038[i] {pos, angles in degrees * 182.044 (0x00569010), __ftol}
                    const uint32_t tab = 0x00594038 + static_cast<uint32_t>(i) * 24;
                    for (int c = 0; c < 3; ++c) p->pos[c] = F32(exe, tab + 4 * c);
                    for (int c = 0; c < 3; ++c) p->rot[c] = static_cast<int>(F32(exe, tab + 12 + 4 * c) * F32(exe, 0x00569010));
                    p->model = komono40Intact[i % 12] == 1 ? 0x17C6 : 0x17C7;  // 0x007DCDB8[i]
                } else {
                    Note("komono kind 40 variant " + std::to_string(k) + " (0x004636A0) not ported");
                }
            }
            ts.KillCurrent();
        }
        if (kind == 41) {  // generic komono ctor 0x00461CF0: new task 0x005936BC[41] = 0x0046CC50
            Komono41Task* p = NewTask<Komono41Task>(WorldTramp<&Impl::TaskKomono41>, 0x0046CC50);
            memcpy(p->pos, r.pos, 12);
            memcpy(p->rot, r.rot, 12);
            p->life = static_cast<int16_t>(r.state);
            p->area = static_cast<int8_t>(runner->variant);
            p->areaCount = 0;
            p->flags34 = 0x80000001u;
            memcpy(p->part[0], r.pos, 12);
            p->part[1][0] = r.pos[0] + 8.0f;  // 0x004C43A0
            p->part[1][1] = r.pos[1];
            p->part[1][2] = r.pos[2] - 1.0f;  // 0x004C4380
            p->radius = 7.0f;                  // 0x40E00000
            ts.KillCurrent();
        }
        return false;
    }
    default:
        return false;
    }
}

// Camera sub-mode setups (0x00403970.. via 0x00576C14) also install the players' +0x7C callback (0x009A5CDC /
// 0x009A5E0C): 0x00415120 (player object animation) or 0x00415180 (damage overlay on hits)
void World::Impl::ApplySetupCallbacks() {
    if (!runner) return;
    if (runner->camera.setupCount != lastSetupCount) {
        lastSetupCount = runner->camera.setupCount;
        if (getenv("WORLD_TRACE_SETUP")) fprintf(stderr, "setup seed %d fn %x\n", seedCounter, runner->camera.setupFn);
        uint32_t cb = 0, cb80 = 0;
        switch (runner->camera.setupFn) {
        case 0x00403980: cb = 0x00415120; break;
        case 0x004039A0: cb = 0x00415120; cb80 = 0x00415960; break;
        case 0x004039D0: cb80 = 0x00415970; break;
        case 0x00403A00: cb = 0x00415180; cb80 = 0x00415980; break;
        case 0x00403A30: case 0x00403A60: case 0x00403A90: cb = 0x00415180; cb80 = 0x00415990; break;
        default: break;
        }
        for (Player& P : player) {
            if (cb != 0) P.cb7C = cb;
            if (cb80 != 0) P.cb80 = cb80;
        }
    }
    if (runner->camera.playerFnCount != lastPlayerFnCount) {  // camera command 0x12 (0x00403780)
        lastPlayerFnCount = runner->camera.playerFnCount;
        for (Player& P : player) P.cb80 = runner->camera.playerFn;
    }
}

}  // namespace game
