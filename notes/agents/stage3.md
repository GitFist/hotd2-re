# Stage 3: capture, mid-game stage start, stage-3 objects

`game::World` starts stage 3 of the auto-play run (`build\script_auto_full.txt`) at its Stage_Start frame **29736**
with the existing `WorldCarry` (no new carry fields) and replays it with the rand stream, the task list and the event
ops **exact through 38081**, i.e. the whole stage up to the boss scene (opening cutscene, the boat ride with its
fish, crates, wading zombies, the enemy boat carrying a civilian and enemies, the throwers, the bird flocks). The next
divergence is the stage-3 boss (actor 0x45, scene 0x0B, 38082), not ported.

**CONFIRMED** = code read + frame-exact against the capture; **CODE** = read and ported, not exercised.

## 1. Capture

`build\scene_capture_auto_st3.txt` (1.12 GB, main checkout): frames 29700..46999 = the end of stage 2, stage 3 as the
auto player plays it (it dies at the boss), the five continues, the game over (45612) and the route map back to the
title. Hook log `build\scene_capture_auto_st3_hook.log`. Cheap K-only run (task functions, timeline):
`build\scene_capture_auto_st3_k.txt` (frames 29500..45799, no draws). Deterministic: two full captures gave the same
world_test results.

```
HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_SCENECAP=1 HOTD2_SCENECAP_FRAMES=47000 HOTD2_SCENECAP_FROM=29700
HOTD2_SCENECAP_TASKS_FROM=29700 HOTD2_SCENECAP_EVENTS=1 HOTD2_SCENECAP_SPRITES=1 HOTD2_SCENECAP_SOUNDS=1
HOTD2_SCENECAP_RANDLOG=1 HOTD2_INPUT_SCRIPT=<main>\build\script_auto_full.txt
HOTD2_SCENECAP_TASKS = the stage-2 list (stage2.md) + 0x48EAD0,0x4083D0,0x46CEA0,0x448800,0x43FE90,0x48B140,0x45CD90,
  0x4753F0,0x42E9D0,0x42F660,0x42F930,0x42ED50,0x41FDB0,0x4200F0,0x423050,0x420550,0x41FE30,0x420180,0x4209B0,0x420820,
  0x4758E0,0x424900,0x4246F0,0x414200,0x414280,0x413DE0,0x414420,0x4144C0,0x460E60,0x435E90
HOTD2_SCENECAP_WATCH = the stage-2 list + 0x7DC6F0:288 (boss block),0x9A26A0:4,0x7DD0A4:4,0x9CA08C:4,0x9C90C0:4,
  0x9A37E4:4,0x9C8E10:4,0x9CA0EA:2,0x9C72E0:4,0x9A2D78:4,0x811200:64,0x9A2C20:16,0x7DCBB0:4,0x9A2C88:4
```
The watch list needed a bigger buffer: `HOTD2_SCENECAP_WATCH` is now read into 4096 characters.

Model ids: the hook mapped model pointers to ids through a cache; a stage's models loaded at the addresses of the
previous stage's freed models were logged with the old ids. `ModelId` (scene_capture.cpp) now prefers a slot whose
loaded flags (0x009A66AC & 0x8001) are set. Some stage-2 slots stay flagged loaded with the same pointer, so a few
section models (6184 / 6191 as 5042 / 5040 from 30303) are still logged with stage-2 ids: world_test's "stage section
draws" check fails on those frames (15533 / 16865 match).

