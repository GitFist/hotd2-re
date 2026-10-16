// game::World - actor type 0x31 with skeleton 0x18 (stage 6): included by world_st2_actor31.cpp (it uses that file's
// helpers), before TaskActor31. Setup, node draw and behaviour 5 of skeleton 0x18 are the stage-5 agent's (in that file). Status and verification: notes/agents/stage6.md.

namespace {

// pose index -> one of four motions
int PoseMotion(const Actor31Task& a, int m0, int m1, int m2, int m3) {
    switch (PoseIdx(a.flags136C)) {
    case 1: return m1;
    case 2: return m2;
    case 3: return m3;
    default: return m0;
    }
}

// 34: 0x00451480 appear: blink in three steps toward the record point (P word 1 frames first), then P byte 3
void Beh34(World::Impl& w, Actor31Task& a) {
    if (a.sub > 1) {
        if ((w.frameCounter5C50 & 1) == 0) {
            a.alpha = 1.0f;
            a.flags136C &= ~4u;
        } else {
            a.alpha = 0.0f;
            a.flags136C |= 4;
        }
    }
    switch (a.sub) {
    case 0: {
        a.f1348 = 3.0f;
        a.flags34 |= 0x100;
        int motion = 0;
        switch (PoseIdx(a.flags136C)) {
        case 0: motion = 0x208; break;
        case 1: motion = 0x1FD; break;
        case 2: motion = 0x1F3; break;
        case 3: motion = 0x205; break;
        default: w.Note("actor 0x31 behaviour 34 with pose index > 3 (0x00451480)"); break;
        }
        w.AnimBlendTo(a, motion, 0, 4);  // 0x004119A0
        memcpy(a.from, a.pos, 12);       // +0x13C0
        ++a.sub;
        a.jumpFrame = a.record.ParamS32(4);  // +0x1330
        a.flags136C |= 4;
        a.alpha = 0.0f;
        break;
    }
    case 1: break;
    case 2: case 3: case 4: break;
    default: return;
    }
    if (a.sub == 1) {
        if (0 < --a.jumpFrame) return;
        ++a.sub;
    }
    for (;;) {
        if (a.sub == 2) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, a.from[0], a.from[1], a.from[2]);
            mx::RotY(m, a.rot[1]);
            const float l[3] = { 0.0f, 0.0f, a.f1348 * F32(w.exe, 0x0055CCD4) };
            mx::Transform(m, l, a.pos);  // 0x004A8A80
            ++a.sub;
            a.f1344 = 30.0f;
            a.flags34 &= ~0x80000u;
        }
        if (a.sub == 3) {
            const float c = a.f1344 - 1.0f;
            a.f1344 = c;
            if (!(c <= 0.0f)) return;
            ++a.sub;
        }
        if (a.sub == 4) {
            const float c = a.f1348 - 1.0f;
            a.f1348 = c;
            if (c <= 0.0f) {
                w.Sound(0x2225A9);
                a.flags136C &= ~4u;
                a.alpha = 1.0f;
                a.flags34 &= ~0x100u;
                a.behaviour = static_cast<int16_t>(a.record.ParamS8(3));
                a.sub = 0;
                return;
            }
            a.sub = 2;
            return;
        }
        return;
    }
}

// 0x00450CF0: knock-back direction +0x1354 (0 floor, 1 ceiling, 2..5 walls by the facing quadrant)
void KnockDir(Actor31Task& a) {
    const uint32_t f = a.flags136C;
    if (f & 0xC0) {
        const uint32_t yaw = static_cast<uint32_t>(a.rot[1]);
        int s1 = 0, s2 = 0;
        if (InRange(yaw, 0x8000, 0x2000)) { s1 = 0x4000; s2 = 0xC000; }
        if (InRange(yaw, 0, 0x2000)) { s1 = 0xC000; s2 = 0x4000; }
        if (InRange(yaw, 0x4000, 0x2000)) { s1 = 0; s2 = 0x8000; }
        if (InRange(yaw, 0xC000, 0x2000)) { s1 = 0x8000; s2 = 0; }
        const uint32_t s = static_cast<uint32_t>((f & 0x40) ? s2 : s1);
        if (InRange(s, 0x4000, 0x2000)) a.f1354 = 2;
        if (InRange(s, 0xC000, 0x2000)) a.f1354 = 3;
        if (InRange(s, 0, 0x2000)) a.f1354 = 5;
        if (InRange(s, 0x8000, 0x2000)) a.f1354 = 4;
        return;
    }
    a.f1354 = (f & 0x100) ? 1 : 0;
}

