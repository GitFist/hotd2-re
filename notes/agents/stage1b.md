# Stage 1 after scene 1: the rest of stage 1, the boss and the stage clear

`game::World` replays the whole of stage 1 of the auto-play run frame-exact:

```
world_test game build\scene_capture_auto_st1.txt --script build\script_auto_st1.txt
```

rand state / rand calls / task list / sounds / players / enemy hp + position / live enemies / attackers / lock-on slots /
event ops / camera view / letterbox / rank / hit shake are exact from 901 to **14658** (the last frame of stage 1; the
results screen 14232..14658 included). Stage 2 starts at 14659 (sub-state 1, Stage_Start 0x00460030): that transition
is not chained inside one run; stage 2 is checked with `--stage-start 14659` (world_st2.cpp, notes/agents/stage2.md).

Remaining differences before 14659 (both older than this work): players' point / angles 0x009C71E0 at 3843 (one angle
unit in the Event3 rail's 0x00401C50, x87 rounding) and draw matrices from 1338 (actor 0x22 variant 0's second object,
err 0.006: node 1 model-view x camera matrix).

**CONFIRMED** = code read + frame-exact against the capture; **CODE** = read and ported, not exercised. Symbols:
`stage1b_symbols.csv`. Decompiles of functions missing from the shared export were made from a private read-only copy
of the Ghidra project in the scratchpad (the shared project in `ghidra/` was not written).

## 1. Progress (first divergence of the auto capture, in order)

| frame | cause | fix |
|---|---|---|
| 1344 / 3229 / 3607 | voice pseudo-sound, rail player angles, actor 0x25 | earlier |
| 4328 .. 5624 | items, civilian voice 0x0048D140, rescue bonus, route branch (civilian op 0x19) | earlier |
| 5830 .. 8396 | behaviour 39, civilian mesh push, board, komono 77 / 50 / 57 / 34 / 66, actor 0x29, bats, type 0x44 state 17, behaviours 33 / 26 / 12, actor 0x31, animated prop mesh | earlier |
| 8397 | behaviour 37 + carried object | world_st1b_held.cpp |
| 8407 | actor type 0x12 | 0x0043F9D0 / 0x0043FA60 |
| 8543 | komono 31 | 0x0046A1C0 |
| 8736 (camera) | civilian motion change with position alignment | 0x0048C310 flags 0x20000 / 0x18000 |
| 8815 | behaviour 36 | 0x0045B190 |
| 9160 | civilian attachments (life item, "P1" label) | ops 0x13..0x15, 0x0048CD10, 0x0048DCC0, 0x0048DFE0 |
| 9477 | the boss | world_st1b_boss.cpp |
| 10132 | actor type 0x28 (placeholder before) | 0x00432610 |
| 11728 / 11807 (camera) | lock-on point read live (+0x100) | SyncLock after every boss / mount update |
| 11744 | second-phase targets use 0x009C71F0 (World::Impl::playerYaw is never written: use runner->camera.playerYaw) | |
| 11908 (lock-on) | rider +0x121 = 0 (not -1) | |
| 12622 | komono 52 | 0x00463D20 |
| 14232 | stage clear | actor types 0x61 / 0x62 |
| 14656 (sub-state) | 0x0045ED90 + mode-6 sub-states 5 / 6 | |

## 2. Final numbers

auto_st1: everything above exact to 14658; first divergence 14659 (stage 2 start). Regressions unchanged:
play2scene1 79/79, play1full 2436/2436, 2p 5099/5099, 2p_continue 5099/5099, stage3 demo 814/814 (rand / tasks /
players), auto_st2 `--stage-start 14659` exact to 17271 as before.

## 3. Merge with main (stage-2 agent)

- Actor type 0x31: main's `world_st2_actor31.cpp` is kept (exact on auto_st2); my `world_st1b_beast.cpp` was deleted
  after adding its behaviours 1 (0x0044A360 flinch) and 19 (0x0044E410 rise) to main's file. Both versions gave the same
  auto_st1 result.
- Actor 0x25 commands 9 / 10 / 11: main's version kept (same auto_st1 result).
- `pushObj` (0x009C71C8) replaces my `lastPusher`; `SpawnHitBlood` / `SpawnFlyingHeadModel` from main.
- `Actor22Task` moved to world_internal.h (+ `boss` block for variant 1).

## 4. Behaviour 37 and the carried object (world_st1b_held.cpp)

