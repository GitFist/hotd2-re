# Original mode (game type 1) completeness (agents "orig3", continued by "orig4") - STATUS: see section orig4

## Done (committed, all CODE: no new capture was made; regressions unchanged)

- Rand-site labels in world_modes_items.cpp are return addresses: take 0x0046773D (0x004675A0) / 0x00466CB5
  (0x00466BE0), kind-71 sway 0x00467867 / 82 / 97 / 0x004678B2 / C7, kind-71 ctor 0x004627AD / CB / EA / 0x00462808.
- Auto player (src/hook/auto_play.cpp): PREFER groups `fn,..@from-to` or `@S<stage>[:<scene>]` separated by ';',
  grid aim over PREFER spheres (civilian-node check per point), collision-mesh PREFER targets (face centres of obj+0x14C
  through obj+0x150 and the active view), HOTD2_INPUT_AUTO_PREFER_HITS=n, HOTD2_INPUT_AUTO_ONLY=from-to.
- Item 0xC scaling: civilians 0x0048D1F0 (head 1.5/1/1.5, hands/feet 2/2/2), ClearChar 0x004357F0 (2/1/2 + part
  lists), stage-3 boss intro body 0x004208F0 (game mode 6). Item 0x15 (big heads): enemies 0x00452DA0 / 0x00453B50,
  flying heads 0x0040A130, actors 0x20 (0x00448ED0 / 0x00449530), 0x21 (0x00451720 / 0x00452090), 0x31 (0x00449620 /
  0x0044A300): node-2 hit radius x 2 and node 2 drawn scaled.
- Held object state 4: 0x00442B90 aim at the target player's point -> task 0x00443B90.
- Bosses 3 / 4 / 5 type-1 item damage factor (0x00420B56, 0x004232C1, 0x00491E31, 0x0047CF38).
- Komono 73 shot (0x00470B70), 74 (0x00470E20), 75 (0x004710C0), 76 (0x00471330), 72 (0x00470750), komono 43 item
  (0x0046CEA0), actor 0x52 variants 2..4 (0x0043F720), actor 0x53 type 1 (0x00431430), body gun model 0x00416810.
- Regression suite (world_test / frontend_test incl. modes, orig1..3, routes): unchanged by these changes (before and
  after the main merge).

## (orig3's plan, superseded by section orig4)

## Survey (type-1 objects; spawn records of every scene script)

Stage 1: komono 69 (scene 9 -> 12), prop 17 items 0/2 (scene 4 -> 13). Stage 2: prop 17 items 0/2/5/6 (scene 3 -> 30),
actor 0x53 P1=2 (scene 8 -> 32), prop 17 items 0/2/0xB/0xC (scene 12 -> 31), komono 72 (scene 16), actor 0x52 v2
(scene 18 -> 33), komono 24 chain slot 1 (scene 22 -> 34). Stage 3: prop 17 scenes 3 (items 2/5) / 7 (items 0/6).
Stage 4: komono 75 (scene 2), 74 (scene 4), 76 (scenes 5 / 14 -> 21 / 20), 73 (scene 7 -> 22), 56 (scene 9 -> 17),
actor 0x52 v3 / v4 (scene 10 -> 18 / 19). Stage 5: prop 17 (scene 4 -> 6). Stage 6: actor 0x64 score routes.
Komono 70 / 34 / 77 items in most stages.

## orig4: verified against the original (captures, fixes, open)

All runs: HOTD2_NOSAVE=1, windowed / keep-active, SCENECAP with EVENTS / SPRITES / SOUNDS / RANDLOG, the TASKS / WATCH
lists of misc2's orig captures (+ 0x470E20, 0x470750, 0x4710C0, 0x467B90), absolute HOTD2_INPUT_SCRIPT paths. Run
helper / env files: scratchpad orig4\run.ps1, env_*.txt. Items are poked into the inventory at frame 100 (game memory
and the save block copy) and passed to frontend_test with `--save-patch`.

