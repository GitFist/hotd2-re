// game::World - level collision meshes (coli\*.bin): the collision list task 0x00406010 (ops 0x10 / 0x11), the
// segment test 0x004AAA40 (floor queries Coll_FloorHeight 0x00409D40 -> 0x004053B0) and the sphere test 0x004AAFF0
// (actor push 0x004057F0).
// CONFIRMED = code read + frame-exact against build\scene_capture_play2scene1.txt (enemy positions, T dumps).
//
// Mesh layout (Res_LoadCollision 0x0048A310: coli0.bin at 0x0098F200, coli<stage + 1>.bin at 0x00990A00, raw): a
// mesh = s32 group count, then per group: s32 face count, float box max (x, y, z), box min (x, y, z), then the faces,
// 18 words each: plane (a, b, c, d), dominant axis (s32 0 / 1 / 2 = x / y / z), 4 vertices (x, y, z), material
// (non-zero word; the segment test returns it). Ops 0x10 / 0x11 give -1-terminated lists of mesh addresses.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

struct Face {
    float a, b, c, d;
    int32_t axis;
    float v[4][3];
    uint32_t material;
};
static_assert(sizeof(Face) == 72, "face size");

}  // namespace

bool World::Impl::CollLoad() {
    if (coliLoaded) return true;
    coliLoaded = true;
    const char* names[2] = { "coli0.bin", nullptr };
    char other[32];
    snprintf(other, sizeof(other), "coli%d.bin", cfg.eventMode + 1);  // 0x0048A3B0: coli0 + coli<0x009A1A08 + 1>
    if (cfg.eventMode == 6) snprintf(other, sizeof(other), "coliT.bin");
    names[1] = other;
    if(cfg.portTyping&&cfg.eventMode==7) {
        if(cfg.scene==3||cfg.scene==23)names[1]="coli2.bin";
        else if(cfg.scene==11||cfg.scene==13)names[1]="coli1.bin";
        else names[1]=nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        if(!names[i])continue;
        std::ifstream f(gameDir + "/coli/" + names[i], std::ios::binary);
        if (!f) {
            Note(std::string("collision file ") + names[i] + " missing");
            continue;
        }
        coli[i].assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    return true;
}

// Mesh data at an exe address (inside one of the two loaded files), or nullptr.
const uint8_t* World::Impl::CollAt(uint32_t addr, size_t bytes) {
    CollLoad();
    static const uint32_t base[2] = { 0x0098F200, 0x00990A00 };
    for (int i = 0; i < 2; ++i)
        if (addr >= base[i] && addr - base[i] + bytes <= coli[i].size()) return coli[i].data() + (addr - base[i]);
    return nullptr;
}

// Ops 0x10 / 0x11 set the list pointer 0x009A1A14 / 0x009A34D8; the task 0x00406010 copies the list (to
// 0x009C7040 / 0x009CA0C0) when the pointer differs from the end of the last list it read.
void World::Impl::CollListOp(int which, uint32_t list) {
    collPending[which] = list;
    collHasPending[which] = true;
}

// 0x00406010 (task created by 0x00405FE0 from 0x00404480)
void World::Impl::TaskCollision(Task&) {
    for (int w = 0; w < 2; ++w) {
        if (!collHasPending[w]) continue;
        collHasPending[w] = false;
        uint32_t p = collPending[w];
        if (p == collEnd[w]) continue;
        collList[w].clear();
        for (uint32_t v = 0; mem.Read(p, &v) && v != 0xFFFFFFFFu; p += 4) collList[w].push_back(v);
        collEnd[w] = p + 4;
    }
}

// 0x004AAA40(start, end, mesh): the segment against the mesh faces (one-sided: 0x009CAC5C is never set, so only a
// segment going from behind a face (plane <= 0) to its front (> 0) hits). The hit nearest to `end` wins (cac globals).
// Returns the face material (0: no hit).
uint32_t World::Impl::MeshSegment(const float s[3], const float e[3], uint32_t mesh) {
    const uint8_t* p = CollAt(mesh, 4);
    if (!p) {
        Note("collision mesh address outside the loaded files");
        return 0;
    }
    float lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
        if (s[k] < e[k]) {
            lo[k] = s[k];
            hi[k] = e[k];
        } else {
            lo[k] = e[k];
            hi[k] = s[k];
        }
    }
    int32_t groups;
    memcpy(&groups, p, 4);
    uint32_t addr = mesh + 4;
    uint32_t best = 0;
    float bestD = 0;
    for (int g = 0; g < groups; ++g) {
        const uint8_t* gp = CollAt(addr, 28);
        if (!gp) break;
        int32_t n;
        float box[6];
        memcpy(&n, gp, 4);
        memcpy(box, gp + 4, 24);
        const uint32_t facesAt = addr + 28;
        addr = facesAt + static_cast<uint32_t>(n) * 72;
        if (!(lo[0] <= box[0] && lo[1] <= box[1] && lo[2] <= box[2] && box[3] <= hi[0] && box[4] <= hi[1] && box[5] <= hi[2])) continue;
        for (int i = 0; i < n; ++i) {
            const uint8_t* fp = CollAt(facesAt + static_cast<uint32_t>(i) * 72, 72);
            if (!fp) break;
            Face f;
            memcpy(&f, fp, 72);
            const float d1 = ((f.a * s[0] + f.b * s[1]) + f.c * s[2]) + f.d;
            if (!(d1 <= 0.0f)) continue;  // 0x009CAC5C (two-sided) is 0
            const float d2 = ((f.a * e[0] + f.b * e[1]) + f.c * e[2]) + f.d;
            if (d2 <= 0.0f) continue;
            const float A = d2, B = -d1;
            const float inv = 1.0f / (A + B);
            const float hx = (A * s[0] + B * e[0]) * inv;
            const float hy = (A * s[1] + B * e[1]) * inv;
            const float hz = (A * s[2] + B * e[2]) * inv;
            bool inside = true;
            for (int k = 0; k < 4 && inside; ++k) {
                const float* P = f.v[k];
                const float* Q = f.v[(k + 1) & 3];
                float t;
                if (f.axis == 0) t = ((P[1] - hy) * (Q[2] - P[2]) - (Q[1] - P[1]) * (P[2] - hz)) * f.a;
                else if (f.axis == 1) t = ((P[2] - hz) * (Q[0] - P[0]) - (P[0] - hx) * (Q[2] - P[2])) * f.b;
                else if (f.axis == 2) t = ((P[0] - hx) * (Q[1] - P[1]) - (P[1] - hy) * (Q[0] - P[0])) * f.c;
                else break;  // other values: no edge test (0x004AAC89 -> 0x004AAF04)
                if (t < 0.0f) inside = false;
            }
            if (!inside) continue;
            const float dz = e[2] - hz, dy = e[1] - hy, dx = e[0] - hx;
            const float dist = (dx * dx + dy * dy) + dz * dz;
            if (best != 0 && !(dist < bestD)) continue;
            best = f.material;
            bestD = dist;
            cac54 = hx;
            cac58 = hy;
            cac50 = hz;
            cac64[0] = f.a;
            cac64[1] = f.b;
            cac64[2] = f.c;
            cac48 = dist;
            cac60 = f.d;
        }
    }
    cac40 = best;
    return best;
}

