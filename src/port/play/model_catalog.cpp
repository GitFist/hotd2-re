#include "model_catalog.h"
#include "model_enemy_sounds.h"
#include "model_scripted_sounds.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace play {
namespace {

using Animations = std::vector<ModelAnimation>;

// Fixed tick cues from World::Impl::EnemyMotionSounds. Per-model voices,
// weapon motors, NPC commands and native boss cues are added after assembly
// so a shared motion cannot borrow a different character's sounds.
std::vector<ModelSoundCue> MotionSounds(int motion) {
    struct Cue { int motion; uint32_t sound; std::initializer_list<int> ticks; };
    const Cue cues[] = {
        {0x00B, 0x2616A9, {1, 10, 20, 30}}, {0x0B3, 0x2616A9, {9, 27}},
        {0x0C3, 0x2916A9, {6, 36}}, {0x108, 0x2916A9, {7, 18}},
        {0x114, 0x2916A9, {6, 36}}, {0x1AE, 0x2616A9, {6, 21}},
        {0x1C1, 0x2916A9, {2, 26}}, {0x1C3, 0x3C16A9, {30}},
        {0x1C4, 0x3D16A9, {17, 50}}, {0x2B5, 0x2616A9, {4, 20}},
        {0x307, 0x3C16A9, {40}}, {0x30A, 0x3C16A9, {40}},
        {0x310, 0x2916A9, {8, 27}}, {0x3B0, 0x2616A9, {8, 32}},
        {0x3B1, 0x2616A9, {2, 26}}, {0x3C8, 0x2616A9, {6, 37}},
    };
    for (const auto& cue : cues) if (cue.motion == motion) {
        std::vector<ModelSoundCue> result;
        for (int tick : cue.ticks) result.push_back({tick, cue.sound});
        return result;
    }
    return {};
}

void Add(Animations& out, int id, const char* name = nullptr) {
    if (std::any_of(out.begin(), out.end(), [id](const auto& a) { return a.motionId == id; })) return;
    // Motion files have numerical IDs, not embedded animation names. Keep
    // unnamed clips honest instead of guessing which action a pose depicts.
    out.push_back({name ? std::string(name) : "Animation " + std::to_string(out.size() + 1), id, MotionSounds(id)});
}
void Range(Animations& out, int first, int last) {
    for (int i = first; i <= last; ++i) Add(out, i);
}
Animations Clips(std::initializer_list<int> ids) {
    Animations out;
    for (int id : ids) Add(out, id);
    return out;
}
Animations Bank(int first, int last, int initial = -1, const char* initialName = nullptr) {
    Animations out;
    if (initial >= 0) Add(out, initial, initialName);
    Range(out, first, last);
    return out;
}
void Label(Animations& clips, int id, const char* name) {
    for (auto& clip : clips) if (clip.motionId == id) clip.name = name;
}
void Sound(Animations& clips, int id, int tick, uint32_t sound) {
    for (auto& clip : clips) if (clip.motionId == id) clip.sounds.push_back({tick, sound});
}

Animations PlayerAnimations() {
    Animations out;
    Add(out, 0x32C, "Standing"); // PlayerBodyCreate / Horde MakeBody.
    // All player costumes use the same sixteen-node player motion rig, also
    // used by Original Mode when a player changes character (orig5.md).
    Range(out, 0x2BB, 0x2F9);
    Range(out, 0x319, 0x382);
    Add(out, 0x3BA);
    Label(out, 0x338, "Game over"); // world_player.cpp PlayerBodyDeath.
    Label(out, 0x32A, "Use communicator");
    Label(out, 0x356, "Pick up pistol");
    Label(out, 0x35D, "Fire three shots");
    // Authored NPC commands in st2evtbl.bin, interpreted by world_npc.cpp:
    // script 0x1011C waits for motion ticks 0x26/0x40/0x5A before op13.
    // Amy (0xA8DC) and Harry (0xA9A8) sound once when their frozen firing
    // pose is released. Subsequent shots restart that same clip in the scene.
    Sound(out, 0x35D, 0x26, 0x3416A9);
    Sound(out, 0x35D, 0x40, 0x3416A9);
    Sound(out, 0x35D, 0x5A, 0x3416A9);
    Label(out, 0x32D, "Fire - Amy animation"); Sound(out, 0x32D, 0, 0x3216A9);
    Label(out, 0x355, "Fire - Harry animation"); Sound(out, 0x355, 0, 0x3016A9);
    return out;
}

Animations CivilianAnimations() {
    Animations out;
    Add(out, 0x294, "Standing"); // CivCreate's initial pose.
    Range(out, 0x171, 0x18A);
    // 0x18B..0x18E have zero ticks in the original exe, so they cannot play.
    Range(out, 0x18F, 0x1A0);
    Range(out, 0x21A, 0x2B1); // people.bin; shared sixteen-node civilian rig.
    return out;
}

std::vector<ModelCatalogEntry> BuildCatalog() {
    std::vector<ModelCatalogEntry> out;
    auto append = [&](const char* name, ModelCategory category, int skeleton, Animations animations,
                      int head = -1, int part1 = -1, int part2 = -1) -> ModelCatalogEntry& {
        ModelCatalogEntry e;
        e.name = name;
        e.category = category;
        e.skeletonType = skeleton;
        e.headPart = head; e.part1 = part1; e.part2 = part2;
        e.animations = std::move(animations);
        out.push_back(std::move(e));
        return out.back();
    };
    const auto player = PlayerAnimations();
    // Identity audit against EXE skeleton roots 0x004E0430 and the original
    // type-0x25 spawn records, whose byte +0x24 is the skeleton kind:
    // st1evtbl.bin 0x1D58 / 0x1DFC are James / Gary (player1 / player2).
    // st2evtbl.bin 0xA8A8 is Amy (0x3B, player3, head 0x15AD), while
    // 0xA974 is Harry (0x3C, player4, sunglasses head 0x15BF). Their scripts
    // at 0xA8DC / 0xA9A8 begin with motions 0x32D / 0x355 respectively.
    // These identities were also checked by rendering the original heads;
    // do not infer Amy/Harry from the former Horde-mode comments.
    append("James Taylor", ModelCategory::Agents, 0x39, player);
    auto gary = player;
    // Gary's matching script at st2evtbl.bin 0x10208 has its final shot two
    // ticks earlier and uses his weapon sound instead of James's.
    for (auto& a : gary) if (a.motionId == 0x35D)
        a.sounds = {{0x26, 0x3316A9}, {0x40, 0x3316A9}, {0x58, 0x3316A9}};
    append("Gary Stewart", ModelCategory::Agents, 0x3A, std::move(gary));
    append("Harry Harris", ModelCategory::Agents, 0x3C, player);
    append("Amy Crystal", ModelCategory::Agents, 0x3B, player);
    // G: st1evtbl.bin record 0x1B98, kind 0x3E, script 0x1BCC motion 0x148;
    // hou.bin body/head 0xC39 with the g.bin motion bank.
    append("G", ModelCategory::Agents, 0x3E, Bank(0x146, 0x14C));
    // Rogan: endevtbl.bin record 0x1F68, kind 0x3F, script 0x1F9C motion
    // 0x2CF; logan.bin head 0x1265 plus PlayerBodyNode's chest attachment.
    append("Thomas Rogan", ModelCategory::Agents, 0x3F, player).extraMeshes = {{1, 0x125D}};

    // Complete appearances verified against original EVT civilian spawn records
    // and native gallery captures. Bank names are not character identities:
    // hito_fem is a man, and galjk / galjkaa wear casual jackets. Describe the
    // visible outfits rather than assigning invented names or occupations.
    // Several heads need these authored hair/hat parts to close the scalp.
    struct Civilian { const char* name; int kind, head, part1, part2; };
    const Civilian civilians[] = {
        {"Boy - Cap and Shorts", 0x20, 1, 38, 39},
        {"Older Woman - Apron", 0x21, 0, 36, -1},
        {"Older Woman - Blue Skirt", 0x22, -1, 36, 37},
        {"Boy - Yellow Shirt", 0x23, 2, 39, -1},
        {"Man - Red Cap", 0x24, 13, 41, 64},
        {"Man - Tan Jacket", 0x25, 9, 61, -1},
        {"Woman - Dark Suit", 0x26, 3, 50, 53},
        {"Woman - Green Shirt", 0x27, 3, 45, -1},
        {"Woman - White Blouse", 0x28, -1, 45, -1},
        {"Woman - Red Skirt", 0x29, -1, 50, 53},
        {"Woman - Pink Jacket", 0x2A, 3, 45, -1},
        {"Woman - Orange Jacket", 0x2B, 3, 45, -1},
        {"Man - Navy Outfit", 0x2E, 7, 61, 59},
        {"Man - White Shirt", 0x2F, -1, 61, -1},
        {"Man - Brown Vest", 0x30, -1, 62, -1},
        {"Man - Blue Blazer", 0x31, -1, 67, -1},
        {"Man - Checked Shirt", 0x32, 7, 62, -1},
        {"Man - Brown Blazer", 0x33, -1, 67, -1},
        {"Older Man - Brown Cardigan", 0x34, 14, 75, -1},
        {"Older Man - Apron", 0x35, -1, 71, -1},
        {"Older Man - Green Shirt", 0x36, 17, 78, -1},
        {"Worker - Yellow Helmet", 0x37, 14, 80, -1},
        // Stage 6 records 0x97A6B8 / 0x97ACC4 use this complete civilian
        // with no extra parts (deka_musume.bin, scripts 0x2A / 0x2B).
        {"Woman - Black Dress", 0x38, -1, -1, -1},
    };
    const auto civilianMotions = CivilianAnimations();
    for (const auto& c : civilians)
        append(c.name, ModelCategory::Civilians, c.kind, civilianMotions, c.head, c.part1, c.part2);

    // Names were checked against the original enemy banks, native model
    // captures and Sega's manual/Perfect Guide creature names. Bank filenames
    // are not display names (zndina is Eric, znjoe is Peter, zngold is Goldman).
    // Basic locomotion comes from 0x00592CBC[kind][variant], with that enemy family's
    // own motion bank added (never arbitrary clips chosen by node count).
    auto basic = Clips({0x3CF, 0x3E8, 0x3FE, 0x3C8, 0x3F0});
    Label(basic, 0x3CF, "Waiting"); Label(basic, 0x3FE, "Walk"); Label(basic, 0x3C8, "Run");
    auto heavy = Bank(0xFD, 0x112, 0x10E, "Waiting");
    Label(heavy, 0x110, "Walk"); Label(heavy, 0x108, "Run");
    auto& heavyEntry = append("Bob", ModelCategory::Enemies, 0x00, heavy);
    // EnemyDrawModel's two-layer living torso (base model 0x1B3D).
    heavyEntry.nodeOverrides = {{1, 0x1B3E, 20}};
    heavyEntry.extraMeshes = {{1, 0x1B52, 30}};
    auto assassin = basic; Range(assassin, 0xB, 0xE);
    append("Mickey", ModelCategory::Enemies, 0x01, assassin);
    auto chainsaw = basic; Range(chainsaw, 0xC1, 0xC9);
    Label(chainsaw, 0xC3, "Chainsaw charge");
    append("Max", ModelCategory::Enemies, 0x02, chainsaw).nodeOverrides = {
        {5, 0x1BD3, 5}, {8, 0x1BCD, 5} // EnemyDrawModel's running saw teeth.
    };
    append("Eric", ModelCategory::Enemies, 0x03, Bank(0x113, 0x119, 0x116, "Waiting"));
    append("Goldman - Undead", ModelCategory::Enemies, 0x04, chainsaw);
    auto brute = Bank(0x2B2, 0x2BA, 0x2B8, "Waiting");
    Label(brute, 0x2B3, "Walk"); Label(brute, 0x2B5, "Run");
    append("Ricky", ModelCategory::Enemies, 0x05, brute);
    append("Patrick", ModelCategory::Enemies, 0x06, brute);
    auto walker = basic;
    Range(walker, 0x395, 0x3A0); Range(walker, 0x3BB, 0x3E0);
    for (int id : {0x3E2, 0x3E3, 0x3E4, 0x3E6, 0x3E7, 0x3E8, 0x3E9, 0x3EA,
                   0x3EF, 0x3F0, 0x3F1, 0x3F2, 0x3F3, 0x3F4, 0x3F5, 0x3F6, 0x3F7,
                   0x3F8, 0x3F9, 0x3FB, 0x3FC, 0x3FD, 0x3FE, 0x3FF, 0x400, 0x401,
                   0x402, 0x403, 0x405, 0x417, 0x418, 0x420, 0x421}) Add(walker, id);
    append("David", ModelCategory::Enemies, 0x07, walker);
    append("Steve", ModelCategory::Enemies, 0x08, walker);
    auto swamp = Bank(0xB1, 0xBE, 0xBB, "Waiting"); Range(swamp, 0x160, 0x170);
    Label(swamp, 0xB8, "Rise from water");
    // Kind 9 is the silver phase form paired with kind 0x12 while Franklin
    // materializes (EnemySetupKind18 / EnemyKind9Update in world_st6_enemy.cpp).
    append("Franklin - Phase Form", ModelCategory::Enemies, 0x09, swamp);
    append("Peter", ModelCategory::Enemies, 0x0A, Bank(0x1DD, 0x1E4, 0x1E0, "Waiting")).extraMeshes = {
        {1, 0x1CB9, 120} // EnemyDrawModel: living chest over base 0x1C96.
    };
    auto ninja = Bank(0x1A1, 0x1B1, 0x1AF, "Waiting");
    append("Kageo", ModelCategory::Enemies, 0x0B, ninja);
    // This is the complete brown model. Kageo J is its separate gameplay
    // variant: EnemySetupKind12 additionally removes the lower body.
    append("Kageo - Brown", ModelCategory::Enemies, 0x0C, ninja);
    // EnemySetupKind12 variant 4 removes the second root subtree (slots
    // 9..15), then restores the severed waist mesh at slot 9. Its locomotion
    // table 0x00592CBC[0xC][4] uses hzom.bin, not the upright kage.bin clips.
    auto crawler = Bank(0x419, 0x41F, 0x41E, "Waiting");
    for (int id : {0x3E1, 0x3E5, 0x3FA, 0x404}) Add(crawler, id);
    Label(crawler, 0x41B, "Crawl"); Label(crawler, 0x41D, "Fall");
    append("Kageo J - Crawler", ModelCategory::Enemies, 0x0C, crawler).nodeOverrides = {
        {9, 0x1DA3}, {10, 0}, {11, 0}, {12, 0}, {13, 0}, {14, 0}, {15, 0}
    };
    append("Ken", ModelCategory::Enemies, 0x0D, Bank(0x3AC, 0x3B9, 0x3B3, "Waiting"));
    auto sword = basic; Range(sword, 0x1C0, 0x1C5);
    Label(sword, 0x1C1, "Charge"); Label(sword, 0x1C3, "Sword swing");
    append("Gregory", ModelCategory::Enemies, 0x0E, sword);
    append("Ebitan - Green", ModelCategory::Enemies, 0x0F, swamp);
    append("Ebitan - Brown", ModelCategory::Enemies, 0x10, swamp);
    append("Ebitan - Black", ModelCategory::Enemies, 0x11, swamp);
    append("Franklin", ModelCategory::Enemies, 0x12, swamp);
    auto axe = basic; Range(axe, 0x306, 0x318); Range(axe, 0x3EB, 0x3EE);
    Label(axe, 0x307, "Axe throw"); Label(axe, 0x310, "Run");
    append("Johnny", ModelCategory::Enemies, 0x13, axe);
    append("Cecil", ModelCategory::Enemies, 0x14, axe);
    auto leaper = Bank(0x11A, 0x13C, 0x3A8); Range(leaper, 0x3A1, 0x3AB);
    // Actor31's 0x16 / 0x18 / 0x19 share the szom locomotion rig; 0x17
    // has a distinct 24-node rig and exclusively uses kame.bin.
    append("Ryan", ModelCategory::Enemies, 0x16, leaper);
    append("Keith", ModelCategory::Enemies, 0x17, Bank(0x1B2, 0x1BF, 0x1BA));
    auto blade = leaper; Range(blade, 0x1E5, 0x216);
    append("Lance", ModelCategory::Enemies, 0x18, blade);
    append("Randy", ModelCategory::Enemies, 0x19, leaper);
    append("Cat", ModelCategory::Enemies, 0x1A, Bank(0x2FA, 0x305));
    append("Bouere", ModelCategory::Enemies, 0x1B, Bank(0x13D, 0x145));
    // Use the same centered opening clip as Bouere; 0x140 is its traveling hop.
    append("Bouere - Gold", ModelCategory::Enemies, 0x1C, Bank(0x13D, 0x145));
    auto murrer = Bank(0x217, 0x219, 0x218, "Moving");
    Label(murrer, 0x217, "Bite"); Label(murrer, 0x219, "Shot reaction");
    append("Murrer", ModelCategory::Enemies, 0x1D, murrer);
    // Anim2_InitNode's second complete appearance, 0x004E1F88[1][slot].
    // The cellar-window / stage-2 groups select this set in TaskBatInit.
    append("Murrer - Alternate", ModelCategory::Enemies, 0x1D, murrer).nodeOverrides = {
        {1, 0x1373}, {2, 0x1375}, {3, 0x1377}, {4, 0x1379}, {5, 0x137B},
        {6, 0x137D}, {7, 0x136F}, {8, 0x1381}, {9, 0x137F}
    };
    // Actor 0x46 is one creature assembled from two synchronized rigs.
    // SpawnBirds / SpawnBody use zabat.bin and zabat_wing.bin together.
    auto bat = Clips({0x407}); Label(bat, 0x407, "Flying");
    bat[0].companionMotionId = 0x406;
    append("Bat", ModelCategory::Enemies, 0x1E, bat).companionSkeletonType = 0x1F;
    // Actor 0x43 (world_st2_bats.cpp) has a hand-built body/wing/face/leg
    // hierarchy and authored mesh-frame cycles, rather than an EXE rig.
    append("Owl", ModelCategory::Enemies, -1,
           {{"Flapping", -1, {}, -1, 30}, {"Shot pose", -1, {}, -1, 30}}).assembly = ModelAssembly::Owl;
    // Komono 11's two whole-body frames are models 0x1CF / 0x1D0 in
    // komono_souko.bin (TaskKomono11). Native captures show the moth's
    // eyespot wings and insect body; the old bird/bat code comment was wrong.
    append("Moth", ModelCategory::Enemies, -1,
           {{"Flapping", -1, {}, -1, 2}}).assembly = ModelAssembly::AmbientInsect;
    // Actor 0x51's swimming frames and its two authored shot appearances
    // (Hits in world_st3_fish.cpp). The body is a complete mesh each tick.
    append("Fish", ModelCategory::Enemies, -1,
           {{"Swimming", -1, {}, -1, 20}, {"Shot in air", -1, {}, -1, 1},
            {"Shot underwater", -1, {}, -1, 1}}).assembly = ModelAssembly::Fish;

    // Goldman's living scene model belongs with the bosses by user request;
    // his undead ending model above is a separate enemy appearance.
    // st2evtbl.bin record 0x948 has kind 0x3D and script 0x97C motion 0x150:
    // player_gold.bin body/head 0xD27, using the gold.bin motion bank.
    append("Goldman", ModelCategory::Bosses, 0x3D, Bank(0x14D, 0x15F));
    auto kuarl = Bank(0x383, 0x394, 0x38D, "Standing");
    Label(kuarl, 0x393, "Walk"); Label(kuarl, 0x394, "Fast walk");
    append("Judgment - Kuarl", ModelCategory::Bosses, 0x44, kuarl).nodeOverrides = {{2, 0}};
    auto zeal = Bank(0x408, 0x416, 0x411, "Hover");
    Label(zeal, 0x40C, "Charge"); Label(zeal, 0x40A, "Death");
    Label(zeal, 0x408, "Hit reaction 1"); Label(zeal, 0x409, "Hit reaction 2");
    Sound(zeal, 0x40C, 0, 0x4317A9); // BossBeh2 switches to 0x00570BE6 (0x40C) with this sound.
    for (auto& a : zeal) a.companionMotionId = a.motionId == 0x40C ? 0x14 : 0x10;
    auto& zealEntry = append("Judgment - Zeal", ModelCategory::Bosses, 0x45, zeal);
    zealEntry.nodeOverrides = {{2, 0x2A9}};
    zealEntry.companionSkeletonType = 0x46;
    auto hierophant = Bank(0x15, 0x3A, 0x21, "Opening pose"); // SpawnBoss2: MotionOfRow(0xB).
    Label(hierophant, 0x2C, "Floating death pose");
    Sound(hierophant, 0x37, 0x2D, 0x1617A9); // St12, MotionOfRow(0x1B).
    // Chest mesh animations depend on the fight's state machine. Show their
    // complete closed resting form while the authored skeletal clip plays.
    append("Hierophant", ModelCategory::Bosses, 0x47, hierophant).extraMeshes = {{1, 0x2CB}, {1, 0x2EE}};
    append("Tower - Long Neck", ModelCategory::Bosses, 0x48, Bank(0x3B, 0x49));
    auto tower = Bank(0x4A, 0x64);
    Sound(tower, 0x57, 0x17, 0x3D16A9); Sound(tower, 0x59, 0x3F, 0x3D16A9); // HeadIntro.
    append("Tower - Short Neck", ModelCategory::Bosses, 0x49, tower);
    auto strength = Bank(0x65, 0x7F, 0x6B);
    Label(strength, 0x7C, "Roar"); Sound(strength, 0x7C, 0, 0x231BA9); // St14.
    Label(strength, 0x65, "Attack 1"); Label(strength, 0x7A, "Attack 2"); Label(strength, 0x7B, "Attack 3");
    append("Strength", ModelCategory::Bosses, 0x4A, strength).nodeOverrides = {{5, 0x444}}; // SpawnBoss4's weapon hand.
    auto magician = Bank(0x80, 0x94, 0x8E, "Standing");
    Label(magician, 0x94, "Fireball barrage"); Sound(magician, 0x94, 0, 0xB23A9); // Beh10, sub4.
    Label(magician, 0x8D, "Dash"); Sound(magician, 0x8D, 0, 0xF23A9); // Beh10.
    auto& magicianEntry = append("Magician", ModelCategory::Bosses, 0x4B, magician);
    // Boss5Node: the living surface is two layers, animated with seedCounter
    // modulo forty. Some nodes retain their rigid base; four animate both.
    magicianEntry.nodeOverrides = {{4, 0x6DB, 40}, {6, 0x568, 40}, {11, 0x4EF, 40}, {13, 0x5E6, 40}};
    magicianEntry.extraMeshes = {
        {1, 0x660, 40}, {2, 0x540, 40}, {3, 0x5B9, 40}, {4, 0x703, 40},
        {5, 0x68A, 40}, {6, 0x590, 40}, {7, 0x6B3, 40}, {10, 0x637, 40},
        {11, 0x517, 40}, {12, 0x49C, 40}, {13, 0x60E, 40}, {14, 0x4C7, 40}, {15, 0x44B, 40}
    };
    auto emperor = Bank(0x95, 0xB0, 0xA3, "Hover");
    // 0x125A9 is a loop-stop command whose _OFF.wav does not exist. Only
    // preview the actual attack sample; the gallery owns playback lifetime.
    Label(emperor, 0xA5, "Summon orbs"); Sound(emperor, 0xA5, 78, 0x25A9);
    Label(emperor, 0xA7, "Orb attack"); Sound(emperor, 0xA7, 0x1E, 0x1225A9);
    Label(emperor, 0xB0, "Hit reaction"); Sound(emperor, 0xB0, 0, 0x2225A9); // State3.
    append("Emperor", ModelCategory::Bosses, 0x4C, emperor).extraMeshes = {
        // Actor2DNode's reflective outer surface, on top of the inner body.
        {1, 0x7DE}, {1, 0x7E6}, {2, 0x7D6}, {3, 0x7D8}, {4, 0x7E5},
        {5, 0x7E3}, {6, 0x7D7}, {7, 0x7E4}, {8, 0x7E2}, {9, 0x7DB},
        {10, 0x7DD}, {11, 0x7D5}, {12, 0x7CE}, {13, 0x7DC}, {14, 0x7D4}, {15, 0x7CD}
    };
    for (auto& entry : out) {
        AddModelEnemySounds(entry);
        AddModelCivilianSounds(entry);
        AddModelScriptedSounds(entry);
        for (auto& animation : entry.animations) {
            std::sort(animation.sounds.begin(), animation.sounds.end(), [](const auto& a, const auto& b) {
                return a.tick != b.tick ? a.tick < b.tick : a.soundId < b.soundId;
            });
            animation.sounds.erase(std::unique(animation.sounds.begin(), animation.sounds.end(), [](const auto& a, const auto& b) {
                return a.tick == b.tick && a.soundId == b.soundId;
            }), animation.sounds.end());
        }
    }
    return out;
}

} // namespace

const char* ModelCategoryName(ModelCategory category) {
    switch (category) {
    case ModelCategory::Agents: return "AGENTS";
    case ModelCategory::Civilians: return "CIVILIANS";
    case ModelCategory::Enemies: return "ENEMIES";
    case ModelCategory::Bosses: return "BOSSES";
    }
    return "MODELS";
}

const std::vector<ModelCatalogEntry>& ModelCatalog() {
    static const auto catalog = BuildCatalog();
    return catalog;
}

} // namespace play
