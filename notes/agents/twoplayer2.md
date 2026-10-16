# Two players across the whole game, continues, game over (twoplayer2)

Follows notes/agents/twoplayer.md (P2 join, rail midpoint, pause, continue, game over in stage 1 scene 1). This pass
records a whole two-player game and the wanted continue / game-over captures with the auto player, and makes
`world_test` exact on them.

All input here is **scripted / automatic** (the hook's `HOTD2_INPUT_AUTO` / `HOTD2_INPUT_SCRIPT`, written through the
same gun-slot writes as a joined raw-input device). It proves nothing about physical guns; raw-input device filtering
is untouched. The two-gun physical test is the user's.

## 1. Auto player for two players (`src/hook/auto_play.cpp`)

`HOTD2_INPUT_AUTO=2` existed (alternate-frame shooting, P2 start when idle in a game) but was never run. Changes:

- **Both players start on the title** (default): game mode 4 (mode select) joins a player on start *or* trigger
  (0x00414FC0 via the idle task, 0x00413230), so P2 pulls its trigger on the same frame as P1's confirm (900); both
  start the game (states 0 / 0, 4 credits left of 6). `HOTD2_INPUT_AUTO_P2=game`: the old behaviour (P2 presses start
  in the game; the first test run `script_2p_join1300.txt` joined at 1300, state 10 until the intro ends).
- `HOTD2_INPUT_AUTO_IDLE=p:from-to[,...]` (p = 1 / 2, boot frames): that player presses nothing (no shots, reloads,
  starts) - it is hit until out, its continue countdown runs out, it does not join. Used for the continue /
  game-over captures.
- **Stage-5 boss rule**: never shoot the stage-5 boss (0x0047C960) while its flag 0x4000 (animation frozen) is set.
  See section 4: hitting it then soft-locks the original.
- Statistics of the whole-game run (hook log): P1 6055 shots / 5904 hits, P2 5864 / 5718 at frame 63000; aim
  prediction mismatches 0 for both players.

## 2. Captures and scripts (main checkout `build\`)

Common environment (run by the twoplayer2 runner: HOTD2_NOSAVE=1, HOTD2_SHADOW=1, HOTD2_WINDOWED=1,
HOTD2_KEEPACTIVE=1, HOTD2_SCENECAP=1): `HOTD2_SCENECAP_EVENTS=1 HOTD2_SCENECAP_SPRITES=1 HOTD2_SCENECAP_RANDLOG=1
HOTD2_SCENECAP_SOUNDS=1`, `HOTD2_SCENECAP_TASKS` = the stage-4 list of stage4.md + the stage-5 / 6 additions of
stage5.md (154 functions), `HOTD2_SCENECAP_WATCH` = the stage-4 list + `0x9A244A:2,0x9CA0EC:2,0x9A21BA:2,
0x9C89C0:20,0x9C8EC0:200,0x9C9100:20,0x9CA0F8:4`.

