# Stage 2: capture, mid-game stage start, stage-2 objects

`game::World` can now start a later stage of a game (mode 6 sub-state 1) from the state a capture shows, and replays
stage 2 of the auto-play run (`build\script_auto_full.txt`) frame-exact from its Stage_Start frame 14659 to frame 17271
(rand / task list; draws exact to 16345 apart from one 2.3e-3 rounding), i.e. the opening (rider + vehicle, burning
trucks, props), scene 1 (hanging signs, bottles, boxes, figures) and the first leaping zombies (actor type 0x31). The
next divergence is the bats (actor type 0x43) at 17260.

**CONFIRMED** = code read + frame-exact against the capture; **CODE** = read and ported, not exercised;
**HYPOTHESIS** marked. Symbols: `stage2_symbols.csv`.

## 1. Capture

`build\scene_capture_auto_st2.txt` (1.1 GB, main checkout), frames 14400..29799 (stage 1 end, the whole of stage 2
14659..29733, the stage-3 start 29735). Replay of the auto script (deterministic: Z/K/E at the stage-2 start identical
to `scene_capture_auto_st1.txt`, and between the three stage-2 captures made here). Hook log:
`build\scene_capture_auto_st2_hook.log`. Cheap K-only run used to find the task functions:
`build\scene_capture_auto_st2_k.txt` (38 MB, K / E / W, no draws).

New hook options (`src/hook/scene_capture.cpp`): `HOTD2_SCENECAP_FROM=<frame>` (nothing written before that frame;
frames still counted from boot) and `HOTD2_SCENECAP_MODELS=0` (no M lines).

```
HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_SCENECAP=1 HOTD2_SCENECAP_FRAMES=29800 HOTD2_SCENECAP_FROM=14400
HOTD2_SCENECAP_TASKS_FROM=14400 HOTD2_SCENECAP_EVENTS=1 HOTD2_SCENECAP_SPRITES=1 HOTD2_SCENECAP_SOUNDS=1
HOTD2_SCENECAP_RANDLOG=1 HOTD2_INPUT_SCRIPT=<main>\build\script_auto_full.txt
HOTD2_SCENECAP_TASKS=0x413E90,0x4842A0,0x435F40,0x435AA0,0x4342E0,0x433A10,0x451860,0x4329D0,0x474F30,0x4521B0,
  0x46FE00,0x451980,0x432AF0,0x484D40,0x451D80,0x451DF0,0x473CF0,0x465FB0,0x4724A0,0x4522A0,0x4533F0,0x452DA0,
  0x431340,0x449910,0x46B320,0x40A230,0x46B5F0,0x4460C0,0x448210,0x430BD0,0x448A80,0x448CE0,0x48A920,0x48A3E0,
  0x448DF0,0x4831E0,0x482EE0,0x471FA0,0x468180,0x467E50,0x442820,0x444D80,0x444EE0,0x469380,0x46F840,0x46F580,
  0x465950,0x40E3B0,0x468F00,0x43C440,0x43F010,0x482F60,0x46A0F0,0x43DA20,0x43E540,0x43E650,0x4694A0,0x471CC0,
  0x467080,0x467290,0x46E3A0,0x472AA0,0x4331D0,0x433B00,0x449020,0x456880,0x438730,0x439F10,0x439E00,0x43A000,
  0x476150,0x468E50,0x437AC0,0x484EA0,0x43FA60,0x435C80,0x43FBC0,0x434EF0,0x435760
HOTD2_SCENECAP_WATCH=<the autoplay list> + 0x9C8E80:8,0x9C8A7C:4,0x9A2C30:4,0x9A3794:4,0x9C8E08:8,0x9A5920:160,
  0x9C7000:4,0x7DCCCC:4,0x9CA0E8:4,0x9A3540:64,0x9C8E50:4,0x9C8E58:4,0x9A2224:4,0x9C71C4:4,0x7DCDB8:12,0x9C88A4:4,
  0x9C7200:256,0x9C7010:16,0x9A2460:576,0x9A2960:576,0x9CA09C:4,0x9A26C0:40
```
(watch list = the `scene_capture_auto_st1.txt` list of autoplay.md plus the entries above; the task list = every task
function first seen in stage 2 except the mass effects 0x407xxx / 0x4667A0 / 0x46C570).

