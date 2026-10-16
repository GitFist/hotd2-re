# Automatic gun input for gameplay captures (HOTD2_INPUT_AUTO)

Goal: deterministic, replayable captures of real game-mode-6 play far beyond the blind fixed-position scripts (which die
in stage 1 scene 1). Result: the auto player clears **all of stage 1 including the boss** (stage 2 starts at frame 14660)
and **all of stage 2**, and gets to stage 3 scene 4 before the credits run out (game over at frame ~45610).

This is scripted/automatic input written through the same gun-slot writes as a joined raw-input device. It is NOT
device input and proves nothing about physical guns or mice; raw-input device filtering is untouched.

## 1. What was added

| File | |
|---|---|
| `src/hook/auto_play.{h,cpp}` | `autoplay::Decide(player, bootFrame, prev)`: reads game memory (never writes), returns aim (normalized) + buttons (1 trigger, 2 reload, 4 start) |
| `src/hook/input_hook.cpp` | `HOTD2_INPUT_AUTO=1` (P1) / `=2` (P1 + P2): once per boot frame 0x009A5C40 each decision that differs from the previous one is formatted as a script line, **parsed back exactly like `LoadScript`**, appended to the script and written to `game\input_auto.txt`; `ApplyScript` then applies it like any `HOTD2_INPUT_SCRIPT` line (same `ApplyPlayer` writes). Replay = identical input by construction. `HOTD2_INPUT_AUTO_PERIOD=N` (default 2): frames per trigger cycle (press 1 frame, release N-1) |
| `src/hook/scene_capture.cpp` | `HOTD2_SCENECAP_TASKS` buffer 512 -> 2048 characters (long task lists) |
| `src/hook/CMakeLists.txt` | `auto_play.cpp` |

`input_auto.txt` has the `build\script_*.txt` format (`<boot frame> <player> <x> <y> <buttons>`), readable by the hook's
`LoadScript`, `world_test --script` and `play.exe --script`. x / y are written as `(pixel + 0.5) / 640` (`/ 480`) with 6
decimals, so `floor(x * 640)` gives back the chosen pixel. The first line (`0 0 0.5 0.5 0`) connects P1 at frame 0
(the old scripts connect at 700; the replay of an auto script is identical either way).

## 2. Decisions per frame

- **Title**: start (bit 4) held 12 frames from boot frame 700 (as `script_p1_start.txt`); game mode 0x009C8E98 then
  becomes 4, which waits for a **trigger** pull: trigger at the centre 900..902 (as the scripts), game mode 6 from 900.
  No further title starts once a game has been played (credits).
- **Continue**: player state (player +0x02) 4 = continue countdown -> start held 12 frames (retried after 120 frames;
  the first press one frame after the switch to state 4 is ignored by the game, the retry 120 frames later is taken).
  Start is never pressed in state 5 (it would pause, twoplayer.md section 4).