Item choice: the select's combination table 0x0056B014 forbids two items of one category (weapons 0..2 are category 0
and combine only with items 0xE+): orig3's "items 0 + 5" and "0x14 + 0x15" prefixes stalled in the select. A state-17
prop also counts a player whose second slot is empty (the record's -1 padding matches it; 0x00461C70).

| capture (main build\) | script / env | content | frontend_test (`--script` the script, `--save-patch`) |
|---|---|---|---|
| scene_capture_orig4_w1..w4.txt (0..16999 / 16900..32999 / 32900..48999 / 48900..60999) | script_orig4_scout.txt (prefix script_orig4_prefix.txt: grenade = item 2 + infinite credits 0x13; auto from 1124, PREFER as orig3) | stage 1 9 -> 12, 4 -> 13; stage 2 1 -> 29, 3 -> 30; stage 3; stage 4 0 -> 16 (komono 40 kind 9) -> 1 -> 2 (komono 75) -> 23 boss; stage 5 to the Boss2 rematch | 9C9F3F:01,9C9F50:01: every logic check exact in all four (w4: task list 9719 / 12100, see open items) |
| scene_capture_orig4_s2.txt (59300..78000) | script_orig4_s2.txt (scout replayed to 59438, auto) | the stage-5 Boss2 rematch soft lock (see below) | exact (23877 frames) |
| scene_capture_orig4b_1.txt (0..44999) + scene_capture_orig4b_2.txt (44900..103800) | script_orig4b_1.txt / script_orig4b_2.txt (prefix script_orig4b_prefix.txt: shotgun = item 0 + infinite credits; b_2 replays b_1 to 44900, auto from 44901) | a whole Original-mode game: item select, stage 1 1 -> 10, 4 -> 13; stage 2 1 -> 29, 3 -> 30, 7 -> 25 -> 27 -> 28 -> 37 boss; stage 3 7 -> 8 -> 2 -> 11; stage 4 0 -> 16 -> 1 -> 2 -> 23; stage 5 4 -> 6 (state-17 prop) -> 5 -> 7; stage 6 3 -> 9 (actor 0x64 score) -> 11 -> 12; ending 3 -> 5 -> 7, staff roll, name entry, title, how-to-play | 9C9F3D:01,9C9F50:01: b_1 every logic check exact (44199 frames; camera too after merging main 8319b73); b_2 exact to 58124 on this branch, and to the end (58266 frames: modes 6 / 7 / 9 / 3, name entry sprites 7100 / 7100) with the routes2b branch merged (enemy behaviour 25, 0x00457FB0, not on main yet); camera 1-2 pitch units at 58229..58264 (stage 5 scene 0) |
| scene_capture_orig4_bighead.txt (0..14999) | script_orig4_bighead.txt (prefix script_orig4_bighead_prefix.txt: toy gun 0xC + item 0x15) | stage 1 with flags 0x009C88AC / 0x009C88A8 (civilian head / hands / feet scaled, big enemy heads, flying heads) | 9C9F49:01,9C9F52:01: every logic check exact (14199), draws 13801 / 13883 (the known 0x00491700 / 0x00465FB0 frames) |
| scene_capture_orig4_st2f.txt (14550..32999) | script_orig4_st2f.txt (b_1 replayed to 14600, auto: SKIP the rider 0x451720.. @S1:0, PREFER 474F30@S1:12; 469510@S1:22, PREFER_HITS=0) | stage 2 0 -> 11 -> 12 -> 31 -> 14 -> 22 -> 34 -> 23 -> 26 -> 27 -> 28 -> 37 | 9C9F3D:01,9C9F50:01: exact (18450 frames) |
| scene_capture_orig4_st2e.txt (14550..32999) | script_orig4_st2e.txt (b_1 replayed to 14600, auto: SKIP the shutter 468F00@S1:7, PREFER 474F30; 431430@S1:8, PREFER_HITS=1) | stage 2 0 -> 1 -> 29 -> 3 -> 30 -> 5 -> 6 -> 7 -> 8 -> 32 (actor 0x53 type 1) -> 9 -> 28 -> 37 | 9C9F3D:01,9C9F50:01: exact (18450 frames) |

Fixed / ported (all verified on the captures above):
- 0x00404A00: the object-mesh shot normal turns by the object's +0x64 angles for every actor kind (ActorTask::Rot64;
  was enemies only): impact effect angles on props / hanging props / rafts / komono 27 / props 0x44 states 6 / 17.
