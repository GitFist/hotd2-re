# Enemy AI (actor type 0x30): stage-1 attract demo enemy

Agent notes (Stage 2, enemy subsystem). Sources: decompiles in `ghidra/export-hotd2/functions`, disassembly
(`tools/disasm.py`), exe tables, and the full-object capture `build/scene_capture_enemies_full.txt`
(serials 65605 / 65607, frames 2880..3077 / 2969..3160).

**CONFIRMED** = read in code and matched against the capture; **CODE** = read in code, not exercised by the
capture; **HYP** = hypothesis. Port: `src/game/enemy.{h,cpp}`, test `tests/enemy_test.cpp` (all checks pass).

## 1. Summary of the demo enemies (CONFIRMED)

Both enemies are kind 15 (`znebi2` parts, models 7154..7190), variant 0, behaviour 23, params at 0x0097AC30
`{0x0017000F, -1, 0xB1, 0x24}`. Both have 1 hp. Timeline of enemy 1 (enemy 2 is the same, shifted):

| frame | what | fields |
|---|---|---|
| 2880 | spawned by op 0x0B; constructor only (main loop runs from the next frame) | beh 23 / sub 0, motion 0x3BC (bind pose), hp 1 |
| 2881 | beh 23 state 0: motion 0xB1 (177, "lunge"), trigger -1 → fires at once: pick target (1 rand), face it, sub 2 | +0x121 = player 1, rot.y = 0x85BF |
| 2881..2896 | plays motion 177 (tick 0..15); root motion moves x/z | |
| 2897 | shot in the head by P1: 100 damage → hp -99 → dead | beh 9 (death fall), live count 0x009C904A − 1 |
| 2897..2902 | 6-frame blend into death motion 0x3DB (987); knock-back jump (15 frames) | |
| 2912..2957 | free fall with gravity −0.0368, two bounces (×−0.25), lands on the floor (y −9.76) | |
| 2958 | landed → beh 7 (corpse); attacker count 0x009C7006 − 1 | |
| 2959..3077 | corpse: 120-frame countdown, sinks 0.04/frame, "twitch" = 1 rand per frame | anim counter 76 (or 75 if r%17 == 16) |
| 3078 | countdown hits 0 → `Actor_Destroy` → task killed | |

The demo never reaches tick 36 (params+0xC), so the lunge attack itself (player damage) is not exercised.
Later hits on the corpse still subtract hp and advance node damage stages (-114, -139, -159, -174).

## 2. Object layout (0x13F4 bytes)

`obj` = task. The animation block A = obj+0x194 is what `Enemy_DrawSkeleton` 0x004110D0 and the existing
anim notes call "obj" (their +0x60 type = obj+0x1F4, +0x20 motion = obj+0x1B4, +0x18 frame = obj+0x1AC).