// Candidate of 0x00405680 (entry of 0x0059F4D0, key 0x0059ECD8 = ftol(|end - hit| * 10)); 0x00405760 takes the one
// with the smallest 16-bit key (stable radix sort 0x00405080).
void World::Impl::CollAddCandidate(const float end[3]) {
    const float dz = end[2] - cac50, dy = end[1] - cac58, dx = end[0] - cac54;
    const float d = std::sqrt((dx * dx + dy * dy) + dz * dz);
    CollCand c;
    c.key = static_cast<uint32_t>(static_cast<int32_t>(d * 10.0f)) & 0xFFFF;
    c.pos[0] = cac54;
    c.pos[1] = cac58;
    c.pos[2] = cac50;
    memcpy(c.n, cac64, 12);
    c.material = cac40;
    c.dist2 = cac48;
    collCands.push_back(c);
}

bool World::Impl::CollPickCandidate() {
    if (collCands.empty()) return false;
    size_t bi = 0;
    for (size_t i = 1; i < collCands.size(); ++i)
        if (collCands[i].key < collCands[bi].key) bi = i;
    const CollCand& c = collCands[bi];
    cac54 = c.pos[0];
    cac58 = c.pos[1];
    cac50 = c.pos[2];
    memcpy(cac64, c.n, 12);
    cac40 = c.material;
    cac48 = c.dist2;
    cac4c = 0;  // word 14 of the entry: only 0x00405B10 writes it; the hit task clears the entries every frame
    return true;
}

