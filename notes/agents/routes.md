# Alternate routes: survey and ports

The whole-game exactness was verified along the auto-play route only (`script_auto_full.txt` /
`script_auto_full2.txt`). This note lists every route branch of the arcade game, which scenes the auto-play
captures visit, and what was ported / verified for the others.

## 1. How the route branches

- **Scene table** 0x00597890[event mode] (stage 1 = mode 0 at 0x00597378): one entry {s16 kind, a, b, c} per scene,
  read by `Evt_NextVariant` 0x0045F000 (event op 0x4F) when the scene's last variant ends:
  kind 0: next scene a; kind 1: next scene {a, b, c}[route branch 0x009C88A4]; kind 2: the stage ends and the next
  stage starts at scene a (scene + 1 is a -1 entry). `0x009C88A4` is reset to 0 by every op 0x4F (each variant), so
  a writer must act during the scene's last variant.
- **Writers of 0x009C88A4** (all code refs, `ghidra\xrefs\datarefs_all.csv`):

| writer | address | value | game type |
|---|---|---|---|
| civilian script op 0x19 (Civ_RunBlock) | 0x0048BED5 | arg (1; in the rescued path of most scripts, in the killed path of stage-1 scene 6 and stage-2 scene 6, see the tables) | all |
| actor 0x21 (stage-2 rider) death | 0x00451AE9 | 1 | all |
| actor 0x2C (shootable switch, 0x00432D50 / 0x00432DB0) | 0x00432D55.. | 0 at spawn, shot toggles 0 / 1 | all (not used by any stage table) |
| actor 0x52 variants 2..4 (critter, 0x0043F720) shot | 0x0043F797 | 0x00564442[variant] (2 / 1 / 2) | type 1 (the ctor 0x0043F4C0 removes variants >= 2 otherwise) |
| actor 0x53 | 0x004314D9 | 2 when 0 | P s16 1 >= 2: type 1 |
| actor 0x64 route check (stage 6) | 0x00435FB0 | 1 all civilians met rescued; 2 score (type 1) | |
| actor 0x65 ending branch | 0x00436140 | 0 / 1 / 2 | |
| komono 14 route sign (0x00468180), 19 shutter (0x00468F00) | | shot flips 1 - state; set at spawn | all |
| komono 25 (0x00469AE0) | 0x00469B40 | 1 when shot in stage-2 scene 23 (within 254 frames) | all |
| komono 21 (0x004694A0), 40 shatter (0x0046C570), 56 (0x0046F090), 69 / 70 / 75, Prop44_State17 (0x00474F30) | | 2 | type 1 (Original mode) |

- **Players' route** 0x009C8E84 (1 or 2 players) is a separate switch: event ops 0x01..0x04 spawn only with one
  player, 0x05..0x08 only with two (extra enemies of 2-player games; listed per scene below as "2P spawns").

## Visual check

Light run (windowed, keep-active, SCENECAP + SHOTS only) of `script_st1_noresc9.txt`: reference shots
`build\shots_routes\shot_06500 / 07300 / 07600 / 08000.bmp` (scene 2, scene 3 frogs / barrels). `play.exe game --script
build\script_st1_noresc9.txt --screenshot p_%d.png --every 100 --frame 8000` at the same frames: mean abs pixel
difference 4.0 / 7.0 / 10.0 / 8.4 (credit line "CREDIT(S) 5" vs FREE PLAY, shot flashes); barrels, frogs and the
door match by eye.

## Steering the auto player

`HOTD2_INPUT_AUTO_SKIP=fn[,fn...][@from-to]` / `HOTD2_INPUT_AUTO_PREFER=...` (auto_play.cpp): targets whose task
function is listed are never / first aimed at in the boot-frame window. Combine with `HOTD2_INPUT_SCRIPT` +
`HOTD2_INPUT_AUTO_FROM` to replay a known script up to the branch. A quiet window (script lines up to F, a release
line, auto from G) lets a civilian die (no-rescue). PREFER on the stage-2 route sign (komono 14, 0x00468180,
18300..18440) did not change the route (the sign was shot an even number of times or not at all): to be retried with
a window of one shot.

## 2. Survey (arcade, game type 0)

Scene numbers are 0x009A2BC0. "Visited" = reached by an auto-play capture (`scene_capture_auto_st1..st6_end`) or
the no-rescue capture (`scene_capture_norescue_st1`, stage 1 scenes 10 / 3). Branches whose writer only exists in
game type 1 (Original mode) are marked "type 1".

### Stage 1 (mode 0, st1evtbl.bin)