- **Shooting** only in game mode 6, player state 5, lives > 0, `0x009C8E00` (can shoot) != 0, one press per
  `period` frames:
  - ammo (player +0x1C) == 0 -> reload (bit 2, the scripts' off-screen reload);
  - else the first feasible aim point (below) -> trigger;
  - else, if ammo < 6 -> reload.
  - With `=2` the players alternate frames (P1 even, P2 odd), so the hit feedback entries belong to one shot.

### Target selection (addresses)
- **Targets** = the hit system's own list: `g_shotTargets` 0x0059D8E8 (5 words, count 0x005A4C80). The hit task
  0x004044A0/0x00404570 runs after the player tasks and before the actors, tests the shots against the entries
  registered during the previous frame and then resets the count. `Input_Poll` runs before any task, so the list read
  there is exactly what this frame's shot will be tested against, with the view-space positions it will use.
- Object fields: +0x00 task fn, +0x08 serial, +0x34 flags (0x80 skeleton target, 0x8000 not shootable, 0x10 mesh
  target: skipped, 0x4000000 dead: skipped as a target but kept as an occluder), +0x70 view-space sphere centre,
  +0x124 radius, +0x1F4 skeleton type (s16). Skeleton targets: the walk of `Hit_TestSkeleton` 0x00404700 /
  `Hit_TestNodeRecursive` 0x00404750 from `0x004E0430[type]` (Node +0x14 slot, +0x16 child count, +0x18 children); node
  records obj+0x20C+slot*0x90: +0x00 model (0 inactive), +0x68 view hit point, +0x74 flags (0x10 mesh: skipped), +0x78
  radius (0: skipped).
- **Projection**: centred aim `(F*x/-z, F*y/-z)`, F = `g_focalLength` 0x009A2D70 (float). The raw gun pixel is the one
  whose calibrated value (Gun_UpdateRecords 0x0040CBB0: mode 6 fixed points via `game::GunCalibrate`, else the gun
  record's calibration +0x16/+0x1E) is nearest. The ray (cx, cy, -F) must pass within 0.8 r of the point and (skeleton)
  0.8 r of the gating object sphere.
- **Order**: skeleton actors by view distance; other targets (props, thrown objects) after them (distance * 3 + 100).
  Head (node slot 2) first, then nodes by radius.
- **Civilians** (actor type 0x10: task fns 0x0048A3E0 / 0x0048A920 / 0x0048B140) are never aimed at; a point is
  rejected if any civilian sphere/node (1.3 r, "might hit") has a key `ftol(-z*10) & 0xFFFF` <= the intended entry's key
  (the nearest entry wins in `Hit_Deliver`). No civilian was shot in the stage-1 run (the native world reports a civilian
  **rescue** bonus there, see 5).
- **Feedback** (next frame, after the hit task ran): hit flag 0x009C9010[p] and the nearest entry (0x0059ECD8[0].index ->
  0x0059F4D0 + idx * 0x3C, +0x28 object). A target that absorbs 3 shots in a row without being the hit object (level
  geometry in the way, `Hit_TestLevel`) is skipped for 90 frames.
- Check: the gun record's centred aim (0x009C8FC0 + p*0x28 +4/+6) of the next frame is compared with the prediction:
  1 mismatch in 1839 stage-1 shots (the first shot of the game, 2432: the record still showed (0, 0)).
- Stage 1 statistics (hook log, to boot frame 14400): 1839 shots, 1809 hit the intended object, 30 hit nothing / something else; 80 points
  rejected because of civilians; 319 reloads.

## 3. Determinism / replay

Auto run vs a replay with `HOTD2_INPUT_SCRIPT=build\script_auto_st1.txt`, same capture settings, 14800 frames:
- **Z (rand state), K (task list), Y (every rand call: site, serial, value), N, E (event ops), U (sound commands):
  identical in all 14800 frames.**
- W lines differ only in player +0x74..+0x7B (a heap pointer: the process heap base differs between runs by 0x130000);
  T dumps only in heap pointers and in the trailing words past the object (the next heap block's header);
  M lines in 5158 frames: 3828 are capture model-id aliases (the hook's pointer -> id cache, a capture artifact: same
  matrices, different id for a model at a reused address, e.g. 5568/8233, 4261/4583) and 1330 are frames the original
  did not render in one of the runs (frame skip; the logic is unaffected).
- The light runs (W lines only) reach the same states on the same frames (stage 2 at 14660, score 18210) as the fully
  instrumented capture.

## 4. Captures and scripts (main checkout `build\`)

| File | |
|---|---|
| `scene_capture_auto_st1.txt` (993 MB) | frames 0..14799: title, stage 1 complete incl. the boss (scene 9 from 9326, boss task fns 0x0049B620 / 0x0048FE90 from 9476), stage end 14231..14659, stage 2 start 14660. Env below |
| `script_auto_st1.txt` | its input (4322 lines) |
| `script_auto_st1_hook.log` | hook log of that run (auto input statistics) |
| `script_auto_full.txt` | input of a whole auto game (light run, 13993 lines): stage 1, stage 2 (14660..29736), stage 3 (29737..) to game over at the stage-3 boss (~45610) |
| `wt_auto_st1.txt` | `world_test` output on the new capture |
| `ref\play2scene1_{2000,2300,3100}.bmp` + `ref\play2scene1_capture.txt` | clean reference shots of the `script_p1_reload.txt` run (light run: windowed, keep-active, SCENECAP + SHOTS only; F lines identical to `scene_capture_play2scene1.txt` at those frames) |

Stage-1 timeline of the auto run (route record 0x009A5C30, scenes appended by op 0x4D): scene 1 903, 2 2697, 3 5686,
4 5972, 5 6417, 6 7399, 7 8396, 8 8543, 9 (boss) 9326; lives 3 -> 4 (5850) -> 5 (9160); score 18210 at the stage end.
Stage 3: scene 4 (from 38082) is the stage-3 boss (the multi-headed serpent with a health bar); the auto player loses a life every ~250-300 frames there (from 39362; 4 continues, game over 45611) while the boss bar goes down only slowly. Not analysed (probably a weak point / attacking head that the nearest-skeleton rule does not pick).

Capture environment (`scene_capture_auto_st1.txt` and its replay):
```
HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_SCENECAP=1 HOTD2_SCENECAP_FRAMES=14800 HOTD2_SCENECAP_EVENTS=1
HOTD2_SCENECAP_SPRITES=1 HOTD2_SCENECAP_SOUNDS=1 HOTD2_SCENECAP_TASKS_FROM=860 HOTD2_SCENECAP_RANDLOG=1
HOTD2_INPUT_AUTO=1   (replay: HOTD2_INPUT_SCRIPT=build\script_auto_st1.txt)
HOTD2_SCENECAP_TASKS=0x413E90,0x466930,0x48A920,0x48A3E0,0x4533F0,0x452DA0,0x472830,0x46C240,0x482EE0,0x46BCC0,
  0x465FB0,0x433B70,0x46D850,0x4842A0,0x43F5C0,0x449020,0x473B90,0x4724A0,0x471CC0,0x43DD00,0x432C80,0x467E50,
  0x43C440,0x43DA20,0x430BD0,0x43E540,0x43E360,0x43E650,0x46DAB0,0x474120,0x46F350,0x474F30,0x449910,0x46A580,
  0x46AD20,0x471FA0,0x465950,0x46FE00,0x442820,0x444D80,0x43FA60,0x444EE0,0x432610,0x433A10,0x46A1C0,0x48DFE0,
  0x49B620,0x437AC0,0x48FE90,0x435E90,0x491700,0x435C80,0x434EF0,0x435760,0x49DEA0
HOTD2_SCENECAP_WATCH=0x9C8E98:2,0x9A1A08:2,0x9A2BC0:2,0x9A2BB0:2,0x9C90A4:4,0x9A5C60:304,0x9A5D90:304,0x9C904A:2,
  0x9C7006:2,0x9C8E96:2,0x9C6F00:4,0x9C6F20:4,0x9C6F2C:4,0x9A5EC0:128,0x9A6000:420,0x9A61A4:420,0x9A1A10:4,
  0x9A2C8C:4,0x9C8E00:4,0x9CA0F4:4,0x9C8FC0:80,0x9C71E0:24,0x9CA0E4:4,0x9A5C40:4,0x9A32A0:4,0x9A2BB8:4,
  0x9C8E60:16,0x9C8E70:16,0x9A5900:4,0x9A5C44:4,0x9C9028:4,0x9C8E94:2,0x9A5C30:16
```
The task list = the `play2scene1` set + every stage-1 object task function first seen after scene 1 (from a K-line
run), excluding the mass effects (0x407xxx, 0x4667A0, 0x46C570).

Task functions first seen per part of stage 1 (frame first seen, task-frames, serials), for the scene agents:
scene 2+ `0x473B90` 4088, `0x484D40` 4175, `0x465FB0` 4217, `0x4724A0` 4240, `0x471CC0` 4327, `0x48A920` 4866;
scene 5 `0x43DD00` 6408, `0x432C80` / `0x467E50` 6807, `0x43C440` / `0x43DA20` / `0x430BD0` / `0x43E540` 6897..6899,
`0x43E360` 6933, `0x43E650` 7018; scene 6 `0x46DAB0` 7218, `0x474120` 7528, `0x46F350` / `0x474F30` 7839,
`0x449910` 7915, `0x46A580` 8024, `0x46AD20` / `0x471FA0` 8050, `0x465950` 8106 (213 serials); scene 7/8
`0x46FE00` 8395 (20 serials, 125260 task-frames), `0x442820` / `0x444D80` / `0x43FA60` / `0x444EE0` 8396..8467,
`0x432610` / `0x433A10` / `0x46A1C0` 8542, `0x48DFE0` 9159; boss `0x49B620` / `0x437AC0` / `0x48FE90` 9476,
`0x435E90` 10079, `0x491700` 10080, `0x435C80` 10582, `0x49DEA0` 12483; stage end `0x434EF0` / `0x435760` 14231,
`0x413E40` 14658.

## 5. world_test on the new capture

`world_test game build\scene_capture_auto_st1.txt --script build\script_auto_st1.txt` (full output
`build\wt_auto_st1.txt`; stage start 901, compared 901..6793, then the native P1 reaches game mode 7 while the original
plays on):
- exact through **3606** for rand (Z / Y / sites), task list, events, enemies, camera, lock-on, players, gun record.
- first divergences, in order:
  - draws 1338 (the known actor 0x22 rounding frames 1338..1366);
  - sound commands (U) **1344**: native `40000001`, capture `2000006D` (5722 / 5893 frames match);
  - players' point / angles 0x009C71E0 **3229**: point equal, angles native (27277, -11922, -32768) vs capture
    (0, 44691, 0) through 3270 (the rail angle path of scene 2);
  - task list **3607**: native keeps actor 0x25 task 65771 (0x004842A0) alive, note `actor 0x25 script op 9
    (0x004842A0) not ported` (the original ends it there; 18 extra native draws);
  - rand calls / enemies 4867 (a task serial one lower in native: 65817 vs 65818);
  - players 5488: score native 3180 vs 3580 = the **civilian rescue bonus +400** (`0x004156C0 +400 not modelled`);
  - camera 5624, rand state / event ops 5653, native game over 6793.
- Notes (unported paths reached): actor 0x25 script op 9; civilian motion change with rotation alignment (0x0048C310
  flags 0x18000); civilian rescue bonus (0x004156C0 +400); civilian script op 0x19 (0x009C88A4); civilian script op
  0x1D (0x00435B80); civilian voice 0x0048D140; env ops 25 / 35 (interpolator 0x0040AD90); item drop slot 1
  (0x00471BD0 / 0x004722A0); title card light setup 0x0040E140 / 0x0040C2C0.

## 6. Open
- Stage-3 boss (scene 4): solved, see stage4.md section 1 (boss weak-node rules; the new full-game script `build\script_auto_full2.txt` finishes the game).
- The hook's model-id cache aliases models at reused addresses (section 3); a capture-side fix would need the id
  resolved at load time.
- `HOTD2_INPUT_AUTO=2` (two players) is implemented (P2 presses start on the title at 700 and when idle in a game,
  alternate-frame shooting) but was not run.
