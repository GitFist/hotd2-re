# Two players, continue, pause and game over in the native world (Stage 3)

Extends `game::World` (game mode 6, notes/agents/gameplay.md) with P2 joining mid-game, the two-player rail, the
pause and continue with the start button, credits, and the game-over screen (game mode 7) up to the switch to the
title. Verified frame by frame with `tests/world_test.cpp --script` against captures of the original played with
scripted gun input.

**CONFIRMED** = code read + frame-exact against a capture; **CODE** = read in code, not reached by the captures;
**HYPOTHESIS** marked.

Files: `src/game/world_player.cpp` (join / state 10 / continue / pause / credits / players' body draw bit / rail
midpoint), `src/game/world_gameover.cpp` (new: game mode 7 + route map), small additions in `world.cpp` (Step,
StartStage, camera task tail, route record), `world.h` (API, below), `world_internal.h`, `world_enemy.cpp`
(0x00407200 node blood, root interpolation rounding), `world_actors.cpp` (one line: actor 0x20 calls the node blood),
`mstack.cpp` (0x00401C50 rounding), `tests/world_test.cpp`.

## 1. Results

`world_test <game> <capture> --script <script>` (two-player captures: `--p2mode` defaults to 0, see 3).

| Capture / script | frames | result |
|---|---|---|
| `scene_capture_2p.txt` / `script_2p.txt` | 901..5999 | everything exact through **2964**: rand Z / Y / sites, task list, event ops, camera, enemies (446 enemy-frames), lock-on, mode / sub-state, letterbox, rank, shake, players (state, lives, score, ammo, shots, hits, aim), P1 / P2 gun records (2064 / 1565), continue timers, credits, players' point / angles; draws 1861 / 1883 (the 22 known actor-0x22 rounding frames 1338..1366 + 1). First divergence **2965**: scene 1 (op 0x4F) spawns the komono / actors of the other agent's work (first extra rand 0x00463605, task 0x0046BCC0); event ops, mode, letterbox, rank, shake, aim, gun records, credits, continue timers and the players' point stay exact to 5999 |
| `scene_capture_2p_continue.txt` / `script_2p_continue.txt` | 901..5999 | **all 5099 frames exact** for rand, task list, event ops, enemies (3680), lock-on, mode / sub-state, letterbox, rank, shake, players, both gun records, continue timers, credits (5 -> 4 at 1400 -> 3 at 4560), players' point; camera 5098 / 5099 (5562), draws 3392 / 3415 (actor 0x22 + the 5562 camera frame) |
| `scene_capture_play1full.txt` / `script_p1_start.txt` | mode 6 901..3336 | unchanged and exact (draws now 2226 / 2251: the 2430 shadow case is fixed); players' point / angles 2436 / 2436 (was 2100) |
| same, game over | **3337..3853** (517) | rand, task list, event ops, mode (7 -> 3 at 3853), players, continue timers, players' point exact 517 / 517; camera 515 / 516 (3719); draws 296 / 302 (below) |
| `scene_capture_play2full.txt` / `script_p1_reload.txt` | 901..1766 | unchanged (scene 1 at 1767 not ported); draws now 816 / 816 up to 1766 (1211 shadow case fixed). Its game over (4520..4739) is not reached |
| `scene_capture_stage3.txt` (demo) | 2786..3599 | unchanged, all exact (draws 805 / 814, the 9 known rounding cases) |

Game-over draw differences: 3734 / 3737 / 3742 / 3743 (the old P1 task's copy of the body, errors 0.009..0.02 at
distance ~100: HYPOTHESIS: the odd-tick blend when the body's counter advances twice per frame, see 5, or the
view the test rebuilds from the F line, as no stage task draws the identity view in mode 7) and 3853 (the mode-switch
frame: the agent's rotations match, its whole skeleton is offset uniformly = the F line already shows the camera after
the switch to the title; the view-space marker matches). 3719
(camera): the map setup frame resets context 0 without building its views (no task runs, nothing drawn); the
capture's F line shows the reset pose.