| scene | branch condition | next scenes | visited |
|---|---|---|---|
| 0 | - | 1 | yes |
| 1 | civilian 0x00978CA8 rescued (op 0x19 = 1) | rescued 9, not 10 | 9 yes; 10 yes (norescue) |
| 10 | - | 3 | yes (norescue) |
| 3 | - | 4 | yes (norescue to 7099; st1_noresc9 whole scene; exact) |
| 9 | civilian 0x0097BEE4 rescued; type 1: komono 69 -> 2 | rescued 7, not 2, type 1: 12 | 7 yes; 2 yes (st1_noresc9); 12 no (type 1) |
| 2 | - | 3 | yes (st1_noresc9, exact) |
| 7, 8 | - | 8, 4 | yes |
| 12 | - (type 1 only) | 8 | no |
| 4 | type 1: Prop44_State17 -> 2 | 6, type 1: 13 | 6 yes; 13 no (type 1) |
| 13 | - (type 1 only) | 6 | no |
| 6 | civilian 0x0097B038 shot / killed (op 0x19 in that path) | killed 5, rescued 11 | 11 yes; 5 yes (st1_route5) |
| 5, 11 | - | 14 | yes (5: st1_route5) |
| 14 | boss; stage end | stage 2 at 0 | yes |

### Stage 2 (mode 1, st2evtbl.bin)

| scene | branch condition | next scenes | visited |
|---|---|---|---|
| 0 | rider (actor 0x21, 0x00977BD0) killed -> 1 | killed 1, escaped 11 | 1 yes; 11 yes (st2_escape) |
| 1 | type 1 -> 2 | 2, type 1: 29 | 2 yes; 29 no |
| 2 | - | 3 | yes |
| 3 | type 1 (actor 0x53 v2 / Prop44) -> 2 | 4, type 1: 30 | 4 yes; 30 no |
| 4 | - | 5 | yes |
| 5 | route sign (komono 14, 0x009791D8): spawn sets 0, a shot flips (1 - state) | 1: 6, 0: 21 (sign not shot) | 6 yes; 21 yes (st2_route21) |
| 6 | civilian 0x0097A038 shot / killed (op 0x19 in that path) | killed 17, rescued 7 | 7 yes; 17 yes (st2_route17) |
| 7 | shutter (komono 19, 0x0097A578) shot flips | 1: 25, 0: 8 | 25 yes; 8 yes (st2_shutter, exact) |
| 8 | type 1 -> 2 | 10, type 1: 32 | yes (st2_shutter, exact) |
| 10, 9 | - | 9, 28 | yes (st2_shutter, exact) |
| 11, 13 | - | 12, 14 | yes (st2_escape, exact) |
| 12 | type 1 -> 2 | 13, type 1: 31 | yes (st2_escape, exact) |
| 14 | civilian 0x0097FA20 rescued | rescued 22, killed 15 | yes (st2_escape, exact); 15 yes (st2_route15) |
| 15, 16 | - | 16, 35 (stage end) | yes (st2_route15 / st2_route21) |
| 17, 19, 20 | - | 18, 20, 35 | yes (st2_route17) |
| 18 | type 1 (actor 0x52 critters / items) -> 2 | 19, type 1: 33 | yes (st2_route17) |
| 21 | - | 16 | yes (st2_route21) |
| 22 | type 1 -> 2 (komono 27 reads route == 1) | 23, type 1: 34 | yes (st2_escape) |
| 23 | komono 25 (0x00986ECC) shot in time -> 1 | 1: 26, 0: 24 | yes (st2_escape: 26; st2_route24: 24) |
| 24, 26 | - | 9, 27 | 26 yes; 24 yes (st2_route24, exact) |
| 25 | - | 27 | yes |
| 27, 28 | - | 28, 37 (stage end) | yes |
| 29..34 | type 1 only | 3, 5, 14, 9, 20, 23 | no |
| 35 | stage end (stage-2 boss) | stage 3 at 0 | yes (st2_route21 / 17 / 15) |
| 37 | stage end | stage 3 at 7 | yes |

### Stage 3 (mode 2, st3evtbl.bin)

| scene | branch condition | next scenes | visited |
|---|---|---|---|
| 0 (start after stage-2 scene 35) | civilian on the boat (0x0048B140) rescued | rescued 3, killed 1 | yes (st2_route21: 3; st3_route1: 1) |
| 1, 3 | - | 2, 4 | yes (st3_route1, st2_route21) |
| 4 | type 1 -> 2 | 5, type 1: 10 | yes (st2_route21) |
| 5, 6, 10 | - | 6, 13 (stage end), 6 | 5 / 6 yes (st2_route21); 10 no (type 1) |
| 7 (start after stage-2 scene 37) | civilian 0x0097E590 rescued | rescued 8, killed 9 | 8 yes; 9 yes (st3_route9) |
| 8, 9 | - | 2, 4 | yes |
| 2 | - | 11 (boss) | yes |
| 11 | boss; stage end | stage 4 at 0 | yes |
| 13 | stage-3 boss in mode 1 (0x007DC6F1 = 1); stage end | stage 4 at 4 | yes (st2_route21, see section 4) |

