# Draws 2 (agents "draws2" and its continuation "draws3") - STATUS: done, merged with main

Symbols: `draws2_symbols.csv`. Scratch: `<scratchpad>\draws3\` (regress.ps1 = the whole world_test / frontend_test
suite incl. routes 2a, modes 8.2 / 9, two-player and misc2 captures; rcmp.py compares two result folders; qana.py /
qframes.py read Q lines; session.ps1 = runs of the original under the shared lock).

## Kept from the draws2 WIP commit (logic identical on the whole suite)
- 2p_cont camera +-1 from 8445: civilian collision sphere (0x0048A920 modes 1..3) and penalty node point computed
  world -> view -> camera; the original takes node +0x28 (model-view) x camera matrix (0x004A9230 / 0x004A92A0 /
  0x004A8CC0). Fixed (world_civilian.cpp), also actor 0x22's second skeleton (world_actors.cpp). 2p_cont camera
  9911 / 9911, 2p_gameover 212 / 212.
- frontend_test compares draw matrices with the %.7g print interval like world_test (the 0x0049B260 intro-body
  "failures", 274 frames per capture, were this test artefact).
- Skeleton 0x4C (final boss): skin twins 0x004EDA52[i] with negated normals (0x0041A520 / 0x0041A580), env-map uv on
  them.
- Komono kind 1 water (0x0046E3A0): per-frame uv wave of the loaded model + TSP | 0x2000 (`ModelEdit::uvs / tspOr`,
  `World::InlineRecords`). Note: after the resource-model revert below the "loaded" test is `modelLoaded != 0` (it read
  the removed 0x80 flag and was silently off at first).
- Hydra water lift 0x00424C10 (boss mode scene 0x0D), boss / training select light colours (0x004AA0A0 per entry,
  grade-5 red 0x0041DC10).
- Hook: `HOTD2_SCENECAP_RES=1` -> "Q <type> <arg> <step> <result> <return address>" for every request step 0x0041D5A0
  (0x41D580 = Res_Step; 0x41D838/846 = 0x0041D810; 0x41D99E/9AC = flush 0x0041D970; 0x41DA18/A26 = 0x0041D9D0; 0x41DABD
  = 0x0041DA70).

## Resource queue: why loads cannot be timed exactly (root cause), main's model kept
Q-line capture of the whole auto_full2 game: `build\scene_capture_draws3_res2.txt` (F / E / Q lines, 0..97299).
- Game_Frame 0x0049E220 steps the queue only 5 times (10 while frames are skipped, 0x007DEBAC), but the frames that
  load show 1..765 steps. The rest come from the frame pacing **0x004A5710**: while it waits for the frame time it calls
  the idle callback 0x007DEB7C = **0x0049E730** (installed by 0x0049E4A0), which runs one more 0x0041D500 whenever more
  than 1 ms remain. The queue therefore advances by wall-clock time.
- On the capture machine nearly every request completes in the frame it was queued in; 35 of 371 loading frames stop
  mid-request (after 1..279 steps; no byte or step budget fits: 18781 stops after 53 KB of pol files, 1157 loads
  460 KB). Captures with heavy logging have less idle time, so the same load can spill differently per run: modes_orig
  7786 misses the breakable 0x00465FB0 for a frame where auto_st1 7400 draws it.
- Draws2's open question (auto_st1 1338 draws model 6145, pol 0x69 requested at 1337) is answered: 0x69 was loaded in
  the idle time of frame 1337.
- Variants measured on the whole suite (draws frames):
  - draws2's step model at 5 steps per frame: worse than main on 37 captures (too slow);
  - draws2's state machine with unlimited steps: better than main's on 9, worse on 4 (fe_orig / fe_morig1 -2,
    st2_shutter / auto2_st3boss -1);
  - main's model with unlimited requests per frame: better on 12, worse on 4 (fe_orig / fe_morig1 / st2_shutter /
    st3boss -1 each).
  None is better everywhere, so main's model (10 whole requests at the start of the next frame) stays; the step model
  and its WORLD_RES_STEPS / WORLD_RES_TRACE switches are gone.
- New: model residency also in the attract demo (game mode 5: Stage_Start 0x00460030 frees the slots, 0x0041D510 ->
  0x00418690, before its loads) and in story modes 10 / 11 (0x0041F9B0 / 0x0041FB00: 0x0041D510, pol 0x26, texture
  0x17B, flush). The old note "the attract demo starts with the title's models still loaded" was wrong.

## Other fixes (draws3)
- Final boss creature pattern 0 (0x0042C0B0): the second skeleton (Mem_Alloc block) keeps 0x00410440's plain node draw
  0x00411050; the port used the enemy node draw 0x004534A0, whose head look turned its node 2 (2p_auto_b 0x0042C1A0
  draws 1676 -> 1740 / 1740).
- Sky 0x004132D0: the angle of 0x004A9AE0 is rounded to a float (fild, fmul qword, fstp dword, fsincos). Story mode 10
  starts with sky type 0 for 200 frames, which keeps Sky_Create's leftover task yaw (0x007C2118 = 0): both mode-10
  runs of the attract capture fit 683979520 (`WorldConfig::storySkyYaw`; any value with the same float angles fits).
- Final boss blast 0x004298C0: drawn in its own white camera light 0x0041DC10(1, camera pitch / yaw, 1, 1, 1) ..
  0x0041DCC0 (sweep 84750: 20.0 -> 4.5). Its 0x00429C90 (a v scroll of the model data at 0x009BD204) is not ported.
- frontend_test debug: `HOTD2_FT_LISTBAD=<check name>` lists every failing frame, `HOTD2_FT_DRAWMAX`.

## Numbers (draws frames, main e8ded32 -> this branch; every other check identical)
| capture | main | draws3 |
|---|---|---|
| 2p_auto_b | 24598 / 24784 | 24662 |
| 2p_cont (camera 9728 -> 9911 / 9911) | 9546 / 9730 | 9729 |
| 2p_gameover | 211 / 213 | 212 |
| attract loop (frontend_test --resync): demo / mode 10 / mode 11 | 3380 / 3482, 3186 / 3703, 5149 / 6136 | 3422, 3702, 5253 |
| menus2_attract: same | 3378 / 3480, 3186 / 3703, 5150 / 6137 | 3420, 3701, 5254 |
| modes_orig / misc2_orig1 / misc2_orig2 | 13801 / 13910, 15722 / 15827, 14664 / 15918 | 13821, 15740, 15251 |
| play1full (frontend) / modes_weapon1..3 / menus2_weapon12, 13 | 2231 / 2251, 3997..3998 / 4017..4018 | all exact |
| all other captures of the suite (world_test st1..st6, routes, 2P, pause, boss / training modes) | | unchanged |

render_compare 9/9; scene_compare (title 650..1050, st1 demo 2820..3200 with build\ref) identical to main.

## Screenshot sweeps (tools/sweep_compare.py, mean block colour difference; none worse than main in any frame)
| sweep | original shots | main | draws3 |
|---|---|---|---|
| whole game, phase 0 (324) | build\shots_lighting_sweep | 1.52 | 1.40 |
| whole game, phase 150 (324) | **build\shots_sweep_p150** (new) | 2.48 | 2.37 |
| two players, script_2p_auto, every 600 at phase 300 (110) | build\shots_draws3_2p (new) | 1.57 | 1.36 |
| boss mode 4 (hydra), every 300 (15) | build\shots_draws3_boss4 (new) | 1.63 | 1.58 |
| boss mode 2, every 150 (12) | build\shots_draws3_boss2 (new) | 24.69 | 24.65 |
| attract loop, no input, every 600 at phase 300 (50) | build\shots_draws3_attract (new) | 1.74 | 1.74 |

Port renders: `play.exe game --script build\script_auto_full2.txt --screenshot <dir>\o_%d.png --every 300 --every-phase 150
--frame 97150 --width 640 --height 480`; 2P: `--script build\script_2p_auto.txt --p2mode 5 --credits 6 --every 600
--every-phase 300 --frame 65700`.

Remaining sweep differences:
- 150 / 450 / 750 (boot, also in the boss-mode runs): the original's window still shows black / the title intro while
  the port is at the SEGA logo / mode select: the original's PrintWindow capture lags during the boot loads (the game
  is in sync afterwards). Not a port difference.
- 84750 (4.5, edges 0.48): blast streak details (0x00429C90 v scroll). **Fixed (polish3): 0.7.**
- **Fixed (texres): 0.8 / 0.7; the original culls the sky 6047 completely (no in-memory cull flip for it, formats.md "Strip chunk"); the texture was right.** Attract 8100 / 20700 (14.4 / 19.4, red cast): story mode 11's final-boss scene. The original is black behind the boss
  where the port shows a red room. The capture draws the sky type 3 models 6047 / 6912 there too and the env (black fog
  -1000..3000) is the same in the port's env log, so it is a render-state difference (fog / material of those models,
  or stage section objects the original does not draw: the section check only tests a subsequence). Not resolved.
- Attract 21900 (5.2): a dark-red cape in front of the camera in the port only. **Fixed (polish3): the actor 0x24 of mode 11, 0.3.**

## Remaining (not fixed)
- **Fixed (polish3: mx::Inverse, boss4 camera and draws exact):** Hydra in boss mode (modes_boss4): 4209B0 neck draws at 2191 (one frame, ~0.003), and on the hit frames of the
  variant-5 head 0x004231C0 (2778 / 2781 / 2783 / 2784 / 3373.., neck nodes ~2 angle units) with the sparks
  0x004246F0 / blood 0x00407120 at its node points; 0x00421F20's per-node angles on those frames not found. The
  4231C0 failures from 3689 are the open stage-3 boss camera in mode 1.
- **Fixed (polish3: adv2evtbl.bin partial last word):** Story mode 11 actor 0x24 variant 1 (0x00483030, motion 0xEA, kind 0x41): from 21277 its root drifts and at 21282 the
  native motion wraps (tick 100 -> 0, root y 10.5) while the original keeps walking: 883 frames (NPC / motion data).
- **Fixed (polish3: hands grip from the model-view nodes):** Attract demo: player 0x00413E90 draws at 17115 (err 0.07), 0x00444EE0 (28 frames).
- The original's load timing (above): single frames of breakables / props at load points differ per capture.
- Lighting leftovers listed in lighting.md (0x004AA0A0 colour flashes, 0x0041DC70 around non-skeleton draws).

## Findings (documented, not portable)
- Fish blood pools (0x00438C10 / 0x00439450 -> 0x00408370): yaw = uninitialised stack word.
- Boss-1 dust ring 0x00491700: +0x68 yaw never written (Task_Create without Task_Clear): stale heap.
- Final boss creature pattern > 4: unreachable (table 0x0055CFC0 rows 0..3 hold 0..3 only).
- 0x004755B0 +0x2A8 edit sets bit 30 of strip ctrl words: no PC draw path reads it.
- Actor 0x24 "3958 logged as 672" / komono 9 "5467 as 47": capture pointer-cache aliases in the old play2scene1 capture.
