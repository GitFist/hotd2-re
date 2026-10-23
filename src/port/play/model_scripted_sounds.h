#pragma once

#include "model_catalog.h"

#include <algorithm>
#include <initializer_list>

namespace play {

inline void AddModelCivilianSounds(ModelCatalogEntry& entry) {
    if (entry.category != ModelCategory::Civilians) return;
    auto cue = [&](int motion, int tick, uint32_t sound) {
        for (auto& animation : entry.animations) if (animation.motionId == motion) {
            const auto duplicate = std::find_if(animation.sounds.begin(), animation.sounds.end(),
                [&](const auto& previous) { return previous.tick == tick && previous.soundId == sound; });
            if (duplicate == animation.sounds.end()) animation.sounds.push_back({tick, sound});
        }
    };
    // CivVoice (world_civilian.cpp 0x0048D140) runs immediately after an
    // alternative script begins. These are that script's first motions, read
    // from the 0x005702A8 table for the shipped actor-0x10 spawn records.
    // Voice group follows the actual civilian kind; the blue blazer's 0x17E
    // script explicitly selects group 1 (0x0056BD7C).
    struct Reaction { int kind, motion; };
    static constexpr Reaction reactions[] = {
        {0x20,0x17A}, {0x21,0x172}, {0x21,0x17A}, {0x21,0x229},
        {0x22,0x17A}, {0x22,0x26C}, {0x23,0x17A},
        {0x24,0x172}, {0x24,0x17A}, {0x24,0x19F}, {0x24,0x252}, {0x24,0x25A},
        {0x25,0x17A}, {0x25,0x17E}, {0x25,0x229}, {0x26,0x172}, {0x26,0x278},
        {0x27,0x17A}, {0x27,0x19F}, {0x27,0x26B},
        {0x28,0x17A}, {0x28,0x19F}, {0x28,0x26B}, {0x29,0x17A}, {0x29,0x194},
        {0x2A,0x172}, {0x2A,0x178}, {0x2A,0x17A},
        {0x2B,0x17A}, {0x2B,0x194}, {0x2B,0x19F},
        {0x2E,0x172}, {0x2E,0x17A}, {0x2E,0x199}, {0x2E,0x265},
        {0x2F,0x172}, {0x2F,0x229}, {0x2F,0x2A4},
        {0x30,0x17A}, {0x30,0x229}, {0x30,0x254}, {0x30,0x256}, {0x30,0x25E}, {0x30,0x25F}, {0x30,0x26B},
        {0x31,0x17A}, {0x31,0x17E}, {0x31,0x198}, {0x32,0x229}, {0x32,0x278}, {0x33,0x278},
        {0x34,0x17A}, {0x34,0x189}, {0x34,0x198}, {0x34,0x199}, {0x34,0x1A0}, {0x34,0x266}, {0x34,0x278},
        {0x35,0x172}, {0x35,0x17A}, {0x35,0x2A4},
        {0x36,0x17A}, {0x36,0x184}, {0x36,0x279}, {0x37,0x178}, {0x37,0x17A}, {0x38,0x17A},
    };
    uint32_t scream;
    switch (entry.skeletonType) {
    case 0x20: case 0x23: scream = 0x2000000D; break;
    case 0x21: case 0x22: scream = 0x20000011; break;
    case 0x24: case 0x25: case 0x2E: case 0x31: case 0x32: case 0x33: scream = 0x2000001A; break;
    case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: scream = 0x2000000B; break;
    default: scream = 0x20000012; break;
    }
    for (const auto& reaction : reactions) if (entry.skeletonType == reaction.kind)
        cue(reaction.motion, 0, reaction.kind == 0x31 && reaction.motion == 0x17E ? 0x20000012 : scream);

    // CivRunScript ops 0x21/0x22 arm model-local sound timers. Only blocks
    // that explicitly select the motion are included; the timer's first
    // decrement happens in that same update, hence tick = delay - 1. Where
    // several scenes reuse a pose, keep one authored performance rather than
    // overlaying their unrelated voices. Scene/caption op 0x1D is excluded.
    struct Timed { int kind, motion, tick; uint32_t sound; };
    static constexpr Timed timed[] = {
        {0x21,0x183,14,0x20000026}, // 0056FC88
        {0x21,0x238,0,0x20000036}, // 0056F5D8
        {0x22,0x176,0,0x2000000E}, // 0056CC88
        {0x24,0x182,0,0x2000001D}, // 0056F4A4
        {0x24,0x23B,0,0x20000016}, // 0056F9DC
        {0x24,0x250,0,0x20000019}, // 0056EB78
        {0x24,0x25B,0,0x20000018}, // 0056E07C
        {0x24,0x281,0,0x2000000A}, // 0056E97C
        {0x24,0x287,0,0x20000004}, // 0056F510
        {0x24,0x28B,0,0x2000000A}, // 0056F27C
        {0x25,0x17B,0,0x2000001D}, // 0056FA4C
        {0x26,0x183,0,0x20000021}, // 0056D0E8
        {0x26,0x184,4,0x1E16A9}, // 0056D508
        {0x26,0x18A,9,0x20000024}, // 0056CE60
        {0x26,0x190,19,0x20000010}, // 0056D8E4
        {0x26,0x274,0,0x2000000B}, // 0056D1A0
        {0x26,0x27A,19,0x20000006}, // 0056D1EC
        {0x27,0x173,0,0x20000024}, {0x27,0x173,80,0x20000024}, // 0056D600
        {0x27,0x175,9,0x2000000C}, // 0056C164
        {0x27,0x177,9,0x20000024}, // 0056C030
        {0x27,0x26B,0,0x3218A9}, // 0056C0A0 supersedes 0056C08C in the same block
        {0x28,0x175,9,0x2000000C}, // 0056C164
        {0x28,0x177,9,0x20000024}, // 0056C030
        {0x28,0x26B,0,0x3218A9}, // 0056C0A0
        {0x2A,0x171,9,0x20000024}, // 0056C580
        {0x2A,0x190,0,0x2000000C}, // 0056E418
        {0x2B,0x173,0,0x20000005}, // 0056F938
        {0x2E,0x173,0,0x20000019}, // 0056BBE0
        {0x2E,0x181,9,0x20000020}, // 0056DDDC
        {0x2E,0x185,9,0x20000018}, // 0056BC0C
        {0x2E,0x274,0,0x2000001A}, // 0056DEB8
        {0x2F,0x2A9,0,0x2000002F}, // 0056E2FC
        {0x2F,0x2AE,29,0x2000001D}, // 0056E280
        {0x30,0x177,59,0x20000017}, // 0056FB84
        {0x30,0x247,0,0x2000002F}, // 0056EDCC
        {0x30,0x258,0,0x2000001D}, // 0056F088
        {0x30,0x260,0,0x2000001F}, // 0056ED78
        {0x30,0x28B,0,0x2000000A}, // 0056F104
        {0x31,0x17B,0,0x20000014}, // 0056BDAC
        {0x32,0x183,0,0x20000019}, // 0056E594
        {0x32,0x22D,0,0x20000020}, // 0056E5DC
        {0x32,0x22E,0,0x2000001D}, // 0056E608
        {0x34,0x173,9,0x20000029}, // 0056D37C
        {0x34,0x19C,24,0x20000003}, // 0056D484
        {0x34,0x24A,29,0x20000014}, // 0056FD64
        {0x34,0x267,9,0x20000015}, // 0056DD10
        {0x34,0x270,9,0x20000014}, // 0056D85C
        {0x34,0x271,1,0x20000015}, // 0056D3DC
        {0x34,0x274,0,0x20000013}, // 0056D3B4
        {0x35,0x2A9,0,0x2000002B}, // 0056EA84
        {0x35,0x2AE,39,0x20000029}, // 0056EA08
        {0x36,0x18A,9,0x20000029}, // 0056F3B4
        {0x36,0x238,0,0x20000038}, // 0056F704
        {0x36,0x271,0,0x20000014}, // 0056CAC4
        {0x36,0x298,0,0x2000002D}, // 0056F370
        {0x37,0x173,0,0x20000014}, // 0056EF20
        {0x38,0x238,0,0x20000037}, // 0056F7AC
    };
    for (const auto& sound : timed) if (entry.skeletonType == sound.kind)
        cue(sound.motion, sound.tick, sound.sound);
}

// Native boss state machines and actor-0x25 commands attach audio to motions,
// rather than storing audio in the motion files themselves. Keep these cues
// separate from scene-wide dialogue, camera timers and player-hit sounds.
inline void AddModelScriptedSounds(ModelCatalogEntry& entry) {
    auto cue = [&](int motion, uint32_t sound, std::initializer_list<int> ticks) {
        for (auto& animation : entry.animations) if (animation.motionId == motion) {
            for (int tick : ticks) {
                const auto duplicate = std::find_if(animation.sounds.begin(), animation.sounds.end(),
                    [&](const auto& previous) { return previous.tick == tick && previous.soundId == sound; });
                if (duplicate == animation.sounds.end()) animation.sounds.push_back({tick, sound});
            }
        }
    };

    if (entry.category == ModelCategory::Agents) {
        // Shared player-rig clips retain their authored shot timing, but the
        // four named agents use their own weapon samples. EVT st2 scripts
        // A8DC/A9A8/1011C/10208 independently identify these four sounds.
        uint32_t gun = 0;
        switch (entry.skeletonType) {
        case 0x39: gun = 0x3416A9; break;
        case 0x3A: gun = 0x3316A9; break;
        case 0x3B: gun = 0x3216A9; break;
        case 0x3C: gun = 0x3016A9; break;
        }
        if (gun) for (auto& animation : entry.animations)
            if (animation.motionId == 0x32D || animation.motionId == 0x355 || animation.motionId == 0x35D)
                for (auto& sound : animation.sounds) sound.soundId = gun;
        // st2evtbl D520 / D624 share this bridge-running clip and step sound;
        // its script explicitly waits for motion ticks 0x1E and 0x42.
        if (entry.skeletonType == 0x39 || entry.skeletonType == 0x3A)
            cue(0x325, 0x3119A9, {0x1E, 0x42});
        // st2evtbl 5818 / 58C4: their hurt sounds release a frozen opening
        // pose, so the isolated clip's effect belongs at motion tick zero.
        if (entry.skeletonType == 0x39) cue(0x323, 0x1EA9, {0});
        if (entry.skeletonType == 0x3A) cue(0x323, 0x21DA9, {0});
        return;
    }

    if (entry.category == ModelCategory::Bosses) switch (entry.skeletonType) {
    case 0x44: // world_st1b_boss.cpp MountBeh0 / 1 / 2, variant 0.
        cue(0x386, 0x18A9, {0x32});
        cue(0x386, 0x2316A9, {0x46}); cue(0x386, 0x2B16A9, {0x46});
        // Attack rows 0x005703E0: motion and sound-tick fields.
        cue(0x383, 0x218A9, {0}); cue(0x383, 0x18A9, {149});
        cue(0x384, 0x218A9, {0}); cue(0x384, 0x18A9, {120});
        cue(0x385, 0x218A9, {0}); cue(0x385, 0x18A9, {139});
        cue(0x393, 0x218A9, {0x37, 0x81, 0xCF, 0x11A});
        cue(0x394, 0x218A9, {0x2D, 0x67, 0xA7, 0xE1});
        cue(0x390, 0x418A9, {0x4B, 0xA6}); cue(0x391, 0x418A9, {0x3C, 0x84});
        cue(0x388, 0x418A9, {0x39, 0x5A}); cue(0x389, 0x418A9, {0x2F, 0x48});
        cue(0x38B, 0x3C16A9, {0xF, 0x1E, 0x28});
        cue(0x38C, 0x3C16A9, {0xD, 0x18, 0x1E});
        cue(0x38E, 0x417A9, {0}); cue(0x38F, 0x417A9, {0});
        cue(0x38A, 0x2216A9, {0x68});
        break;
    case 0x45: // Zeal: Stagger / BossBeh1 / BossBeh2 / BossBeh3, variant 1.
        cue(0x408, 0x2A18A9, {0}); cue(0x409, 0x2A18A9, {0});
        cue(0x40E, 0x3018A9, {0}); cue(0x40A, 0x2A16A9, {0x92});
        break;
    case 0x47: // Hierophant: rows 0x00596408, St0/1/7/9/11/12/15/18.
        cue(0x2D, 0xC16A9, {0x1E});
        cue(0x26, 0x417A9, {0x37});
        cue(0x1D, 0xD17A9, {0}); cue(0x33, 0x2A16A9, {0x23});
        cue(0x30, 0x1617A9, {0}); cue(0x24, 0x2A16A9, {0});
        cue(0x3A, 0x1717A9, {0}); cue(0x1B, 0x1417A9, {0});
        break;
    case 0x48: // Tower's central head (world_st3_boss.cpp TaskHead).
        // HeadDraw increments breath before TaskHead compares it: breath
        // 3 / 15 correspond to the clip's zero-based ticks 2 / 14.
        cue(0x40, 0x271AA9, {2}); cue(0x40, 0x251AA9, {14});
        cue(0x49, 0x241AA9, {8});
        cue(0x41, 0x1C17A9, {8}); cue(0x41, 0xB16A9, {0x70});
        // TaskBody5 uses the same rig for the final swimming phase.
        cue(0x42, 0x261AA9, {0});
        cue(0x3B, 0x271AA9, {0}); cue(0x3C, 0x271AA9, {0});
        cue(0x3D, 0x4116A9, {0x36});
        break;
    case 0x49: // Outer head attack/flinch tables 0x00588F1C..0x00588F50.
        for (int motion : {0x4A, 0x4B, 0x4D, 0x4E}) {
            cue(motion, 0x271AA9, {2}); cue(motion, 0x251AA9, {14});
        }
        for (int motion : {0x63, 0x64, 0x55, 0x56}) cue(motion, 0x241AA9, {8});
        cue(0x4C, 0x1C17A9, {8}); cue(0x4C, 0xB16A9, {0x4B});
        break;
    case 0x4A: // Strength: StIntro, St6, St18, St20/21 and St22.
        // SoundOn starts the living chainsaw motor for the fight. Death
        // keeps it running until SoundOff at the authored fall tick.
        for (auto& animation : entry.animations) cue(animation.motionId, 0x4D17A9, {0});
        cue(0x69, 0x4E17A9, {0x82});
        cue(0x75, 0x231BA9, {0}); cue(0x7D, 0x271BA9, {0});
        // Carried-object table 0x005704F8 has two authored throw clips.
        cue(0x67, 0x1BA9, {0x3F}); cue(0x68, 0x1BA9, {0x3F});
        for (int motion : {0x6F, 0x73, 0x71}) cue(motion, 0x281BA9, {0});
        cue(0x69, 0x241BA9, {0}); cue(0x69, 0xB16A9, {0x82});
        break;
    case 0x4B: // Magician: projectile ShotState0 and states 2/5/7/10.
        cue(0x82, 0xD23A9, {0x1D});
        cue(0x8C, 0xD23A9, {0x46}); cue(0x8B, 0xD23A9, {0x46});
        cue(0x8A, 0xD23A9, {0x5A});
        cue(0x88, 0x1123A9, {0}); cue(0x89, 0x1123A9, {0});
        cue(0x8F, 0x1023A9, {0x1F}); cue(0x94, 0xC23A9, {0xA5});
        break;
    case 0x4C: // Emperor: world_st6_boss.cpp State3/4/5 and BossDeath.
        cue(0xAA, 0x1225A9, {0xB}); cue(0xAA, 0x1625A9, {0x37});
        cue(0x9B, 0x1B25A9, {0});
        cue(0x9B, 0x1C25A9, {0x2A}); cue(0x9B, 0x1D23A9, {0x2A});
        cue(0xAB, 0x1225A9, {0}); cue(0x9D, 0x225A9, {0});
        cue(0x9E, 0x1925A9, {0}); cue(0x9F, 0x325A9, {0});
        // The re-formation sample accompanies these three return motions.
        cue(0xA8, 0x25A9, {0}); cue(0xA9, 0x25A9, {0}); cue(0xAC, 0x25A9, {0});
        break;
    }
}

} // namespace play