// 0x004053B0(start, end) as used by Coll_FloorHeight 0x00409D40: collision-mesh objects of the target copy (not
// ported), then the level lists 0x009C7040 and 0x009CA0C0.
// 0x00404F60: the segment in the object's frame (inverse 0x004A8D20 of +0x150) against its mesh; the hit point goes
// back through +0x150
uint32_t World::Impl::ObjMeshSegment(const ActorTask& a, const float s[3], const float e[3]) {
    const Mat4 inv = mx::Inverse(a.m150);
    float le[3], ls[3];
    mx::Transform(inv, e, le);
    mx::Transform(inv, s, ls);
    cac40 = 0;
    if (a.objMesh == -1) return 0;
    if (MeshSegment(ls, le, static_cast<uint32_t>(a.objMesh)) == 0) return 0;
    const float h[3] = { cac54, cac58, cac50 };
    float o[3];
    mx::Transform(a.m150, h, o);
    cac54 = o[0];
    cac58 = o[1];
    cac50 = o[2];
    return cac40;
}

uint32_t World::Impl::CollSegmentAll(const float s[3], const float e[3]) {
    collCands.clear();
    cac40 = 0;
    for (const Target& t : targetCopy) {  // 0x00405550 (the current actor 0x009A26A0 is not excluded here)
        const ActorTask* a = t.obj;
        if (!a || a->objMesh == -1 || (a->flags34 & 0x80008000) || !(a->flags34 & 0x10) || !(a->flags34 & 0x40)) continue;
        if (ObjMeshSegment(*a, s, e) == 0) continue;
        float n[3];
        mx::Transform(a->m150, cac64, n);  // the normal goes through the full matrix (0x004A8A80), as in the original
        memcpy(cac64, n, 12);
        CollAddCandidate(e);  // 0x00405620 (entry flags | 0x40, object)
    }
    for (int w = 0; w < 2; ++w)
        for (uint32_t m : collList[w])
            if (MeshSegment(s, e, m) != 0) CollAddCandidate(e);
    if (CollPickCandidate()) return cac40;
    return 0;
}

// Coll_FloorHeight 0x00409D40(x, y, z): segment (x, y - 1000, z) -> (x, y, z); hit y (0x009CAC58) or the ground
// height 0x009C8E58. *material = 0x009CAC40.
float World::Impl::FloorQuery(float x, float y, float z, int* material) {
    const float s[3] = { x, y - 1000.0f, z }, e[3] = { x, y, z };  // 0x004C49C4
    const uint32_t m = CollSegmentAll(s, e);
    *material = static_cast<int>(m);
    return m != 0 ? cac58 : ground;
}

