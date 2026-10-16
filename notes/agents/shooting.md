# Player shooting and the attract-demo auto-gun (stage-2 agent notes)

Scope: gun input → aim → shot ray → hit test → hit delivery → damage/score, reload, and how the attract demo
produces its shots. Evidence = decompiled C / disassembly (addresses cited) and the kept capture
`build\scene_capture_actors.txt` (stage-1 demo, frames 2780–3299, Z/N/K/T lines). Port: `src/game/shooting.*`,
test `tests/shooting_test.cpp`. **C** = confirmed (code + capture or unambiguous code), **H** = hypothesis.

## 1. Answer first: the demo auto-gun (C)

`Player_UpdateAim` **0x00415710**(player, gunRecord) is called every frame by both trigger handlers
(0x00414940 / 0x00414B90). When `0x009C71C4 == 1` it fabricates the gun input. `0x009C71C4` is written by
`Game_ApplyState` 0x0040E860 (`= (new state == 5)`) and 0x0045EF92/0x0045EF9D (`= (state 0x009C8E98 == 5)`):
state 5 is the attract demo.

```
roll = rand() % 60                      // call 0x00415729 (ret 0x0041572E) if 0x009C7006 == 0,
fire = roll < (0x009C7006 ? 6 : 1)      //      0x0041573B (ret 0x00415740) otherwise
if fire: x = (rand() % 150) * 2 - 150   // -150..148   (ret 0x00415757)
         y = (rand() % 100) * 2 - 100   // -100..98    (ret 0x0041576B)
else:    x = y = (short)gunRecord       // the code stores its own pointer argument (0x00415779/0x0041577D);
                                        // harmless: fired = 0
rec+4 = x; rec+6 = y; rec+0x14 = fire; rec+0x10 = 1 (valid)
--- then for every mode:
player+0x18 = 0 (shot pending); player+0x10 = (float)rec+4; player+0x14 = (float)rec+6
```
- `0x009C7006` (s16) is the **live-enemy counter** (++ in enemy constructors, -- when an enemy is finished; ~60
  writers). So the demo fires on average once per second per player with no enemy, 6× faster when enemies are
  alive. It does **not** target enemies: the aim is uniform in a ±150×±100 box around the screen centre
  (centred units, y up), i.e. the middle 47 %×42 % of the screen.
- No ammo is used in state 5 (0x00414940: `ammo--` skipped when `0x009C8E98 == 5`); shots still need
  `0x009C8E00 != 0` (letterbox open, §4).
- **Verified on the capture** (`shooting_test`): starting from each frame's previous rand state (Z), two
  `DemoAutoGun` calls (P1 then P2) reproduce the frame's 0x00415710 rand calls in **513/513 frames** (roll site =
  enemy rate, number of aim calls = number of shots). The player tasks are therefore the first rand consumers of
  a frame. Rand chain: 517 frames consistent; the 2 others (2786, 2788) contain srand.
- P1-before-P2 is task order (serials 65579 < 65580) — **H** for the player labels below (counts and aims do
  not depend on it).

### Demo timeline (stage 1, kept capture)
| frame | event |
|---|---|
| 2797 P2 (0,-40), 2804 P1, 2827 P1, 2857 P1 | slow-rate shots (no enemy), no target in the line → nothing |
| 2829/2830 | event op 0x0B (0x0097A420, record 0x0097ABE8) spawns actor kind 3 of 0x00432FF0 → task 0x00433AC0 spawns a **0x62 water splash** (0x00407340) and dies. **Not a shot.** |
| 2880 | enemy 1 (task 0x004533F0, serial 65605) spawned; 0x009C7006 > 0 from 2881 → fast rate |
| **2897** | P1 shot (58,30) hits enemy 1. Enemies have HP 1 (+0x11C); death voice `Enemy_PlayVoice(obj,2)` (rand ret 0x0040A7B9 = case 2, the slot-2 kill), flags become 0x0C030E81 (dead 0x4000000) |
| 2903, 2919, 2922, 2926 | further hits on the dying body (rand 0x004093E2 only: no voice, +0x136C bit 31 already set) |
| 2940 | **0x61 splash** at (-246.2, -9.76, -553.0) from enemy code (0x61 creators 0x004569B0/0x00456B70/0x00456C50/0x00456D10) — the body falling into the water (**H**: which state) |
| 2958 | live-enemy counter back to 0 (rate site switches to 0x0041572E) |
| 2969 | enemy 2 (65607) spawned |
| **2983** | P1 shot (6,-74) kills enemy 2 (voice case 1, 0x0040A778) |
| 2988 | P2 hits the dying body |
| 3023 | 0x61 splash of enemy 2; 3041 counter back to 0 |
| 3172 | P2 shot (70,-82) hits breakable prop 65599 (task 0x00465FB0, nearest of two candidate props) → debris task 0x004667A0 (created by 0x004666B0), prop +0x32C counts up, prop task ends 3242 |

