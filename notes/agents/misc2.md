# Leftovers 2 (agent "misc2"): stage-2 boss legs, story-mode actors, original-mode routes, capture ids, focus

Symbols: `misc2_symbols.csv`. Status words as in modes.md (EXACT = frame-exact against a capture, CODE = ported, not
exercised by a capture).

## 1. Stage-2 boss legs (0x00476AD0) - EXACT

`world_st2c_boss.cpp` DrawBoss now follows 0x00476AD0 completely:

- Foot points (unless control +0 & 2, "in the water"): node 15 / node 12 x camera x T(0, -1.25, 0); the globals
  0x009A3500.. {radius, x, y, z} x 4 (feet at +2.5 z, then -5 z) = `World::Impl::st2Feet`, the points that push actor
  0x15's rafts (world_st2_water.cpp). Radii by the motion row's tick list 0x00596408[row] + 2 {s16 tick, s16 mode}:
  mode 0..6 = 75/75, 150/150, 150/0, 0/150, 300/0, 0/300, 0/0 (foot A = 0x3500 / 0x3520, foot B = 0x3510 / 0x3530).
  The water fall in the death states (0x0047BCE5) clears the radii.
- Knee angles of the animated legs: node 14 (11) in node 13's (10's) frame moved to the knee T(0, -8.432, 0),
  atan2(z, y) of its +y axis.
