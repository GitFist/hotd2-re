// game::World - route planning support (tests/world_test --plan, notes/agents/routes.md): the hit system's target
// list and a side-effect-free preview of a shot, the same selection as the hit task 0x004044A0 -> 0x00404570
// (World::Impl::TaskHit). Not used by the simulation itself.

#include <functional>

#include "game/shooting.h"
#include "game/world_internal.h"

namespace game {

namespace {

bool IsCivilianFn(uint32_t fn) { return fn == 0x0048A3E0 || fn == 0x0048A920 || fn == 0x0048B140; }

bool Live(const World::Impl& cw, const TaskData* d) {
    World::Impl& w = const_cast<World::Impl&>(cw);
    for (Task* t : w.ts.Live())
        if (t->data.get() == d) return true;
    return false;
}

uint32_t SerialOf(const World::Impl& cw, const TaskData* d) {
    World::Impl& w = const_cast<World::Impl&>(cw);
    for (Task* t : w.ts.Live())
        if (t->data.get() == d) return t->serial;
    return 0;
}

}  // namespace

float World::Focal() const { return impl_->focal; }

std::vector<World::ShotTargetInfo> World::ShotTargets() const {
    std::vector<ShotTargetInfo> out;
    const Impl& w = *impl_;
    for (const Impl::Target& tg : w.targets) {
        ActorTask* a = tg.obj;
        if (!a || !Live(w, a)) continue;  // (a target whose task ended after registering: not read)
        ShotTargetInfo i;
        i.fn = a->fn;
        i.serial = SerialOf(w, a);
        i.flags = a->flags34;
        memcpy(i.view, a->viewPos, 12);
        i.radius = a->radius;
        i.civilian = IsCivilianFn(a->fn);
        const int kind = a->SkeletonKind();
        if ((a->flags34 & 0x80) && kind >= 0 && !(a->flags34 & 0x8000)) {
            for (int n = 0; n < 32; ++n) {
                EnemyNode* e = a->Node(n);
                if (!e || e->model == 0 || (e->flags & 0x10) || e->radius == 0.0f) continue;
                i.nodes.push_back({ e->hit[0], e->hit[1], e->hit[2], e->radius });
            }
        }
        out.push_back(std::move(i));
    }
    return out;
}

World::ShotPreview World::PreviewShot(int p, int x, int y) {
    Impl& w = *impl_;
    ShotPreview r;
    int16_t gx = 0, gy = 0;
    bool valid = false;
    GunCalibrateMode6(x, y, &gx, &gy, &valid);
    if (!valid) return r;
    Impl::Player& P = w.player[p];
    const ShotRay saved = P.ray;
    P.ray = MakeShotRay(static_cast<float>(gx), static_cast<float>(gy), w.focal);
    struct Entry {
        int32_t key;
        ActorTask* obj;
        int node;
        bool level;
    };
    std::vector<Entry> entries;
    std::vector<World::Impl::LevelShotHit> level;
    for (const Impl::Target& tg : w.targets) {
        ActorTask* a = tg.obj;
        if (!a || !Live(w, a)) continue;
        if (a->flags34 & 0x10) {
            World::Impl::LevelShotHit h;
            if (w.ShotObjMesh(p, *a, h)) entries.push_back({ h.key, a, -1, false });
            continue;
        }
        if (!ShotHitsSphere(P.ray, a->viewPos, a->radius)) continue;
        const int kind = a->SkeletonKind();
        const Skeleton* sk = kind >= 0 ? &w.EnemySkeleton(kind) : nullptr;
        if ((a->flags34 & 0x80) && sk && !sk->nodes.empty() && !sk->nodes[0].children.empty() && !(a->flags34 & 0x8000)) {
            std::function<void(int)> visit = [&](int i) {
                const SkeletonNode& d = sk->nodes[i];
                EnemyNode* n = a->Node(d.index);
                if (n && n->model != 0) {
                    if (!(n->flags & 0x10)) {
                        if (n->radius != 0.0f && ShotHitsSphere(P.ray, n->hit, n->radius))
                            entries.push_back({ HitSortKey(n->hit[2]), a, d.index, false });
                    } else if (n->mesh != -1) {
                        World::Impl::LevelShotHit h;
                        if (w.ShotNodeMesh(p, *n, h)) entries.push_back({ h.key, a, d.index, false });
                    }
                }
                for (int c : d.children) visit(c);
            };
            for (int c : sk->nodes[0].children) visit(c);
        } else {
            entries.push_back({ HitSortKey(a->viewPos[2]), a, -1, false });
        }
    }
    w.ShotLevel(p, level);
    for (const World::Impl::LevelShotHit& h : level) entries.push_back({ h.key, nullptr, -1, true });
    P.ray = saved;
    if (entries.empty()) return r;
    std::vector<std::pair<uint32_t, int32_t>> keys;
    for (size_t i = 0; i < entries.size(); ++i) keys.push_back({ static_cast<uint32_t>(entries[i].key), static_cast<int32_t>(i) });
    SortHitKeys(keys);
    const Entry& e = entries[keys[0].second];
    r.any = true;
    r.level = e.level;
    if (e.obj) {
        r.fn = e.obj->fn;
        r.serial = SerialOf(w, e.obj);
        r.node = e.node;
        r.civilian = IsCivilianFn(e.obj->fn);
    }
    return r;
}

}  // namespace game