The 2p continue camera 4181..4276 divergence (yaw 1 unit) was the enemy root interpolation 0x00410920 computed in
double: with the 24-bit float steps it is gone (demo / play1 unchanged). The remaining 5562 is a 1e-3 difference of
enemy 65590's body point at tick 19 of motion 1008 (enemy skeleton code, not two-player specific).

## 2. P2 joins mid-game (CONFIRMED, 2p capture)

- Idle player (state 9, task 0x004145F0 -> 0x00414600) presses start: Player_TryJoin 0x00414FC0(task, 3) (mode 6;
  the title uses state 0): start (0x00413230: P1 0x009C9028 & 8, P2 & 0x80000), mode != 7, 0x009A5900 bit 1,
  Credit_Use 0x00406DC0 (shared credits 0x00406E10). If 0x009A5900 bit 0 is clear the player waits: +0x48 = 3,
  state 10 (task 0x00414680 -> 0x00414690).
- 0x009A5900 bit 0 = "players may join now": the end of Cam_BuildViews (0x00402197..0x004021C2) clears it while the
  camera runs a cutscene (mode 1) outside the demo unless bits 0x30 are set, and sets it otherwise. World:
  `TaskCamera` / `BuildViews`. 2p: start at 1400 (cutscene) -> state 10 until 2430; 2431 the camera is in mode 2,
  0x00414690 takes state 3 and runs 0x00413E50 at once (streak / shots / hits 0, Player_Init row 3: 3 lives, score
  0, players++, route++, hit timer 0xB4, callbacks 0x004150E0 / 0x00415940). 0x009A5900 is 3 at the stage start
  (title start |1, Game_ApplyState |2).
- P2's gun: the coordinator's P2 device mode (0x007DC698) is not 6, so Gun_UpdateRecords 0x0040CBB0 uses the
  player's calibration (gun record +0x16 x / +0x1E y, {0, 320, 640, 0} / {0, 240, 480, 0} in the capture) to the
  tables 0x004D1CF0 / 0x004D1CF8 (raw 384 -> 52). `WorldConfig::gunCal`, `WorldInput::deviceMode`; world_test reads
  the calibration from the capture and `--p2mode` (default 0) sets P2's mode.
- Everything else in the two-player frames (enemies picking either player, lock-on, the second player's shots,
  head look 0x00453D70 with route 2, hit shake per player, rank +4 per player) was already in the world and is exact.

## 3. Two-player rail 0x0040C9C0 (CODE)

Camera sub-mode 1/1 rail: with one player (0x009C8E80 == 1) the players' point 0x009C71E0 / angles 0x009C71EC..F4 are
the body of the player in game (0x009C7000); otherwise the midpoint 0x00401E90(P1, P2, 1, 1) = 0.5 * b + 0.5 * a (24-bit
FPU) and angles 0x00401EC0(P1, P2, 1, 1) (P1 + half the wrapped difference, idiv). Not reached by the captures: P2
joins after the intro, and the later cutscenes use sub-mode 1/3 (0x0040C380). A capture with both players starting
on the title is needed (section 7).

Fixed on the way: the 1/3 rail angles (0x00401C50 via 0x00401BE0 -> Cam_DirToAngles 0x004016B0, the 24-bit version,
and the final fpatan * 0x004C4378 rounded to float before __ftol): the players' point / angles check is exact in all
captures (play1 was 2100 / 2436).

## 4. Pause, continue, credits (CONFIRMED)

