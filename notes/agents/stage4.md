# Stage 4: auto play through the whole game, stage-4 capture, mid-game stage-4 start

## 1. Auto play: the whole game (`build\script_auto_full2.txt`)

The auto player (`src/hook/auto_play.cpp`, autoplay.md) now finishes the game: stages 1..6, all six bosses, the
ending and the return to the title (game mode 3 at 97200), with **no continue** (lives 5 -> 3 at the final boss).
The old run (`script_auto_full.txt`, kept unchanged with its captures) lost every life at the stage-3 boss.

### Why the stage-3 boss killed the old auto player
- The heads (task `Boss3_HeadTask` 0x004209B0) use skeleton types 0x49 / 0x48 (head 2). `ReadTargets` only walked
  skeleton types < 64, so the heads were aimed at as plain spheres (their object sphere, r 45): the shot landed on
  whatever neck node was nearest, which only sparks (0x004247D0). The capture shows every head at full hp (45) 1200
  frames into the fight. 0x004E0430 has 86 skeleton pointers; types up to 85 are now walked.
- A head only takes damage on its head node `block+0x598` (block = obj+0x1390) while the jaw (nodes block+0x599 /
  +0x59A: node record angle +0x0C plus block+0xD8[node]) is open by more than 0x1700 and block+0x5A4 (u16) is not in
  0x1701..0xBFFF; never head 2 unless 0x007DC6F1 == 2 (the stage-6 variant). One damaging hit in the attack state 5
  interrupts the bite (state 6) before the hit tick block+0x763C (Player_TakeHit 0x00415300 there).
- Projectiles 0x00424900 only exist in game type 3 (0x009CA08C); 0x004246F0 is the spark task. Nothing else hits.

### New target rules (all read-only; `HOTD2_INPUT_AUTO_LEGACY=1` restores the old rules)
| Target | Rule |
|---|---|
| any skeleton type 64..85 | node walk (was sphere); walk depth 64 (the big head 0x48 is a 27-node chain) |
| Boss3 heads 0x004209B0 | only the head node, only while damageable (jaw / neck rule above); attacking heads (state 5) first, soonest bite first; no other node / sphere may be first along the ray (aim at the node centre or +-0.5 r) |
| Boss2 0x00476150 (stage 2 and the stage-5 rematch) | node slot 1 only (0x00476270: other nodes spark); not while +0x34 & 0x100 |
| stage-5 boss 0x0047C960 | node slots 4, 6, 11, 13 only (0x0047CE10); not while +0x34 & 0x4000100 |
| final boss 0x00426C30 | node slot 1, sub-state +0x1312 in 1..5, no +0x34 & 0x40000100, and the ray within +0x1374 of node 1's matrix (obj+0x2C4) x (2.3121, 0.1097, 0) (0x00428DA0 / 0x00428F00); one damaging hit stops its charge |
| final boss orbs 0x00429D30 | shot first, nearest first (they hit the player) |
| weak-node targets | never put on the 90-frame "absorbs shots" block list |

`HOTD2_INPUT_AUTO_DEBUG=1` logs boss-head state every 4 frames and every boss decision (shot / why not).
`HOTD2_INPUT_AUTO_FROM=<frame>` with `HOTD2_INPUT_SCRIPT`: replay the script before that boot frame, automatic input
from it (input_auto.txt then holds the complete script). Every 600 frames the hook logs a `progress` line.

### How the script was made
Successive runs, each replaying the previous script up to its switch frame (all decisions are recorded, so the
result is one plain script): old `script_auto_full.txt` to 38081, auto from 38082 (stage-3 boss .. stage 5), 62300
(stage-5 Boss2 rematch), 65400 (stage-5 boss), 74900 (stage-6 heads), 76700 (final boss .. end). Hook log of the
last run: `build\script_auto_full2_hook.log`.