// 0x0044DE80(dir): velocity of the next knock-back frame along the pose's "down" (+0x1354); 0 when done
int KnockVelocity(World::Impl& w, Actor31Task& a, int dir) {
    const int i = a.jumpFrame, n = a.jumpFrames;
    const int i1 = i + 1;
    a.jumpFrame = i1;
    if (n <= i) return 0;
    const float g = F32(w.exe, 0x00565E1C), g1 = F32(w.exe, 0x00565E20), neg = F32(w.exe, 0x004C4C64);
    const float fn = static_cast<float>(n);
    auto arc = [&](float d) {
        return static_cast<float>(i1) * g1 + (static_cast<float>(n * n) * g + d + d) / static_cast<float>(n * 2);
    };
    switch (dir) {
    case 0:
        a.vel[0] = (a.to[0] - a.from[0]) / fn;
        a.vel[1] = arc(a.to[1] - a.from[1]);
        a.vel[2] = (a.to[2] - a.from[2]) / fn;
        return 1;
    case 1:
        a.vel[0] = (a.to[0] - a.from[0]) / fn;
        a.vel[1] = arc(a.to[1] - a.from[1]) * neg;
        a.vel[2] = (a.to[2] - a.from[2]) / fn;
        return 1;
    case 2:
        a.vel[0] = arc(a.to[0] - a.from[0]) * neg;
        a.vel[1] = (a.to[1] - a.from[1]) / fn;
        a.vel[2] = (a.to[2] - a.from[2]) / fn;
        return 1;
    case 3:
        a.vel[0] = arc(a.to[0] - a.from[0]);
        a.vel[1] = (a.to[1] - a.from[1]) / fn;
        a.vel[2] = (a.to[2] - a.from[2]) / fn;
        return 1;
    case 4:
        a.vel[0] = (a.to[0] - a.from[0]) / fn;
        a.vel[1] = (a.to[1] - a.from[1]) / fn;
        a.vel[2] = arc(a.to[2] - a.from[2]) * neg;
        return 1;
    case 5:
        a.vel[0] = (a.to[0] - a.from[0]) / fn;
        a.vel[1] = (a.to[1] - a.from[1]) / fn;
        a.vel[2] = arc(a.to[2] - a.from[2]);
        return 1;
    default:
        return 1;
    }
}

// 0x0044C370: the surface under the pose (floor / wall / ceiling): a segment from 1000 (ceiling 1500) units along
// the pose's "down" to 4.5 units the other way; false when nothing is hit (walls, ceiling)
bool PoseSurface(World::Impl& w, Actor31Task& a, float out[3]) {
    const uint32_t yaw = static_cast<uint32_t>(a.rot[1]);
    int s1 = 0, s2 = 0;
    if (InRange(yaw, 0x8000, 0x2000)) { s2 = 0xC000; s1 = 0x4000; }
    if (InRange(yaw, 0, 0x2000)) { s1 = 0xC000; s2 = 0x4000; }
    if (InRange(yaw, 0x4000, 0x2000)) { s1 = 0; s2 = 0x8000; }
    if (InRange(yaw, 0xC000, 0x2000)) { s1 = 0x8000; s2 = 0; }
    const uint32_t f = a.flags136C;
    int pitch = 0x4000, ry = 0;
    if (f & 0x80) { pitch = 0; ry = s1; }
    if (f & 0x40) { pitch = 0; ry = s2; }
    if (f & 0x100) { pitch = 0xC000; ry = 0; }
    auto probe = [&](int y, float z, float res[3]) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotX(m, pitch);
        mx::RotY(m, y);
        const float l[3] = { 0.0f, 0.0f, z };
        mx::Transform(m, l, res);
    };
    float near4[3], far1[3];
    probe(ry + 0x8000, 4.5f, near4);
    probe(ry, (f & 0x100) ? 1500.0f : 1000.0f, far1);
    w.cac40 = 0;
    if (!(f & 0x100) && (f & 0x20)) {
        if (w.CollSegmentAll(far1, near4) == 0) return false;
        out[0] = w.cac54;
        out[1] = a.pos[1];
        out[2] = w.cac50;
        return true;
    }
    if (w.CollSegmentAll(far1, near4) == 0) {
        if (f & 0x100) return false;
        out[0] = a.pos[0];
        out[1] = w.ground;
        out[2] = a.pos[2];
        return true;
    }
    out[0] = w.cac54;
    out[1] = w.cac58;
    out[2] = w.cac50;
    return true;
}