Stage-2 timeline (route record 0x009A5C30, scene 0x009A2BC0): Stage_Start 14659 (sub-state 1 frame), scene 0 14661,
title card 14995, opening area 15175, scene 1 15646, 2 15979, 3 16018, 4 17115, 5 17224, 6 18449, 7 19289, 0x19 19542,
0x1B 21296, 0x1C 22659, boss scene 0x25 23979 (boss tasks 0x476150 / 0x468E50 / 0x437AC0 from 23979, 0x435C80 25784),
stage end 29309 (0x434EF0 / 0x435760), sub-state 5 at 29733, 6 at 29734, stage 3 Stage_Start 29735.

## 2. Stage boundary and the mid-game start

The stage switch (game mode 6): the stage-end sub-states 5 / 6 (0x00460390 / 0x004603B0) count 0x007DCCCC down, then
0x009A1A08 += 1, `Evt_LoadTable` 0x00413160 (event mode 1 = `st2evtbl.bin`), sub-state 1 = 0x0045FFF0: every player
in state 5 -> state 2, Stage_Start 0x00460030(&0x007DCCD0, 0), sub-state 2. The scene 0x009A2BC0 is not changed (0).
Unlike sub-state 0 (0x0045FEF0, the game start) it does NOT run the rank init 0x00460770, the route-record clear,
the per-game resets (0x009C89C0.., 0x009C8EC0.., 0x00463680) or the mode switch that clears 0x009C8E80 / 0x009C8E84.
Stage_Start itself is the same as for stage 1 (srand from 0x009A32A0, Mem_Init, the stage tasks, Stage_ResetState:
per player +0x22 streak / +0x24 shots / +0x26 hits = 0). The players' tasks start at row 2 (0x00413E40, flags 8: no
lives / score reset, players / route not counted) one frame later.

`WorldConfig::carry` (`game::WorldCarry`, world.h) + `World::Impl::ApplyCarry` (world_st2.cpp, called at the end of
StartStage) carry exactly:

| State | Address | Why |
|---|---|---|
| player state (5 -> 2), lives, lives shown, score, credit snapshot, next state, +0x12C | player +0x02..+0x48, +0x12C | player struct is not reset by Stage_Start |
| aim, ammo, empty, reload blink, out timer, continue timer, crosshair | +0x10.., +0x1C.., +0x28, +0x68, +0xE8 | kept through the Stage_Start frame (row-2 init one frame later resets the HUD fields) |
| hit timers | 0x009C8E08[2] | |
| rank, rank timer, rank timer on | 0x009C8E96, 0x009C8A7C, 0x009A2C30 | rank init only in sub-state 0 |
| players, previous players, route | 0x009C8E80 / +2 / +4 | no mode switch |
| 0x009A5900 | | |
| route record | 0x009A5920 (160) | cleared only in sub-state 0 |
| komono-40 variant-1 pieces unbroken | 0x007DCDB8[12] | set by 0x00463680 at the game start only; pieces 0..2 were shot in stage 1 |
| tracer rolls | 0x009A2460 + (p * 6 + i) * 0x30 + 0x18 | never reset (+0x1000 per frame), drawn rotated |
| task serial counter, seed counter 0x009A32A0, credits / free play / coins, difficulty, gun calibration | | as for stage 1 |

`world_test <game> <capture> --script <script> --stage-start F`: the start frame's K line (first serial), W 0x009A1A08
(event mode -> `World::Load(game, eventMode)`), 0x009A2BC0 (scene), 0x009A32A0 (seed counter) and the carried state
from the W lines of frame F - 1; the script is consumed up to F - 1 without edges (`Script::Seek`). Missing W entries
are listed as a warning. `--komono40 <12 digits>` overrides 0x007DCDB8 for captures without it.

**Verified:** with the carry, frames 14659..14799 of the older `scene_capture_auto_st1.txt` (which already contains the
stage-2 start) and 14659..17271 of the stage-2 capture are exact in every check (rand state and calls, tasks, events,
camera, players, lock-on, live enemies / attackers). Each carried item was found as a divergence: player aim / empty at
14659, the komono-40 model ids at 15175 (0x17C6 vs 0x17C7), the tracer roll at 15509 (0x1000 vs 0x4000 rotation of
model 2936). The sound check ignores `World::Impl::Voice`'s 0x4000xxxx markers (no U line: Snd_PlayVoiceLine
0x00435B80 does not go through Snd_PlayGameSound).