// 0x004AAFF0(centre, radius, mesh): sphere against the faces: the nearest face point (plane projection inside the
// quad, else the nearest point of the first failing edge / its end points) closer than the radius. cac globals:
// 0x009CAC54.. = that point, 0x009CAC64.. = centre - point, 0x009CAC48 = squared distance, 0x009CAC4C = plane value.
uint32_t World::Impl::MeshSphere(const float c[3], float r, uint32_t mesh) {
    const uint8_t* p = CollAt(mesh, 4);
    if (!p) {
        Note("collision mesh address outside the loaded files");
        return 0;
    }
    int32_t groups;
    memcpy(&groups, p, 4);
    uint32_t addr = mesh + 4;
    const float r2 = r * r;
    uint32_t best = 0;
    float bestD = 0;
    const float x = c[0], y = c[1], z = c[2];
    for (int g = 0; g < groups; ++g) {
        const uint8_t* gp = CollAt(addr, 28);
        if (!gp) break;
        int32_t n;
        float box[6];
        memcpy(&n, gp, 4);
        memcpy(box, gp + 4, 24);
        const uint32_t facesAt = addr + 28;
        addr = facesAt + static_cast<uint32_t>(n) * 72;
        if (!(x - r <= box[0] && y - r <= box[1] && z - r <= box[2] && box[3] <= x + r && box[4] <= y + r && box[5] <= z + r)) continue;
        for (int i = 0; i < n; ++i) {
            const uint8_t* fp = CollAt(facesAt + static_cast<uint32_t>(i) * 72, 72);
            if (!fp) break;
            Face f;
            memcpy(&f, fp, 72);
            const float pv = ((f.a * x + f.b * y) + f.c * z) + f.d;
            const float t = -(pv / ((f.a * f.a + f.b * f.b) + f.c * f.c));
            float d2 = -(t * pv);
            if (!(d2 <= r2)) continue;
            // projection onto the plane
            float px = t * f.a + x, py = t * f.b + y, pz = t * f.c + z;
            int failEdge = -1;
            if (f.axis == 0 || f.axis == 1 || f.axis == 2) {
                for (int k = 0; k < 4; ++k) {
                    const float* P = f.v[k];
                    const float* Q = f.v[(k + 1) & 3];
                    float e;
                    if (f.axis == 0) e = ((Q[2] - P[2]) * (P[1] - py) - (Q[1] - P[1]) * (P[2] - pz)) * f.a;
                    else if (f.axis == 1) e = ((Q[0] - P[0]) * (P[2] - pz) - (Q[2] - P[2]) * (P[0] - px)) * f.b;
                    else e = ((Q[1] - P[1]) * (P[0] - px) - (P[1] - py) * (Q[0] - P[0])) * f.c;
                    if (e < 0.0f) {
                        failEdge = k;
                        break;
                    }
                }
            } else {
                failEdge = 0;  // 0x004AB378: any other axis value takes the first edge's path
            }
            float qx = px, qy = py, qz = pz;
            float nx = f.a, ny = f.b, nz = f.c;  // inside the quad: the plane normal (0x004ABC9E with a / b / c)
            if (failEdge >= 0) {
                // nearest point on the edge line from vertex B = v[k+1] towards A = v[k] (0x004AB378 ...)
                const float* A = f.v[failEdge];
                const float* B = f.v[(failEdge + 1) & 3];
                const float ex = A[0] - B[0], ey = A[1] - B[1], ez = A[2] - B[2];
                const float u = ((B[2] - z) * ez + (B[1] - y) * ey + (B[0] - x) * ex) / ((ez * ez + ey * ey) + ex * ex);
                qx = B[0] - u * ex;
                qy = B[1] - u * ey;
                qz = B[2] - u * ez;
                d2 = ((qx - x) * (qx - x) + (qy - y) * (qy - y)) + (qz - z) * (qz - z);
                if (r2 < d2) continue;
                const float dot = ((qz - B[2]) * (qz - A[2]) + (qy - B[1]) * (qy - A[1])) + (qx - B[0]) * (qx - A[0]);
                if (0.0f <= dot) {  // outside the edge: nearest end point
                    float dx = x - A[0], dy = y - A[1], dz = z - A[2];
                    d2 = (dx * dx + dy * dy) + dz * dz;
                    qx = A[0];
                    qy = A[1];
                    qz = A[2];
                    if (r2 <= d2) {
                        dx = x - B[0];
                        dy = y - B[1];
                        dz = z - B[2];
                        d2 = (dx * dx + dy * dy) + dz * dz;
                        qx = B[0];
                        qy = B[1];
                        qz = B[2];
                        if (r2 <= d2) continue;
                    }
                }
                nx = x - qx;
                ny = y - qy;
                nz = z - qz;
            }
            if (best != 0 && !(d2 < bestD)) continue;
            best = f.material;
            bestD = d2;
            cac48 = d2;
            cac54 = qx;
            cac58 = qy;
            cac50 = qz;
            cac64[0] = nx;
            cac64[1] = ny;
            cac64[2] = nz;
            cac4c = pv;
            cac60 = f.d;
        }
    }
    return best;
}