So the "deaths ≈2937/≈3030" seen on screen are the bodies' fall/splash; the kill shots are at 2897 and 2983.
Shots: 41 in 512 frames.

## 2. Data

### Player struct 0x009A5C60 + p*0x130 (p = 0/1; task obj +0x34 = p)
| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | s16 | ? (passed to 0x00406CE0) | 0x00414280 |
| +0x02 | s16 | player state (0x00415080 sets it + task fn from 0x00579CD0[state*5]); 4 = playing?, 9 = idle | 0x00415080, 0x0040E860 |
| +0x06 | s16 | **lives** (torches; HUD 0x004174A0; forced 1 in state 5; 0 → game-over path) | 0x00413E90, 0x00415630 |
| +0x0C | s32 | **score** (`Player_AddScore` 0x004156C0, clamps at 0) | C |
| +0x10/+0x14 | f32 | shot aim, centred (copied from the gun record) | 0x00415710 |
| +0x18 | s32 | **shot pending** (set by trigger, consumed by the hit task) | 0x00414940, 0x00404570 |
| +0x1C | s16 | **ammo** (6; special weapons: 0x009A2248+p*0x14) | 0x00414940, 0x00414B30 |
| +0x1E | s16 | empty flag | |
| +0x22 | s16 | head-shot streak bonus (+10 per head shot, reset by body hits) | 0x00409430 |
| +0x24 | s16 | shots fired (unless 0x009A5C48) | |
| +0x26 | s16 | hits | 0x00409430 |
| +0x68 | s32 | continue countdown (0x9FFF, −0x2D per frame) | 0x00414280 (**H**) |
| +0x78 | ptr | current object for the task (0x009A26A0) | 0x00413E90 |
| +0x7C/+0x80 | fn | per-state callbacks called by `Player_Task` | 0x00413E90/0x00415100 |
| +0x124 | s8 | device kind (gun/pad variant) | 0x0040D980 |
| +0x125 | s8 | input kind: 1 gun, 0 pad/keyboard cursor, −1 none | 0x0040D980, 0x0040CBB0 |

### Gun record 0x009C8FC0 + p*0x28 (`Gun_UpdateRecords` 0x0040CBB0, from shot input 0x009C8800 of `Game_UpdateShots`)
+0/+2 raw screen x,y (0xFFFF = off screen); **+4/+6 centred aim** (x right, y up); +8/+0xC float pad cursor;
**+0x10 valid** (inside calibrated range; shots need it); +0x12 on screen; **+0x14 fired**; +0x16.. per-player
calibration (copied by `Gun_ResetRecords` 0x0040CB10 from 0x009C9FB8/0x009C9FC8 + p*0x7C).
- Gun (kind 1): `Gun_Calibrate` 0x0040D4F0 maps raw → centred piecewise-linearly through 3 calibration points.
  Device mode 6 uses fixed points (0x0040CD37): x {−265, 0, 265} after −320 → {−264, 0, 264} (0x004D1CF0),
  y {50, 240, 430} → {192, 0, −192} (0x004D1CF8). Valid if x ∈ [−320, 319] and y ∈ [−240, 239]
  (`Gun_CalibrateClamped` 0x0040D3E0). Ported (`GunCalibrateMode6`).
- Pad (kind 0): `Gun_MovePadCursor` 0x0040CFE0 moves a float cursor (×2 with a button), clamps to ±320/±240.

