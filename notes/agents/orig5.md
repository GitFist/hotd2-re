# Original mode (game type 1): the last gaps (agent "orig5")

Follows orig3.md (section orig4). Symbols: `orig5_symbols.csv`. Scratch: `<scratchpad>\orig5\` (run.ps1 = the orig4 run
helper on this worktree, env_*.txt = the capture environments, regress.ps1 / rcmp.py = the suite and its comparison,
capx.py / ctx.py / fnlife.py = capture readers, turn_emu.py = 0x00403C00 in Unicorn).

## Fixes and ports (all verified on the captures below)

| item | root cause | fix |
|---|---|---|
| Stage-5 camera pitch +-1-2 at orig4b_2 58229..58264 (stage 5 scene 0) | the behaviour-25 burst (0x0043E720 -> first run 0x0043E790) takes a lock-on slot (Target_AddDirect 0x00408E80) with the enemy's +0x100; its first run copies the enemy's **current** +0x100 into its own +0x100, which the lock-on table reads live. The port kept the spawn-time point in its lock target (no SyncLock), so the camera's look-at (midpoint of slots 2 + 3, 0x00403050) was half a frame of motion off: the camera matrix (0x009A6040, before the angle normalisation) differed from 58227, every enemy's body point +0x100 (taken through that matrix) with it, the angles from 58229 | `SyncLock` in the first run (world_st5_enemy.cpp) |
| Stage 2 scene 18 (type 1): an item of the wrong kind (task 0x004724A0 instead of 0x004675A0), then everything | komono 34 barrel 0x0046A580 in game type 1 with +0x2A0 != -1 (only the op-0x0B barrels of 0x00473940 set it) drops an original-mode item 0x00467B90 (slot +0x2A0) at the floor + float 0x004C43AC, life +0x199, then 0x007DCD14 = 0 (0x0046A9EB) | world_st1b.cpp TaskBarrel; the item-0x1E branch (0x0046A97B) returns after its drop (it fell through to a second drop) |
| Two players in the item select: players' point 0x009C71E0 1.5 instead of 0 from 803 | the init rows' callbacks 0x004150C0 / 0x004150E0 (+0x7C) and 0x00415920 / 0x00415940 (+0x80) only store 0x00415120 / 0x00415180 and 0x00415970 / 0x00415980 on their first call. The port stored the final ones, so PlayerInit's immediate row run (0x00414770 -> 0x00413E90) placed P1's body with route 1 (P2 not joined yet): the bodies were not symmetric and the rail midpoint 0x0040C9C0 fed back 1.5 | PlayerInit keeps the row values; PlayerCb7C / PlayerCb80 turn them into the final callbacks without acting (world_player.cpp) |
| Item characters (items 0x16..0x1C, character c = 0x009A2242 + p * 0x14) | not ported ("another character's body" note) | skeleton 0x39 + c (8: 0x21, 9: 0x34) for the player body when created (0x00416450 at 0x0041649F) and when the item select ends (0x0048A140 at 0x0048A1FD: new pose, slot 14 + p); actor 0x25 cutscene doubles: skeleton bytes 0x39 / 0x3A -> P1's / P2's character (0x004840D0 at 0x00484116), script op 9 gun model 0x004EC9E0[c * 3 + a] with b 0 / 1 = P1's / P2's character (0x00484739); op 0x52 / 0x53 pols 0xBE / 0xBF -> 0xBE + c (8: 0xA6, 9: 0xB9) (0x0045F500 / 0x0045F580, world.cpp Resource) |

Original-mode facts found on the way:
- Two-player games take ONE item per player (Column 0x00488820: in two-player games the first pick ends the column,
  slot 2 = start). The prefix of the first 2P try picked two each and stalled the select (original and port alike).
- Komono 73 (stage 4 scene 7 -> 22) needs a player holding item 5, 6 or 0xC (grenade / shotgun do not); komono 76 in
  scene 14 (-> 20) needs item 0, 2 or 0xB; in scene 5 (-> 21) its slot 1 counts with any item.
- A shot civilian is not dead after one hit (hook PREFER on civilians: shot until it dies).
- Held object state 4 (0x00442B90 -> 0x00443B90) was already in orig4b_1 (33414.., stage 3 scene 7 enemies), orig4b_2
  and orig4_w4 (stage-4 boss): exact there.

## Tools

- frontend_test:
  - enemy checks from the T dumps of 0x004533F0 / 0x00452DA0 (as world_test: hp / behaviour / flags, position, body
    point +0x100, tolerance 1e-3; `HOTD2_FT_ENEMYWHY=1` lists every difference, `HOTD2_FT_ENEMYEPS` its threshold).
    The Original-mode captures had no enemy check before; all exact.
  - `--dump F --dump-to G` also prints both camera contexts (matrix +0x40, target +0xD8; capture W 0x9A6000 /
    0x9A61A4) and the native lock-on slots.
  - `--plan out --plan-from F --plan-end E [--plan-prefer / --plan-skip fn@a-b, --plan-quiet a-b, --plan-civ a-b,
    --plan-watch fn]`: the route planner (tests/route_planner.h, moved out of world_test) on a world booted like the
    capture (`--save-patch`, `--p2mode`, `--poke`); the capture is not read. Prints scene / route / lives / civilians.
  - `--poke F:<va>:<hex>[,...]`: the capture's HOTD2_POKE in game memory before frame F (World::TestPoke: 0x009CA0EC /
    0x009A21BA civilians rescued / met, 0x009A5C6C / 0x009A5D9C player scores).
- Hook auto player (auto_play.cpp): a PREFER / SKIP entry `fn/off=val` also needs the s16 at task + off (e.g.
  `43F720/130C=4`: actor 0x52 variant 4); civilian functions in PREFER are aimed at and shot until they die (route
  steering: a civilian killed), PREFER_HITS does not apply to them.
- PowerShell note: quote the planner's filters ('470E20,...@a-b'); unquoted, `470E20` is parsed as the number 4.7e22.

## Captures (main checkout build\, all HOTD2_NOSAVE=1, windowed / keep-active, SCENECAP with EVENTS / SPRITES / SOUNDS /
RANDLOG, the orig4 TASKS / WATCH lists; env files in the scratch folder)

| capture (frames) | script / input | content |
|---|---|---|
| scene_capture_orig5_g1b.txt (16900..56399) | script_orig5_g1b.txt (prefix script_orig5_g1b_prefix.txt = script_orig4_scout.txt to 18560: grenade + infinite credits; auto from 18567; PREFER civilians @S1:6 / @S3:4, 43F720@S1:18, 474F30,467B90@S2, 470E20@S3:4, 46F090@S3:9, 43F720/130C=4@S3:10, 471330@S3:14; SKIP 43F720/130C=3@S3:10; PREFER_HITS=0) | stage 2 5 -> 6 (civilian killed) -> **17 -> 18 (actor 0x52 v2) -> 33** -> 20 -> 35; stage 3 0 -> 3 -> 4 -> **10** -> 6 -> 13; stage 4 4 (civilian killed) -> **9 (komono 56) -> 17** -> 11 -> 10 (v3 then v4) -> **19** -> 12 -> 14 (komono 76) -> **20** -> 15 -> 23 boss; stage 5 start |
| scene_capture_orig5_g3.txt (42500..47999) | script_orig5_g3.txt (prefix script_orig5_g3_prefix.txt = g1b's input to 42600; auto from 42601, PREFER 43F720/130C=3@S3:10, SKIP 43F720/130C=4@S3:10) | stage 4 10 (v4 then v3) -> **18** -> 12 -> 14 -> 20 -> 15 -> 23 |
| scene_capture_orig5_g24.txt (17200..59999) | script_orig5_g24.txt (prefix script_orig5_g24_prefix.txt = scout to 17300; auto from 17343, SKIP the route sign 468180@S1:5, PREFER 470750@S1:16, 471330@S3:5, 470B70@S3:7) | stage 2 5 -> 21 -> **16 (komono 72 item taken)** -> 35; stage 3 0 -> 3 -> 4 -> 10 -> 6 -> 13; stage 4 4 (komono 74: three hits, item) -> 5 (komono 76 slot 1) -> **21** -> 7 (komono 73 shot, no route: grenade) -> 8 -> 6 -> 25 boss; stage 5 |
| scene_capture_orig5_g7.txt (0..62399) | script_orig5_g7.txt (prefix script_orig5_toygun_prefix.txt: toy gun 0xC + infinite credits, POKE 100:9C90CC:01,100:9C90D3:01,100:9C9F49:01,100:9C9F50:01; auto from 1124, SKIP 468180@S1:5, PREFER 474F30@S0;474F30@S1;470750@S1:16;471330@S3:5;470B70@S3:7) | stages 1-3 (stage 2 5 -> 21 -> 16 -> 35, stage 3 -> 10 -> 13), stage 4 4 (komono 74) -> 5 -> **21** -> 7 (**komono 73 with item 0xC: route 2**) -> **22** -> 8 -> 6 -> 25; stage 5 4 -> 6 -> 5 |
| scene_capture_orig5_2p.txt (0..32999) | script_orig5_2p.txt (prefix script_orig5_2p_prefix.txt: P1 item 2, P2 item 0x16 = character 2; POKE 100:9C90C2:01,100:9C90D3:01,100:9C90D6:01,100:9C9F3F:01,100:9C9F50:01,100:9C9F53:01; HOTD2_INPUT_AUTO=2 from 1124) | **two-player Original mode**: item select for two, stage 1, stage 2 to its boss (37); P2's character body / gun model in the cutscenes |
| scene_capture_orig5_g6a.txt (79400..82499) | script_orig5_g6a.txt (orig4b's game: script_orig4b_2.txt replayed to 79950, auto from 79951; POKE as orig4b + 79500:9CA0EC:1000 = civilians rescued 16 = met) | stage 6 3 (all rescued: route 1) -> **5 -> 8** (score 96870: 70000..99999) -> 12 |
| scene_capture_orig5_g6b.txt (79400..82499) | script_orig5_g6b.txt (as g6a + 79500:9A5C6C:A0860100 = P1 score 100000) | stage 6 3 -> 5 -> **7** (score > 99999) -> 12 |
| scene_capture_orig5_g6c.txt (79400..82499) | script_orig5_g6c.txt (as g6a but only the score poke) | stage 6 3 (score route 2) -> **9 -> 10** (score >= 100000) -> 12 |

The stage-6 route checks (actor 0x64, 0x00435FB0) are reached with pokes of the counters they read, because a whole
Original-mode game with every civilian rescued, or with 100000 points at scene 9, was not at hand: scene 3 (state 0)
all civilians met rescued (0x009CA0EC == 0x009A21BA) -> 5, else type 1 score > 69999 -> 9; scene 5 (state 1) score
> 99999 -> 7, > 69999 -> 8, else 6; scene 9 (state 2) score < 100000 -> 11, else 10. The pokes are applied at the
start of boot frame 79500 (stage 6 scene 3, before its actor 0x64), in the original by HOTD2_POKE and natively by
frontend_test `--poke` (World::TestPoke).

Command lines:
```
frontend_test game build\scene_capture_orig5_g1b.txt --script build\script_orig5_g1b.txt --save-patch 9C9F3F:01,9C9F50:01
frontend_test game build\scene_capture_orig5_g3.txt --script build\script_orig5_g3.txt --save-patch 9C9F3F:01,9C9F50:01
frontend_test game build\scene_capture_orig5_g24.txt --script build\script_orig5_g24.txt --save-patch 9C9F3F:01,9C9F50:01
frontend_test game build\scene_capture_orig5_g7.txt --script build\script_orig5_g7.txt --save-patch 9C9F49:01,9C9F50:01
frontend_test game build\scene_capture_orig5_2p.txt --script build\script_orig5_2p.txt --save-patch 9C9F3F:01,9C9F50:01,9C9F53:01 --p2mode 5
frontend_test game build\scene_capture_orig5_g6a.txt --script build\script_orig5_g6a.txt --save-patch 9C9F3D:01,9C9F50:01 --poke 79500:9CA0EC:1000
frontend_test game build\scene_capture_orig5_g6b.txt --script build\script_orig5_g6b.txt --save-patch 9C9F3D:01,9C9F50:01 --poke 79500:9CA0EC:1000,79500:9A5C6C:A0860100
frontend_test game build\scene_capture_orig5_g6c.txt --script build\script_orig5_g6c.txt --save-patch 9C9F3D:01,9C9F50:01 --poke 79500:9A5C6C:A0860100
frontend_test game build\scene_capture_orig4b_2.txt --script build\script_orig4b_2.txt --save-patch 9C9F3D:01,9C9F50:01
```

## Results (frontend_test, game mode 6; every other check of each capture exact too)

| capture | logic (mode, rand Z / Y, tasks, events, sounds, camera, credits, rank, players' point) | enemies (T: state / position / body) | draws |
|---|---|---|---|
| orig5_g1b | **all exact** (39100 frames) | 10675 / 10673 / 10673 of 10675 (32363..: the rider of a removed boat, freed memory, known) | 37152 / 38542 (rider 0x0045CD90 from the same freed block, blood pools 0x00407E30.., 0x0046B5F0 17133 as on orig4_w2) |
| orig5_g3 | **all exact** (5000) | exact (1271) | **4997 / 4997** |
| orig5_g24 | all exact (42800) but the camera at 49741 (one frame, stage 4 scene 25 boss: the target differs by 2 float ulps in x; 0x00403C00 emulated with the native inputs gives the native value, so an input not dumped differs) | 10827 / 10825 / 10825 (the boat rider) | 40582 / 42234 (same kinds) |
| orig5_g7 | **all exact** (61199) | 15479 / 15477 / 15477 (the boat rider) | 57677 / 60280 (same kinds, load timing 0x00465FB0) |
| orig5_2p | **all exact** (32199 frames, both players) | exact (16505) | 30177 / 31640 (blood pools, 0x00491700, 0x00482EE0 alias: the one-player kinds; the character draws are exact after the ports) |
| orig5_g6a | **all exact** (3100) | exact (627) | 3097 / 3100 |
| orig5_g6b | **all exact** (3100) | exact (627) | 3096 / 3098 (the scene-12 sky load frame 0x004132D0, load timing) |
| orig5_g6c | **all exact** (3100) | exact (627) | 3098 / 3100 (the same sky frame) |
| orig4b_2 | all exact; camera 56800 -> **56828** / 58266, draws 55931 -> 55994 / 57941 | exact (7536) | |

orig4b_2's camera frames left: 87794..92263 (final boss, sporadic: angles equal, the view's translation 3..5 float ulps
off at |pos| ~ 9800) and 92462..93840 (ending scene 0, event mode 9: angles and position equal, the captured "stage
draw" carries an extra translation (0, 8.55, 2.82): not the view).

Regression suite (coordinator's list + routes.md 4 / 5, modes.md 8.2 / 9 incl. train 2..9 / boss 0..8 / weapons /
net / attract, twoplayer2.md, misc2.md, orig3.md, draws2.md): every check identical to main before this branch except
orig4b_2 (camera / draws better, above); the new frontend_test enemy checks are exact on every frontend capture.
render_compare 9/9.

## Open

- Stage-4 boss (scene 25) camera, orig5_g24 49741: one frame, 2 ulps (the look-at object's point is not in the T
  dumps).
- orig4b_2 camera: final-boss translation ulps, ending scene 0 capture view (above).
- Stage-6 scenes 7 / 8 / 10 were reached by poking the route counters (an unpoked game with all civilians rescued or
  100000 points at stage-6 scene 9 is still to be captured; the toy gun game orig5_g7 had 79750 at stage-5 scene 5).
- Two-player Original mode: stages 1-2 captured; the game over / route map with an item character (0x004613C0 /
  0x004617F0 read the characters) not.
- The other item characters (c 3..9, items 0x17..0x1C) only through the same code paths as c 2.