// Hit_TestLevel 0x00404B80: the shot ray of player p (world: camera matrix 0x009A6040 x the view-space ray of
// Shot_BuildRay 0x00406110, origin 0) from 1000 units out back to the eye, against both level lists; each hit is a
// candidate (0x00404CB0: key = ftol(-(view z) * 10), flags 0x10).
void World::Impl::ShotLevel(int p, std::vector<LevelShotHit>& out) {
    Mat4 rot = cam.inverse;
    rot.m[12] = rot.m[13] = rot.m[14] = 0.0f;  // 0x004A9FD0(0)
    float dir[3], origin[3];
    const float zero[3] = { 0, 0, 0 };
    mx::Transform(cam.inverse, zero, origin);
    mx::Transform(rot, player[p].ray.dir, dir);
    const float s[3] = { dir[0] * 1000.0f + origin[0], dir[1] * 1000.0f + origin[1], dir[2] * 1000.0f + origin[2] };
    for (int w = 0; w < 2; ++w)
        for (uint32_t m : collList[w]) {
            if (MeshSegment(s, origin, m) == 0) continue;
            LevelShotHit h;
            h.pos[0] = cac54;
            h.pos[1] = cac58;
            h.pos[2] = cac50;
            memcpy(h.n, cac64, 12);
            h.material = cac40;
            float v[3];
            mx::Transform(cam.view, h.pos, v);
            h.key = static_cast<int32_t>(-(v[2] * 10.0f));
            out.push_back(h);
        }
}

// Hit_TestObjectMesh 0x00404A00: the shot segment (0x00404AD0) against the object's mesh (0x00404F60); the normal is
// rotated by the object's angles +0x64..+0x6C (RotZ, RotY, RotX); candidate 0x00404B50 (flags | 0x40) -> 0x00404CB0
bool World::Impl::ShotObjMesh(int p, ActorTask& a, LevelShotHit& out) {
    Mat4 rot = cam.inverse;
    rot.m[12] = rot.m[13] = rot.m[14] = 0.0f;
    float dir[3], origin[3];
    const float zero[3] = { 0, 0, 0 };
    mx::Transform(cam.inverse, zero, origin);
    mx::Transform(rot, player[p].ray.dir, dir);
    const float s[3] = { dir[0] * 1000.0f + origin[0], dir[1] * 1000.0f + origin[1], dir[2] * 1000.0f + origin[2] };
    if (ObjMeshSegment(a, s, origin) == 0) return false;
    int ang[3] = { 0, 0, 0 };
    if (const int* r = a.Rot64()) memcpy(ang, r, 12);
    Mat4 m = Mat4::Identity();
    mx::RotZ(m, ang[2]);
    mx::RotY(m, ang[1]);
    mx::RotX(m, ang[0]);
    mx::Transform(m, cac64, out.n);
    out.pos[0] = cac54;
    out.pos[1] = cac58;
    out.pos[2] = cac50;
    out.material = cac40;
    out.obj = &a;
    float v[3];
    mx::Transform(cam.view, out.pos, v);
    out.key = static_cast<int32_t>(-(v[2] * 10.0f));
    return true;
}