### Stage 4 (mode 3, st4evtbl.bin)

| scene | branch condition | next scenes | visited |
|---|---|---|---|
| 0 | type 1 -> 2 | 1, type 1: 16 | 1 yes |
| 1 | civilian 0x00978584 rescued | rescued 2, not 3 | 2 yes; 3 yes (st4_noresc1, st4_routeB) |
| 2 | - | 23 (boss, stage end) | yes |
| 3, 11, 13, 8, 6 | - | 10, 10, 6, 6, 25 (stage end) | 3 yes (st4_routeB, st4_noresc1); 11 yes (st4_routeC9); 13 yes (st4_routeB13); 8 / 6 yes (st4_route4) |
| 4 (start after stage-3 scene 13) | civilian 0x0097A978 rescued | rescued 5, not 9 | 5 yes (st4_route4); 9 yes (st4_routeC9) |
| 5 | type 1 -> 2 | 7, type 1: 21 | yes (st4_route4) |
| 7 | type 1 (actor 0x52 critters) -> 2 | 8, type 1: 22 | yes (st4_route4) |
| 9 | type 1 (komono 56 / 40) -> 2 | 11, type 1: 17 | yes (st4_routeC9) |
| 10 | type 1 (actor 0x52 critters 0x0097D9E8 / 0x0097DA10: 1 or 2) | 12, type 1: 18 / 19 | yes (st4_routeC9, st4_noresc1) |
| 12 | civilian 0x0097D7CC rescued | rescued 14, not 13 | 14 yes (st4_routeC9, st4_noresc1); 13 yes (st4_routeB13) |
| 14 | type 1 -> 2 | 15, type 1: 20 | yes (st4_routeC9, st4_noresc1) |
| 15 | - | 23 | yes (st4_routeC9, st4_noresc1) |
| 16..22 | type 1 only | 1, 11, 12, 12, 15, 7, 8 | no |
| 23 | boss; stage end | stage 5 at 0 | yes |

### Stage 5 (mode 4), stage 6 (mode 5), ending (mode 9)

| stage | scene | branch condition | next scenes | visited |
|---|---|---|---|---|
| 5 | 4 | type 1: Prop44_State17 -> 2 | 5, type 1: 6 | 5 yes; 6 no |
| 6 | 3 | actor 0x64 (state 0): every civilian met was rescued (0x009CA0EC == 0x009A21BA) -> 1; type 1 score -> 2 | 4, 1: 5, 2: 9 | 4 yes; 5 yes (game5); 9 no (type 1) |
| 6 | 5 | actor 0x64 state 1 (op 0x0A short record 0x0097A0E8): type 1 score only | 6, type 1: 7 / 8 | 6 yes (game5); 7 / 8 no (type 1) |
| 6 | 9 | actor 0x64 state 2: score < 100000 -> 1 | 10, 1: 11 | no (scene 9 is type 1 only) |
| 6 | 4..11 | - | 12 (final boss) | 4 / 6 yes |
| ending | 3 | actor 0x65 (0x00436140): score class 0 (>= 80000) -> 1; else the score's last digit (continues) 0 -> 2 | 0: 4, 1: 5, 2: 6 | 5 yes (game5); 6 yes (game6); 4 yes (game9) |

**2P spawns** (event ops 0x05..0x08, two players only) exist in most scenes of every stage (extra enemies, stage-2
scene 21 and stage-3 scenes 1 / 7 also civilians). Only stage-1 scene 1 is covered by two-player captures (`2p`,
`2p_continue`).

## 3. Ported and verified

**Stage 1 no-rescue route (scene 1 -> 10 -> 3)**, `world_routes.cpp`:
- actor type 0x11 (frogs of scene 3): ctor 0x0043A080, task 0x0043A1E0, control block 0x38 bytes at +0x1310,
  command list P s16 5.. (0 wait camera / tick / motion, 3 turn to a yaw), states 0x00592660: 0 wait 0x0043A9A0,
  1..3 hop 0x0043AA10 (screen-edge limits with `_CIacos` 0x004AD0B0 computed at 53-bit precision: the CRT sets the
  control word to 0x27F), 4 / 5 look 0x0043B0E0, 6 leap at a player 0x0043B270 (Player_TakeHit, then falls away and
  is removed), 7 sit 0x0043B880 (croak), 8 die 0x0043B990 (knocked back from the camera, blood pool, sinks); shot
  0x0043A2E0 (+80 points), throat pulse model 0xB94 + frame on node 2 (0x0043A440), body point lift 1.0, collision
  sphere at node 1 pushed by other targets (0x0043A500).