### Timeline (boot frames; stage = 0x009A1A08, scene 0x009A2BC0, route record 0x009A5C30)
| | |
|---|---|
| stage 1 / 2 / 3 | as script_auto_full.txt (Stage_Start 901 / 14659 / 29736), identical input to 38081 |
| stage-3 boss | scene 0x0B 38082; one life lost (41193); boss down ~42500 (+1500) |
| stage 4 | Stage_Start **44107** (the sub-state-1 frame; 0x009A1A08 = 3 and sub-state 1 are set at 44106), scenes 1 45355, 2 46413, boss scene 0x17 48032 (boss task 0x004919D0 from 48128), stage end 56004 |
| stage 5 | Stage_Start 56007 (stage 4 set at 56006), scenes 1 57833, 2 60938, 3 62071 (Boss2 rematch 0x00476150), 4 63248, 5 63976, 7 65136 (boss 0x0047C960), stage end 70803 |
| stage 6 | Stage_Start 70806 (stage 5 set at 70805), scenes 1 73255, 2 74409 (Boss3 heads, variant 2), 3 75737, 4 76500, 12 76610 (final boss 0x00426C30 + orbs 0x00429D30) |
| end | final boss down at 84086 (the script's last shot; no input after 84087), ending (event mode 9) by 87000, title (game mode 3) 97200 |
| lives | 3 at the start, 5 after stage 1; lost: 41193 (stage-3 boss), 83438 / 84049 (final boss); no continue |

### Determinism
A second auto run (`HOTD2_INPUT_AUTO_FROM=76700`, same prefix) produced exactly the script's lines, and its K/Z/E/U/
Y/N lines over 76000..79699 are identical to a replay of `script_auto_full2.txt` (`build\scene_capture_auto2_det.txt`
vs `build\scene_capture_auto2_k.txt`, 3700 frames); W differs only in heap pointers (player +0x74..+0x7B, route
record +4..+7).

## 2. Captures (main checkout `build\`)

| File | |
|---|---|
| `script_auto_full2.txt` | the whole game, 20394 input lines (boot to the final boss's last shot 84087; the ending needs no input) |
| `script_auto_full2_hook.log` | hook log of the last auto run (progress lines every 600 frames, life losses, boss decisions with `HOTD2_INPUT_AUTO_DEBUG`) |
| `scene_capture_auto_st4.txt` (630 MB) | **stage 4**, frames 44000..56099 (end of stage 3, the stage-4 start 44107, the boss, stage end 56004, stage-5 start 56007), full instrumentation, env below. Hook log `scene_capture_auto_st4_hook.log` |
| `scene_capture_auto_st4_allocs.txt` | the same stretch, K / Y / U / A lines only (A = every task creation, new `HOTD2_SCENECAP_ALLOCS=1`) + shots `game\shot_{46700,47200,48100,48600,49500,51000,53000}.bmp` (left in game\) |
| `scene_capture_auto2_k.txt` (50 MB) | cheap run, frames 43500..79699 (stages 4, 5, 6 up to the final boss): K / Z / N / Y / E / U and W (game mode, stage, scene, sub-state, P1, seed counter, route record). Use it to list the task functions of stages 5 / 6 and their timeline |
| `scene_capture_auto2_det.txt` | determinism run (auto from 76700), frames 76000..79699 |
| `wt_auto_st4.txt` | world_test output on the stage-4 capture at the end of this work |

Stage-4 capture environment (replay of the new script):
```
HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_INPUT_SCRIPT=<main>\build\script_auto_full2.txt HOTD2_SCENECAP=1
HOTD2_SCENECAP_FROM=44000 HOTD2_SCENECAP_FRAMES=56100 HOTD2_SCENECAP_TASKS_FROM=44000 HOTD2_SCENECAP_EVENTS=1
HOTD2_SCENECAP_SPRITES=1 HOTD2_SCENECAP_SOUNDS=1 HOTD2_SCENECAP_RANDLOG=1
HOTD2_SCENECAP_TASKS=0x413E90,0x4842A0,0x435F40,0x435AA0,0x4342E0,0x433A10,0x451860,0x4329D0,0x474F30,0x4521B0,0x46FE00,0x451980,0x432AF0,0x484D40,0x451D80,0x451DF0,0x473CF0,0x465FB0,0x4724A0,0x4522A0,0x4533F0,0x452DA0,0x431340,0x449910,0x46B320,0x40A230,0x46B5F0,0x4460C0,0x448210,0x430BD0,0x448A80,0x448CE0,0x48A920,0x48A3E0,0x448DF0,0x4831E0,0x482EE0,0x471FA0,0x468180,0x467E50,0x442820,0x444D80,0x444EE0,0x469380,0x46F840,0x46F580,0x465950,0x40E3B0,0x468F00,0x43C440,0x43F010,0x482F60,0x46A0F0,0x43DA20,0x43E540,0x43E650,0x4694A0,0x471CC0,0x467080,0x467290,0x46E3A0,0x472AA0,0x4331D0,0x433B00,0x449020,0x456880,0x438730,0x439F10,0x439E00,0x43A000,0x476150,0x468E50,0x437AC0,0x484EA0,0x43FA60,0x435C80,0x43FBC0,0x434EF0,0x435760,0x48EAD0,0x4083D0,0x46CEA0,0x448800,0x43FE90,0x48B140,0x45CD90,0x4753F0,0x42E9D0,0x42F660,0x42F930,0x42ED50,0x41FDB0,0x4200F0,0x423050,0x420550,0x41FE30,0x420180,0x4209B0,0x420820,0x4758E0,0x424900,0x4246F0,0x414200,0x414280,0x413DE0,0x414420,0x4144C0,0x460E60,0x435E90,0x42E230,0x45A4F0,0x4919D0,0x492210,0x443200,0x443B90,0x444F70,0x4383C0,0x428CD0,0x48DFE0,0x46A580,0x46AD20,0x46A1C0
HOTD2_SCENECAP_WATCH=0x9C8E98:2,0x9A1A08:2,0x9A2BC0:2,0x9A2BB0:2,0x9C90A4:4,0x9A5C60:304,0x9A5D90:304,0x9C904A:2,0x9C7006:2,0x9C8E96:2,0x9C6F00:4,0x9C6F20:4,0x9C6F2C:4,0x9A5EC0:128,0x9A6000:420,0x9A61A4:420,0x9A1A10:4,0x9A2C8C:4,0x9C8E00:4,0x9CA0F4:4,0x9C8FC0:80,0x9C71E0:24,0x9CA0E4:4,0x9A5C40:4,0x9A32A0:4,0x9A2BB8:4,0x9C8E60:16,0x9C8E70:16,0x9A5900:4,0x9A5C44:4,0x9C9028:4,0x9C8E94:2,0x9A5C30:16,0x9C8E80:8,0x9C8A7C:4,0x9A2C30:4,0x9A3794:4,0x9C8E08:8,0x9A5920:160,0x9C7000:4,0x7DCCCC:4,0x9CA0E8:4,0x9A3540:64,0x9C8E50:4,0x9C8E58:4,0x9A2224:4,0x9C71C4:4,0x7DCDB8:12,0x9C88A4:4,0x9C7200:256,0x9C7010:16,0x9A2460:576,0x9A2960:576,0x9CA09C:4,0x9A26C0:40,0x7DC6F0:288,0x9A26A0:4,0x7DD0A4:4,0x9CA08C:4,0x9C90C0:4,0x9A37E4:4,0x9C8E10:4,0x9CA0EA:2,0x9C72E0:4,0x9A2D78:4,0x811200:64,0x9A2C20:16,0x7DCBB0:4,0x9A2C88:4
```
(task list = the stage-3 list of stage3.md + every task function first seen in stage 4 per the K-only run; watch list = the
stage-3 list). For stage 5 use FROM / TASKS_FROM = 55900 and FRAMES = 70900 (Stage_Start 56007); for stage 6 FROM = 70700,
FRAMES = 87100 (Stage_Start 70806), plus the stage's new task functions from `scene_capture_auto2_k.txt`.

Task functions first seen in stage 4 (frame first seen, task-frames): `0x42E230` 45219 (actor 0x46 variant 0),
`0x45A4F0` 47761 (thrown weapon, main's world_thrown.cpp), `0x4919D0` 48128 (boss, ctor 0x004917E0), `0x492210`
50641 (boss effect, 0x004920C0), `0x443200` 51621 / `0x443B90` 51669 / `0x444F70` 51717 (carried / thrown / broken
object family of 0x00442820), `0x4383C0` 56008, `0x428CD0` 56041 (stage-5 start), `0x48F190` / `0x475B50` /
`0x46EB20` 56482 (stage 5). Also running in stage 4 (seen before): 0x4533F0 enemies, 0x449910 actor 0x31 (skeleton
0x17), 0x46C570 komono 40, 0x473CF0, 0x465FB0, 0x48A920 / 0x48B140 civilians, 0x43FE90 vehicle (from 48123, motions
2..4 not ported), 0x437AC0 / 0x435C80 / 0x435E90 boss name card / bar / sound repeat.

## 3. Stage-4 start (`world_test ... --stage-start 44107`)

`world_test game build\scene_capture_auto_st4.txt --script build\script_auto_full2.txt --stage-start 44107`. The
existing `WorldCarry` is enough: the start frame is exact in every check and the run stays exact until the first
unported object; **no field had to be added** (ApplyCarry / MakeCarry unchanged). Note: the K line of 44106 still lists
stage-3 leftovers (actor 0x25 71126, 0x435760 x4); they are gone at 44107 (Stage_Start's Mem_Init), so 44106 is not a
valid start frame. The in-run chain (`--prefix`) was not run for stage 4: the native world does not reach the
stage-3 end with script_auto_full2 (its stage-3 boss fight differs from script_auto_full.txt from 38082 on and has
not been captured; the stage-3 boss capture of stage3.md is the old script's).

## 4. Ported (`src/game/world_st4*.cpp` + small shared edits)

| Piece | Original | Status |
|---|---|---|
| enemy behaviour 30: wait / crouch / jump to P floats 1..3 (generic jump motion 0x0044D860 / table 0x0044DA60 / fit 0x0044D5F0 / parabola 0x0044DD00) | 0x00458A70 | CONFIRMED |
| enemy behaviour 17: hold motion P1 for P2 frames, then P byte 3 (15: walk length (float)(uint)P3) | 0x004574D0 | CONFIRMED |
| Enemy_SetupKind case 0xE: node 5 collision-mesh weapon | 0x00452FD0 | CONFIRMED (mesh shot: see 5) |
| hit effect "head model 0x1DC2": flying head 0x1DC1 | 0x00454050 -> 0x0040A130 | CONFIRMED |
| actor 0x46 variant 0: one flyer on a quadratic B-spline (0x00589944, weights 0x0042DFD0), dive at the players, shot fall | 0x0042D9C0 / 0x0042E230 | CONFIRMED |
| actor 0x31 behaviour 18: walk P float 1 units, then 7 | 0x0044E2A0 | CONFIRMED |
| shots on enemy node slots 16..31 (skeleton 0x17 slot 23): Enemy_ApplyShot slot clamp 15 -> 31, 32 node copies | 0x004092F0 / 0x00409430 | CONFIRMED |
| komono kind 75 (game type 0: event flag 0x14, then ends) | 0x004710C0 | CONFIRMED |
| auto play, scene capture A lines | (hook) | |

Shared edits: `EnemyTask::jump1360 / jumpTab` (world_internal.h), `World::Impl` stage-4 block (EnemyBehaviourSt4,
EnemySetupKind14, SpawnStage4, SpawnBird0, BirdSpawnBody, BirdShotFx), one-line hooks in world_enemy.cpp (behaviour
chain, SetupKind 0xE, flying head 0x1DC1, slot clamp), world.cpp (SpawnStage4 first in TaskSpawn), world_st3_birds.cpp
(variant 0 -> SpawnBird0 and two small exports), world_st2_actor31.cpp (Beh18 + case), CMakeLists.txt.

## 5. Results and the next divergence

Progress of the first task-list divergence: 44938 (behaviour 30) -> 44976 (head 0x1DC1) -> 45220 (birds variant 0)
-> 45261 / 45952 (bird fall speed: 0x004C4C98 is a double; node slot clamp) -> 46520 (actor 0x31 behaviour 18) ->
46678 (behaviour 17) -> 46896 (komono 75, found with the A lines) -> **47355**.

`wt_auto_st4.txt` (11993 frames 44107..56099): rand / tasks / events / players / enemies exact **44107..47354**
(task list 3326, rand 3775 frames); camera view to 47763; draws differ from 44960 only by the unported komono-40
variant-9 extra draw (0x0046C570).

**Next divergence 47355**: P1's shot hits the kind-14 enemy 72609's weapon (node 5 collision mesh, material 0x35:
spark 0x1216A9, no damage) in the original; the native mesh test misses by about 2 units and the shot kills it
through the head. Native and original node-5 world matrices (camera matrix x node model-view, from the T dump of
47354) are identical, so the difference is the matrix the original uses at hit time: Hit_TestNodeMesh 0x004048A0
multiplies the node's **model-view of the previous frame** (+0x28, drawn with frame 47354's view) by the **current**
camera matrix 0x009A6040 (frame 47355, after the camera task). `ShotNodeMesh` (world_st2c_enemy.cpp) uses the node's
true world matrix. Fix: keep the draw-time model-view (or the previous view) per node and build the mesh matrix as
modelView(prev) x cam.inverse(now) (the same applies to Hit_TestObjectMesh's +0x150 if the camera moves); then rerun.

Unported paths reached (world_test notes; most after 47355): actor type 25 (0x19, placeholder task), vehicle motions
2 / 3 / 4 (0x00440190 table; the boss stretch from 48123), enemy attachment draw of kind 23 (0x00419B40, draw only),
komono 40 variant 9 extra draw (0x0046C570), civilian head look modes 1 / 6 (0x0048D1F0), actor 0x31 landing dust
angles (0x00401870), env ops 25 / 35 / 36 (0x0040AD90), title card light setup. The boss (actor ctor 0x004917E0 ->
task 0x004919D0, state table 0x00597298 with 20 states, shots 0x00491B40: damage on node 2 or on a level material
0x3D hit; draw / parts 0x004928D0, walls 0x00493330) and the carried-object family 0x00443200 / 0x00443B90 /
0x00444F70 are not ported. Frontend check: `frontend_test game build\scene_capture_attract_loop.txt --resync` (demo
3 = stage 4) was not rerun.