- Stage_Start 0x00460030 in game type 1: weapon effect 5 model 0x109D, 3 pol 0x3D (effect 0x53 models 0x125..), 4 pol
  0x41 (residency).
- Civilian attachments: op 0x15 0x0048CBF0 requests the item's model and its kind's extra model; 0x0048DD60 releases
  them (residency).
- Komono 40 kind 9 (0x0046C570): counter 0x007DCF18 (reset by 0x004636A0) of broken pieces; at 2 with event flag 0x11
  route branch 2 (stage 4 scene 0 -> 16).
- Komono 75 in game type 1: world_st4_actors.cpp handled kind 75 for every game type; now only outside type 1.
- Player_Start 0x00414770 game-type-1 part: magazine from the item, weapon row 0x00416420, rows 1 / 3 the item lives
  (0x00416340 / 0x004163D0), row 3 first 0x00416240 (the continue loses the items' effects).
- Item 0xC: the civilians' head / hand / foot hit points are on the scaled stack top (0x0048D1F0 scales without a
  push), so the hit spheres move with the drawn parts.
- Komono 24's type-1 item 0x004699C0 (task 0x00466BE0, rand 0x00469A5D); komono kind 48 (0x00463B20 -> 0x0046DDE0,
  stage 2 scene 26 lamp: light slot, 30 pieces, models 0x17AC / 0x17AD) - an arcade object too.
- Staff-roll sprites (0x00432590) tagged for frontend_test's name / ending sprite check.
- Auto player: a PREFER target shot 40 times without a hit is given up. frontend_test: HOTD2_FT_DRAWWHY (each frame's
  first bad draw), HOTD2_FT_CAMWHY (both camera contexts of each bad camera frame).

orig3's code now verified: actor 0x53 type 1 (st2e), komono 69 / state-17 props (stages 1 / 2 / 5), komono 75, item 0xC / 0x15 scaling and big
heads (stage 1: enemies, flying heads, civilians), item damage factors of the bosses (grenade 4.0: stage-3 / stage-4
bosses; shotgun: stages 5 / 6), Original-mode stage ends / ending / name entry, auto_play PREFER groups (stage / scene
groups, mesh targets), rand-site labels 0x0046773D / 0x00466CB5.

Notes on the original's behaviour:
- Stage-5 Boss2 rematch soft lock (s2 capture, original and port alike): with the grenade (4.0) the boss drops below a
  phase threshold in state 8, goes to state 6 c9C 0 (walk home) and its home point lies beyond the arena wall clamp
  (z -4990): it walks forever, never attacks, flag 0x1F never comes. The shotgun run (b_2) does not meet it.
- Stage-4 boss wound marks (0x00492210) read the freed boss block's +0x34 once the boss is gone; in w4 a later task's
  +0x34 lands there and the mark ends at 51838 (task list from 51839 in w4). Not modelled (no heap model; the arcade
  st4 / st5 captures show the opposite case).

Open (**closed by orig5.md**: every scene below captured and exact, the stage-5 camera pitch fixed; the open items left are listed there):
- Not reached by any capture: stage 2 scene 33 (actor 0x52 v2 in scene 18; needs the scene-6 civilian killed), stage 3
  scene 10 (item from the scene-4 prop needs item 2 / 5), stage 4 scenes 17..22 (komono 56 / 73 /
  76, actor 0x52 v3 / v4), stage 6 scenes 7 / 8 / 10 (actor 0x64 states 1 / 2 thresholds), ending 4 / 6, two players.
- Komono 74 (stage 4 scene 4) / 72 (stage 2 scene 16), actor 0x52 variants 2..4, held object state
  4, body gun model 0x00416810 with another character: still CODE.
- Animated prop 0x00474120: its +0x64.. comes from 0x0040E070 at the slot of the trigger flag's number, past the
  motion's data (the next motion in memory): impact angles on it differ (2 hits in w1).
- Draw-only: stage 5 scene 0 camera pitch (b_2), komono 40 pieces at
  huge coordinates (float precision), blood pools 0x00407E30.. as on the arcade captures.
