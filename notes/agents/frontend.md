# Front end: the game from boot (World::Boot)

`game::World` now runs the whole game from boot: `World::Boot(cfg)` is frame 0 of Game_Frame (game mode 16) and every
following `Step()` is one Game_Update 0x0040E730 for whatever mode the game is in. play.exe boots like the original.
Symbols: `frontend_symbols.csv`. Test: `tests/frontend_test.cpp`.

**CONFIRMED** = code read + frame-exact against a boot capture; **CODE** = read and ported, not exercised;
**HYPOTHESIS** marked.

Files: `src/game/world_front.cpp` (Game_Update / mode switch, boot logos, title, mode select, demo cycle, game start,
story modes), `src/game/world_front_tut.cpp` (how-to-play), small additive edits in `world.h` (Boot, ForceMode,
QuitRequested, WorldHud front-end fields, WorldConfig::creditSetting / titleSkyYaw), `world_internal.h` (Impl::Front
state, declarations), `world.cpp` (Step -> FrontStep when booted; StartStage: no second input pass from the front end,
story modes without srand / 0x004809D0; the demo timer task), `world_player.cpp` (credit use in mode 4, the mode-select
join requests mode 6), `effects.h` (Sky::Create: Sky_Create's leftover yaw), `src/port/render/front_screens.*` (bitmap
screens), `src/port/tools/play.cpp` (hook-up).

## 1. Game_Update and the mode switch (CONFIRMED)

