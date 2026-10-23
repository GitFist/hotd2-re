#pragma once

#include "model_catalog.h"

#include <algorithm>
#include <initializer_list>

namespace play {

// Audio belonging to enemy actions, in addition to EnemyMotionSounds' fixed
// footstep / swing ticks. These are native sound IDs, not guesses from WAV
// names. Scene ambience, player damage voices, and physics-dependent landing
// sounds do not have a motion-relative tick and are not attached here.
inline void AddModelEnemySounds(ModelCatalogEntry& model) {
    if (model.category != ModelCategory::Enemies) return;
    auto cue = [](ModelAnimation& animation, int tick, uint32_t sound,
                  std::initializer_list<uint32_t> variants = {}) {
        const auto same = [=](const ModelSoundCue& c) { return c.tick == tick && c.soundId == sound; };
        auto found = std::find_if(animation.sounds.begin(), animation.sounds.end(), same);
        if (found == animation.sounds.end()) {
            animation.sounds.push_back({tick, sound});
            found = animation.sounds.end() - 1;
        }
        if (variants.size() != 0) found->variants.assign(variants);
    };
    auto motions = [&](std::initializer_list<int> ids, int tick, uint32_t sound,
                       std::initializer_list<uint32_t> variants = {}) {
        for (auto& animation : model.animations)
            if (std::find(ids.begin(), ids.end(), animation.motionId) != ids.end())
                cue(animation, tick, sound, variants);
    };
    // Native EnemyVoice (0x0040A6F0) draws independently for every event,
    // including when the previous draw happened to choose the same sample.
    // Preserve the five impact choices from 0x00577674 and the two attack
    // choices from each family row at 0x005776A0 / 0x005776A8.
    const std::initializer_list<uint32_t> impacts = {0x216A9, 0x316A9, 0x416A9, 0x616A9, 0xD16A9};
    const std::initializer_list<uint32_t> heavyAttacks = {0x1317A9, 0x2217A9};
    const std::initializer_list<uint32_t> otherAttacks = {0x1617A9, 0x117A9};
    const int kind = model.skeletonType;
    if (kind >= 0 && kind <= 0x14) {
        // The native pain and death voices are fixed per family. Only the
        // impact and attack-growl rows contain random sample alternatives;
        // the shared approach groan also stays fixed.
        const bool heavy = kind == 0 || kind == 2 || kind == 5 || kind == 6 ||
                           kind == 9 || kind == 0xE || (kind >= 0xF && kind <= 0x11);
        const uint32_t pain = heavy ? 0x617A9u : 0x817A9u;
        const uint32_t death = heavy ? 0xE17A9u : 0xD17A9u;
        const uint32_t attack = heavy ? 0x1317A9u : 0x1617A9u;

        // Native 0x00592F18[kind][variant] attack-motion rows. A kind mask
        // matters: a motion reused as an idle or reaction by another family
        // must not acquire that family's attack voice. EnemyBeh3 starts the
        // selected motion and calls EnemyVoice(e, 3) together (0x00455A40).
        struct Action { int motion; uint32_t kinds; };
        static constexpr Action attacks[] = {
            {0x00E, 0x000002u}, {0x0C2, 0x00001Cu}, {0x0C8, 0x00001Cu}, {0x0C9, 0x00001Cu},
            {0x0FD, 0x000001u}, {0x0FE, 0x000001u}, {0x0FF, 0x000001u},
            {0x16A, 0x1B8202u}, {0x16B, 0x1B8202u}, {0x16C, 0x1B8202u}, {0x16F, 0x1B8202u},
            {0x1A1, 0x001800u}, {0x1A2, 0x001800u}, {0x1C3, 0x004000u}, {0x1C4, 0x004000u},
            {0x1DD, 0x000400u}, {0x1DE, 0x000400u}, {0x2B2, 0x000060u}, {0x2BA, 0x000060u},
            {0x307, 0x180002u}, {0x30A, 0x180002u},
            {0x3AC, 0x002000u}, {0x3AD, 0x002000u}, {0x3AE, 0x002000u}, {0x3AF, 0x002000u},
            {0x3C4, 0x1C5982u}, {0x3C6, 0x185882u}, {0x3C7, 0x185882u}, {0x3C9, 0x1C59C2u},
            {0x3CB, 0x1818A2u}, {0x3D0, 0x185882u}, {0x3D7, 0x1FFFFEu}, {0x3E5, 0x181882u},
            {0x3E9, 0x040100u}, {0x3EA, 0x040100u}, {0x3EC, 0x180000u}, {0x3ED, 0x180000u},
            {0x3EE, 0x180002u}, {0x3F4, 0x180082u}, {0x3F5, 0x1B8282u}, {0x3FA, 0x181882u},
        };
        // EnemyBeh2 (0x00455720) changes to motion-set row 0 and emits the
        // native approach growl. Rows come from 0x00592CBC[kind][variant].
        static constexpr Action approach[] = {
            {0x00D, 0x000002u}, {0x0BB, 0x078200u}, {0x0C7, 0x000014u}, {0x10E, 0x000001u},
            {0x116, 0x000008u}, {0x165, 0x1F8202u}, {0x1AF, 0x001800u}, {0x1C2, 0x004000u},
            {0x1E0, 0x000400u}, {0x2B8, 0x000060u}, {0x311, 0x180002u}, {0x3B3, 0x002000u},
            {0x3B5, 0x002000u}, {0x3BC, 0x000001u}, {0x3CF, 0x184196u}, {0x41E, 0x001800u},
        };
        for (auto& animation : model.animations) {
            for (const auto& action : attacks)
                if (animation.motionId == action.motion && (action.kinds & (1u << kind)))
                    cue(animation, 0, attack, heavy ? heavyAttacks : otherAttacks);
            for (const auto& action : approach)
                if (animation.motionId == action.motion && (action.kinds & (1u << kind))) cue(animation, 0, 0x1917A9);
        }
        // EnemyHitMotion uses 0x00592FC8 -> 0x00567850 / 0x00567870. A
        // nonfatal shot plays both blood impact and the family pain sample.
        for (uint32_t sound : {0x216A9u, pain})
            motions({0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107,
                     0x3C0, 0x3C1, 0x3CE, 0x3D1, 0x3D3, 0x3D5, 0x3D6}, 0, sound,
                    sound == 0x216A9 ? impacts : std::initializer_list<uint32_t>{});
        // EnemyStartDeathMotion / EnemyDeathMotionWalk (0x004560B0 /
        // 0x00456220). The shot that starts death calls EnemyVoice(e, 1).
        for (uint32_t sound : {0x216A9u, death})
            motions({0x1A5, 0x1AC, 0x3D9, 0x3DA, 0x3DB, 0x3DC, 0x3DD, 0x3DE,
                     0x3DF, 0x3E0, 0x3F9, 0x404, 0x41A}, 0, sound,
                    sound == 0x216A9 ? impacts : std::initializer_list<uint32_t>{});
        // EnemySetupKind2 (0x00452FD0): Max's saw / Eric's laser blade
        // start with their living actor, independently of its current motion.
        // Gallery playback owns the loop and stops it on pause / mute / exit.
        // Kind 4 (undead Goldman) shares Max's rig but has no such spawn cue.
        if (kind == 2 || kind == 3) {
            for (auto& animation : model.animations) {
                const int m = animation.motionId;
                const bool dying = (m >= 0x3D9 && m <= 0x3E0) || m == 0x3F9 ||
                                   m == 0x404 || m == 0x41A || m == 0x1A5 || m == 0x1AC;
                cue(animation, 0, dying ? (kind == 2 ? 0x4E17A9u : 0x2025A9u)
                                        : (kind == 2 ? 0x4D17A9u : 0x1F25A9u));
            }
        }
        // Beh33's variant-7/8 throw entries: Mickey's paired knives leave at
        // tick42; Johnny/Cecil's left/right axe throws leave at tick35. Both
        // flight states (0x00459690 / 0x004598F0) start the native weapon loop.
        if (kind == 1) motions({0xE}, 42, 0x4F17A9);
        if (kind == 0x13 || kind == 0x14) motions({0x3EC, 0x3ED}, 35, 0x4F17A9);
    } else if (kind >= 0x16 && kind <= 0x19) {
        // Actor31 Hits / Beh2 / Beh33 (0x004499A0 / 0x0044A450 /
        // 0x00450E40): full-body shot reactions. Landing thumps are driven by
        // collision, not a stored clip tick, so only the shot itself is cued.
        for (uint32_t sound : {0x216A9u, 0x817A9u})
            motions({0x3A1, 0x3A2, 0x3A3, 0x3A6, 0x3A7, 0x3AA, 0x3AB,
                     0x1BC, 0x1BD, 0x1BE, 0x1BF, 0x215, 0x1FE, 0x1F4, 0x206}, 0, sound,
                    sound == 0x216A9 ? impacts : std::initializer_list<uint32_t>{});
        if (kind == 0x16)
            for (uint32_t sound : {0x216A9u, 0xD17A9u})
                motions({0x11E}, 0, sound, sound == 0x216A9 ? impacts : std::initializer_list<uint32_t>{});
        if (kind == 0x18 || kind == 0x19)
            for (uint32_t sound : {0x216A9u, 0xD17A9u})
                motions({0x11D}, 0, sound, sound == 0x216A9 ? impacts : std::initializer_list<uint32_t>{});
        // Beh17 (0x0044C2E0): the recovery interrupted by a fatal shot.
        motions({0x127}, 0, 0x1C17A9);
        if (kind == 0x17) {
            // Beh24 / Beh32 start the table's 0x1B9 attack and growl together.
            motions({0x1B9}, 0, 0x1617A9, otherAttacks);
        }
        if (kind == 0x18) {
            // Beh31 creates the thrown blade at tick 0x19, then WeaponFlight
            // (0x0044FD40) starts its loop. The gallery stops it at clip wrap.
            motions({0x1F7, 0x1F6, 0x1FC, 0x1FB, 0x1F2, 0x1F1, 0x204, 0x203}, 0x19, 0x5117A9);
            // Beh22's landing restarts 0x1E9 alongside the landing sound,
            // attack voice, and flicker loop. Its exit 0x1E8 cues the leap
            // halfway through (44 original ticks / 2).
            motions({0x1E9}, 0, 0x2916A9);
            motions({0x1E9}, 0, 0x1617A9, otherAttacks);
            motions({0x1E9}, 0, 0x1F23A9);
            motions({0x1E8}, 22, 0x2516A9);
        }
    } else if (kind == 0x1B) {
        // Actor11Shot and Actor11Sit (world_routes.cpp): ordinary Bouere's
        // shot motion / croak. The gold item has a separate reward cue.
        motions({0x13F}, 0, 0x716A9);
        motions({0x141}, 0, 0x3716A9);
    } else if (kind == 0x1C) {
        motions({0x13F}, 0, 0x3B17A9); // TaskCreatureItem (0x00471FA0).
    } else if (kind == 0x1D) {
        // TaskBat (0x0043C440) starts motion 0x219 with the shot sample and
        // its authored bubbling splash. The two stage-1 choices are selected
        // by Rand(0x0043C4E6) & 1; movement / bite have no intrinsic sound.
        motions({0x219}, 0, 0x1D18A9, {0x1D18A9, 0x1E18A9});
        motions({0x219}, 0, 0x118A9);
    }
    if (model.assembly == ModelAssembly::Owl && model.animations.size() >= 2) {
        // State0 / StartSwoop (world_st2_bats.cpp): takeoff wing beat, and
        // TaskBat's two equally weighted shot samples (0x00446130).
        cue(model.animations[0], 0, 0x4A17A9);
        cue(model.animations[1], 0, 0x4217A9, {0x4217A9, 0x4117A9});
    }
    if (model.assembly == ModelAssembly::Fish && model.animations.size() >= 3) {
        // Hits (0x00438C10) plays the same impact before the air/water branch.
        cue(model.animations[1], 0, 0x716A9);
        cue(model.animations[2], 0, 0x716A9);
    }
    // Cat (actor53), flying Bat (actor46 / motion407), the moth, and swimming
    // fish have no per-cycle audio. Their scene or shot events must not be
    // substituted for a quiet locomotion clip.
}

} // namespace play
