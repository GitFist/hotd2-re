// Port-only randomized Original campaign. Scripted rescues, bosses, riders and cinematic actors
// retain their records; ordinary grounded encounters draw from compatible zombie kinds.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <vector>
#include "game/world_modes.h"
#include "game/mstack.h"

namespace game {
namespace {
constexpr int kBonus[] = {3, 4, 5, 6, 7, 8, 9, 10, 13, 29, 30, 31, 32};
struct Zombie { int kind, variant, hp; };
constexpr Zombie kZombies[] = {
    {7, 0, 100}, {8, 0, 150}, {5, 0, 250}, {0, 0, 320}, {2, 2, 220}, {11, 0, 110}
};
struct Costume { int kind, headPart, part1, part2; };
// Complete appearances from the original civilian spawn records. Many civilian heads need
// a separate hair/hat mesh from their parts list; the bare skeleton alone has an open scalp.
// Only authored combinations are used, with the ordinary agent outfits added at the end.
constexpr Costume kCostumes[] = {
    {0x20,1,38,39}, {0x21,0,36,-1}, {0x22,-1,36,37}, {0x23,2,39,-1},
    {0x24,13,41,64}, {0x25,9,61,-1}, {0x26,3,50,53}, {0x27,3,45,-1},
    {0x28,-1,45,-1}, {0x29,-1,50,53}, {0x2A,3,45,-1}, {0x2B,3,45,-1},
    {0x2E,7,61,59}, {0x2F,-1,61,-1}, {0x30,-1,62,-1}, {0x31,-1,67,-1},
    {0x32,7,62,-1}, {0x33,-1,67,-1}, {0x34,14,75,-1}, {0x35,-1,71,-1},
    {0x36,17,78,-1}, {0x37,14,80,-1},
    {0x39,-1,-1,-1}, {0x3A,-1,-1,-1}, {0x3B,-1,-1,-1}, {0x3C,-1,-1,-1},
    {0x3D,-1,-1,-1}, {0x3E,-1,-1,-1}, {0x3F,-1,-1,-1}
};
const Costume* FindCostume(int kind) {
    for (const auto& costume : kCostumes) if (costume.kind == kind) return &costume;
    return nullptr;
}
// Separate, stateless cosmetic streams: skipping a line or drawing a character again cannot
// change equipment, encounters, the original game RNG, or a rejoining player's choices.
uint32_t CosmeticHash(uint32_t key) {
    key ^= key >> 16; key *= 0x7FEB352Du;
    key ^= key >> 15; key *= 0x846CA68Bu;
    return key ^ (key >> 16);
}
bool SceneHuman(int kind) {
    // 0x41 has a different 20-bone rig. Bosses and player weapon bodies are not costume candidates.
    return (kind >= 0x20 && kind <= 0x40) || kind == 0x42 || kind == 0x43;
}
uint32_t LineSound(const World::Impl& w, int line) {
    return w.exe.U32(0x00589DA8 + line * 16 + 0xC);
}
int VoiceForSound(const World::Impl& w, uint32_t sound) {
    if (!RandomizerActive(w) || (sound >> 28) != 2) return 0;
    const auto& r = w.modes->randomizer;
    if (r.voiceLines.empty()) return 0;
    size_t choice = CosmeticHash(CosmeticHash(r.seed) ^ sound ^ 0x564F4943u) % r.voiceLines.size();
    for (size_t n = 0; n < r.voiceLines.size(); ++n) {
        const int line = r.voiceLines[(choice + n) % r.voiceLines.size()];
        if (LineSound(w, line) != sound) return line;
    }
    return 0;
}
uint32_t Next(World::Impl& w) {
    auto& x = w.modes->randomizer.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}
int Bonus(World::Impl& w, int weapon, int previous = -1) {
    std::vector<int> choices;
    for (int item : kBonus)
        if (item != previous && OriginalItemsCompatible(w, weapon, item)) choices.push_back(item);
    return choices.empty() ? -1 : choices[Next(w) % choices.size()];
}
void WeaponResources(World::Impl& w) {
    for (const auto& it : w.modes->pitems) {
        if (it.b8[1] == 5) w.ProcessRequest({0x50, 0x109D});
        if (it.b8[1] == 3) w.ProcessRequest({0x52, 0x3D});
        if (it.b8[1] == 4) w.ProcessRequest({0x52, 0x41});
    }
}
void LoadKind(World::Impl& w, int kind) {
    std::set<uint32_t> files;
    auto load = [&](uint32_t model) {
        if (!model || model >= 0x2800) return;
        if (model < w.modelLoaded.size() && w.modelLoaded[model]) return;
        const uint32_t file = static_cast<uint16_t>(w.exe.S16(0x004E83B4 + model * 2));
        if (file < 0xE7) {
            if (files.insert(file).second) w.ProcessRequest({0x52, file});
        } else w.ProcessRequest({0x50, model});
    };
    for (const auto& node : w.EnemySkeleton(kind).nodes) load(node.modelId);
    const uint32_t at = w.exe.U32(0x0052ED08 + 4 * kind);
    if (w.exe.InData(at)) {
        const uint32_t count = w.exe.U32(at), defs = w.exe.U32(at + 4);
        for (uint32_t i = 0; i < count && i < 32 && w.exe.InData(defs); ++i) {
            const uint32_t def = w.exe.U32(defs + 4 * i);
            if (w.exe.InData(def)) load(w.exe.U32(def));
        }
    }
}
void LoadBanner(World::Impl& w) {
    w.ProcessRequest({0x54, 0x17B});
    for (const auto& slots : w.modes->randomizer.slots)
        if (slots[1] >= 0) {
            const int sprite = w.exe.S16(0x0059721C + slots[1] * 2);
            w.ProcessRequest({0x54, w.exe.U32(0x0057A5BC + sprite * 4)});
        }
}
void TaskRandomizer(Task&) {
    auto& w = *CurrentWorld();
    auto& r = w.modes->randomizer;
    if (!RandomizerActive(w) || w.gameMode != 6) { w.ts.KillCurrent(); }
    // Section scripts can release a model file chosen for a replacement. Keep live replacements resident.
    for (Task* task : w.ts.Live()) {
        const auto* e = dynamic_cast<const EnemyTask*>(task->data.get());
        if (e && e->record.type == 0x30 && e->record.addr == 0 && (e->flags34 & 1)) LoadKind(w, e->anim.kind);
    }
    if (!w.canShoot || (w.flags5900 & 0x18) || r.banner <= 0) return;
    LoadBanner(w);  // chapter scripts can release the item-caption textures before gameplay starts
    --r.banner;
    PortGlyphText(w, 320, 62, .65f, "RANDOMIZER  SEED " + std::to_string(r.seed), 1, true);
    static const char* names[] = {"PISTOL", "SHOTGUN", "MACHINE GUN", "GRENADE LAUNCHER"};
    for (int p = 0; p < 2; ++p) {
        if (w.player[p].state != 5) continue;
        const float x = p ? 475.0f : 165.0f;
        PortGlyphText(w, x, 80, .6f, "P" + std::to_string(p + 1) + " " + names[r.slots[p][0] + 1], 1, true);
        const int item = r.slots[p][1];
        if (item >= 0) PortSprite(w, w.exe.S16(0x0059721C + item * 2), x, 100, 1, .7f, .7f, 0, 2);
    }
}
} // namespace

bool RandomizerActive(const World::Impl& w) {
    return w.cfg.portFeatures && w.modes && w.modes->randomizer.active;
}
void RandomizerBegin(World::Impl& w, bool enabled) {
    if (!w.modes) return;
    // Restore the Original inventory after a randomized run, even when quitting through the pause menu.
    if (w.modes->randomizer.active)
        for (int i = 0; i < 33; ++i) w.modes->items[i] = w.modes->save.S8(SaveData::kItems + i);
    auto& r = w.modes->randomizer;
    r = {};
    r.active = enabled && w.cfg.portFeatures;
    if (r.active) {
        r.seed = w.seedCounter ? w.seedCounter : 1;
        r.rng = r.seed;
        // Voiced subtitle groups only, one per audio file. Silent title cards and UI text stay intact.
        std::set<uint32_t> sounds;
        for (int line = 1; line < 401; ++line) {
            const uint32_t sound = LineSound(w, line), index = sound & 0xFFF;
            if ((sound >> 28) != 2 || index >= 467 || w.exe.S16(0x00589DA8 + line * 16 + 2) <= 0 ||
                w.exe.S16(0x00580448 + index * 36 + 2) < 0 ||
                w.exe.CStr(0x00580448 + index * 36 + 4, 32).empty()) continue;
            if (sounds.insert(sound).second) r.voiceLines.push_back(line);
        }
        w.Note("randomizer: seed " + std::to_string(r.seed));
    }
}

int RandomizerVoiceLine(const World::Impl& w, int line) {
    if (line <= 0 || line >= 401) return line;
    const int replacement = VoiceForSound(w, LineSound(w, line));
    return replacement ? replacement : line;
}
uint32_t RandomizerVoiceSound(const World::Impl& w, uint32_t sound) {
    const int replacement = VoiceForSound(w, sound);
    return replacement ? LineSound(w, replacement) : sound;
}
int RandomizerCharacterKind(const World::Impl& w, int kind) {
    if (!RandomizerActive(w) || !SceneHuman(kind)) return kind;
    uint32_t choice = CosmeticHash(CosmeticHash(w.modes->randomizer.seed) ^ static_cast<uint32_t>(kind) ^ 0x43415354u);
    if (choice & 1) return kZombies[(choice >> 1) % std::size(kZombies)].kind;
    // The other half of the cast becomes a different civilian/agent, stable throughout this run.
    size_t pick = (choice >> 1) % std::size(kCostumes);
    if (kCostumes[pick].kind == kind) pick = (pick + 1) % std::size(kCostumes);
    return kCostumes[pick].kind;
}
void RandomizerCharacterDraw(World::Impl& w, const EnemyTask& e, size_t firstDraw) {
    const int kind = RandomizerCharacterKind(w, e.anim.kind);
    if (kind == e.anim.kind || e.bodyOf >= 0 || !(e.anim.drawFlags & 1) || firstDraw >= w.draws.size()) return;
    const auto& skeleton = w.EnemySkeleton(kind);
    if (skeleton.nodeCount != 16 || skeleton.nodes.size() != 16) return;
    LoadKind(w, kind);  // scene resource unloads must not remove a live costume
    const Costume* costume = FindCostume(kind);
    auto partDef = [&](int part) { return w.exe.U32(0x004EC748 + 4 * part); };
    auto loadModel = [&](uint32_t model) {
        if (model < w.modelLoaded.size() && !w.modelLoaded[model]) w.ProcessRequest({0x50, model});
    };
    if (costume)
        for (int part : {costume->headPart, costume->part1, costume->part2})
            if (part >= 0) loadModel(w.exe.U32(partDef(part) + 4));
    if (kind == 0x3F) loadModel(0x125D);
    const uint64_t diagnosticBit = uint64_t{1} << (e.anim.kind - 0x20);
    if (!(w.modes->randomizer.reportedCostumes & diagnosticBit)) {
        w.Note("randomizer: costume " + std::to_string(e.anim.kind) + " -> " + std::to_string(kind));
        w.modes->randomizer.reportedCostumes |= diagnosticBit;
    }
    // Scene callbacks can turn a head independently of the motion and submit held objects.
    // Keep those actions, including the device, briefcase and muzzle flashes used by agent scenes.
    Mat4 head = Mat4::Identity();
    bool haveHead = false;
    std::vector<WorldDraw> props;
    for (size_t i = firstDraw; i < w.draws.size(); ++i) {
        const auto& draw = w.draws[i];
        if (draw.node == 2 && !draw.skinCount && draw.viewSpace && !haveHead) { head = draw.world; haveHead = true; }
        if (draw.model == 0x7ED || draw.model == 0x10E3 || draw.model == 0xE24) props.push_back(draw);
    }
    // Original AnimDraw has already executed all callbacks, hit registration and root motion.
    // Replace only its render submissions; the actor's state, collision rig and civilian role never change.
    w.draws.resize(firstDraw);
    Mat4 pose[16];
    pose[0] = e.anim.nodes[0].mv;
    // Civilian meshes are authored for the original 0.9 scale; agents/zombies for 1.0.
    // Respect that difference so a civilian costume does not grow into an agent's close-up camera.
    auto nativeScale = [](int k) { return k >= 0x20 && k <= 0x38 ? .9f : 1.0f; };
    const float scale = nativeScale(kind) / nativeScale(e.anim.kind);
    mx::Scale(pose[0], scale, scale, scale);
    std::function<void(int, const Mat4&)> node = [&](int entry, const Mat4& parent) {
        const auto& d = skeleton.nodes[entry];
        const int i = d.index;
        Mat4 mv = parent;
        mx::Translate(mv, d.offset[0], d.offset[1], d.offset[2]);
        const auto& source = e.anim.nodes[i];
        mx::RotZ(mv, source.cur[2]); mx::RotY(mv, source.cur[1]); mx::RotX(mv, source.cur[0]);
        if (i == 2 && haveHead) {
            std::copy(head.m, head.m + 12, mv.m);
            mx::Scale(mv, scale, scale, scale);
        }
        pose[i] = mv;
        const int model = i == 2 && costume && costume->headPart >= 0 ?
            static_cast<int>(w.exe.U32(partDef(costume->headPart) + 4)) : static_cast<int>(d.modelId);
        const bool pelvisSkin = i == 9 && (model == 0xE3C || model == 0xE4D || model == 0xEA2 ||
            model == 0xEB6 || model == 0xEC6 || model == 0xED6 || model == 0xEF6 ||
            model == 0xF06 || model == 0xF83 || model == 0x15B0);
        if (model && !pelvisSkin) w.Draw(model, mv, 8, -1.0f, false, i, true);
        for (int child : d.children) node(child, mv);
    };
    for (int child : skeleton.nodes[0].children) node(child, pose[0]);
    const uint32_t at = w.exe.U32(0x0052ED08 + 4 * kind);
    if (w.exe.InData(at) && !e.anim.attachHidden) {
        const int count = static_cast<int>(w.exe.U32(at));
        const uint32_t defs = w.exe.U32(at + 4), tab = 0x004ED1E0;
        for (int i = 0; i < count && i < 2 && w.exe.InData(defs); ++i) {
            const uint32_t def = w.exe.U32(defs + 4 * i);
            if (!w.exe.InData(def)) continue;
            const int base = w.exe.U32(tab + i * 0x14 + 0x10) & 15;
            auto* draw = w.Draw(w.exe.U32(def), pose[base], 8, -1.0f, false, base, true);
            if (!draw) continue;
            draw->skinCount = 4;
            for (int k = 0; k < 4; ++k) draw->skin[k] = pose[w.exe.U32(tab + i * 0x14 + 4 * k) & 15];
        }
    }
    if (costume)
        for (int part : {costume->part1, costume->part2}) if (part >= 0) {
            const uint32_t def = partDef(part);
            const int bone = w.exe.U32(def) & 15;
            w.Draw(w.exe.U32(def + 4), pose[bone], 8, -1.0f, false, bone, true);
        }
    if (kind == 0x3F) w.Draw(0x125D, pose[1], 8, -1.0f, false, 1, true);
    for (auto prop : props) {
        if (prop.viewSpace && (prop.node == 5 || prop.node == 8))
            for (int axis = 12; axis < 15; ++axis) prop.world.m[axis] += pose[prop.node].m[axis] - e.anim.nodes[prop.node].mv.m[axis];
        w.draws.push_back(std::move(prop));
    }
}
void RandomizerStage(World::Impl& w) {
    if (!RandomizerActive(w) || w.cfg.gameMode != 6 || w.cfg.gameType != 1 || w.cfg.eventMode > 5) return;
    auto& r = w.modes->randomizer;
    if (r.chapter == w.cfg.eventMode) return;
    r.chapter = w.cfg.eventMode;
    for (int p = 0; p < 2; ++p) {
        // Guarantee a different weapon at the next chapter, including the standard pistol.
        int weapons[4], count = 0;
        for (int i = -1; i < 3; ++i) if (i != r.slots[p][0] || r.chapter == 0) weapons[count++] = i;
        r.slots[p][0] = weapons[Next(w) % count];
        r.slots[p][1] = Bonus(w, r.slots[p][0], r.slots[p][1]);
    }
    OriginalSetLoadouts(w, r.slots);
    r.banner = 300;
    w.Note("randomizer: chapter " + std::to_string(r.chapter + 1) + " equipment rolled");
}
void RandomizerStageReady(World::Impl& w) {
    if (!RandomizerActive(w) || w.cfg.gameType != 1 || w.cfg.eventMode > 5) return;
    WeaponResources(w);
    LoadBanner(w);
    w.NewTask<WTask>(TaskRandomizer, 0x48010000);
}

bool RandomizerSpawn(World::Impl& w, uint32_t op, const SpawnRecord& record) {
    if (!RandomizerActive(w) || w.gameMode != 6 || w.cfg.eventMode > 5 || record.type != 0x30 || op == 0x0C) return false;
    const int behaviour = record.ParamS8(2), kind = record.ParamS8(0);
    // These behaviours use the common walk/attack motion tables. Keep flags and path parameters on the
    // first actor. Other behaviours can own a civilian, event flag, vehicle, custom jump or boss script.
    if (behaviour != 1 && behaviour != 2 && behaviour != 14 && behaviour != 15) return false;
    if (kind < 0 || kind > 20 || kind == 9 || (record.flags & 0x31000008u) || record.lo != 0) return false;
    auto& r = w.modes->randomizer;
    ++r.encounters;
    int occupied = 0, pending = 0;
    for (const auto* actor : w.actorSlots) occupied += actor != nullptr;
    for (Task* task : w.ts.Live()) {
        const auto* e = dynamic_cast<const EnemyTask*>(task->data.get());
        pending += e && e->fn == fn::kEnemyCreate;
    }
    // Never remove the scripted actor, and leave four of the fourteen actor slots for scripted arrivals.
    const int count = std::min(1 + static_cast<int>(Next(w) % 3), std::max(1, 10 - occupied - pending));
    r.extraEnemies += count - 1;
    w.Note("randomizer: encounter " + std::to_string(r.encounters) + " enemies " + std::to_string(count));
    for (int n = 0; n < count; ++n) {
        const auto& z = kZombies[Next(w) % (sizeof(kZombies) / sizeof(kZombies[0]))];
        SpawnRecord rec = record;
        rec.params[0] = static_cast<uint8_t>(z.kind);
        rec.params[1] = static_cast<uint8_t>(z.variant);
        rec.state = static_cast<uint16_t>(z.hp);
        if (z.kind == 2) {
            // Chainsaw hands read P[4] as a mesh, not the displaced zombie's unused data.
            // Original stage-1/2 chainsaw records use this shared coli0.bin mesh in every chapter.
            constexpr uint32_t chainsawMesh = 0x009903E0;
            memcpy(rec.params + 16, &chainsawMesh, sizeof(chainsawMesh));
        }
        // No event script dereferences ordinary walking records. Zero marks replacements for residency.
        rec.addr = 0;
        if (n) {
            float s, c; mx::SinCos(w.cam.yaw, &s, &c);
            const float offset = n == 1 ? -5.0f : 5.0f;
            rec.pos[0] += c * offset;
            rec.pos[2] -= s * offset;
        }
        LoadKind(w, z.kind);
        auto* e = w.NewTask<EnemyTask>(WorldTramp<&World::Impl::TaskEnemy>, fn::kEnemyCreate);
        e->lock = &w.lockTargets[w.nextLockTarget++];
        e->flags34 = rec.flags | 1;
        memcpy(e->pos, rec.pos, sizeof(e->pos));
        memcpy(e->rot, rec.rot, sizeof(e->rot));
        e->hp = e->hpMax = static_cast<int16_t>(rec.state);
        e->lo = static_cast<int16_t>(rec.lo);
        e->record = rec;
        e->anim.kind = static_cast<int16_t>(z.kind);
        e->variant = z.variant;
    }
    return true;
}

int RandomizerPickItem(World::Impl& w) {
    constexpr int n = static_cast<int>(sizeof(kBonus) / sizeof(kBonus[0]));
    const int pick = static_cast<int>(Next(w) % (n + 3));
    return pick < 3 ? pick : kBonus[pick - 3];
}
bool RandomizerCollect(World::Impl& w, int p, int item) {
    if (!RandomizerActive(w)) return false;
    p = p < 0 || p > 1 ? (w.player[0].state == 5 ? 0 : 1) : p;
    auto& r = w.modes->randomizer;
    if (item >= 0 && item < 3) {
        r.slots[p][0] = item;
        if (!OriginalItemsCompatible(w, item, r.slots[p][1])) r.slots[p][1] = Bonus(w, item);
    } else {
        if (std::find(std::begin(kBonus), std::end(kBonus), item) == std::end(kBonus)) item = Bonus(w, r.slots[p][0]);
        // A bonus pickup always works: incompatible equipment switches to the standard pistol.
        if (!OriginalItemsCompatible(w, r.slots[p][0], item)) r.slots[p][0] = -1;
        r.slots[p][1] = item;
    }
    // Reapply both players so global item flags cannot leak from an unequipped bonus. Preserve the
    // other player's cooldown; a pickup only reloads the receiving player's gun.
    uint8_t otherCooldown[4]; memcpy(otherCooldown, w.modes->pitems[1-p].b10, 4);
    OriginalSetLoadouts(w, r.slots);
    memcpy(w.modes->pitems[1-p].b10, otherCooldown, 4);
    WeaponResources(w); LoadBanner(w);
    const int magazine = ItemMagazine(w, p);
    w.player[p].ammo = static_cast<int16_t>(magazine < 0 ? 6 : magazine);
    w.player[p].empty = w.player[p].reloadBlink = 0;
    r.banner = 180;
    return true;
}
uint32_t RandomizerHash(const World::Impl& w) {
    if (!RandomizerActive(w)) return 0;
    const auto& r = w.modes->randomizer;
    uint32_t hash = 2166136261u;
    auto mix = [&](uint32_t value) { hash = (hash ^ value) * 16777619u; };
    mix(r.seed); mix(r.rng); mix(r.chapter); mix(r.banner); mix(r.encounters); mix(r.extraEnemies);
    for (const auto& slots : r.slots) for (int item : slots) mix(item);
    return hash;
}
} // namespace game