`FrontStep` = Game_Update 0x0040E730: counters (0x009A32A0, 0x009A5C50), resource steps, Game_UpdateInput (InputFrame;
mode 5 only the buttons, the demo's guns are its auto-gun as in World::Step), 0x009A5C40 + 1, the pause 0x0040E500, then
unless paused Game_UpdateMode 0x004608A0 (the mode function, then PRESS START 0x004067D0), Mode_Commit 0x0040E860,
Mode_CheckStart 0x004043C0.

- Mode_Request 0x0040E850 sets 0x007C17A0; Mode_Commit applies it at the END of the frame (World::Impl::SetGameMode +
  0x009A21C0 = 0). Mode_CheckStart runs after the commit, so a start press is committed one frame later.
- Mode_Next 0x0040E960: next = 0x004E1014[mode] (the three language rows are the same table): 3 -> 9 -> 5 -> 11 -> 10 ->
  3, 6 / 7 -> 3.
- Mode_CheckStart: title (3) with 0x009A5C44 > 2 and start (0x00404460: 0x009C9028 & 0x80008; 0x009A3790 = the player)
  -> sound 0x121A9, 0x0040AC10 (0x009A5C44 = 0, credits cleared), cursor 0x009A2226 = 0, request 4. Modes 5 / 9 / 10 /
  11 -> request 3.
- PRESS START 0x004067D0: 0x005A4D44 += 0x009A5C40 - 0x005A4D48 (0x004067A0 resets both: the counter then equals the
  boot counter again), drawn when 0x004C4B90[mode] (short, stride 4), 0x009A5C44 > 3 and (counter & 0x20) == 0.
  The world adds it to WorldHud::sprites.
- 0x009A5C44 (mode frames): reset by each mode's init, +1 per mode frame, +1 in mode-6 sub-state 2 (0x004601D0).

## 2. The modes

| mode | function | frames | ported |
|---|---|---|---|
| 16 boot | 0x0041E1F0: SEGA logo (0x0041DD60), fade 0x0041DEF0 (301 frames: +0.01 x 100, hold 100, -0.01), free, empireBlack logo, fade, 0x0041E1D0 -> mode 3 directly (no request) | 0..604 | CONFIRMED (mode / sub-state / 0x009A5C44 every frame) |
| 3 title | 0x00495EB0: init (no task run), Task_InitRoot(0x00496060): camera 0x004961A0 (path 0x1D, tick + 1 then evaluate), rail / players / count, Sky_Create, stage draw (title tables 0x00576AEC / 0x00576B04: models 6139 / 6091 / 6093), logo 0x004960A0; L/R cheat; 480 frames -> 9 | 605..1085 | CONFIRMED |
| 4 mode select | 0x00496200: Task_InitRoot(0x004962C0): selector 0x00496960, views 0x00482C40, players, screens 0x00496840; bitmaps hod2title00..11 + 8 labels x 3 tiles; cursor up / down 0x005978C8 / D0, confirm 0x005978D8 (trigger or start) | until the confirm | CONFIRMED (arcade); others refused, see 5 |
| 6 game start | 0x0045FEF0: the selector's confirm sets 0x009A21C0 = 1 and the credits (0x00406F60(0x00496B70(e))), the player's idle task (later in the list) sees the same trigger, Player_TryJoin -> Credit_Use -> Mode_Request(6), state 0; next frame mode 6 sub-state 0 = StartStage | | CONFIRMED |
| 5 attract demo | 0x00426800: demo table 0x00589828 {event mode, scene, frames, key offset, bgm, -, bgm id, key file}: (0, 4, 1893), (1, 28, 1744), (3, 5, 1744), -1 wraps; 0x00463680, StartStage (mode 5), 0x004067A0; BGM 0x0041D450 at the second frame; the demo timer task 0x00426960 (0x007DC910 - 1, < 4 -> 0x007DC90C = 0) ends it -> index + 1, Mode_Next | 1741 frames (demo 1 1890) | stage start CONFIRMED; demo content see 4 |
| 11 / 10 story | 0x0041FB00 / 0x0041F9B0: event mode 11 / 10 (adv2evtbl / advevtbl), Task_InitRoot(0x0041FAB0) = the stage tasks without 0x004809D0 / demo timer, no Stage_Start (no srand), 0x004067A0, Stage_ResetState, tasks run in the init frame; Stage_End (0x009C71C0) -> Mode_Next | 4928 / 3705 | partly, see 4 |
| 9 how-to-play | 0x004996C0: Task_InitRoot(0x00499F90): camera 0x0049AE90 (path 0x1E, held by the script), script 0x00499FE0, label / reload / torches / bullets, zombies A / B (C / D spawned at script 0x514 / 0x541), players, floor 0x17FA; 0x004997A0 also in the init frame; 1699 frames or camera tick > 0xE6 -> 5 | 1086..2784 | CONFIRMED |
| 7 game over | existing (World::GameOverFrame) -> SetGameMode(3) | | CONFIRMED (from boot) |

How-to-play details: zombie tasks keep their own counter +0x1320 (frozen while 0x007DD16C), blend motions with
Anim_BlendTo, shots 0x0049AF10 (bullet - 1, sounds, view-space flash task 0x00416E90 at the node hit point / its z),
the hit flash 0x0049AA90 (model 0x939, 0x009CA0E4 shake like Player_HitShake), the death 0x0049AFB0 (0x78 frames
sinking 0.04, then Actor_Destroy). Zombie D's part list 0x00597BE4 draws part 0x2D on its node. No rand call in the
whole mode (the capture's rand state is constant through it).

Bitmap screens (0x0049E950 / 0x0049EB30 / 0x0049EAD0 / 0x0049EAF0 / 0x0049EB50 / 0x0049EBE0 / 0x0049ED70): 64 slots
{surface, x, y, w, h, z, u, v, alpha, visible}; a slot is only added if the file loads; drawn as one TL strip each.
`WorldHud::screens` lists the visible slots of the frames that draw them; play.exe draws them with
`port::FrontScreens` (BMP reader, first in the list, no depth test). The draw state is not compared with the original
(the captures have no screen draws): visual check only (boot logo, mode select look right).

Resources: the front-end modes switch the mode-6 model residency off (their inits load their own models).

## 3. Results (`frontend_test`)

`frontend_test <game> <capture> [--script s] [--credit-setting N] [--p2mode M] [--resync] [--from F --to F] [--dump F]`
boots the world at frame 0 with the script's input and compares every capture frame; results per game mode. The task
serials are history-dependent, so an exact K line at a mode's start means every earlier mode created exactly the
original's tasks.

| capture | result |
|---|---|
| `scene_capture_hud.txt` (W from boot) | modes 16@0, 3@605, 9@1086, 5@2785 exact; mode / sub-state / 0x009A5C44 / 0x009A5C40 every frame 0..3299; camera views of title / how-to-play / demo 2694 / 2694 |
| `scene_capture_play1full.txt` + `script_p1_start.txt` | modes 3@605 4@701 6@900 7@3336 3@3853 9@4334 exact. Title: K / Z / Y 481 / 481, sprites 576 / 576, draws 479 / 479 (second title), camera 576 / 577 (3854: init frame, see 6). Mode select: sprites 199 / 199, K 41 / 41. Game (from boot, no capture input): rand / tasks / events / camera 2436 / 2436, draws 2227 / 2251 (as world_test). Game over 517 / 517. How-to-play: K / Z / camera / sprites 165 / 165, draws 164 / 164 |
| `scene_capture_2p.txt` + `script_2p.txt` (`--credit-setting 5`, default) | as world_test: game 5099 / 5099 rand / tasks / camera / credits (credits 6 at the confirm, 5 after P1's join, 4 after P2's: setting 5 fits), draws 1996 / 2018; mode select credits 41 / 41 |
| `scene_capture_play2hud2.txt` + `script_p1_reload.txt` | title / mode select / game / game over / how-to-play 778 / 778 (K, Z, camera, sprites), draws 777 / 777 |
| `scene_capture_attract_loop.txt` (K / W / Z from 15400; E / C / F from boot) | first loop: 16, 3, 9, 5 exact; the stage-1 demo ends on time (4676) but its play diverges after the old capture's end (camera 3817, events 4118: world, not front end); mode 11 ends 20 frames early (unported actors), so the second loop is 20 frames early natively. With `--resync` (restart each mode at the capture's switch with its first serial and rand state, World::ForceMode): title 25865.. K / Z / draws exact (camera 480 / 481); how-to-play 26346..28044 K / Z / camera 1699 / 1699, draws 1698 / 1698; mode 10 22160..25864 rand 3705 / 3705, events 3704 / 3704, camera 3703 / 3705 (one 0.004 rounding frame), tasks / draws differ (unported actor 0x00499A80, sky yaw); mode 11 17232..: K diverges at 17233 (actor 0x00434DA0), events 17413, rand 18016; demo 2 (stage 2 scene 28) 15490..: K to 15563 (first: missing thrown-weapon task 0x0045A4F0 at 15564), rand to 15615; demo 3 (stage 4) 28045..: K to 28365, rand to 28737 |

world_test regressions (`build\regress`, all eight runs) unchanged against the pre-change baseline except auto_st2,
which improved with main's stage-2 merge: stage3 demo 814 / 814, play1full 2436 + 517, 2p / 2p_continue 5099, play2scene1
3620, auto_st1 13899 / 13899, auto_st2 --stage-start 14659 15141 / 15141 rand / tasks / events, auto_st3 --stage-start
29736 15877 / 15877 + 763 game-over frames.

## 4. Not ported / open (reported through World::Notes where reached)

- Mode select entries 1..6: done since (modes.md sections 5..8; network = the lobby without a network, modes.md 8.6).
  Exit (7) quits (`World::QuitRequested`).
- Story modes 10 / 11 (update, modes.md section 8): 0x00434DA0 (mode 11 title card, 200 frames), actor 0x50
  (0x004997E0 -> 0x004998A0 / 0x00499A80) and actor 0x26 state 0 (0x0048E350) are ported: every mode switch of the
  attract loop is on the original's frame (no resync needed); mode 10 rand / events exact, tasks to 22860; mode 11 events
  / camera exact, tasks to 18035. Still open: actor 0x24 / 0x25 (NPC) functions, actor 0x28 in mode 10, actor types 36 /
  45, kind 14, actor 0x22 variant 3, sky panels, the env interpolator, the sky yaw leftover.
- Demos 2 / 3 (update): demo 2 rand / tasks exact for the whole demo (the held object's throw 0x00442B90 state 5);
  demo 3 tasks to 28988, rand to 28980 (an actor 0x31 rand call in stage 4 scene 5; the old capture has no Y lines
  there; a new capture with them is still wanted). Since the fourth menus pass (modes.md section 9): demo 3 exact
  (actor 0x31 behaviours 24 / 25 / 32), new capture build\scene_capture_menus2_attract.txt (HOTD2_DEMOKEY_GUARD).
- The demo input files etc\key_*.bin (0x00426980) are not in the installed files; the demos' shots come from the
  auto-gun (as in World).
- Sky_Create's leftover yaw (Sky::Create): the title uses 180687044 (first title of a boot, HYPOTHESIS; the second title
  of play1full matched with the same value); modes 10 / 11 start from StartStage's sky (yawInit) - the original's
  leftover differs (mode 10 sky draws 1.7 off).
- Tracer rolls (0x009A2460 + 0x18) are kept across stage starts in the front end (never reset by the original), but
  their values after a long attract history are not reproduced (demo 2 tracer draws at 15508).
- 0x00413CC0 mode-select dimming after 18000 idle frames: counted, not drawn (HYPOTHESIS: 0x009C9020 / 0x009A37A0 =
  any input); the keyboard masks 0x005978E0 / E8 with 0x009A37A8 are not driven (play.exe maps Up / Down to the pad
  bits 0x10 / 0x20).
- Init-frame camera: the frame after a mode switch builds no views (title / map setup); the F line shows the reset
  context while the native view is the previous one (3854, 4741; the 3719 case of world_test).
- 0x0049DFD0 (once per mode switch): the original renders nothing in such frames (no draw lines); frontend_test does
  not compare frames without draw lines (counted as "not rendered"). The renderer skip itself is not modelled.
- Joins via AUTORELOAD (0x007C17AC) at the top of Game_Update: off in the captures, not ported.

## 5. Captures wanted

- A boot capture with K / W / S lines from frame 0 through a whole attract loop (sprites of modes 10 / 11, the boot
  screens' state 0x007DDAA8.., 0x009A5C44), and W 0x009A2460 for the tracer rolls.
- The mode select with cursor moves and the dimming (0x007C1EB0 / 0x009C9020 / 0x009A37A0 / 0x009A37A8).