| off | type | meaning | written by |
|---|---|---|---|
| 0x00 | fn | task function: 0x004533F0 Enemy_Update (kind 9: 0x00453290) | Task, 0x00452DA0 |
| 0x08, 0x0C, 0x18.., 0x20, 0x28.. | | task serial, alive, siblings, parent, children (coordinator) | Task_Create |
| 0x34 | u32 | flags (below) | everywhere |
| 0x38 | u32 | flags2: 1 not counted, 2 live count released, 4 attacker count released, 8/0x10/0x20 from record flags, 0x40 actor slot held | 0x00452FD0, 0x0045656x, 0x00409270 |
| 0x3C | s32 | actor slot in 0x009C88C0[14] (−1 none) | Actor_RegisterSlot 0x00409270 |
| 0x40 | f32[3] | position (world) | record, behaviours, root motion 0x00410C50 |
| 0x4C | f32[3] | velocity, added to pos each frame after the behaviour | behaviours |
| 0x58 | f32[3] | acceleration (only used by behaviours, e.g. 0x5C gravity in beh 9) | 0x004550E0, 0x0044E120 |
| 0x64/0x68/0x6C | s32 | rotation x/y/z (0x10000 = 360°); y = facing | record, Enemy_FaceTarget |
| 0x70 | f32[3] | view-space position of the body point (0x100) | Actor_UpdateViewPos 0x00409B70 |
| 0x100 | f32[3] | world body point (torso node, slot 1; slot 2/9 for special kinds); +4.0 to y after use | Enemy_NodeWalk 0x004114C0, 0x00409B70 |
| 0x10C | f32[3] | same point before the inverse view | 0x004114C0 |
| 0x11C | s16 | hit points | Enemy_InitHp 0x0040A8B0, Enemy_SubtractHp 0x004098C0 |
| 0x11E | s16 | record hp (+difficulty bonus, clamped 1..300) | 0x00408A20, 0x0040A8B0 |
| 0x120 | s8 | slot in 0x009A5EC0 (32-byte records), cleared at death/despawn (HYP: group) | — |
| 0x121 | s8 | target player (−1 none) | Enemy_PickTarget 0x00455DE0 |
| 0x124 | f32 | 0x004C4D28[kind] (kind 15: 10.0) (HYP: hit/approach radius) | 0x00452DA0 |
| 0x128 | f32 | 3.5, collision radius / centre height | 0x00452DA0 |
| 0x12C | f32[3] | collision sphere centre (x, y+0x128+1.0, z) | 0x00454AC0 |
| 0x138..0x148 | | push by another object: ptr, strength, direction | collision code |
| 0x190+p | s8 | node slot hit by player p this frame | shot code (other subsystem) |
| 0x194..0x130B | | **animation block A** (below) | anim |
| 0x130C | s32 | variant (params byte 1) | 0x00452DA0 |
| 0x1310 | s16 | behaviour index into 0x00592AE8 | 0x00452DA0, behaviours, 0x00453EB0 |
| 0x1312 | s16 | behaviour sub-state | behaviours |
| 0x1314 | s16 | tick of the last motion sound | Enemy_MotionSounds 0x00452A10 |
| 0x1316 | s16 | record "lo" → low bits of 0x136C | 0x00408A20 |
| 0x1318 | u8 | lost-part mask (bit 0x004C4D18[slot]) | 0x004098E0, 0x00409AF0 |
| 0x1319 | s8 | 1 = last hit on slot 1, 2 = on slot 2 (selects hit-reaction motion) | 0x004543F0 |
| 0x131A | s8 | chosen attack index | beh 3 |
| 0x131C | s8 | killer player | 0x00409430 |
| 0x131D / 0x131E / 0x131F | s8 | attack counters (0x131D init −1); 0x131F = 2 for kind 0 else 0 | 0x00452DA0, beh 1/2 |
| 0x1320 / 0x1324 | s32 | head look pitch / yaw (≤ 0xC0 per frame, ±0x4000) | Enemy_HeadLook 0x00453BE0 |
| 0x1328 | s32 | animated-texture counter of special node models | 0x004534A0 |
| 0x1330 | s32 | scratch counter: beh 23 trigger id; beh 9 jump frame; beh 7 corpse countdown | behaviours |
| 0x1334 | s32 | beh 9: jump duration, then fall-frame counter | 0x0044DB50, 0x004550E0 |
| 0x1338 / 0x133C | s32 | collision toggle timer (60) / attack cooldown | 0x00454900, beh 2 |
| 0x134C, 0x1388, 0x138C | f32 | fade-in parameters (kinds 9 / 0x12) | 0x00452FD0, 0x004534A0 |
| 0x1350 | s32 | facing saved at corpse start | 0x00454F20 |
| 0x1358 | s32 | attack limit (HYP) | — |
| 0x1364 | s32 | type of the last hit (0x009A58F8[p]) | 0x00453EB0 |
| 0x1368 | u32 | flags3: 2 water splash done (also disables motion sounds), 8/0x10/0x40/0x80 death-motion selectors, 0x20 fading | |
| 0x136C | u32 | flags4: 0x8 jump target preset, 0x10000 landing dust done, 0x20000 holds the global "attack" slot 0x009A34F0, 0x100000 scripted (beh 23), 0x20000000/0x40000000 collision, 0x80000000 dead; record lo in the low bits; init 0x60000000 | |
| 0x1390 | ptr | params (spawn record + 0x24) | 0x00408A20 |
| 0x13A4 | ptr | parent enemy (kind 0x12 spawns a kind-9 child) | 0x00452FD0 |
| 0x13C0 / 0x13CC | f32[3] | jump start / target | Enemy_JumpSetup 0x0044DB50 |
| 0x13D8 | f32[3] | saved position | 0x00452FD0, beh 3 |
| 0x13E4 | f32[3] | point the enemy faces (target player position) | Enemy_FaceTarget 0x00455F40 |