### Shot ray block 0x009A2CB8 + p*0x68 (`Shot_BuildRay` 0x00406110(p, x, y)) (C)
v = (x, y, −F), F = **0x009A2D70 = 240 / tan(halfFov)** (double 0x004ED1D0, written every frame by
`Render_BeginWorld` 0x004184C0) = **640.2079** = projection m11·240 = m00·320.
- +0x3C view dir = normalize(v) (0x004AAA00); pitch/yaw = `Cam_DirToAngles`(dir)
- +0x00 world origin = camera matrix (0x009A6040 + ctx*0x1A4) × local offset +0x30 (0 in all paths seen)
- +0x0C world dir = rotation part × view dir
- +0x18 = −pitch, +0x1C = −yaw, +0x20/+0x24 = sin/cos(−pitch), +0x28/+0x2C = sin/cos(−yaw) (2π/65536, double 0x004C4370)

### Target list (C)
`Hit_RegisterTarget` **0x00405160**(obj): skipped if obj+0x34 & 0x8000; registered if flag 0x10 (collision-mesh
object) or view z obj+0x78 ≤ 0. Flag-0x10 objects first get obj+0x150 = obj+0x150 × camera matrix
(model-view → model-world). Entry (5 words at 0x0059D8E8, count 0x005A4C80):
`{obj, obj+0x34, obj+0x12C, obj+0x130, obj+0x134}` — the last three are the object's **world collision-sphere
centre** (enemy: (−245, 4.5, −543.3); radius obj+0x128), used by the object-object pushers 0x00405B10/0x004057F0/
0x004053B0 on the copy 0x005A3098 (count 0x0059D8E4) made by `Hit_CopyTargets` 0x00405360. The **shot test does
not use these words**: it reads obj fields directly.
`Actor_RegisterShootable` 0x00409B70: obj+0x70..+0x78 = view × obj+0x100..+0x108 (view-space aim point), then
0x00405160. Callers of 0x00405160: 0x00409B70 (enemies, props), 0x0042BB80, 0x00439B50, 0x0043F950, 0x004494D0,
0x004525C0, 0x00474120, 0x00474770, 0x004755B0, 0x004757F0, 0x0048EAD0.

Shootable object fields: +0x34 flags, +0x70..+0x78 view pos, +0x124 shot radius (enemy 10, breakable prop 6),
+0x14C collision mesh id (−1 none), +0x150 matrix, +0x190+p hit node per player, +0x1F4 skeleton type.
Skeleton node record obj+0x20C + slot*0x90: +0 active, +0x68..+0x70 view pos, +0x74 flags (0x10 mesh, 0x20 hit
zone, 8 shot, 2<<p by player), +0x78 radius, +0x88 mesh id, +0x8C hit count.

### Flags obj+0x34 (shot-related)
| bit | meaning |
|---|---|
| 0x2 / 0x4 | shot by P1 / P2 this frame (`1 << (p+1)`) |
| 0x8 | **was shot** (cleared by the receiver) |
| 0x10 | collision-mesh target (test segment vs mesh obj+0x14C) |
| 0x40 | (with 0x10) mesh entry |
| 0x80 | skeleton target: whole sphere only gates the per-node tests |
| 0x8000 | not shootable |
| 0x4000000 | dead (enemy) |

## 3. Per-frame order (C unless noted)
1. `Game_Update` 0x0040E730: if 0x007C17AC (**H**: the AUTORELOAD ini key) refills ammo 0 → 6 for players whose
   device mode has bit 31;
   `Render_BeginWorld` (F); `Game_UpdateInput` 0x0040E4D0 → Input_Poll, `Game_UpdateShots` 0x0040CE70 (raw shot
   input 0x009C8800), …, `Gun_UpdateRecords` 0x0040CBB0.
2. Tasks in creation order. **Player tasks** `Player_Task` 0x00413E90 (P1, P2): state callbacks; if lives > 0:
   `Player_Trigger` 0x00414940 (special weapons mode 0x009CA08C == 1: 0x00414B90); crosshair; HUD.
   - `Player_Trigger`: `Player_UpdateAim`; if fired: if !valid or ammo == 0 → reload only for a gun aimed off
     screen with ammo ≠ 6 (`Player_Reload` 0x00414B30: ammo = 6, sound 0x3E16A9); else if 0x009C8E00: ammo−−
     (not in demo / infinite flag 0x009C9FD9+p*0x7C), empty flag at 0, **pending = 1**, shots++,
     `Shot_BuildRay`, `Shot_SpawnTracer` 0x00416F70, gun sound 0x004EC8BC[p]. Then pad reload button
     (0x009C9028 & 0x009C9F6C[p][kind]) for input kind 0. Ported as `PlayerTrigger` (unit-tested only).
