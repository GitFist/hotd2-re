# Stage 5: captures of stages 5 / 6, the native stage 5

**Result: every logic check is exact for the whole of stage 5**: `world_test --stage-start 56007` on
`scene_capture_auto_st5.txt` compares 14893 frames (56007..70899). Rand state / calls / sites, the task list, event
ops, live enemies, attackers, enemy hp / position, lock-on slots, sounds, game mode / sub-state, letterbox, rank, hit
shake, players, aim, gun record, continue timers and the players' point are exact on every frame. That covers the car
opening, the Judgment rematch, the riders, the apparitions, the Boss2 rematch, the stage-5 boss, the stage end and the
chain into stage 6. The in-run chain also works: stage 4 from 44107 into the stage-5 capture is exact for 55900..70899,
and stage 5 into the stage-6 capture is exact to 71452, where the first unported stage-6 object appears. No `WorldCarry`
field was added.

**CONFIRMED** = ported and exercised frame-exact; **CODE** = ported, not exercised.

## 1. Captures (main checkout `build\`)

One deterministic replay of `script_auto_full2.txt`, captured from 55900 to 97299 and then split by frame. The
replay's progress lines match `script_auto_full2_hook.log`.

| File | Frames | Contents |
|---|---|---|
| `scene_capture_auto_st5.txt` (843 MB) | 55900..70899 | end of stage 4, Stage_Start 56007, Boss2 rematch 62071, boss 0x0047C960 from 65718, stage end 70803 |
| `scene_capture_auto_st6.txt` (961 MB) | 70700..86999 | Stage_Start 70806, Boss3 heads, the final boss 76610..84086, start of the ending |
| `scene_capture_auto_st6_end.txt` (225 MB) | 86900..97299 | the ending, the switch to the title (mode 3) at 97200 |
| `scene_capture_auto_st5_st6_hook.log` | | hook log of the run |

Environment: the stage-4 one (stage4.md section 2), with these differences:
```
HOTD2_SCENECAP_FROM=55900 HOTD2_SCENECAP_FRAMES=97300 HOTD2_SCENECAP_TASKS_FROM=55900 HOTD2_SCENECAP_ALLOCS=1
HOTD2_SCENECAP_TASKS = the stage-4 list + 0x46C570,0x48F190,0x475B50,0x46EB20,0x4757F0,0x49CDF0,0x490CE0,0x49DEA0,
  0x46EBD0,0x433E90,0x46EDC0,0x433FE0,0x434100,0x46FCC0,0x47C960,0x47DC30,0x47EFA0,0x47F4A0,0x47E130,0x47E2B0,0x480700,
  0x480810,0x48F560,0x4755B0,0x453290,0x4729E0,0x46FA10,0x46EEB0,0x48F930,0x426C30,0x429D30,0x48FB40
```
The extra task functions are every one first seen in stage 5 or 6 in `scene_capture_auto2_k.txt`. That K-only run
ends at 79699, so task functions that first appear after it (in the ending) have K lines but no T dumps. The split
cuts the stream at the `F <n>` lines; the hook writes nothing stateful, so each file reads like a capture that started
at its own first frame.

## 2. Stage start and carry

`world_test game build\scene_capture_auto_st5.txt --script build\script_auto_full2.txt --stage-start 56007`.
The existing `WorldCarry` is enough. The stage-4 to stage-5 chain was checked with a new world_test option:
`--prefix` can now be combined with `--stage-start`. The run starts at the prefix capture's stage-start frame and is
compared from the main capture's first frame:
```
world_test game build\scene_capture_auto_st5.txt --script build\script_auto_full2.txt --stage-start 44107 --prefix build\scene_capture_auto_st4.txt
world_test game build\scene_capture_auto_st6.txt --script build\script_auto_full2.txt --stage-start 56007 --prefix build\scene_capture_auto_st5.txt
```

## 3. Ported

| Piece | Original | File | Status |
|---|---|---|---|
| actor 0x2B lights (states 0 / 1 / 2, light slots 0x00480A60) | 0x00438060 / 0x004380B0 / 0x00438230 / 0x004383C0 | world_st5_actors.cpp | CONFIRMED |
| actor 0x2D variant 0 (skeleton 0x4C pose, node draw) | 0x00426A70 / 0x00428CD0 / 0x00428F70 / 0x00429040 | world_st5_actors.cpp | CONFIRMED |
| skeleton-0x4C attachments (18 entries, pairs 0x004EDA50) | 0x004198B0 / 0x00419B40 | world_st5_actors.cpp (`AttachDraw4C`) | CONFIRMED |
| the players' car (actor 0x26 state 4) + wreck | 0x0048F190 / 0x00475A50 / 0x00475B50 | world_st5_car.cpp | CONFIRMED |
| komono 51 / 53 / 54 / 65 | 0x0046EB20 / 0x0046EBD0 / 0x0046EDC0 / 0x00464360 + 0x0046FCC0 | world_st5_car.cpp, world_st5_actors.cpp | CONFIRMED |
| actor 0x44 state 13 rising gate (mesh target) | 0x00473640 / 0x004757F0 | world_st5_actors.cpp | CONFIRMED |
| actor 0x33 states 7 / 8 / 9 | 0x00433E90 / 0x00433FE0 / 0x00434100 | world_st5_actors.cpp | CONFIRMED |
| riding enemies (record flag 8), behaviours 32 / 10 | 0x0045E770 / 0x0045E830 / 0x00455490 | world_st5_enemy.cpp | CONFIRMED |
| Judgment rematch: rider variant 2 intro, mount variant 1 intro and limits, variant-2 death position | 0x0049CE10 / 0x00490D00 / 0x00490150 / 0x0049C910 | world_st1b_boss.cpp (additive) | CONFIRMED |
| actor 0x31 skeleton 0x18: setup, node draw, attachments, behaviours 27 / 5 | 0x00449620 / 0x00449F90 / 0x00419B40 / 0x0044EF90 / 0x0044AB70 | world_st2_actor31.cpp (additive) | CONFIRMED |
| the stage-5 boss (actor 0x32): 12 states, shots, projectiles (4 states), trail / smoke / glow / beam / sparks / explosion tasks | 0x0047F5F0 .. 0x00480810 | world_st5_boss.cpp | CONFIRMED (the state-3 sub-1 path is CODE) |
| sky types 8 / 10 / 11 (view-space panels) | 0x00413620 / 0x00413500 | world_st5_actors.cpp (`SkyPanels`) | CONFIRMED (draws) |
| komono 18 prop-skeleton draw | 0x0040DD90 | world_st2c_boss.cpp | CONFIRMED (draws) |
| name-card model requests (0x7ED / 0x7EE not resident outside stage 1) | 0x0041D5D0 in 0x00437AC0 | world_st1b_boss.cpp | CONFIRMED (draws) |
| Boss2 leg nodes in the original draw order (13..15, 10..12) | 0x00476AD0 | world_st2c_boss.cpp | draws |