- komono kind 6 (0x004668A0): model cycler 0x1032..0x1062.
- komono kind 0: barrel groups 0x00593CF0 (ctor 0x00462A80, task 0x00464620; corners 0x005937F8 / 0x00465590;
  break animation 0x00465500 (prop motion 0x1D9); 15 pieces 0x00465170 / 0x004653B0; supports, shake, fall and
  rock; item counters). Table 0x007DCDD4 is kept with ghost entries for removed barrels (the original keeps stale
  pointers).
- Fixes found on the way: hanging props (type 0x44 state 1, 0x00473CF0) are collision-mesh targets (+0x14C mesh,
  +0x150 model-view) - the scene-3 door pushes the chainsaw enemy; actor 0x29 (0x00432C80) does not draw on the
  frame it ends.
- `world_test game build\scene_capture_norescue_st1.txt --script build\script_norescue_st1.txt`: every logic check
  exact 901..7099 (rand, sites, tasks, enemies, players, camera, lock-on, sounds); draws 5979/6013 after the merge
  with main (bat skeleton draw now ported there; the rest: actor 0x22 / 0x25 rounding as on the auto captures).

**Stage 1, scene 9 not rescued (scene 9 -> 2 -> 3 -> 4 -> 6 -> 11 -> 14)**: capture `build\scene_capture_st1_noresc9.txt`
(frames 5600..14999, 700 MB), input `build\script_st1_noresc9.txt` (= `script_auto_st1.txt` to 5685, no input
5686..6299, auto from 6300). Scene 2 is new; scene 3 is played from its start to its end.
- `world_test game build\scene_capture_st1_noresc9.txt --script build\script_st1_noresc9.txt --prefix
  build\scene_capture_auto_st1.txt`: **every logic check exact 5600..14999** (through the boss and the stage clear);
  draws 8886/9395 (merged with main): the stage-clear rescued civilians 0x00435760, bats, 0x00491700 as on auto_st1, plus one frame
  (6359) where actor 0x24's parts 0x11DD / 0x11DF (models 4573 / 4575) are drawn one frame early natively.
- No new code was needed for scene 2 (types 0x44 states 0 / 1, komono 4 / 7, actor 0x24, enemies).
- Not reached: stage-1 scene 5 (rescuing the scene-6 civilian 0x0097B038). The auto player never shoots its two
  attackers in time (they stand behind the civilian: civilian-blocked aim points), so a capture needs hand-made input.

**Stage 2, shutter left closed (scene 7 -> 8 -> 10 -> 9 -> 28 -> 37)**: capture `build\scene_capture_st2_shutter.txt`
(19300..24999, 390 MB), input `build\script_st2_shutter.txt` (= `script_auto_full.txt` to 19399, then auto with
`HOTD2_INPUT_AUTO_SKIP=0x468F00@19400-19700`: the shutter komono 19 is never aimed at).
- Ported: komono kind 49 (generic ctor case 0x31 -> 0x0046E6E0, a pot rolling on 13 rim points 0x00594A68 when
  shot), actor type 0x44 state 4 (0x00472EB0: the hanging prop with other parameter offsets and a scale), enemy
  behaviour 41 (0x0045BE30: walk to the list point within 5, then behaviour 35 or the script end); props' event-flag
  writes now reach the event flags (`AfterPropUpdate`: the hanging door 0xA60 sets its trigger flag 0x23 when flag
  0x36 is set, and opens).
- `world_test ... --prefix build\scene_capture_auto_st1.txt`: **every logic check exact 19300..24999**; draws
  4933/5700 (blood pools 0x00407E30.., komono 58 / 60, breakables as on auto_st2).