**flags 0x34** (CONFIRMED where seen): 1 alive/targetable (record flags | 1); 2/4 hit by player 0/1 this frame;
8 hit this frame; 0x80 skeleton has children; 0x100 not hittable (beh 23 waiting); 0x200|0x400|0x800 set on every
hit outside game mode 6 (0x200 blocks wound-model swaps); 0x1000 invulnerable (no hp loss); 0x2000 hit-reaction
motion; 0x4000 hold animation (counter not advanced); 0x8000 not in the hit-sphere list; 0x10000 not in the draw
depth list / gone; 0x20000 dying; 0x80000 landed in water; 0x8000000 was hit (not registered as target);
0x4000000 killed; 0x40000000 hit reaction active; 0x80000000 destroyed.

**Animation block A = obj+0x194** (CONFIRMED for the fields used here; two tracks, track 1 = upper body):
A+0x00 counter track 0 (incremented after each evaluation unless 0x34 & 0x4000), A+0x04 counter track 1,
A+0x08 tick (counter % (len+1)), A+0x0C tick track 1, A+0x18/0x1C stored frame, A+0x20/0x24 motion id,
A+0x28/0x2C blend start counter, A+0x30/0x31 blend length+1, A+0x33 loop count, A+0x34/0x35 partial-skeleton
node per track, A+0x36 track-1 active, A+0x37/0x38 flags (1 blending, 0x20 odd-tick interpolation, 8/0x10 track 1),
A+0x3C/0x40 attachment count/array, A+0x5D/0x5E ended / near end, A+0x60 skeleton type = kind, A+0x64 skeleton
flags (= 7: 1 draw, 2 root motion, 4), A+0x68 rotation order (= 1: calls rotX, rotZ, rotY after the translate, i.e. M = Ry·Rz·Rx·T(pos)), A+0x6C root position,
A+0x78 node buffer (16 slots × 0x90: +0 model id, +4 angles, +0x28 matrix, +0x68 view-space hit point, +0x74 flags,
+0x7C local hit offset, +0x8C damage stage), A+0x1158 node draw callback (= obj+0x12EC), A+0x115C post-root
callback (= obj+0x12F0), A+0x1160 previous root position, A+0x116C scale (kinds 0x20..0x38: 0.9; 0x1E 0.6;
0x1F 0.7; else 1.0), A+0x1170/0x1174 extra draw callback.

## 3. Lifecycle and per-frame order

**Spawn** (event op 0x0B → `Actor_SpawnRecord0B` 0x00408A20 → task 0x13F4, fn = 0x009A2280[0x30] =
`Enemy_Create` 0x00452DA0, run on the spawn frame):
1. A+0x20 = 0x3BC (0x41E for variant 4); kind (+0x1F4) = params byte 0 (unless already 9).
2. `Enemy_InitHp` 0x0040A8B0: hp = max hp = clamp(record hp + 0x005776B0[difficulty 0x009C8E94] (−30, −15, 0, 0, 0;
   game mode 5 uses index 2), 1, 300).
3. variant (+0x130C) = params byte 1; `Anim_InitEnemy` 0x00410440 (scale, counters, flags A+0x64 = 3, rotation order
   5, attachment array 0x0052ED08[kind], `Enemy_InitPose`, registers the actor slot); then A+0x64 |= 4, rotation
   order = 1; callbacks +0x12EC = 0x004534A0, +0x12F0 = 0x00454900; A+0 = slot % 10.
4. +0x124 = 0x004C4D28[kind], +0x128 = 3.5, 0x136C = 0x60000000 | record lo; +0x131F; head look angles towards the
   camera (0x009C71E0.., y + 15) unless record flag 0x40000.
5. `Enemy_SetupKind` 0x00452FD0: record flags 2/4/8 → flags2; per-kind switch (2/3: gas/fade flags, 9, 0xC, 0xE,
   0x12 spawns a kind-9 child). Kind 15: nothing.
6. +0x121 = +0x131D = −1, behaviour (+0x1310) = params byte 2, sub = 0.
7. Unless kind 9: if behaviour != 0x1F, 0x009C7006 (attacking enemies) and 0x009C904A (live enemies, waited on by
   event op 0x44 `Evt_WaitEnemies` 0x0045FC10) are incremented; task fn = 0x004533F0.