## 3. Results

`world_test game build\scene_capture_auto_st2.txt --script build\script_auto_full.txt --stage-start 14659`
(15141 frames compared, 14659..29799):

| Check | First divergence | Frames matching |
|---|---|---|
| rand state Z / calls Y | 17272 | 2613 / 15141 (Z) |
| task list K | 17260 (actor type 0x43 bats) | 2601 |
| event ops E | 17312 | 2778 |
| camera view | 17264 | 2730 |
| players | 17272 | 2613 |
| draws | 16346 (2.3e-3 rounding, one node) | 2419 / 14801 |
| game mode / sub-state, rank, credits, continue timers | | 15141 / 15138 (sub-state differs from the stage end 29733) |

Steps (first divergence after each): carry only -> 15175; rider / vehicle, 0x27, 0x33 st2, 0x44 st2 / 17, komono 40
v1-5 / 66 / 70 -> 16099; behaviours 21 / 27, actor 0x53, komono 35 / 37 / 77 -> 16596; actor 0x31 -> 16737 -> 17002
-> 17051 (wall / ceiling jumps) -> 17272 (surface snap 0x0044C640).

Regressions unchanged: play2scene1 3620 / 3620 logic, draws 3564 / 3567; play1full 2436 logic, draws 2226 / 2251;
2p 5099 logic, draws 1996 / 2018; stage3 demo 814 logic, draws 805 / 814.

Side effect on stage 1 (`scene_capture_auto_st1.txt --script build\script_auto_st1.txt`): the task list now matches to
4327 (was 3606: actor 0x25 command 9 was the first stage-1 divergence); next there is item slot 1 (0x00471CC0).

## 4. Shared-code changes (small, additive)

- `EnemyAnim::nodes` 16 -> 32 records (skeleton 0x1A has 19 nodes; `& 15` wrapped nodes 16..18 onto 0..2).
- `EnemyTask::nodeFn` / `postRootFn` (checked first in AnimNode / AnimDraw), `EnemyAnim::attachHidden`
  (0x00409D10), `World::Impl::pushObj` (0x009C71C8, set by SpherePush), `SpawnHitBlood` (0x00407310),
  `SpawnFlyingHeadModel` (0x0040A130 with a model), `komono40Intact` (0x007DCDB8), `SpawnStage2` / `EnemyBehaviourSt2`
  hooks (one line each in TaskSpawn / EnemyBehaviour), `ApplyCarry` (one line in StartStage).
- Actor 0x25: commands 9 (gun model on node 5), 10 (skip by players in game), 11 path follow incl. the offset angle
  (0x00401AE0), 0x12 (Task_Kill then the waiting path); part models released when the actor goes (0x004124B0).
- Komono 40: variant 1 (positions / angles 0x00594038, model by 0x007DCDB8), variants 2..5 (same pointer table as
  6..), the per-variant extras (scale / angles) for 6, 9, 11..14, 17..19.

## 5. Not ported / open (World::Notes)

- Actor type 0x43 bats (0x00445DB0, task 0x004460C0 with sub-behaviours 0x00592944, draw 0x00447C20, particles
  0x00448900 / 0x00448A80 / 0x00448CE0 / 0x00448DF0, death 0x00448210): next divergence (17260).
- Actor 0x31: behaviours 1, 5, 8, 11, 17.., skeleton 0x17 / 0x18 specifics, landing dust angles from the floor normal
  (0x00401870), effect blocks 0x004073A0 (type 0x4B) and the skeleton-0x16 footstep effects of 0x0044D260 type 0x5A.
- Komono 37 states 1 / 2 (box knocked off and tumbling).
- The rest of stage 2 after 17260: civilians, more komono kinds, the boss (0x00476150 / 0x00468E50 / 0x00437AC0 /
  0x00435C80), the stage end (0x00434EF0 / 0x00435760, sub-states 5 / 6).
- Players' point angle 0x009C71F0 is off by one in some frames from 15179 (rail angles, event_runner, not stage-2
  code; camera view still exact).