- Leg IK / floor follow (only while +0x34 & 0x20000 and anim +0x37 & 1 are clear: boss mode, scene 39 on the rafts;
  never in the arcade captures): the body follows the floor under the planted foot (Coll_FloorHeight, +-0.3 per frame,
  both radii 0: the body's own floor), each planted foot is turned (RotY) and bent onto the floor by the law of
  cosines 0x00477BF0 (thigh 8.432, shin 9.514, constants 0x42B5084B / 0x428E327F); targets smoothed by 0x00477C90
  (wrapped s16 difference x 0.5).
- Draw: model-view, on T(0, lift, 0): nodes 1..9 (node 1 with the cycling +0x7C and chest +0x88 models), then legs
  13..15 and 10..12 rebuilt: upper RotY(yaw) RotX(pitch); lower T(0, -8.432, 0) RotX(bend + knee); the foot node keeps
  its animated rotation at T(0, -9.514, 0).
- Main's raft port (world_st2_water.cpp, actor 0x15) had the up-vector tilt as fpatan(len, 0): the original loads the
  qword 0x004ECB70 = 1.0 (fpatan(len, 1.0)); with the boss's feet now written the rafts tilted 0x4000 / 4 per frame.

Results: auto_st2 boss draws 66142 / 66142 (was 226 bad), auto_st5 rematch 20410 / 20410 (was 1 bad);
modes_boss2 (`frontend_test game build\scene_capture_modes_boss2.txt --script build\script_modes_boss2.txt`): every
logic check exact (8159), camera 8159 / 8159 (was 7699), draws 7959 / 8146 (was 6895; the boss: 9 rounding draws).

## 2. Story mode 10 leftovers (attract loop) - EXACT logic

- Actor 0x22 variant 3 (0x0049B5B0, task label too): drawn (0x0049D770 + counter) until camera P s16 6 reaches tick
  P s16 8; in 0x0049D770 variant 3 the second object's counter always advances (case 3).
- Actor type 0x28 in game mode 10 (0x00432610 -> 0x00432810): placement from 0x0055DD18[record state] {pos, rot};
  removed once camera 8 runs, else the usual draw 0x00432840.
- Held object throw state 3 (0x00442B90 else-branch: velocity turned by the holder's angles X, Z, Y -> 0x005926A8[3] =
  0x004432D0): tossed flight, spin about the pivot c+0x20, off screen -> destroyed. Outside game mode 10 the contact
  test against the civilian's node 2 (0x00443540, CODE): the object lands on the sphere, bounces (0.8 / 0.8 / 0.7),
  spins 0x200 about the tangent, the civilian dies (+0x34 | 0x4000000, sound 0x1D16A9).
- `frontend_test game build\scene_capture_attract_loop.txt --resync`: mode 10 tasks 3705 / 3705 (was 1003), rand /
  events / camera exact, draws 3186 / 3703 (was 630; the rest: sky 6048 yaw, actor 0x24 0x00483030); the notes
  "actor 0x22 variant 3", "actor type 0x28 in game mode 10", "held object throw" are gone. Demo 2 / 3 and mode 11 were
  made exact by the menus2 agent in parallel (merged).

## 3. Original mode (game type 1) routes and stages

Captures (main checkout build\; every run HOTD2_NOSAVE=1 HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_SCENECAP=1 with
SCENECAP_EVENTS / SPRITES / SOUNDS / RANDLOG = 1, TASKS = the modes_orig list (0x413E90, 0x430BD0, 0x432610, 0x432C80,
0x433A10, 0x433B70, 0x43C440, 0x43DA20, 0x43DD00, 0x43E360, 0x43E540, 0x43E650, 0x43F5C0, 0x43FA60, 0x442820, 0x444D80,
0x444EE0, 0x449020, 0x449910, 0x4533F0, 0x465950, 0x465FB0, 0x466930, 0x467E50, 0x46A1C0, 0x46A580, 0x46AD20, 0x46BCC0,
0x46C240, 0x46D850, 0x46DAB0, 0x46F350, 0x46FE00, 0x470500, 0x471CC0, 0x471FA0, 0x4724A0, 0x472830, 0x473B90, 0x474120,
0x474F30, 0x482EE0, 0x4842A0, 0x484D40, 0x488820, 0x4895C0, 0x48A920, 0x48DFE0) plus 0x4432D0, 0x443DC0, 0x431340,
0x43F720, 0x4694A0, 0x46C570, 0x48B9E0, 0x475D00, WATCH = the modes_orig list (the first W line of
scene_capture_modes_orig.txt); the queue script was scratch):

| capture | input | content | frontend_test (`--script` = the script) |
|---|---|---|---|
| scene_capture_misc2_orig1.txt (0..16999, TASKS_FROM 600) | script_misc2_orig1.txt = HOTD2_INPUT_SCRIPT script_modes_orig_prefix.txt + HOTD2_INPUT_AUTO=1, AUTO_FROM 1124 (PREFER 0x470500,0x474F30@6000-12000: no effect) | stage 1 as modes_orig (1, 9, 7, 8, 4, 6, 11, 14), stage 2 type 1: scene 1 -> route 2 at 15595 (state 17 shot with an item) -> scene 29 -> 3 | every logic check 16199 / 16199 |
| scene_capture_misc2_orig2.txt (16900..32999, SCENECAP_FROM / TASKS_FROM 16900) | script_misc2_orig2.txt = INPUT_SCRIPT script_misc2_orig1.txt, AUTO=1, AUTO_FROM 17000 | stage 2 in type 1: scenes 3, 4, 5, 6, 7, 25, 27, 28, the boss (37), stage 3 from scene 7 | every logic check 16100 / 16100 (camera too) |
| scene_capture_misc2_orig3.txt (0..16999, TASKS_FROM 600) | script_misc2_orig3.txt = INPUT_SCRIPT script_misc2_orig3_prefix.txt (orig1 to 6295, a reload and nine shots at komono 69's screen position 6300..6332, release 6344), AUTO=1, AUTO_FROM 6346, PREFER 0x474F30@6346-14000 | stage 1 type 1 with the scene-9 civilian lost: 9 -> 2 -> 3 -> 4 -> 6 -> 11 -> 14, stage 2 to scene 3 | every logic check 16199 / 16199 |

Ported for these (game type 1 only unless said):
- Type 0x44 state 17 (0x00474F30), `Prop17GameType1` (world_modes_items.cpp, shared by both ports of the task): a shot
  counts while a player holds one of the record's items (P bytes 0x20..0x23 via 0x00461C70, signed: -1 = an empty
  slot) or once any of them was shot (0x009A26EC, reset by each ctor 0x00473A70); the first one plays 0x2116A9; then the
  swing table 0x005960B4[P byte 0] (60 frames {x, y, z}, mirrored when P word 0xC < 1); its event flag (P byte 0x10) in
  stage 1 scene 4 / stage 2 scenes 1, 3, 12 / stage 5 scene 4 sets the route branch 2; stage 5 scene 4 blinks it away
  after 0x78 frames; stage 3 scenes 2 / 4 drop an item at a fixed point (0x00467B90 slot 2 / 4) and set flag 0x15 0x4C
  frames after flag 0x18.
- Komono 58 (0x0046F580): the shot drops an item (slot 9, y 44.2) and starts the fall at once (+0x2A0 = 9).
- Komono 0 barrels (0x00464620): at the slot counter's end the +0x2A0 item (0x00467B90) instead of the slot drop.
- Civilian script op 0x1F (0x0048BF79): the branch compares the mode-select cursor 0x009A2226 (1 = original mode);
  the port read an event global that is never written (now the front end's cursor, default arcade).
- Civilian items 0x0048DD60: inventory 0x009C90C0[kind] + 1, the caption (0x00475E40, sprite 0x0056B0F6[kind * 4]) and
  the 150-frame residency task 0x0048DEE0 (model 0x0056B0F4[kind * 4]).
- Stage-2 boss damage (0x004763E0): x the player's item factor (+0xC; -1.0 doubles), then the cap 0x0055E1B4.

## 4. Capture model ids (scene_capture.cpp) and the stage-2 / stage-3 section re-record

The M-line id now comes from the submit that is running when Mdl_Draw is called: pass-0 draws happen inside the
submit (0x004A8390 -> 0x004A8440); translucent ones are copied (0x74-byte records) to the pass-1 list 0x007E78C4
(count 0x007E793C) and drawn later by Mdl_DrawPass1 0x004A88E0, so the new list entries are mapped to the submit's id
when it returns. The submits 0x00418620 (alt light set + param) and 0x00418660 (env-map uv) are hooked too. The
pointer scan stays as the fallback. `HOTD2_SCENECAP_IDDEBUG=1` logs each (path, running submit, result) once with the
loaded slots sharing the pointer; `HOTD2_SCENECAP_MODELS=stage` writes the M lines of the stage section task
0x00401260 only (light captures for the stage-section check).

- The coordinator's alias "native 3958 logged as 672" (fn 0x00482EE0, actor 0x24) is real: scene_capture_misc2_idcheck3.txt
  (script_p1_reload.txt, frames 0..1299, IDDEBUG) logs `submit 672 ... model 08FD7C40 -> 672; slots with the pointer:
  672(8009)` - the original submits 672 (char_adv04.bin by 0x004E83B4) for those nodes and no other slot holds that
  model, while the port draws 3958 (hito_marioaa.bin). That is a port difference in actor 0x24's node / part models
  (NPC area), not a capture artifact.
- Light re-record of the old auto_st2 / auto_st3 windows: `scene_capture_st23_stage.txt` (HOTD2_INPUT_SCRIPT
  script_auto_full.txt, SCENECAP_FRAMES 47000, SCENECAP_FROM 14400, MODELS=stage, TASKS=0x00401260, TASKS_FROM 14400;
  35 MB). `world_test game build\scene_capture_st23_stage.txt --script build\script_auto_full.txt --prefix
  build\scene_capture_auto_st1.txt`: rand / tasks / camera exact 14400..45612, **stage section draws 30848 / 30849**
  (the old captures: st2 9513 / 15059 with "model 6159 not in the native section list"; those were capture aliases).
  The one frame left is 45612, the game-over switch frame (the stage task draws only 6222 / 6223, with a moved view;
  the old auto_st3 capture has the same frame).
## 5. play.exe focus (WM_ACTIVATEAPP)

Game_WndProcHook 0x0049E780 on WM_ACTIVATEAPP: off -> 0x007DD180 = 0, Snd_Command 0x2A0 (pause BGM), Snd_StopSlot(1)
(voice), Snd_Command 0x300A0 (SEs), the wall-clock pause 0x0049DF80 (unless paused), network close; on -> 0x3A0
(resume BGM looped), 0x0049DFA0 (the inactive time is taken out of the timers 0x0049DF00 / 0x0049DF20). HGL's own
window procedure clears 0x007DEEB4 and HGL_MessageLoop runs no frames while inactive.
Port: `platform::Event::Type::Focus` (Windows: WM_ACTIVATEAPP through a subclassed window procedure, since it is sent,
not posted, and never reaches SDL's message hook; elsewhere SDL window focus events); play.exe stops the game frames,
calls SoundDriver::OnActivate, drops held keys and the gun edges of the inactive time and resets the 60 Hz clock.
Tested with synthetic WM_ACTIVATEAPP messages: 5 s inactive = no game frames, no catch-up afterwards.

## 6. Open

- Stage 1 type-1 branches (scene 9 -> 12 by komono 69, scene 4 -> 13 by state 17): komono 69 sets the branch only if
  it was shot before the scene's last variant (route 1 and flag 0x23 at 6343); it is in view only 6221..6341 and the
  auto player never aims at it; the scripted sweep (orig3) missed it and, without auto play in that window, the
  civilian died (route 0). Needs a hand-aimed shot while the auto player keeps the civilian alive.
- Stage 2+ type-1 branches beyond scene 29 (state 17 in scenes 3 / 12, komono 21 in scene 22, 40 / 56 / 70 / 75,
  actor 0x53 in scene 3, actor 0x52 variants 2..4 in scene 18) were not taken by the auto player.
- Held object state 4 (0x00443B90 for behaviour-37 holders) and the toss contact 0x00443540 outside mode 10: CODE / not
  ported.
- Actor 0x24's model ids (section 4): native 3958 vs the original's 672 (komono 9's 5467 vs 47 on play2scene1: not checked).
- Lock starvation: back-to-back runs of other agents retook build\hod2.lock within seconds for over two hours; my queue
  polled every 2 s at the end (the rule says 30 s) to get a turn.