Stage-3 timeline (scene 0x009A2BC0 / route record): sub-state 5 at 29733, 6 at 29734, 0x009A1A08 = 2 at 29735,
**Stage_Start 29736** (the sub-state-1 frame; stage2.md's "29735" is the frame that sets sub-state 1), opening
cutscene (actor 0x25) to 31163, title card 30123, boat ride from 31164 (scene 7, then 8 at 33737, 2 at 35472), enemy
boat 33182..35000, flocks 36250 / 36359, boss scene 0x0B from 38082 (boss tasks 0x41FDB0 / 0x4200F0 / 0x423050 /
0x420550, heads 0x4209B0, projectiles 0x4246F0), first life lost 39362, continues at 40301 / 41657 / 42613 / 43655 /
44557, sub-states 11 / 12 at 45610 / 45611, game mode 7 at 45612.

## 2. Stage start

`world_test game build\scene_capture_auto_st3.txt --script build\script_auto_full.txt --stage-start 29736`. The carry
of stage2.md is enough (event mode 2, scene 7, P1 state 5 -> 2, lives 5, score 39630, rank 15, route record, komono-40
pieces, tracer rolls). The stage-2 capture also contains 29736..29799: exact there with the same carry. No field had
to be added to `WorldCarry` / `MakeCarry`. The new per-stage globals (water level 0x007DCBB0, fish attack points
0x009A2C20, flock table 0x007DC918, vehicle 0x009A2C88) are set inside stage 3 itself. Continue / game-over crossing:
the native run reaches the stage end (the boss is missing), so the continue path of the capture is not compared yet.

## 3. What was ported (`src/game/world_st3*.cpp`)

| Piece | Original | Status |
|---|---|---|
| actor 0x25 draw extras P s16 6 = 1..4 (moored boats, the boat on a path with bow waves, model 0x10DF) | 0x00484FF0 | CONFIRMED (3) |
| the players' boat, actor 0x26 state 2 (collision-mesh target, paths 0x156..0x15D / 0x199, yaw from the camera at fixed ticks) | 0x0048EAD0 | CONFIRMED |
| komono kind 1 water surfaces (the vertex wave is left to the renderer) | 0x00462F70 / 0x0046E3A0 | CONFIRMED |
| enemy wakes | 0x00408740 / 0x00408770 / 0x004083D0 | CONFIRMED |
| water splash rings on death splashes / landings / variant-6 attacks; the bounce test reads the hook's floor material | 0x004567C0 via 0x004569B0 / 0x00456B70 / 0x00456C50 / 0x00456D10 | CONFIRMED |
| death jump variants 4..6 | 0x004550E0 | CONFIRMED (5) |
| komono kind 43 floating crates / barrels (bob, tumble, break motion, life item) | 0x00461CF0 case 0x2B / 0x0046CEA0 | CONFIRMED |
| aim effect of shot props, breakable 0x19E8 counts a hit | 0x00465860 / 0x00465950, 0x004650F0(flags, 0) | CONFIRMED |
| Player_AddLife | 0x00415630 | CODE |
| enemy behaviour 24 (glide in, timed attacks) | 0x00457CE0 | CONFIRMED |
| enemy behaviour 38 (motion list), leave 0x0045BA40, body on screen 0x0045CA60 | 0x0045B7B0 | CONFIRMED |
| actor 0x20 variant 2 box walk | 0x00449020 | CONFIRMED |
| actor 0x51 fish (swim, bob, jump at the players, fall back, shot in the air / dead) + splash / ring / flash tasks | 0x00438540 .. 0x0043A000 | CONFIRMED |
| actor 0x13 vehicle: motion 6 (paths 0x161 / 0x160, bow waves, explosion, sinking), explosion task | 0x0043FE10 / 0x0043FE90 / 0x004413C0 / 0x00440770 / 0x0043FCA0 / 0x0043FBC0 | CONFIRMED (motion 6) |
| riders in the vehicle's frame: actor 0x18 enemies (0x0045CD60 / 0x0045CD90, behaviour 46 0x0045CFC0), civilians 0x0048B140, back to world 0x0045D920 | | CONFIRMED |
| actor 0x44 state 11 (shaking / rising model) | 0x00473410 / 0x004753F0 | CONFIRMED |
| thrower start from behaviour 1 (variant 8 facing the camera with a weapon) | 0x00458E10 | CONFIRMED |
| actor 0x46 flocks: variant 1 (25 fly away), variant 2 (circle, dive at the players), companions, splash, flash | 0x0042D9C0 / 0x0042E9D0 / 0x0042ED50 / 0x0042F660 / 0x0042F930 / 0x00430BD0 | CONFIRMED |

Shared changes (small, additive): `EnemyTask::vehicle / framed / frame` (AnimDraw and the floor shadow start from the
vehicle's matrix), `World::Impl` stage-3 members (world_internal.h "stage 3" block), one-line hooks: `SpawnStage3`
(TaskSpawn), `EnemyBehaviourSt3` (EnemyBehaviour), `SpawnVehicleEnemy` (SpawnRecordEnemy), `CivOnVehicle`
(TaskCivilian), `Actor25Extra` (Actor25Draw), `Actor20BoxWalk`, `SpawnWake`, `SpawnSplashRing` / `MatrixToAnglesYAxis`
made non-static, `EnemyThrowReady`, the breakable "effect 0x00465860" event. Fixes found on the way:
- rails 6 / 7 (0x0040C790 / 0x0040C8B0) set the players' angles; the look rail start 0x0040C4C0 sets the players'
  point / angles from context 0 (event_runner.cpp; main's stage-1 work has the same rail lines).
- bonus figure item 0x004722A0: post-root is Anim_InitEnemy's no-op 0x00420810, not the enemy floor snap.
- actors that take a lock slot directly (0x00408E80) must read the table task's slot (+0x120) before releasing it.
- world_test: `--enemy` also traces civilians, `--view-limit N`, stage-section mismatches print both lists with
  `--verbose`, the rand-site check names the first mismatching call.

Branch note: this branch also contains the stage-2 agent's branch (merged twice: bats, thrower, carried objects,
items, komono, 0x00401C50 order) and main 9d17b60 (stage 1, ChainStage). Reconciliation done here: the stage-2 bat
global renamed `batAttacker43` (main's stage-1 bats use 0x007DCBD4), one actor-31 `Beh1` (main's), behaviour 33 goes
to the stage-2 full port first (main's sub-states 0..2 version only when that returns false), item drops try
`DropItemSt1b` then `DropItemSt2`, the 0x00465860 event keeps the hit (0x004650F0(flags, 0)) with this branch's aim fx.

## 4. Results

`world_test game build\scene_capture_auto_st3.txt --script build\script_auto_full.txt --stage-start 29736`
(17264 frames compared, 29736..46999):

| Check | First divergence | Frames matching |
|---|---|---|
| rand state Z / rand calls Y / sites | 38082 (boss) | 8346 / 17068 / 17068 |
| task list K | 38082 (boss: actor 0x45) | 8346 |
| event ops E | 39204 | 10855 |
| live enemies / attackers | 38470 | 8734 |
| players | 39348 | 9614 |
| camera view | 32523 (1-2 angle units, see 5) | 9408 |
| draws | 32427 (2.0e-3 civilian rounding) | 5125 / 16865 |

Progress (first rand divergence): start 31492 -> wakes / splashes 31601 -> komono 43 31773 -> behaviour 24 32146 ->
merge (bats) 32587 -> civilian alignment (cherry-pick) 32835 -> behaviour 38 / 0x19E8 32919 -> fish 33239 -> vehicle
34976 -> 0x44 st 11 35315 -> 0x00458E10 36250 -> flocks 36658 -> lock slots **38082**.

Regressions (same numbers as main 9d17b60): play2scene1 rand/tasks 79 / 79, draws 77 / 77; play1full 2436 logic, draws
2226 / 2251; 2p 5099 logic, draws 1996 / 2018; stage-1 demo 814 logic, draws 805 / 814; auto_st1 901..14799 exact
(13899); auto_st2 `--stage-start 14659`: rand to 20776, tasks to 19543 (main: 17272 / 17260; the merged stage-2 branch).

## 5. Not ported / open

- The boss (actor 0x45: 0x0041FC00 -> 0x0041FDB0 / 0x004200F0 / 0x0041FE30 / 0x00420180 / 0x00420360, block
  0x007DC6F0.., heads 0x004209B0 (500 lines), 0x00423050 / 0x00420550, projectiles 0x00424900 / 0x004246F0, 0x004758E0,
  health bar / voice tasks 0x00435C80 / 0x00435E90), the continues and the game over that follow in the capture.
- Civilian motion-change alignment (0x0048C310 flag 0x20000, main's port) is one ulp off at 32426 (node 1's model-view
  through the camera matrix is not reproduced bit-exactly); the camera tracks that civilian (lock slot 2) and is 1-2
  angle units off on and off from 32523 (no logic divergence results before 38082).
- Lock-on slots differ from 36360 (a released slot) without changing the logic before the boss.
- Vehicle motions other than 6 (0x005926A8 table entries 0..5, 7, 8), vehicle removal without sinking (riders keep a
  stale pointer: noted), the look rail's sidestep (0x0040C5C0, P2 button bit 1), komono 43 / fish game-type-1 paths,
  actor 0x46 variant 0 (0x0042E230), thrown weapons (0x0045A240 / 0x0045A4F0: in the stage-2 branch's behaviour 33).
- Renderer-side state: komono-1 vertex waves, 0x004AA0A0 colour calls, blood pool yaw words of fish (stack garbage).
- Auto play at the boss: not improved (the script and captures are unchanged).