The boss health bar (`SpawnBossBar`) and name cards (`SpawnNameCard`) go through the existing 2D paths
(`WorldHud::sprites`). The Boss2 rematch needed nothing new: the stage-2 port was already exact.

Shared-code edits are small and additive:
- `world_internal.h`: the stage-5 block.
- `world.cpp`: the `SpawnStage5` hook and `SkyPanels`.
- `world_enemy.cpp`: the attachment hooks for kinds 0x4C / 0x18, the attachment cap raised from 16 to 32, the
  ride-flag hooks and the behaviour chain.
- `world_actors.cpp`: actor 0x22 variant 2 sent to `SpawnBoss22`.
- `world_st1b_boss.cpp`, `world_st2_actor31.cpp` and `world_st2c_boss.cpp`: as in the table above.
- `tests/world_test.cpp`: `--prefix` together with `--stage-start`.
- CMakeLists.

For the stage-6 agent: the final boss is actor 0x2D variant 1 (0x00426A70 -> 0x00426C30, also skeleton 0x4C). Derive
it from `Kind4CTask` (world_st5.h: alpha +0x1370) so `AttachDraw4C` and the node conventions apply. Light slots are
in `St5State`.

## 4. Progress (first divergence of the rand state / task list)

| Step | Frame |
|---|---|
| start on the stage-4 capture's tail | 56009 (light task) |
| lights, actor 0x2D, attachments | 56483 |
| car, wreck, komono 51 | 58074 |
| gate (actor 0x44 state 13), Judgment rematch | 61209 |
| komono 53 | 61391 |
| actor 0x33 state 7, riders, behaviour 32 | 61851 |
| actor 0x31 skeleton 0x18 (behaviour 27, draw, setup) | 61944 |
| behaviour 5, komono 54 | **65556** (through the Boss2 rematch) |
| actor 0x33 states 8 / 9, komono 65 | 65718 (the boss) |
| the stage-5 boss | **exact to 70899** |

## 5. Final numbers

`world_test --stage-start 56007` on the stage-5 capture (14893 frames) gives:
- Every logic check exact.
- Camera view 14669 / 14893. The difference is 1-2 angle units at 60252..60372 while the shooting camera tracks the
  Judgment rider's body point (the known node-matrix ulp issue, stage3.md section 5); no logic is affected.
- Draws 12845 / 14706 frames. The remaining differences are rounding of 0.002..0.03, mostly from scaled models; the
  effects 0x00407A70 are off in the same camera window. Two real gaps remain: Boss2's floor shadow is missing in part
  of the rematch, and part of the Judgment mount's draws differ (0x00490CE0, 1653 draws).

Regressions after merging main 2df98a0 (stage 4 complete), all unchanged in logic:
- auto_st1 901..14799, exact.
- auto_st2 (`--prefix`) 14400..29799, exact.
- auto_st3 (`--prefix`) to 45612 plus the game over to 46375, exact.
- auto_st4 `--stage-start 44107` 44107..56099, exact, including the stage-5 start: 0x2B / 0x2D are now in.
- play1full 2436, 2p 5099, stage-3 demo 814.

Draws improved: auto_st1 11809 / 12787 (was 11512), auto_st2 11213 / 15059 (was 8342), auto_st4 10765 / 11809 (was
10474).

## 6. Not ported / open (World::Notes on the stage-5 run)

- env ops 25 / 35 / 36 / 39 (interpolator 0x0040AD90).
- The title-card light setup (renderer).
- Light parameters of the 0x2B lights and the colour / light calls of the bosses (0x0041DC10, 0x004AA0A0: renderer).
- Uv scroll of the sky panels (0x00413560).
- Skin deformation (0x00419980).
- The final boss (actor 0x2D variant 1): stage 6.
- Mount variant 2 (0x00490E90: the training mode?).
- The stage-5 boss state 3 sub 1 (unreachable: sub 0 always leaves to state 4).
- Boss5 in game types 1 / 3.
- Boss2's floor shadow and leg IK (0x00476AD0).
- The 0x009A2BAC frame counter, approximated with the world frame (komono-53 fire models: draw only).