- Pause (0x0040E500, from Game_Update every frame): history 0x007C1798 / 0x007C179A; the toggle 0x0040E9C0 needs the
  pause state unchanged for the two previous frames; pause: game mode 6, sub-state 2, 0x009A5900 & 0x1C == 0
  (0x0040EAB0) and start by a player in state 5 (0x007C1792 = who); resume: start by the same player. While paused
  (0x009A1A00) Game_UpdateMode is skipped (no task, no rand), the input still runs (shots of those frames are lost).
  The 2p continue script's start presses by a playing P1 pause at 3000 and resume at 3060 - CONFIRMED (Y / E empty
  3000..3059). Not modelled: the gun-lost pause (0x009CA100 vs 0x009C6FF8), the pause sounds, 0x009C90A0 reset path.
- Continue of one player while the other plays: state 4 (0x00414200 / 0x00414280, gameplay agent) -> start ->
  0x00414FC0(t, 1) -> state 1 -> 0x00413DE0: overlay off, score +1 (continue count), rank - 1, Player_Init row 1
  (lives reset, score kept, players++). 2p continue: P1 4547 state 4 -> start 4559 -> state 1 -> 4560 state 5, 3
  lives, credits 4 -> 3. CONFIRMED.
- Whole-game continue (both out, mode-6 sub-states 3 / 4): a continue sets sub-state 2 and 0x004141D0 restarts the
  countdown of a player still in state 4 (task fn 0x00414250). CODE (no capture with a continue during sub-state 4).
- Credits: `WorldConfig::credits / freePlay / coinsStart / coinsContinue` (world_test reads 0x009C8E60 /
  0x009C8E70 from the capture: credits 5, not free play). Per-player credits (0x009C8E74 != 0, 0x00406E70) not
  modelled.

## 5. Game over: game mode 7 (0x00460960), CONFIRMED on play1 (3337..3853)

`WorldConfig::gameOver = true` makes Step() run mode 7 until the switch to the title (GameMode() == 3); the default
keeps the old behaviour (play.exe stops at mode 7). Game_UpdateInput and the pause history run in mode 7 too.

| sub-state | frames (play1) | |
|---|---|---|
| 0 | 3337 | Mem_Init; pol 0x4D, motions 0x26 / 0x27 / 0x25, texture 0x155 flushed; texture 0x14F, pol 0x4E queued; players' point / angles 0; root 0x00460BB0: camera task 0x00460E60 (Cam_ResetContexts 0x004021D0, tick 10, first run at once), rail + bodies 0x00414F20, player tasks 0x00414ED0, count 0x00414FB0; players in state 4 -> state 6 (0x00414420); timer 0x009CA0F8 = 200; 0x009A5900 &= ~2; sound 0x10000009 |
| 1 | 3338..3537 | tasks; reload (P1 2 / P2 0x20000) skips when the timer < 0xA5; timer 0 -> 2 |
| 2 | 3538 | root 0x00460CA0: Cam_ResetContexts + Cam_BuildViews, "GAME OVER" task 0x00460CD0; timer 0xB4 |
| 3 | 3539..3718 | tasks; reload skips when < 0xAF; timer 0 -> 4 (game types 2 / 3 restart mode 6: not modelled) |
| 4 | 3719 | route map setup 0x00460F00 (root 0x00460FF0) |
| 5 | 3720..3853 | 0x004610F0: tasks; reload or the walk's end (0x007DCCE4 = -1) -> Game_SetState(0x004E1014[7] = 3) |

- Camera 0x00460E60: context 0 on camera path 0x1F at the task's tick (from 10), look angles roll 0, Cam_BuildViews.
- Player state 6 in mode 7 (0x00414420 / 0x004144C0): body motion 0x004EC8B4[p] (0x338), gun model 0x00416810(t, 1),
  placed by 0x00415A80 at the players' point (0) + 0x00579EA8[(p - 2 + route 0x009C8E88 * 2)] (one player: 0);
  drawn by +0x7C 0x004151D0 (skeleton + shadow), counter + 1 while the camera tick > 0x3B (two players: tick >=
  0x004EC8C4[p] = 80 / 60), at the motion's last tick +0x7C = 0x004151B0 (no draw: from 3507).