// 33: 0x00450E40 shot (skeleton 0x18): knocked off its surface along the pose's "down", lands, recovers (7) or dies
void Beh33(World::Impl& w, Actor31Task& a) {
    const float bounce = F32(w.exe, 0x0055D2D8), half = F32(w.exe, 0x004C43AC), still = F32(w.exe, 0x004C4D08),
                off = F32(w.exe, 0x00565DE8), neg = F32(w.exe, 0x004C4C64);
    switch (a.sub) {
    case 0: {
        const uint32_t f = a.flags136C;
        a.flags136C = f | 0x180000;
        a.flags34 = (a.flags34 & ~0x4000u) | 0x200000;
        const int m = PoseMotion(a, 0x215, 0x1FE, 0x1F4, 0x206);
        if (!(f & 0x400000)) {
            StopMoving(a);  // 0x0044E120
            w.AnimBlendTo(a, m, 0, 5);
            a.hitCount = 0;
        } else {
            w.AnimSetMotion(a, m);
            ++a.hitCount;
        }
        if (!(a.flags34 & 0x2000) && a.hitCount < 2) KnockBack(w, a);  // 0x0044D120
        else a.flags34 |= 0x2000;
        DeadCounts(w, a);  // 0x0044D050
        KnockDir(a);       // 0x00450CF0
        ++a.sub;
    }
        [[fallthrough]];
    case 1:
        if (0x2A < a.anim.tick[0]) a.flags34 |= 0x4000;
        if (KnockVelocity(w, a, a.f1354) == 1) return;
        a.fallFrames = 0;  // +0x1338
        ++a.sub;
        a.flags34 &= ~0x4000u;
        [[fallthrough]];
    case 2: {
        if (0x2A < a.anim.tick[0]) a.flags34 |= 0x4000;
        ++a.fallFrames;
        float p[3];
        if (PoseSurface(w, a, p)) memcpy(a.surf, p, 12);
        else {
            w.Note("actor 0x31 behaviour 33: no surface (0x0044C370 leaves its output; stack garbage in the original)");
            memcpy(p, a.surf, 12);
        }
        a.landMaterial = static_cast<int>(w.cac40);  // +0x1350
        const uint32_t up = 0x3D5F0123u, down = 0xBD5F0123u;
        auto setAcc = [&](int k, uint32_t bits) { memcpy(&a.acc[k], &bits, 4); };
        const bool late = a.fallFrames >= 0x78;
        float v = 0;
        switch (a.f1354) {
        case 0:
            setAcc(1, down);
            if (p[1] < a.pos[1] + a.vel[1] && !late) return;
            a.pos[1] = p[1];
            a.vel[0] = a.vel[0] * half;
            a.vel[1] = a.vel[1] * bounce;
            a.vel[2] = a.vel[2] * half;
            w.Sound(0x2716A9);
            v = a.vel[1];
            break;
        case 1:
            setAcc(1, up);
            if (a.pos[1] + a.vel[1] < p[1] && !late) return;
            a.pos[1] = p[1];
            a.vel[0] = a.vel[0] * half;
            a.vel[1] = a.vel[1] * bounce;
            a.vel[2] = a.vel[2] * half;
            w.Sound(0x2716A9);
            v = a.vel[1];
            break;
        case 2: case 3:
            if (a.f1354 == 2) {
                p[0] = p[0] - off;
                setAcc(0, up);
                if (a.pos[0] + a.vel[0] < p[0] && !late) return;
            } else {
                p[0] = p[0] + off;
                setAcc(0, down);
                if (p[0] < a.pos[0] + a.vel[0] && !late) return;
            }
            a.pos[0] = p[0];
            a.vel[0] = a.vel[0] * bounce;
            a.vel[1] = a.vel[1] * half;
            a.vel[2] = a.vel[2] * half;
            w.Sound(0x2716A9);
            v = a.vel[0];
            break;
        case 4: case 5:
            if (a.f1354 == 4) {
                p[2] = p[2] + off;
                setAcc(2, down);
                if (p[2] < a.pos[2] + a.vel[2] && !late) return;
            } else {
                p[2] = p[2] - off;
                setAcc(2, up);
                if (a.pos[2] + a.vel[2] < p[2] && !late) return;
            }
            a.pos[2] = p[2];
            a.vel[0] = a.vel[0] * half;
            a.vel[1] = a.vel[1] * half;
            a.vel[2] = a.vel[2] * bounce;
            w.Sound(0x2716A9);
            v = a.vel[2];
            break;
        default:
            return;
        }
        if (v < 0.0f) v = v * neg;
        if (still < v && !late) return;
        StopMoving(a);
        a.flags136C &= ~0x4000u;
        a.flags34 = (a.flags34 & ~0x2000u) | 0x100;
        const int r = w.Rand(0x00451307);
        ++a.sub;
        a.jumpFrame = (r % 10 + 1) * 3;  // +0x1330
    }
        [[fallthrough]];
    case 3:
        if (!(a.flags34 & 0x4000000) && a.landMaterial != 0x5A) {
            if (0 < --a.jumpFrame) return;
            w.AnimBlendTo(a, PoseMotion(a, 0x216, 0x1FF, 0x1F5, 0x207), 0, 5);
            ++a.sub;
            a.flags34 &= ~0x4000u;
        } else {
            if (!a.anim.ended) return;  // +0x1F1
            if (a.landMaterial == 0x5A) {
                a.flags34 = (a.flags34 & ~0x4000u) | 0x4000000;
                DeadCounts(w, a);
            }
            Grounded(w, a);  // 0x0044D0A0
            return;
        }
        [[fallthrough]];
    case 4:
        if (w.MotionTicks(a.anim.motion[0]) - 2 <= a.anim.tick[0]) {
            a.flags136C &= 0xFFBFDFFFu;
            a.flags34 = (a.flags34 & 0xBFDFFFFFu) | 0x100;
            a.cooldown = 0x14;  // +0x133C
            a.behaviour = 7;
            a.sub = 0;
        }
        return;
    default:
        return;
    }
}

}  // namespace