3. **Hit task** (created by `Hit_Init` 0x00404480; entry 0x004044A0 → body **0x00404570**, not a Ghidra function).
   For p = 0, 1: hit flag 0x009C9010[p] = 0; if pending: for every registered target: flag 0x10 →
   `Hit_TestObjectMesh` 0x00404A00 else `Hit_TestObjectSphere` 0x00404630; then `Hit_TestLevel` 0x00404B80;
   pending = 0; if any hit: `Hit_Deliver` 0x00404DB0(p), 0x009C9010[p] = 1. Finally `Hit_CopyTargets` and
   count = 0. **The targets are the ones registered during the previous frame** (actors run after this task).
4. Actors: react to obj+0x34 & 8 in their own update, then re-register.

## 4. Hit test (C)
- Sphere: `Shot_RayDistance` **0x004062A0**(p, x, y, z, r): with the block's sin/cos,
  `a = −x·cosY − z·sinY`, `b = z·cosY·sinP − (y·cosP + x·sinY·sinP)`; hit if √(a²+b²) ≤ r (a line through the eye;
  only targets in front are registered). Ported (`ShotRayDistance`).
- `Hit_TestObjectSphere` 0x00404630: sphere (obj+0x70, r obj+0x124). If flag 0x80 and the skeleton
  (0x004E0430[obj+0x1F4]) has children and not 0x8000 → `Hit_TestSkeleton` 0x00404700 → `Hit_TestNodeRecursive`
  0x00404750: active nodes: no 0x10 → `Hit_TestNodeSphere` 0x004047D0 (node view pos, radius; skipped if radius 0),
  0x10 and mesh ≠ −1 → `Hit_TestNodeMesh` 0x004048A0. Otherwise one entry for the object.
- Hit entry (0x3C bytes at 0x0059F4D0): +0 hit point, +0xC view centre, +0x18 normal, +0x24 node/obj, +0x28 obj,
  +0x2C flags (|0x40 mesh, |0x10 has a hit point), +0x30 surface material (0x009CAC40). Key list 0x0059ECD8
  `{key, index}`, **key = __ftol(−viewZ·10)** (10.0 at 0x004C43A4; mesh/level hits use the view z of the hit point,
  0x00404CB0). `Hit_SortByDepth` 0x00405080 = stable LSD radix sort on **key bits 0–15 only**; the first entry wins.
- Mesh/level: `Hit_RayVsMesh` 0x00404AD0 builds the world segment origin → origin + 1000·dir (0x004C49C4);
  0x00404F60 transforms it into model space (inverse of 0x0059D898) and calls 0x004AAA40(start, end, mesh) which
  sets 0x009CAC40 (material, 0 = miss), hit point 0x009CAC50/54/58 and normal 0x009CAC64..6C. Level meshes:
  lists 0x009C7040 (count task 0x009C701C+0x60) and 0x009CA0C0 (+0x61). **H**: these lists are empty in the
  stage-1 demo (no impact effect from any of the 41 shots).
- `Hit_Deliver` 0x00404DB0(p): nearest entry; node entry with 0x20: obj+0x34 |= (2<<p)|8, node flags too,
  obj+0x190+p = node slot; plain/mesh entry: obj+0x34 |= (2<<p)|8, obj+0x190+p = 1. If the entry has a hit point:
  `Hit_RecordImpact` 0x00405260 → 0x009A2C40 + p*0x1C {point, material, normal} and impact effect
  `Fx_SpawnAt` 0x00407340(point, material, material ∈ {5, 0x37}, p). Mode 1 + weapon 3: extra effect 0x53.

## 5. Damage, score (enemies) (C for flow, H for zone semantics)
`Enemy_HandleShot` 0x00453EB0 (enemy task 0x004533F0, 3rd call): if obj+0x34 & 8 → `Enemy_TakeShots`
0x004092F0: order `Enemy_ShotOrder` 0x004093C0 (0x009C7000 = players in game: 0 P1, 1 P2, 2 both → rand at
0x004093E2 decides who is processed first); per player with bit 2<<p: node = obj+0x190+p → 0x009A2D88[p];
`Enemy_ApplyDamage` 0x00409430(p): zone/part from table 0x004C7160[skeleton] (s16 pairs per node), damage from
0x004C8350[skeleton] + 0x004D0D84 bonus, `Enemy_SubtractHp` 0x004098C0 (obj+0x11C −= dmg unless 0x1000), result
kind 0x009A58F8[p] (1 limb, 2 body, 3 severed/kill part, 5 armour). HP < 1 → dead flag 0x4000000, +80 points
(`Player_AddScore`(p, 0x50)). Score per hit: node slot 2 (head **H**) → +120 + streak (player+0x22, += 10);
others (kind ≠ 5) → +10 and streak reset. `Enemy_HitReaction` 0x00454050: blood/impact effects
(0x00407340 types 2, 3, 0x51), `Enemy_PlayVoice` 0x0040A6F0 (0 hurt, 1 death, 2 death slot 2).
Mode 1 doubles points for weapon 2 (0x009A2243). `Player_AddLife` 0x00415630: +1 life up to 0x009A2440 else +300.