**Enemy_Update 0x004533F0** (every frame):
1. current-actor globals 0x009C8E4C / 0x009A26A0 = obj.
2. `Enemy_UpdateUpperBody` 0x004547C0 and `Enemy_UpdateHitFlags` 0x00454660 (hit-reaction track bookkeeping; inactive in the demo).
3. `Enemy_ProcessHits` 0x00453EB0 (section 5).
4. if 0x136C & 0x10000000: 0x0045E770.
5. behaviour 0x00592AE8[+0x1310](obj).
6. pos += vel.
7. `Enemy_AnimateSkeleton` 0x00454860: `Enemy_EvalSkeleton` 0x00411090 → `Enemy_DrawSkeleton` (track ticks
   0x004111A0 / 0x004112E0, root transform with **root motion**: the x/z delta of the motion root position,
   rotated by the object rotation and scaled, is added to obj+0x40/0x48 (0x00410C50); then the post-root callback
   0x00454900 (wall and enemy-enemy collision, snap to floor 0x00454B10); node walk: node matrices, view-space hit
   points, body point +0x100, and the per-node draw callback 0x004534A0 which submits the node's model); then
   A+0 and A+4 += 1 unless 0x34 & 0x4000.
8. unless 0x34 & 0x8000000: `Actor_RegisterTarget` 0x00409010 (list 0x005A4D58, 14 entries; HYP: demo auto-aim / priority).
9. `Actor_UpdateViewPos` 0x00409B70(4.0): +0x70 = view(+0x100); hit-sphere list 0x0059D8E8 (0x00405160); +0x104 += 4; depth list 0x005A4DC8.
10. hook 0x00592BC0 = 0x00456920 (water splash; only stages 5 / 0x37).
11. unless 0x1368 & 2: `Enemy_MotionSounds` 0x00452A10 (sound cues at fixed ticks of a few motions; none for 0xB1/0x3DB).

**Animation tick** (track 0 not blending, 0x004111A0, CONFIRMED 376/376 frames): tick = counter % (len+1)
(len = 0x004E07D0[motion], 60 Hz), frame = tick/2; odd tick < len → interpolate frame and frame+1 (0 if past
len/2), flag 0x20; odd tick == len → frame+1. Motions loop unless the behaviour holds them (0x34 & 0x4000).
`Anim_SetMotion` 0x00411930 restarts at counter 0; `Anim_BlendTo` 0x004119A0(A, motion, startTick, n) blends for
n+1 frames during which the tick stays at startTick, then counter = startTick+1 (death: n = 5 → 6 frames, CONFIRMED).

## 4. Behaviour table 0x00592AE8 (54 entries) and hooks

Detailed (CONFIRMED in the demo): 7, 9, 23 (+10 via 23). The rest: one-line HYP from callees/motions.

| # | fn | notes |
|---|---|---|
| 0 | 0x0041EBB0 | no-op |
| 1 | 0x004554D0 | idle: random idle motion (0x00592CBC[kind][variant]+8/+0xC, rand start tick), may switch to 2 / 5 / 0x21 |
| 2 | 0x00455720 | approach the camera with the walk motion (set+0); in range → Enemy_PickTarget → 3 (or 0x34 for variant 4); else 4 |
| 3 | 0x00455A40 | attack: attack entry 0x00592F18[kind][variant][0x131A] {motion, approach motion, range, …, hit tick}; at the hit tick 0x00456490 (damages the player) → 4 |
| 4 | 0x00455C30 | after attack (HYP) |
| 5 | 0x00455670 | special idle → 1 |
| 6 | 0x00454D20 | die on the spot: Enemy_StartDeathMotion, Enemy_OnDeath, when the motion ends → corpse (7/8) or 0xC |
| 7 | 0x00454F20 | **corpse**: 120 frames, y −= 0.04, twitch rand; then Actor_Destroy |
| 8 | 0x00454FD0 | corpse for kinds 3 / 0x12 |
| 9 | 0x004550E0 | **death knock-back fall** (jump + gravity + bounces) → 7 |
| 10 | 0x00455490 | despawn: release target/counters, Actor_Destroy |
| 11 | 0x00454B90 | fall off a ledge (floor check, motions 0x41D / 0x3BA) |
| 12 | 0x00456DF0 | fall to death (motion 0x3F8) → corpse |
| 13..22 | 0x00456F50.. | scripted moves (walk in, jump down, climb …; motions 2/4) |
| 23 | 0x00457B50 | **scripted lunge** (section 6) |
| 24 | 0x00457CE0 | scripted attack with PlayerHit |
| 25..31 | | scripted entrances (motions 0x1DF, 0x3BB/0x3F7, 0xB9, 0x10F, 0x10C/0x39F …) |
| 32 | 0x0045E830 | attack variant |
| 33 | 0x00459080 | thrown-object / group logic (largest) |
| 34..48 | | kind-specific (bosses / special kinds) |
| 49 | 0x0041EBB0 | no-op |
| 50..53 | | misc (53: motion 0x3D9 death) |

Hooks 0x00592BC0..: [0] 0x00456920 per-frame water check, [1] 0x004569B0, [2] 0x00456B70 landing dust (effect 0x46,
once, 0x136C |= 0x10000), [3] 0x00456C50 spawn splash (variant 6 only), [4] 0x00456D10.

## 5. Shots, damage, death (CONFIRMED against all 7 hit events)

`Enemy_ProcessHits` 0x00453EB0, if 0x34 & 8 (set by the shot code together with bit 1<<(p+1) and obj+0x190+p):
1. `Enemy_CollectShots` 0x004092F0: `Shot_PlayerOrder` 0x004093C0 fills 0x009C8908[2] with the shooting players
   (0x009C7000: 0 P1, 1 P2, 2 both → **1 rand** decides the order); per player: 0x009A2D88[p] = hit slot,
   `Enemy_ApplyShot` 0x00409430 (unless 0x34 & 0x100), clear the node's hit bits.
2. per player with a hit: 0x1364 = hit type, 0x34 |= 0x8000000, `Enemy_HitEffects` 0x00454050 (blood/effects,
   knock-back push task 0x00407310 with 0.75 / 0.5 / 1.0).
3. if not dead yet (0x136C & 0x80000000 clear): not killed → `Enemy_HitReaction` 0x004543F0 + voice(0) (1 rand);
   killed → 0x136C |= 0x80000000, voice(2 if head else 1) (1 rand), sub = 0, behaviour = 9 if (beh 0x34 or
   0x136C & 0x100000 or variant 5/6) else 6.

`Enemy_ApplyShot` 0x00409430 (port `EnemyApplyShot`): idx = stage(slot) + slot·6 in the kind's tables;
`swap = 0x004C7160[kind][idx]`, `next = [idx+1]`; damage = 0x004C8350[kind][idx] + `Enemy_RankDamageMod`
0x00409870 (`0x004D0D84[kind][slot·16 + rank]`, signed char), clamped ≥ 0; rank = 0x009C8E96 (0..15, raised over
time by `Game_UpdateRank` 0x004607B0, initial 0x005679F4[difficulty] = −3/−1/1/4/8). Outside game mode 6 every hit
sets 0x34 |= 0xE00 (no wound models). Then:
- next 0: swap 2 → deflect (type 5); else type 2 + damage; slot 1 at hp < 1 → sever (type 3); other slots once
  (node flag 0x80000000) with a swap model → wound (type 1), stage+1.
- next 1: (if not 0x400 …) type 2 + damage; first time → sever (type 3) + flag.
- next 2: deflect.  - else: type 1 + damage, stage+1 (torso wound count check).
- hp < 1 and not yet killed → 0x34 |= 0x4000000, killer = p, score 0x50 (mode 6 head kills: 1 rand, ¼ decapitation).
Kind 15 base damage per slot (stage 0, 1, 2): torso 1: 40/50/60; head 2: 100/120; arms 3/4/6/7: 25/35, 25/30;
5/8: 20/25/30; hips 9: 40; legs 10..15: 20/30, 20/25, 20. Rank modifier: +20..−20 (e.g. slot 10: 15,10,5,5,0,0,0,−5…).
The capture is explained exactly with rank 8 or 9 (head 100 kills; corpse hits 15, 25, 20, 15).

Death (`Enemy_BehDeathFall` 0x004550E0, kind 15 / variant 0):
- state 0: 0x34 |= 0x20000; `Enemy_OnDeath` 0x004565A0 (release target 0x00456520, 0x34 |= 0x10000, **0x009C904A −1**
  via 0x00456560); `Enemy_StartDeathMotion` 0x004560B0 → motion 0x3DB (kinds 15..17 with 0x136C & 0x100000),
  blend 6 frames; target point = view(+0x70) + (±0.5, −((r%5+1)·0.5 + 3), −5 or +1.5 if view z ≤ −25) / 4
  (variant 0: 2 rands: sign, %5; variant 4: 4 rands and a ±(r%5)·0x100 x-rotation kick; 5/6: other offsets),
  to world with the inverse view (0x009A6040 + cam·0x1A4·4); `Enemy_JumpSetup` 0x0044DB50: duration =
  max(15, ftol(xz distance · 15 / 30)); sub 1.
- state 1: `Enemy_JumpStep` 0x0044DDE0: v.x/z = Δ/T, v.y = n·(−0.027222) + (T²·0.027222 + 2Δy)/(2T) (n = 1..T).
  After T frames: gravity +0x5C = −0.0368 (variants 4/5: −0.0272), sub 2, fall-frame counter 0, same frame continues.
- state 2: v.y += gravity; above the floor (`Coll_FloorHeight` 0x00409D40) → keep falling; else y = floor, landing
  dust (once), bounce v.y ·= −0.25 while |v.y| > 0.15 (and < 120 frames), else stop (0x0044E120) and
  `Enemy_EnterCorpse` 0x00456740: 0x34 |= 0xC000 (hold), counter −1, **0x009C7006 −1**, behaviour 7.
- 0x34 |= 0x4000 once the motion reaches len−1 (hold the last frame).

Corpse (`Enemy_BehCorpse` 0x00454F20): state 0 (mode 6 only: blood pool task 0x00407DA0), countdown 120, 0x34 |=
0xA0000; each frame y −= 0.04, countdown −1, `Enemy_CorpseTwitch` 0x00454E00 (counter = hold table[r%17 == 16],
1 rand for death motions 0x3D9..0x3E0, 0x3F7, 0x3F8, 0x404, 0x41A, 0x1DF); at 0: 0x34 |= 0x10000, release
0x009A5EC0 slot, `Actor_Destroy` 0x00409CC0 (0x34 = (0x34 & ~1) | 0x80018000, 0x00405220 (HYP: unregister collision),
free the actor slot, `Task_Kill` 0x004A7040).

## 6. Behaviour 23 (scripted lunge) 0x00457B50 (CONFIRMED states 0–2; state 3 CODE)

params P = obj+0x1390: P[1] trigger id, P[2] motion, P[3] attack tick.
- state 0: 0x1330 = P[1]; motion P[2] via Anim_SetMotion and 0x34 |= 0x4100 (animation frozen + not hittable);
  for P[2] == 0xBA the pose 0xBB is set instead and only 0x100; 0x136C |= 0x100000, sub 1.
- state 1: wait until P[1] == −1 or equals 0x009A6110 / 0x009A6458 (HYP: camera-context ids of contexts 0 and 2);
  then (0xBA: Anim_BlendTo 0xBA, 2 frames) `Enemy_PickTarget`, `Enemy_FaceTarget` (rot.y towards the target
  player's point: camera position + RotY(camera yaw)·((1−2p)·−1.2, 0, −1.5)), clear 0x4100, spawn hook, sub 2.
- state 2: when tick == P[3] (36): `Player_TakeHit` 0x00415300(target, 1, 0 for motion 0xB1 else 9) (life −1,
  score −100, 90 frames invulnerable; not in mode 5), sub 3.
- state 3: when tick ≥ len−1: effect 0x62 at the position, `Enemy_Despawn` 0x00455490 (counters −1, Actor_Destroy).

`Enemy_PickTarget` 0x00455DE0 (demo branch: 2 players, 0x009C8E80 != 1, 0x009C7006 == 1): p = rand() & 1, ~p if
0x009A2BA0[p] is taken; invalid unless 0x009C6F08 == 2 and (mode 5 or player state 5); off-screen check
0x00409C10 fails → 0x136C |= 0x20000 and 0x009A34F0 = 1; marks 0x009A2BA0[p] = 1 (slot taken). CONFIRMED: P1 for enemy 1, P0 for enemy 2.

## 7. rand() usage (CONFIRMED; 517/519 frames' Z states chain exactly)

The enemies' rand calls come **last** in a frame, enemy 1 before enemy 2 (task order); within an enemy update:
0x004093E2 (shot order, when both players shot), voice 0x0040A737/0x0040A778/0x0040A7B9 (hurt / body death /
head death), then the behaviour's calls. Per path: pick target 1 (0x00455E52); death jump 2 (0x00455218 sign,
0x0045524B %5); corpse 1 per frame (0x00454E59, 238 total = 2 × 119). With these rules `enemy_test` predicts
all 238 corpse ticks and both target picks.

## 8. Port spec (deterministic replay)

State per enemy: the fields of section 2 that are marked used (pos, vel, gravity, rot, hp, flags 0x34/0x38/0x1368/
0x136C, behaviour/sub, counters 0x1330/0x1334, target, jump block, animation block counters/tick/frame/motion and
blend, node stages/flags, params pointer). Globals: live count 0x009C904A, attacker count 0x009C7006, target slots
0x009A2BA0[2], rank 0x009C8E96, game mode 0x009C8E98, player count 0x009C8E84, CRT rand state.
Per frame, in task order, run Enemy_Update steps 1–12 above. Implemented and verified in `src/game/enemy.*`:
`CrtRand`, `EnemyTables` (exe tables), `EnemyInitialHp`, `EnemyRankDamageMod`, `EnemyApplyShot`, `EnemyMotionTick`,
`EnemyDeathMotion`, `EnemyDeathJumpTarget` (view-space part), `EnemyJump` (setup/step), `EnemyFallGravity`,
`EnemyFallStep`, `EnemyCorpseHoldTick`, `EnemyPickTargetSingleAttacker`. Still needed for a full replay: camera
position/view matrices (face target, jump target), floor height (collision), root motion (`anim` + 0x00410C50),
blending (0x004119F0 / 0x00411D70), and the shot source (shooting subsystem).

`enemy_test build... game build\scene_capture_enemies_full.txt`: tables 35, spawn 2, motion ticks 376, shot events 7
(rank 8 or 9), jump frames 30 (exact floats), fall frames 91 (4 bounces), corpse 240, rand: 238 twitches + 2 picks. 0 failures.

## 9. Open questions

1. Behaviours 1–5 / 24–53 (normal walking/attacking zombies) are only skimmed: motion set tables 0x00592CBC /
   0x00592DC0 / 0x00592F18 per kind/variant, approach distance 0x009A2BE0, attack damage 0x00456490.
2. Trigger globals 0x009A6110 / 0x009A6458 (camera-context +0x110?) and the meaning of 0x009C7000 / 0x009C8E80.
3. Blending maths (0x004119F0, 0x00411D70, 0x00411700) and the upper-body track (0x00411B20/0x00411B70, 0x004547C0).
4. Floor height source (0x004053B0) and collision callbacks 0x00454900 / 0x00405B10 / 0x004057F0.
5. 0x009A5EC0 slot (+0x120), 0x0045E770 (flag 0x10000000), draw variants 0x00453B30/0x00453B50 (0x009CA08C modes).
6. Wound model swaps (mode 6 only: 0x004099A0 wound table 0x004D032C, sever subtree 0x00409AB0).

## 10. Captures needed

1. **Ordered rand log**: for every rand() call: frame, return address, current task serial (0x007DF8B0) —
   replaces the "enemies call last" inference.
2. **Per-frame globals** (one line per frame): 0x009C8E96 (rank), 0x009C904A, 0x009C7006, 0x009C7000, 0x009C8E80,
   0x009C8E84, 0x009C8E94, 0x009C8E98, 0x009A2BA0[2], 0x009A34F0, 0x009C6F00, 0x009C71E0..E8 (camera position),
   0x009A6110, 0x009A6458, and the 16 floats of 0x009A6000 + 0x009C6F00·0x1A4 and 0x009A6040 + 0x009C6F00·0x1A4
   (view / inverse view used by enemies) — lets the port check face-target yaw, the jump target and spawn look angles.
3. **Shot log**: hook 0x00409430 entry/exit: frame, enemy serial, player, slot, hp before/after, 0x009A58F8[p].
4. **Floor queries**: 0x00409D40 arguments and result for enemy tasks.
5. A real-game capture (not attract) of stage 1 with enemies that walk and attack (behaviours 1–5), same flags
   as `scene_capture_enemies_full.txt`, to cover the generic AI.