// Hit_RecordImpact 0x00405260 for a level hit: impact record 0x009A2C40 + p * 0x1C (not modelled) and the effect
// of the face material (0x00407340: angles of the face normal; face mode 1 for materials 5 / 0x37, else 0)
void World::Impl::ShotImpact(int p, const LevelShotHit& h) {
    if (p >= 0 && p < 2) {  // the impact record (read by the stage-4 boss, world_st4_boss.cpp)
        memcpy(impactPos[p], h.pos, 12);
        memcpy(impactN[p], h.n, 12);
        impactMat[p] = h.material;
    }
    int pitch = 0, yaw = 0;
    mx::DirToAngles(h.n[0], h.n[1], h.n[2], &pitch, &yaw);  // Cam_DirToAngles 0x004016B0
    const int type = static_cast<int>(h.material);
    SpawnEffectAt(h.pos, pitch, yaw, type, (type == 5 || type == 0x37) ? 1 : 0, p);
}

// 0x004057F0(centre, radius): collision-mesh objects other than the current actor (auto_st1 7992..8010: the leapers
// 0x00449910 against the animated prop 0x00474120, pushed on the original's frames; positions within 1e-4 because the
// prop's +0x150 is an ulp off and mx::Inverse is not the 24-bit 0x004A8D20) and the level list 0x009C7040 (CONFIRMED;
// not 0x009CA0C0);
// the nearest candidate gives 0x009CAC4C = radius - distance (the entry's word 14 is never negative) and the unit
// direction 0x009CAC64 (0x004AAA00). Returns true on contact.
bool World::Impl::MeshPush(const float c[3], float r) {
    collCands.clear();
    cac40 = 0;
    for (const Target& t : targetCopy) {
        const ActorTask* a = t.obj;
        if (!a || static_cast<const WTask*>(a) == CurrentTask()) continue;  // DAT_009A26A0 != object
        if (a->objMesh == -1 || (a->flags34 & 0x80008000) || !(a->flags34 & 0x10) || !(a->flags34 & 0x40)) continue;
        const Mat4 inv = mx::Inverse(a->m150);
        float lc[3];
        mx::Transform(inv, c, lc);
        if (MeshSphere(lc, r, static_cast<uint32_t>(a->objMesh)) == 0) continue;
        if (getenv("HOTD2_PUSHDBG")) {
            fprintf(stderr, "%d push serial %u fn %X by %X r %.9g c %.9g %.9g %.9g m150", frame, CurrentSerial(), CurrentTask() ? CurrentTask()->fn : 0, a->fn, r, c[0], c[1], c[2]);
            for (float v : a->m150.m) fprintf(stderr, " %08X", *reinterpret_cast<const uint32_t*>(&v));
            fprintf(stderr, " lc %.9g %.9g %.9g pt %.9g %.9g %.9g n %.9g %.9g %.9g\n", lc[0], lc[1], lc[2], cac54, cac58, cac50, cac64[0], cac64[1], cac64[2]);
        }
        const float h[3] = { cac54, cac58, cac50 };
        float o[3], n[3];
        mx::Transform(a->m150, h, o);
        cac54 = o[0];
        cac58 = o[1];
        cac50 = o[2];
        mx::TransformDir(a->m150, cac64, n);  // 0x004A8AF0
        memcpy(cac64, n, 12);
        CollAddCandidate(c);  // 0x00405620
    }
    for (uint32_t m : collList[0]) {
        if (MeshSphere(c, r, m) == 0) continue;
        CollAddCandidate(c);
    }
    if (!CollPickCandidate()) return false;
    cac4c = r - std::sqrt(cac48);
    if (cac64[2] + cac64[1] + cac64[0] != 0.0f) {
        const float len = std::sqrt((cac64[0] * cac64[0] + cac64[1] * cac64[1]) + cac64[2] * cac64[2]);
        for (float& v : cac64) v = v / len;  // 0x004AAA00
    }
    cac40 = 1;
    return true;
}

}  // namespace game