**Stage 2, rider escaped (scene 0 -> 11 -> 12 -> 13 -> 14 -> 22 -> 23 -> 26)**: capture
`build\scene_capture_st2_escape.txt` (15050..22499, 366 MB), input `build\script_st2_escape.txt` (auto from 15100
with `HOTD2_INPUT_AUTO_SKIP=0x451720,0x451860,0x451980,0x451D20@15100-16200`: the rider actor 0x21 is never aimed at).
- Ported: komono kind 33 (generic ctor case 0x21 -> 0x00472950, model sequence), actor type 0x44 state 8 (0x00473260
  -> 0x004748C0: prop skeleton 0x10 / motion 0x1D3 while its event flag is set, then 72 pieces flying apart with the
  motion's frame 0x22 -> 0x23 velocities, floor bounce with 3 rands each, drawn at alpha 0.95).
- `world_test ... --prefix build\scene_capture_auto_st1.txt`: rand / rand sites / events / enemies / players / camera
  exact **15050..20075**; task list to 19753. Blockers (not ported here: NPC agent's area, actor 0x24 / 0x31 extras):
  actor 0x24 variant 4 (task 0x004832E0: walks off along (0.646, 0.614) for 0x27 frames; only the task label differs,
  logic stays exact) at 19754, and actor 0x31's thrown weapon 0x004504E0 / task 0x00450780 (states 0x00592AE0:
  0x0044FD40 flight -> Player_TakeHit(target, 1, 6) -> stuck on the screen 0x1E + 0x3C frames; 0x00450050 deflected
  when shot) at 20075. One debris draw (entry 72 of state 8, a position read past the motion frame's 72 positions)
  differs.

**Stage 2, rider escaped (continued)**: with civilian op 0x10 function 0x0048D9F0 (fall, per-frame 0x0048DA20), komono
11 / 25 / 27 / 28 / 29 / 32 / 36 / 42 / 47 / 64, actor 0x44 states 5 / 6 / 7 / 9 / 16, the chain (komono 24) and actor
0x31 behaviour 23: **every logic check exact 15050..22499**; the debris draw 72 difference was print precision (the
debris is drawn from the view: `Draw(..., viewSpace)`).

## 4. Route coverage, round 2 (routes2a)

> Stages 4-6 and the ending (routes2b): section 5.

### Steering: the native route planner

`world_test ... --plan out.txt` replays the script natively to `--plan-from F`, then plans player-1 input up to
`--plan-end E` with the native shot preview (`World::ShotTargets` / `World::PreviewShot`, world_plan.cpp: the hit task's
selection without side effects) and writes the whole input script. Options: `--plan-prefer fn@a-b` / `--plan-skip
fn@a-b` (0 = any target) aim filters, `--plan-quiet a-b` (no input: a civilian dies), `--plan-civ a-b` (aim at the
civilians: kill one), `--plan-watch fn`, `--plan-debug fn|1`. The log prints scene / variant / route (0x009C88A4) /
lives changes. The original then replays the planned script where the port is known exact and the hook's auto player
takes over (`HOTD2_INPUT_SCRIPT` + `HOTD2_INPUT_AUTO=1` + `HOTD2_INPUT_AUTO_FROM`); its applied input
(`game\input_auto.txt`) is kept as the capture's script. Note: `HOTD2_SCENECAP_FRAMES` counts from boot (it is the
last frame + 1, not a count).

### Captures (all with `--prefix build\scene_capture_auto_st1.txt --no-gameover`)

| capture (frames) | script | route | world_test |
|---|---|---|---|
| `scene_capture_st1_route5.txt` (8300..10499) | `script_st1_route5.txt` (civilian of scene 6 shot: `--plan-civ 8396-8640 --plan-skip 0@8396-8640`, auto from 8546) | st1 6 -> 5 -> 14 | every logic check exact; draws 2120 / 2200 (0x00491700 as on auto_st1) |
| `scene_capture_st2_route21.txt` (18400..47499) | `script_st2_route21.txt` (route sign not shot, auto from 18541) | st2 5 -> 21 -> 16 -> 35, st3 0 -> 3 -> 4 -> 5 -> 6 -> 13, st4 4 -> 5 -> 7 -> 8 -> 6 -> 25 | logic exact 18400..43718 except the camera from 40053 (st3 scene 13, below) and one enemy position at 33483 (below); stage 4 (from 43719) is routes 2b's |
| `scene_capture_st2_route17.txt` (18700..28999) | `script_st2_route17.txt` (civilian of scene 6 shot: `--plan-civ 18828-19000 --plan-skip 0@18828-19000`, auto from 19075) | st2 6 -> 17 -> 18 -> 19 -> 20 -> 35 | every logic check exact; draws 9593 / 10118 |
| `scene_capture_st2_route15.txt` (21000..29499) | `script_st2_route15.txt` (civilian of scene 14 left alone: `--plan-quiet 20733-21100`, auto from 21181) | st2 14 -> 15 -> 16 -> 35 | every logic check exact; draws 8094 / 8497 |
| `scene_capture_st2_route24.txt` (22800..25499) | `script_st2_route24.txt` (komono 25 not shot: `--plan-skip 469AE0@22100-23000`, auto from 22916) | st2 23 -> 24 -> 9 | every logic check exact; draws 2320 / 2700 (blood pools); komono 16 / 26 draws exact |
| `scene_capture_st3_route9.txt` (33500..36499) | `script_st3_route9.txt` (civilian of scene 7 left alone: `--plan-quiet 33150-33500` on `script_auto_full.txt`, auto from 33641) | st3 7 -> 9 -> 4 | every logic check exact; draws 2594 / 3000 (blood pools 0x00407E30.. as on the auto captures) |
| `scene_capture_st3_route1.txt` (29200..38399) | `script_st3_route1.txt` (the route-21 script with `--plan-quiet 31700-32600` from 29300: no shots while the boat's civilian dies, auto from 32601) | st3 0 -> 1 -> 2 -> 11 | every logic check exact except the camera at 32580..32582 (yaw 2 units, at the scene-0 end); draws 8822 / 9018 |

Command line: `world_test game build\scene_capture_<tag>.txt --script build\script_<tag>.txt --prefix
build\scene_capture_auto_st1.txt --no-gameover`.

### Visual check (round 2)

Light run of `build\script_st2_route21.txt` (windowed, keep-active, SCENECAP with FROM 99999 / FRAMES 40700 + SHOTS):
`build\shots_routes2a_vis\shot_19800 / 22600 / 27500 / 33300 / 35600 / 36800 / 39000 / 40500.bmp` (st2 scenes 21, 16,
35 boss; st3 scenes 3, 4, 6, 13 boss). `play.exe game --script build\script_st2_route21.txt --screenshot p_%d.png
--every 100 --frame 40500` at the same frames: mean abs pixel difference 5.3 / 4.0 / 8.5 / 9.1 / 4.7 / 6.9 / 8.8 / 9.4
(credit line, shot flashes); the fish, rafts, boss, boats and stage-3 boss match by eye.

### Ported (round 2)

- Stage 2 scene 21 / 16 / 35: actor 0x42 fish school (world_routes_fish.cpp: 0x0042F9B0, tasks 0x0042FCA0 / 0x00430C80 /
  0x00431000, leap 0x0042FC00), actor 0x31 behaviour 23 (0x0044E830), enemy behaviour 19 (0x00457620), komono 59
  (0x0046F750, an invisible target). The water of actors 0x15 / 0x17 is main's world_st2_water.cpp; its raft was fixed to
  the original here (explosion shake 0x009C8E8C, the up-tilt atan2 against 1.0 = 0x004ECB70, drawn from the view); the stage-2 boss's leg IK / foot points are main's (misc2, merged here: boss mode boss 2
  camera 8159 / 8159).
- Stage 2 scene 17 / 18 / 24: komono 16 (0x00462FE0 -> 0x00468640, splash 0x00468CA0), komono 26 (0x00469C80; the
  vertex waves of model 0x197F are not modelled), komono 71 outside type 1 (its ctor rands; the sites of case 0x47 are
  0x004627AD / CB / EA / 0x00462808), the scene-0x12 wall of 0x0046B040.
- Stage 3: vehicle riders' behaviour 48 (0x0045D500: jump off the boat) and behaviour 38 leaving when the vehicle is
  flagged 0x400000 (still drawn in the vehicle's frame that frame; both exact on st3_route1); civilian head look mode 5 (0x0048D1F0: a point in the civilian's / vehicle's frame); civilian op
  0x10 functions 0x0048DBD0 (per-frame 0x0048DC10: move by a local velocity) and 0x0048DB90 (0x0048DBC0: rise); the
  stage-3 boss rocks in modes 1 / 2 (0x00420180).
- Motion residency: op 0x57 releases a motion file until it is loaded again (within a stage).
- Removed actors 0x20 (TaskActor20, world_actors.cpp) are proper Actor_Destroy (flags 0x80018000, target copy entries
  nulled): a removed one no longer pushes the stage-2 rider.

### Open

- **Fixed (polish3, mx::Inverse bit-exact: camera to 43837):** Stage 3 scene 13 (boss mode 1, task 0x004231C0): the camera turn to the head node (end of 0x004231C0) differs by 1..3
  angle units in 40053..40067 (one fast pan; boss mode capture boss4 shows the same 0.3448 at 3689..). The chain
  0x00421F20 already differs by single angle units in 36 frames from 39142 (0x004231C0 draws, all < 1e-2; mode 0 is
  exact on every capture). Side note: in modes 1 / 2 the chain's path walks wrap to the int (-1 & 0xFFBF) + 0x11E =
  0x100DD (the original then reads past the 0x77C4-byte block; Move5 stores 0xDD as a short); not reached in these
  captures, the port keeps 0xDD.
- Enemy 70519 (a rider of the boat removed at 33483): the original reads the freed vehicle block (garbage position).
- **Fixed (polish3, motion residency: stage lists 0x00412FD0 + on-demand loads 0x00412F50):** Stage-2 scene 17: actor 0x44 state 7 (0x00474770) draws from 20138 natively, the original only from 20357 when its
  motion file 0x1B is loaded (op 0x56): motion files never requested in the stage count as resident natively; the
  original's set of resident files at a stage start is not modelled (a strict "loaded this stage" breaks the
  breakables).
- **Fixed (polish3, mx::Inverse):** Held objects (0x00444D80 / 0x00444EE0 debris) drift by 0.002 .. 0.28 in st2_route17 (corner / floor contact
  0x00444280 unverified; leftovers' area).
- Behaviour 23's trigger on the context-2 tick 0x009A6458 (never true in these captures).

## 5. Route coverage, round 2b (stages 4-6, ending)

Captures (all `world_test game build\scene_capture_<tag>.txt --script <script> --prefix build\scene_capture_auto_st1.txt`
unless noted; the route4 / noresc1 captures start at a stage start instead):

| capture (frames) | script | route | world_test |
|---|---|---|---|
| `st4_route4` (42548..49251) | `script_st4_route4.txt`, `--stage-start 42548` (no prefix) | st4 4 -> 5 -> 7 -> 8 -> 6 -> 25 | every logic check exact; draws 6522 / 6523 (44581: load timing) |
| `st4_noresc1` (44107..57999) | `input_auto_st4_noresc1.txt`, `--stage-start 44107` (no prefix) | st4 1 not rescued: 3 -> 10 -> 12 -> 14 -> 15 -> 23 | every logic check exact except the task list from 53969 (boss wound mark, below); draws 13505 / 13505 |
| `st4_routeC9` (43700..47299) | `script_st4_routeC9.txt` (planner; scene 4's civilian left to die) | st4 4 -> 9 -> 11 -> 10 -> 12 -> 14 -> 15 -> 23 | **every check exact**, draws 3596 / 3596 |
| `st4_routeB` (46300..47599) | `script_st4_routeB.txt` (planner; scene 1's civilian left to die) | st4 3 -> 10 | **every check exact**, draws 1300 / 1300 |
| `st4_routeB13` (48300..50399) | `script_st4_routeB13.txt` (planner; scenes 1 and 12's civilians left to die) | st4 12 -> 13 -> 6 -> 25 | **every check exact**, draws 2100 / 2100 |
| `game3_st5` (56300..64399) | `input_auto_game3.txt` (`--no-gameover`) | st5 0 -> 1 -> 2 -> 3 (Boss2 rematch soft-lock, below) | **every check exact** (behaviour 25 + burst), draws 7918 / 7918 |
| `game5` (70500..96691) | `input_auto_game5.txt` | all 18 civilians rescued: st6 0 -> 1 -> 2 -> 3 -> **5 -> 6** -> 12, ending 3 -> 5 (score 82630) -> title | **every logic check exact**; draws 26105 / 26142 (final-boss rounding 0x00426C30 / 0x00429D30 / 0x0042BF30 / 0x0042CDD0, 0x004842A0, one 0x004132D0 frame at the scene-12 load point) |
| `game6` (76900..95807) | `input_auto_game6.txt` | game5 to 76999, then scene 5's items not shot: score 78430, no continue -> **ending 3 -> 6 -> 7** | **every logic check exact**; draws 18827 / 18859 (the same kinds) |
| `game7` (71400..95170) | `input_auto_game7.txt` | game5 to 71500, no input to 72800 (5 hits, continue), items skipped: 5 continues at the final boss, **game over in stage 6** | **every logic check exact**, game over 958 / 958 frames; draws 22736 / 22798 |
| `game8` (77600..99999) | `input_auto_game8.txt` | as game7, but only the score items skipped (0x004724A0 / 0x00471FA0; the life-ups taken): final boss beaten after 5 continues at 96600 (score 77345) | **every logic check exact**; draws 22319 / 22374 (one creature item 0x00471FA0 frame at 77747: 15 draws vs 1, load timing; final-boss part 0x0042BF30 rounding) |
| `game9` (98900..106499) | `input_auto_game8.txt` replayed (no auto) | game8's ending: 3 -> **4** -> 7 -> title | **every logic check exact**; draws 7588 / 7595 (scene 4's first frames 102166..102170 and 102460: models drawn natively before the original has them: load timing) |

Regression commands (add to the suite; P = `--prefix build\scene_capture_auto_st1.txt`):
```
world_test game build\scene_capture_st4_route4.txt --script build\script_st4_route4.txt --stage-start 42548
world_test game build\scene_capture_st4_noresc1.txt --script build\input_auto_st4_noresc1.txt --stage-start 44107
world_test game build\scene_capture_st4_routeC9.txt --script build\script_st4_routeC9.txt P --no-gameover   (also routeB, routeB13)
world_test game build\scene_capture_game3_st5.txt --script build\input_auto_game3.txt P --no-gameover
world_test game build\scene_capture_game<k>.txt --script build\input_auto_game<k>.txt P                  (k = 5, 6, 7, 8)
world_test game build\scene_capture_game9.txt --script build\input_auto_game8.txt P
```

Scripts: the planner prefixes `script_game3_prefix.txt` (18/18 civilians rescued to the stage-4 boss 48134),
`script_game5_prefix.txt` (game4's auto input to 62156 + the planned Boss2 rematch to 64500, below),
`script_game6_prefix.txt` / `script_game7_prefix.txt` (game5's applied input cut at 77000 / 71500). The original then ran
`HOTD2_INPUT_AUTO=1` from the prefix end with `HOTD2_INPUT_AUTO_NOHEAD=64500-200000` (game6 / 7 / 8 also
`HOTD2_INPUT_AUTO_SKIP` on the scene-5 items 0x004724A0 / 0x00471CC0 / 0x00471FA0); env files and the runner
(`run_capture2.ps1`) are in the routes2b scratch folder. Runs that end in the attract loop need `HOTD2_DEMOKEY_GUARD=1`.

Ported / fixed:
- Enemy behaviour 25 (0x00457FB0, stage 5): no damage, approach in motion 0x1E3 until the approach distance 0x009A2BE0,
  then motion 0x1DF and node 1 bursts 95..104 frames later (node 1's stage-1 wound model, blood 0x00407200, 0x0043E720).
  The burst (0x0043E720 / first run 0x0043E790 / task 0x0043E880): models 0x1D31 + n % 40 flying in view space at the
  camera (2P: a random side), hits the player 30 frames after its path parameter passes 0x00564404, shot: +80 and
  falls. Its start point is the node's model-view translation taken through the view (the matrix-stack top at task time
  is the view): view x (view^-1 x node + 1.5 y).
- Komono kind 63 (0x0046FB50, stage-6 scene 5, the reward scene of the all-rescued route): the twelve items of
  0x005950C8 (life-ups, score items, creatures) through the item helpers.
- Actor 0x64 is spawned by op 0x0A (0x00408990: short records {type, s16 state}, +0x11C = low half of word 1); scene 5's
  record 0x0097A0E8 is state 1 (type-1 score routes only), so arcade goes 5 -> 6 (the port read +0x22 and took 5 -> 7).
- Civilian head look mode 3 (0x0048D1F0): the camera look-at point 0x009C6FA8.
- Stage-6 scene 9 (0x64 state 2: score < 100000) is reached only from scene 3's type-1 score branch; stage-5 scene 6 and
  stage-6 scenes 7..11 are game type 1 only (Original mode).

Findings:
- **Boss2 rematch soft-lock (stage 5 scene 3, original game):** when the auto player hits the boss during its walk-in
  (state 2), it ends phase 8 away from its start point; phase 9 then sends it home (state 6, 0x00478C00) along the arena
  wall, which it never reaches: +0x34 & 0x100 stays set (no damage), the game waits forever (game3 / game4 / game2b; the
  native world reproduces it exactly). The planned rematch skips the boss 62156..62560 (`--plan-skip 476150@62156-62560`)
  and it dies at 63126 like on auto_st5. The orig4 agent's runs stalled in the same scene. The hook's auto player now
  never aims at Boss2 while its +0x34 & 0x80000 (walk-in / death states) is set (auto_play.cpp; not used by the
  captures above).
- Low-score games: the stage-6 scene-5 items are worth about 5000 points; skipping them keeps a NOHEAD game under 80000.

Open:
- Stage-4 boss wound mark 0x00492210: after the boss dies the original keeps reading its freed block (+0x34); the count
  resumes once another allocation reuses the block (st4_noresc1: the tasks created at 53931 end the wound at 53969).
  The task heap is an address-ordered first-fit heap with coalescing (alloc 0x004A7400, free 0x004A7510, 16 MB at
  0x007DF828), shared by Task_Create 0x004A6FA0 and other allocations (0x00418200, 0x0041CDA0, 0x004A74E0, ...); modelling
  it needs every allocation's size. Not modelled.
- Single-frame residency differences at load points (st4_route4 44581, stage-6 scene 12's 0x004132D0 frame, game8 77747,
  game9 102166..102170 / 102460): load timing
  (the original's idle callback 0x0049E730 runs extra resource steps by wall-clock time); not fixable here.