## 6. Letterbox / can-shoot (C)
`Letterbox_Task` 0x00413970 (started by 0x00413950) runs the state byte 0x009CA0F4 (jump table 0x00413C80):
0 → draw bars, state 4, can shoot = 1; 1 → opening (can shoot = 1, counter +0x50 up to 40, then state 2);
2 → open (nothing drawn); 3 → closing (counter from 40 down, then bars, state 4, can shoot = 0); 5 → bars, state 4,
can shoot = 0; 6 → state 2, can shoot = 1; 4/7/8 not decoded. **0x009C8E00 = shooting allowed** (also gates reload sounds,
ammo HUD 0x004177D0). Writers of 0x009CA0F4: event/game code 0x00422D30, 0x00437A70, 0x0045EDD0, 0x00480140,
0x0048B9E0, 0x0049B620.

## 7. Native port spec (implemented parts marked ✓)
```
per frame:
  F = 240 / tan(halfFov)                                   ✓ ShotFocalLength
  for p: gun record from input (GunCalibrate*)             ✓ (mode 6), pad cursor: TODO
  for p in task order (P1, P2):
     if demo: ApplyDemoShot(DemoAutoGun(rng, p, liveEnemies > 0))   ✓ (rng = game CRT rand, srand(1) at stage start)
     PlayerTrigger -> Shot: pending[p] = 1, ray[p] = MakeShotRay(x, y, F)   ✓ (decision + view ray)
  hit pass (uses targets registered last frame):
     for p with pending: entries = sphere tests (ShotHitsSphere) / node tests / mesh+level segment tests
     SortHitKeys(HitSortKey(viewZ)) -> deliver flags to entries[0]          ✓ sort, key, sphere; mesh: TODO
  actors: handle flag 8 (damage/score), register (IsShotTarget, view pos)   ✓ predicate
```
The game's rand stream is shared by everything (props, particles, enemies), so a bit-exact demo needs every rand
consumer of the stage ported in task order; the auto-gun itself is exact given the stream.

## 8. Verification (`shooting_test <game> build\scene_capture_actors.txt`)
- Unit: CRT rand seed-1 sequence; mode-6 calibration points; trigger/reload/demo/letterbox rules; radix order;
  F = 640.2079 equals the captured projection (m11·240, m00·320).
- Demo replay: 513/513 frames, 41 shots.
- Hit test vs the previous frame's task dumps (view pos +0x70, radius +0x124, flags +0x34):
  - all **7/7** captured enemy hits (rand 0x004093E2) have the ray inside the enemy body sphere (r = 10);
    **23** shot frames miss every body sphere and none hit; 10 pass the body sphere without a hit — consistent
    with the per-node second stage, which the capture cannot check (dumps capped at 0x400 bytes).
  - breakable props (no node stage): the nearest sphere hit predicts **1/1** prop reaction (+0x32C 0 → non-zero)
    with 0 unexplained reactions; at 3172 two props pass and the nearer key (465 vs 592) is the one that breaks.

## 9. Open
- Node hit spheres and the zone tables (0x004C7160 / 0x004C8350 / 0x004D0D84) — need full enemy dumps.
- 0x004AAA40 (segment vs collision mesh) and where level collision meshes come from (`Res_LoadCollision` 0x0048A310?).
- `Player_TriggerSpecial` 0x00414B90 weapon tables 0x00579EDD.. (cooldowns, burst, shotgun spread with rand).
- Tracer/flash records 0x009A2460 / 0x009A2960 / 0x009A2700 (ring of 6 per player, 0x009CA09C) — drawing not traced.
- Player-life loss (enemy attacks) is outside this pass.