| File | frames | input | content |
|---|---|---|---|
| `scene_capture_2p_auto.txt` (6.5 GB) + `script_2p_auto.txt` | 0..99999 | `HOTD2_INPUT_AUTO=2`, TASKS_FROM=0 | both players from the title through stages 1-4 and stage 5 to its boss; **the original soft-locks at 66343** (section 4) |
| `scene_capture_2p_auto_b.txt` (1.3 GB) + `script_2p_auto_b.txt` | 65900..92871 | `HOTD2_INPUT_SCRIPT=build\script_2p_auto.txt HOTD2_INPUT_AUTO=2 HOTD2_INPUT_AUTO_FROM=66000 HOTD2_SCENECAP_FROM=65900 HOTD2_SCENECAP_TASKS_FROM=65900` (stage-5 boss rule) | stage-5 boss, stage 6 incl. the final boss (creature patterns 2 and 0), the two-player ending, the title (90691). The original exited at ~92450, at the end of how-to-play (mode 9): most likely the known demo-key crash of the following attract demo (missing etc\key_*.bin, main's HOTD2_DEMOKEY_GUARD=1, which this run did not set) |
| `scene_capture_2p_cont.txt` (790 MB) + `script_2p_cont.txt` | 0..10999 | `HOTD2_INPUT_AUTO=2 HOTD2_INPUT_AUTO_IDLE=1:2400-3999,2:2400-5299,1:7500-99999,2:7500-99999` | the wanted continue / game-over cases, below |
| `scene_capture_2p_gameover.txt` + hook log | 10600..11599 | `HOTD2_INPUT_SCRIPT=build\script_2p_cont.txt HOTD2_SCENECAP_FROM=10600 HOTD2_SCENECAP_TASKS_FROM=10600` | the end of the two-player game over: route map with both agents, back to the title |
| `shots_2p_shots\shot_*.bmp` | 1500 .. 11300 | `HOTD2_INPUT_SCRIPT=build\script_2p_cont.txt HOTD2_SCENECAP_SHOTS=...` (light run) | reference screenshots for the HUD check (section 6) |
| `scene_capture_2p_join1300_k.txt` + `script_2p_join1300.txt` | 0..16200 | `HOTD2_INPUT_AUTO=2` before the P2-title change (K / Y / E / W only) | P2 joins in the intro at 1300 |

Timeline of `2p_cont` (states P1 / P2, sub-state 0x009C90A4):

| frame | event |
|---|---|
| 900 | both join on the mode select (states 0 / 0), intro with the two-player rail midpoint 0x0040C9C0 |
| 2400 | both stop shooting |
| 3535 | P1 out: state 4, its own countdown (0x00414280) **while P2 plays** |
| 3769 / 3771 | P2 out: sub-states 3 / 4, the whole-game CONTINUE screen 0x00460530 (P1's own countdown frozen) |
| 4000 | P1 continues **during the whole-game countdown** -> sub-state 2; P2 still in state 4: its countdown restarts (0x004141D0 -> 0x00414250) |
| 4911 | **P2's countdown runs out while P1 plays**: state 6, GAME OVER 0x43E for 120 frames (0x00414420 / 0x004144C0), route 2 -> 1 |
| 5031 | P2 state 9 (idle) |
| 5300 / 5802 | P2 joins again with start (state 10 during a cutscene, state 5 at 5802) |
| 7500 | both stop shooting; P1 out 9148, P2 out 9898 -> sub-state 4 |
| 10812 | nobody continues: **two-player game over** (mode 7, route 2: both bodies, both map agents 0x004617F0 / 0x004618C0) |

Credits: 6 at the mode-select confirm, 4 after both joined, 3 after P1's continue, 2 after P2's rejoin.

**One-player whole-game continue countdown**: with one player every continue is taken during the whole-game countdown
(P1 out = both players "not playing": sub-states 3 / 4 at once). The existing `scene_capture_auto_st3.txt` has four
(sub-state 4 from 40303, 41659, 42615, 43657, each continued by the auto player's start) and the one-player game over
45612: the standard regression (`st3`) is exact on all of it (sub-state, continue timers, CONTINUE sprites 600 / 600,
game-over frames 763 / 763), so no new one-player capture was made.

## 3. Results (`world_test`)

| Capture | command | result |
|---|---|---|
| 2p_auto | `world_test game build\scene_capture_2p_auto.txt --script build\script_2p_auto.txt --to 66000` | **every logic check exact 901..66000** (65100 frames, stages 1-5): rand Z / Y / sites, task list, events, enemies (21335 dump frames), camera, lock-on, sounds, mode / sub-state, players, both gun records, continue timers, credits, players' point / angles, kill counter; draws 63400 / 64190 (blood pools 0x00407E30 / 0x00408100 / 0x00408220, Boss2 0x00476150, 0x00491700: the same kinds as the one-player captures) |
| 2p_auto (to the soft-lock) | `... --to 70000` | also exact 66000..70000: the native world soft-locks the same way |
| 2p_auto_b | `world_test game build\scene_capture_2p_auto_b.txt --script build\script_2p_auto_b.txt --prefix build\scene_capture_2p_auto.txt` | **every logic check exact 65900..90691** (24792 frames: stage-5 boss, stage 6, final boss, ending, back to the title); draws 24598 / 24784 |
| 2p_cont | `world_test game build\scene_capture_2p_cont.txt --script build\script_2p_cont.txt` | every logic check exact 901..10812 (CONTINUE sprites 3036 / 3036), game over 10813..11000 exact (187 frames); camera view 9728 / 9911 (183 frames of +-1 angle from 8445: enemy 65873's body point +0x100 differs by 2e-4, the enemy-skeleton rounding of twoplayer.md section 1, not two-player code) |
| 2p_gameover | see section 5 | |
| join1300 | `world_test game build\scene_capture_2p_join1300_k.txt --script build\script_2p_join1300.txt` | every logic check exact 901..17247 (no draws / T dumps in that capture) |

The standard suite (coordinator's regress.ps1) is unchanged by this work (identical numbers before / after, and after
the merge with main only main's own improvements).

## 4. Fixes and ports (addresses)

- **x87 unordered compare in the screen-side test 0x00409C90** (`fcomp; test ah, 1`: C0 is set for "less" *and* for
  unordered): a new enemy's view position is still 0 / 0 on the frame of its first target pick, the quotient is NaN and
  the original picks player 2. Fixed in `Enemy_PickTarget` 0x00455DE0 (world_enemy.cpp), actor 0x31's 0x0044CA40 and
  the stage-5 boss 0x0047FE90. (2p_auto 7497: the enemy turned to P1 instead of P2.)
- Actor 0x31 PickTarget 0x0044CA40: rand sites 0x0044CA7D / 0x0044CAB2 (two-player branches).
- Stage-2 rider death shooter choice: rand site 0x00451B40 (was 0x00451BC2, not a rand call).
- **Double constants read as floats**: 0x0055CB00 (3.0: the stage-4 boss's thrown object aims beside its target with
  two players, 0x00442B90), 0x00564520 / 0x00564528 (-15 / -40: carried / thrown objects stick in front of the view,
  world_st1b_held.cpp / world_st4_held.cpp). `F32` read the low half (0.0). Found by a scan of every `F32(.., addr)`
  whose original accesses are qword only (scratch script f32double.py; these were the only ones).
- Final boss creature **pattern 0** (0x0042C0B0 -> task 0x0042C1A0, path target 0x0042C490, draw 0x0042C5A0 with the
  second skeleton 0x4E on node 1) and **pattern 2** (0x0042CD30 -> 0x0042CDD0, hits 0x0042D220, node callback
  0x0042D330, draw 0x0042D930), world_st6_boss.cpp. (Patterns 3 / 4 came from main at the merge.)
- Actor 0x31 behaviour 32 (0x00450B20) and the attack table 0x00592A00 hits: ported here and in parallel on main; the
  merge keeps main's version (identical results on the 2P captures).
- Kill counter 0x009A244A: actor 0x20's death increments it (0x004491FD; read only by the training lesson).
  `world_test` checks its change since the first frame (W 0x009A244A).
- World start without the front end (world_test): a gun already connected before the Stage_Start frame keeps its
  shot position (0x009C8F70..), so its record is valid on that frame.
- `world_test --prefix`: P2's device mode (`--p2mode`) also in the uncompared prefix (two-player prefixes diverged).
- Stage-5 boss soft-lock in the original (2p_auto 66343): the boss starts with flag 0x4000 (animation frozen; cleared
  by the first volley 0x0047D410 or the final phase). A damaging hit during its first move (behaviour 6) starts the
  stagger 0x0047CA50, which waits for the frozen motion to end - forever. Both players stand unharmed in front of it
  until the credits are pulled. The native world reproduces it exactly; the auto player avoids it.

## 5. Two-player game over and route map

`world_test game build\scene_capture_2p_gameover.txt --script build\script_2p_cont.txt --prefix
build\scene_capture_2p_cont.txt`: frames 10600..10812 exact (logic; camera / draws: the one +-1 frame of section 3)
and **the whole two-player game over 10813..11373 exact in every check** (561 frames: GAME OVER with both bodies at
route-2 offsets, the route map with both agents 0x004614C0 + 0x004618C0, draws 367 / 367, back to the title at
11373). One fix: the map's end clears the credits and free play (0x00406FD0 -> 0x00407030(0)); credits 561 / 561.

## 6. play.exe

- **Raw-input join with scripted reports**: `tests/play_join_test.cpp` (`build\Release\play_join_test.exe game`):
  fake device handles through `input::RawInput::ProcessMouse` (the production filtering: a null-handle report is
  refused), the per-device conversion `play::DeviceWorldInput` (moved out of play.cpp into src/port/play/gun_input),
  and `World::Boot`. P1's gun: trigger at 700 (title: trigger = start for a player not in the game), 900 (mode
  select: joins); P2's mouse moves first (no join), first trigger pull at 1400 joins P2 in the input layer and the
  world; a third device gets no player; P2's relative motion moves only P2's crosshair. World states: P1 0 at 900,
  5 at 902; P2 10 at 1400, 5 at 2431 = the original's (scene_capture_2p.txt). 2812 / 2812 checks.
- `--p2mode M` (script replays): P2's device mode. The hook switches P2 (Hod2.ini mode 0) to mode 5, so P2's aim
  goes through the calibration path; play.exe's default is mode 6 (fixed calibration). With `--p2mode 5` a replay of
  `script_2p_cont.txt` follows the original's states exactly and draws P2's crosshair (mode 0 replays the same logic
  but draws no crosshair; without the option P1 lost lives from ~6687 that the original did not).
- Credit line: play.exe drew "FREE PLAY" with `--credits`; it now uses the world's credits and coin state
  (0x00407060: 0 none, 2 enough).
- `HOTD2_PLAY_STATELOG=<file>`: mode / sub-state / both players (state, lives, countdown digit) on change.
- **HUD and flow vs the original's screenshots** (light run `shots_2p_shots`, `play.exe game --script
  build\script_2p_cont.txt --credits 6 --p2mode 5 --screenshot p_%05d.png --every 50 --frame 11300`; side by side:
  `build\shots_2p_shots\cmp_*.png`, original left). Mean abs pixel difference 7.2 / 5.2 / 6.0 / 6.3 / 3.4 / 8.7 /
  4.4 / 4.0 / 0.8 / 2.1:

  | frame | scene | HUD |
  |---|---|---|
  | 1500 | intro, both players (rail midpoint) | HOLD YOUR FIRE! for both: same |
  | 2600 | both playing | both ammo / lives rows, P2's crosshair: same |
  | 3600 | P1 out, P2 plays | P1's CONTINUE? 9, PRESS START BUTTON, CREDIT(S) 4; P2 HUD: same |
  | 3900 | both out, whole-game CONTINUE? 8 | both credit lines: same |
  | 4500 | P1 continued, P2's own countdown | same |
  | 4950 | P2's countdown ran out | P2 GAME OVER (0x43E) while P1 plays: same |
  | 5100 / 5400 | P2 idle / waiting (state 10, blink off at 5400) | PRESS START BUTTON, CREDIT(S) 3: same |
  | 10900 | two-player game over | both bodies: same |
  | 11300 | route map, both agents | same layout; the footprint line is red in the original and pale blue natively (the world draws the same 56 footprints, model 0x14C2: renderer) |

  Other differences are renderer ones (the car window reflection, an enemy's eye material).

## 7. Open

- 2p_cont camera +-1 from 8445: enemy 65873 (behaviour 40 sub 2, motion 968) drifts by 1e-4 in z from 8297 (root
  motion rounding); its body point is the shooting camera's only look-at target. Enemy agent's area.
- Final boss creature pattern "> 4" (0x00428307 jump table default: an uninitialised object) not ported.
- world_test crashed (access violation) a few times when run while a Hod2 process of another agent was running;
  never reproduced otherwise. Main's 32aaf04 (freed blocks kept readable for 3 frames) may be the cause; the crash
  trace option `WT_CRASH_TRACE` (compile with /DWT_CRASH_TRACE) prints the stack.