Behaviour 37 (0x0045B380) reads the motion list 0x0045CA10: the object record (event memory) {+0 type, +4 start fn
index 0x005926A8, +8 throw state -> +0x1358, +0xC pos, +0x18 spin, +0x24 vel, +0x30 s16 motion, +0x32 start, +0x34
loops, +0x36 tick, +0x38 list of {motion, start, loops, tick}}. Task 0x00442740 keeps a 0xE0 control block at +0x198:
{+0 holder, +4 holder's civilian, +8 type, +0xC model, +0x10 state (10 = held), +0x14 last pos, +0x2C spin x,
+0x30, +0x34 spin angle, +0x38 axis, +0x44 debris {skeleton, motion, frame, prevFrame}}. Definitions 0x005644D8[type]:
{s16 skeleton, s16 motion, f32 radius, f32 radius128, f32 half sizes x3, u32 hit sound, u32 break sound, s16 hp, s16
models by hp}. The object sits between holder nodes 4 / 7 (0x00442EB0, model-view; here node world x view), falls when
the holder dies (0x00442950 / 0x00444D80, tumbling about +0x38 by +0x34), breaks into the debris animation at hp 0.
Not ported: throws (0x00442B90 and 0x005926A8 entries 2..7), type 2's fly-off 0x00444F70. 0x00444280 (floor contact)
is CODE: the capture's object never touched the floor.

## 5. The boss (world_st1b_boss.cpp)

Actor 0x22 variant 1 (rider, skeleton 0x45 + second object 0x46 on node 1, hp 300 = record state, P s16 0xA fight hp,
P s16 0xC phase threshold, P s16 0xE phase-2 hp, P word 0x10 mount record) and actor 0x23 (mount, skeleton 0x44,
hp 90). Rider fields: +0x1320 phase, +0x1324 taunt letterbox, +0x1328 timer, +0x132C damage relayed by the mount,
+0x1330 counter, +0x1334 / +0x1344 step-back, +0x1340 path tick, +0x1348, +0x1350 path, +0x1354 aggression,
+0x1358..+0x136C event counters, +0x1370 mount distance, +0x13C0 offset / target, +0x13CC saved camera target,
+0x1394 mount. The second object's motion follows the path speed (0x00570D84[rank + phase * 16]: > 2.5 / 2 / 1.5 / 1
-> 0x00570C00[4..0]).

Camera writes: the name card (0x00437AC0) and the death (0x0049C910) write context 0's position / target directly
(0x009A60C0 / 0x009A60D8) with the gameplay camera held in state 6 (0x009CA094 = CameraDirector::forceState6); the
death also calls Cam_LookAngles.