- "GAME OVER" letters 0x00460CD0 (165 frames) spawn sprite-effect tasks 0x00499C60 / 0x00499CE0 (same update as
  hud.h SpriteFx); they are in the task list and reported in `WorldHud::effects`.
- Route map (sub-states 4 / 5): route record 0x009A5920 (one 16-byte row of scenes per stage; op 0x4D appends the
  scene at 0x009A5C30, reset by Stage_ResetState; 0xFF-filled at the mode-6 start; the default route 0x0059351C when
  row 0 is empty or its second entry is 0). Tasks: views 0x00482C40, rail / bodies / players / count again (the old
  player tasks keep running: two tasks draw the same body), agent 0x004614C0 (skeleton 0x39, walk 0x35B, end motion
  0x338; walks 4 px / frame along the path points 0x00567A04 of each (stage, scene), x before y, scrolls 0x007DCCD8,
  drops footprints 0x00461AC0 (model 0x14C2, view space, 1000 frames), marker 0x145B; 0x78 frames after arriving:
  done), route task 0x00461180 (path logic; the map tiles are hud.h RouteMapBackground(scroll):
  `WorldHud::routeMapScroll`). P2 alone: skeleton 0x3A, footprint 0x14D2; both players out: second agent
  0x004617F0 / 0x004618C0 (CODE).

## 6. Fixes outside the new paths

- Players' body draw bit: +0x12C bit 0 of both players is the camera's `playerFlags` bit 0 (op 0x31 / 0x32 / 0x4D and
  camera command 0x10 write it). World now applies the camera's writes to both players (SyncPlayerDrawBit): the
  intro body shadow drawn one frame too long (play1 2430, play2 1211, the gameplay agent's open case) is fixed.
- Actor 0x20 node hits create the node blood task 0x00407120 (0x00407200): 2p 2816.
- Enemy root interpolation 0x00410920 with 24-bit float steps (see 1).

## 7. API additions (world.h, backward compatible)

- `WorldConfig`: `gunCal[2][8]` (per-player calibration for device modes != 6), `gameOver` (run mode 7).
- `WorldHud`: `paused`, `pausePlayer` (PAUSE sprite 0x8E7 / 0x8E8), `credits`, `gameOverTimer` (0x009CA0F8),
  `effects` (sprite-effect sprites: id, x, y, sx, sy, alpha), `routeMapScroll` (>= 0 on map frames). Per player
  `state` now also shows 10 (waiting to join: blinking 0x8E9 at x 50 / 430, y 432 while frameCounter & 0x20),
  1 / 3 (one frame), 6 (out: "GAME OVER" 0x43E, 0x00416900, 120 frames in mode 6).
- `WorldCamera`: `playerPitch / playerYaw / playerRoll` (0x009C71EC..F4).

## 8. Not ported / open

(Captures (a)..(d) were recorded and checked later: notes/agents/twoplayer2.md.)

- Captures needed: (a) both players press start on the title (intro with two players: rail 0x0040C9C0 midpoint,
  P2 intro walk motion 0x34A / 0x334, offsets 0x004EC8D8[p + players * 2]); (b) a two-player game over (both out:
  route 2 body offsets / counters, map with both agents 0x004617F0 / 0x004618C0); (c) a 1p game over with a
  continue during the whole-game countdown (mode-6 sub-state 4 -> 0x004141D0); (d) a continue timeout of one player
  while the other plays (state 6 in mode 6, 120 frames, then state 9 / rejoin).
- play2's game over (4520..4739) waits for scene 1 (other agent).
- Game types 1 / 2 / 3 (special weapons, the mode-7 restart), per-player credits, the gun-lost pause, pause sounds,
  0x009C90A0 reset path, sub-state 0xB (no credits for a continue).
- HUD: the HUD port draws prompts / sprites from `WorldHud`; the world does not produce the state-10 / state-6 / pause
  sprites itself.