Lock-on: actors on direct slots are read live by the camera (0x00403050 reads +0x100 / +0x121 of the slot's object);
game::LockOnTarget is a copy, so the boss and mount sync it after every update.

Globals added to World::Impl: `bossBar` (0x009C8E10), `bossActive` (0x009CA0EA), `stageEndCounter` (0x007DCCCC),
`nextEventMode` (0x009A1A08 after the stage end).

## 6. Not ported (World::Notes on this capture)

- actor 0x25 motion props 0x32A / 0x34C / 0x35D (0x00485790)
- actor 0x31 landing dust angles from the floor normal (0x00401870) (main's file)
- actor push vs collision-mesh object (0x004057F0) (check note)
- ~~bat skeleton draw (0x0040F4F0, skeleton 0x1D)~~ ported, see 6b
- boss health bar sprites 0xB5..0xB8 (0x0041C800), boss name card sprites (0x0041C630), stage clear result models /
  sprites (0x00434EF0) - 2D / view-space draws only
- civilian script op 0x2B (game mode 6)
- decor vertex wave on models 0x1731..0x1734 (0x0046DAB0, renderer)
- env ops 25 / 35 / 39 (interpolator 0x0040AD90)
- title card light setup 0x0040E140 / 0x0040C2C0 (renderer)
- next stage start inside one run (mode 6 sub-state 1 after 0x004603B0)
Also CODE only / noted when reached: held-object throws, 0x0048DD60 item display task 0x00475D00, behaviour 33 subs
3..6, type 0x44 state 17 game type 1 / stage-2 parts, boss variant 2, mount variant 1, game types 1..3 branches.

## 6a. Leapers that wait (actor 0x31 behaviour 8)

Bug from play: when a scene-6 leaper (actor 0x31, the "monkey" zombies) attacked, the camera turned to a wall and
stayed there. The auto run shoots both leapers before they attack, so behaviour 8 (0x0044B3E0: wait while the other
leaper holds the target, idle pose by surface with one rand per frame on the floor, then attack 9 with sound 0x2416A9)
was never reached and was not ported. In the port the waiting leaper kept its walk motion and walked off along the
ceiling through the walls; with no player-targeting slot the shooting camera (0x00403050) looks at the slot 2 + 3
midpoint, so it turned after it. Ported: behaviour 8 (CONFIRMED, 474 frames), 11 (0x0044BC70 off the surface edge:
fall, land) and 17 (0x0044C2E0) (both CODE).

Capture without shooting from 7915 on: `build\script_leaper_wait.txt` (= `script_auto_st1.txt` up to 7914, then no
input) -> `build\scene_capture_leaper_wait.txt` (frames 0..8799, auto_st1 settings + shots). `world_test` on it:
every logic check exact 901..8799 (before: camera / rand from 8138, lock-on from 8113).

## 6b. Leeches / "bats" drawn (actor 0x40, skeleton 0x1D)

Bug from play: the leech-like worms that crawl out of the cellar window (stage 1 scene 8, group 1; scene 3 group 0 on
the no-rescue route; stage 2 scene 0x19 group 2) showed only their shadows. Their draw goes through the second
animation system, now ported in `world_st1b_bats.cpp` (BatSkelDraw): init 0x0040EAE0 / 0x0040EB40 / 0x0040EC50 (it
reads frame 0 twelve bytes too far on: the angles in use before the first draw are the frame's slot + 2 angles),
motion switch 0x0040F840 / blend 0x0040F860 + 0x0040F900, draw 0x0040F4F0 -> 0x0040F520: advance 0x0040F610, root
0x0040EFB0 + object transform 0x0040F220 (dying: height squashed by (0x3D - +0x1330) / 61), walker 0x0040F7B0 with
0x0040ED30 (odd ticks or tick 0: frame angles; even ticks: halfway from the angles in use; blends: 0x0040FFD0 =
0x00412750; jaws on slots 8 / 9 open with +0x1344 while diving in game mode 6), then 0x0040F960 per slot: the seam
vertices of body segments 1..7 are moved onto the neighbouring segment (tables 0x004E1D48 + slot * 0x40) and the
material diffuse is set to 1.0 / 0.6 by the node's world z (-515), in the model's memory (`game::ModelEdit`, carried
by `WorldDraw::edit`, applied by `port::Scene` to a copy). Scene 3 also draws a reflection (y mirrored about the dive
height, 1.75 lower, pitch + 0x8000 in states 2 / 3, +0x34 |= 0x1000000: no new model edits); the capture of the
no-rescue route confirms the 1.75 drop and the re-interpolated node angles on even ticks (its logic diverges earlier,
so it is not compared). world_test: fn 0043C440 3860 / 3860 and 0043DA20 3094 / 3094 draws exact on auto_st1 (were
0), 918 / 918 and 427 / 427 on auto_st2. Not ported: the first-draw normal cache of 0x0040F960 (0x007C17B0 /
0x007C18D0: one word written into the normal x of some seam vertices, lighting only), the actor light 0x0041DC70
(renderer, as for every actor), the stage-2 water surface bulge under the leeches (0x0043F010 / 0x0043F2E0 /
0x0043F3E0, model 0x10CF).

## 7. Files

New: `src/game/world_st1b.cpp` (items, props, actor types 0x12 / 0x28 / 0x29 / 0x40, behaviours 39 / 33 / 26 / 12),
`world_st1b_bats.cpp`, `world_st1b_held.cpp` (behaviours 36 / 37, carried object), `world_st1b_civ.cpp` (civilian
attachments, life label), `world_st1b_boss.cpp` (boss, mount, name card, bar, ring, flash, sound repeat, stage clear,
stage end), `world_npc.cpp` (actor 0x25, superseded by main's).
Small edits: world_internal.h (declarations, Actor22Task moved, CivCtl attachments, EnemyTask::held139C),
world_enemy.cpp (behaviour dispatch 12 / 26 / 33 / 36 / 37 / 39, node callback 0x00463F90, voice rand site), world.cpp
(SoundRepeat -> task, SpawnEffectScaled), world_actors.cpp (case 0x22 variant 1 hook), world_player.cpp (mode-6
sub-states 5 / 6 / 1), world_civilian.cpp (0x0048C310 alignment, attachments ops / frame), world_st2_actor31.cpp
(behaviours 1 / 19), event_runner.cpp (rail player angles), tests/world_test.cpp (`--dump F`).

## 8. Tools (scratchpad, not committed)

`cap.py first last [types] [grep]` (capture lines), `capk.py first last [minserial]` (task lifetimes), `capt.py first
last serial off:fmt...` (T-dump fields), `rd.py va n fmt` (exe data), `dec.ps1 addr...` (private read-only Ghidra
copy), `wt1b.ps1 <auto|st2|p2s1|p1|tp|tpc|demo>` and `reg.ps1` (build + checks).
