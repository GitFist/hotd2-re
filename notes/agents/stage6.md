# Stage 6: the native stage 6, the final boss, the ending and the name entry

## Knife thrower runaway (2026-10-23)

The second encounter in scene 0 spawns three actor-0x31 enemies, skeleton 0x18,
variant 3 (records 0x00977D44 / 0x00977D70 / 0x00977D9C). Killing only one and
waiting let a survivor exhaust its knives and enter behaviour 30. That behaviour
was missing from TaskActor31, so the previous walking motion continued indefinitely.
The still-live actor also remained a camera target, pulling the view away.

Ported the original 0x0044F900 knife-regeneration behaviour: switch to the pose's
stationary motion, wait for the existing node callback to grow the blades, restore
only intact empty hands and their hit spheres, then resume behaviour 7. The camera
needs no separate change. The unrelated generic enemy throw/jump path is unchanged.

`chapter6_encounter_test <game dir>` replays the opening encounter, kills each of
the three possible first victims in separate runs, waits 3,600 frames, checks that
survivors stay within the encounter and regenerate knives, then shoots both and
verifies the camera resumes. Both x64 and x86 pass all three first-kill choices,
releasing the camera at frames 5105 / 5219 / 5256. A negative control with only the
behaviour-30 dispatch removed fails at frame 2101 because knife regeneration never
completes (the earlier position-only check also failed at frame 2301).

Local builds are labelled 2026 10 23 A (x64) / 2026 10 23 NS32 A (x86), with new
multiplayer compatibility IDs 2026102300 / 2026102301 because simulation changed.

## Skippable credits (build U, 2026-10-16)

With port features enabled, either player's fresh trigger/click or Start/Enter edge exits
ending sub-state 8 through StageEnd and EndingCheck. Held input does not dismiss the credits.
Skipping stops ending audio and recenters the staff roll's shifted projection. Scores and
score classes are retained for qualifying name entry; other solo games return to the title,
and connected co-op games return to their existing lobby. The original exactness path is unchanged.
Build U uses compatibility ID 2026101602, so both online clients need U.

`credits_skip_test game` covers Arcade/Original, either player and confirmation input, held and
unrelated inputs, qualifying/nonqualifying scores, co-op lobby return, and deterministic replay.
Optional arguments `build/final-boss-flicker/start.txt build/script_auto_full2.txt` replay the
real final chapter into the credits and verify that skipping reaches name entry with the final
score intact (frame 86633, score 150060). Both local and distribution builds pass.

## Rooftop rendering (build T, 2026-10-16)

The Emperor reveal exposed depth precision loss on the closely layered rooftop and bridge.
The combined world-to-clip transform lost precision at the stage's large coordinates;
separating model/view from projection helped but left intermittent stripes with conventional
floating-point depth. Game draws now project from camera space, and both GPU backends use
reversed depth with matching model/sprite transforms, clear values, and comparison functions.
The original camera, near/far limits, stage geometry, lighting, fog and simulation are unchanged.
Build T retains S's multiplayer compatibility ID because this is a rendering change.

`render_depth_test` (optionally `sdlgpu`) checks closely layered surfaces across 24 translated,
moving-camera views, plus sprite/model occlusion in both directions. Both backends pass.
As a negative control, restoring conventional depth produces 35,568 D3D11 / 76,412 SDL_GPU
rear-surface pixels bleeding through the test's first frame. The fixed version has no such
bleed and identical images at each translated origin.

Visual checks cover the Emperor reveal through the start of the fight, consecutive reveal
frames on both backends, original and 2x textures, stage-1 scenery, and the title scene against
the original capture. Local screenshots and test logs are in `build/final-boss-flicker/`.

**Result: every logic check is exact from the stage-6 start to the title.**

- Stage 6: `world_test game build\scene_capture_auto_st6.txt --script build\script_auto_full2.txt --stage-start 70806
  --civilians 0,1` compares 16194 frames (70806..86999). Rand state / calls / sites, the task list (K), event ops,
  live enemies, enemy hp / position, players, sounds, game mode / sub-state are exact on every frame. That covers the
  Boss3-variant heads, the lift (actor 0x26 state 5), the bridge (0x26 states 6 / 7), the debris / figures / flames
  komono, the stage-6 enemies (kinds 9 / 0x12 with their hit proxies), the final boss with its orbs and the creature,
  its death, the stage end and the start of the ending.
- Ending: `world_test game build\scene_capture_auto_st6_end.txt --script build\script_auto_full2.txt --stage-start
  70806 --civilians 0,1 --prefix build\scene_capture_auto_st6.txt` runs stage 6 natively and compares 86900..97157:
  rand, K, events, sounds, players, mode / sub-state and the camera view are exact on every frame, and the switch to
  the title (mode 3) happens at 97157 like the original.
- Chain: stage 5 from 56007 (`--prefix build\scene_capture_auto_st5.txt`) into the stage-6 capture is exact on rand
  and events across the stage change.

**CONFIRMED** = ported and exercised frame-exact; **CODE** = ported, not exercised; **NOTE** = not ported (a runtime
`Note` is written when met).

## 1. Captures and carry

The captures are the stage-5 agent's (stage5.md section 1): `scene_capture_auto_st6.txt` (70700..86999) and
`scene_capture_auto_st6_end.txt` (86900..97299). Task functions that first appear in the ending have K lines but no T
dumps.

The stage-6 run met no civilians in stages 1..5 that are still counted, but stage 6 starts with civilians "met" = 1
from the earlier stages. `--civilians R,T` sets `WorldCarry::rescueTotal` / `civTotal` for a `--stage-start` run (the
captures from stage 6 on contain W lines for 0x009CA0EC / 0x009A21BA / 0x009C89C0 / 0x009C8EC0 / 0x009C9100, which
world_test reads when present). `WorldCarry` got `haveCivilians`, `rescueTotal`, `civTotal`, `rescueCount[10]`,
`rescueKinds[10][10]`, `civCount[10]` (applied / produced in world_st2.cpp ApplyCarry / MakeCarry); the counters are
`World::Impl::civTotal` (0x009A21BA, incremented by the civilian ctors: world_civilian.cpp unless flag 0x8000000, actor
0x21, komono 19) and `civCount[10]` (0x009C9100). They feed the results screen (actor 0x55).

world_test additions: `--vfrom F` (verbose from frame F), `--civilians R,T`; the captured view no longer takes the
stage draw of model 0x1918 (event mode 9 translates it, it is not the camera); native draws of task serial 0 are skipped
when the capture has no `^0` M lines.

## 2. Stage 6 objects (world_st6.cpp, world_st6_enemy.cpp, world_st6_actor31.inl)

| Object | Address | Status |
|---|---|---|
| Actor 0x26 state 5: lift with the floor display | 0x0048F560 | CONFIRMED (display camera Note 0x0048F819) |
| Actor 0x26 states 6 / 7: bridge | 0x0048F930 / 0x0048FB40 / 0x0048FD00 | CONFIRMED. The dispatcher 0x0048E290 sets `*param_1` after calling the state function, so the first frame runs the state function directly |
| Actor 0x44 state 12: collision-mesh slide | 0x004734A0 -> 0x004755B0 | CONFIRMED |
| Actor 0x64: route check | 0x00435FB0 | CONFIRMED |
| Komono 55: debris, 800 pieces | 0x00463FE0 -> 0x0046EEB0 | CONFIRMED (rand sites 0x0046404C/5B/E6/11C/152/179/190/1A7; task sites 0x0046EF69/90/A7/BD) |
| Komono 61: figures | 0x004641F0 -> 0x004729E0 | CONFIRMED |
| Komono 62: flames (prop skeleton 0x1C, motion 0x1C6) | 0x0046FA10 | CONFIRMED (8 rands at 0x004624F1) |
| Enemy kinds 9 / 0x12 setup + kind-9 hit proxy child | 0x00452FD0 cases | CONFIRMED |
| Kind-9 proxy update | 0x00453290 | CONFIRMED |
| Fade nodes 0x1C6C / 0x1C7C (alpha +0x138C) | 0x004534A0 case | CONFIRMED |
| Behaviour 8: blinking corpse | 0x00454FD0 | CONFIRMED |
| Actor 0x31 behaviours 33 (shot, rand 0x00451307) / 34 (appear) | 0x00450E40 / 0x00451480 | CONFIRMED; helpers 0x00450CF0, 0x0044DE80, 0x0044C370 |
| Actor 0x31 throw-hand rand sites | 0x0044F652 (skeleton 0x18) / 0x0044F6B7 (skeleton 0x16) | CONFIRMED (the two sites were swapped) |
| Attachment draws, kinds 9 / 0x12 / 0x4D..0x52 | 0x00419B40 | draw only; 0x00418660 env-map uv is renderer state |
| Stage-3 boss (Boss3 heads), mode-2 intro | world_st3_boss.cpp | CONFIRMED: health bar SpawnBossBar(320, 35), roar at intro tick 0x4B |

Skeleton-0x18 setup / node draw / Beh5 and actor 0x2D variant 0 are the stage-5 agent's (world_st5_actors.cpp).

## 3. Final boss (world_st6_boss.cpp)

Actor 0x2D variant 1 (0x00426A70): name card (0x005898C8), 8 orbs (ctor 0x00429D00), task 0x00426C30 with states
0..6 (0x00426C50 / 0x00426D70 / 0x00426E60 / 0x00426FD0 / 0x00427D40 / 0x00428400 / 0x00428AE0). Rand sites: pattern
0x00427481, next state 0x0042742F / 0x00427BED / 0x00427C01, state 4 0x00427E7F, death 0x0042844F. The draw is the
stage-5 Actor2D draw (st5::Draw2D) plus the stored node matrices 1 / 5.

- Orbs 0x00429D30 states 0..8 (0x00429D50 .. 0x0042B9D0), slots 0x009A5F40 sorted by view z into 0x009C8D60
  (0x00429680), register 0x0042BB80, draw 0x0042BC10, place 0x0042BCD0, player hit 0x00415500, spark 0x0042BE90,
  trail 0x0042BF30.
- The boss's state 6 removes the boss while the orbs still read its block in the same frame: the port keeps a copy
  (`St6State::bossGhost`) of the block as removed, used by the orbs only when the boss serial is gone.
- Second-phase creature 0x0042C830 / 0x0042C8D0 (node 0x0042C6D0, hits 0x0042CBA0 / 0x0042CCC0), 0x009A2448 = 1 while
  out. Patterns 0x0042C0B0 / 0x0042CD30 / 0x0042D490 are NOTE (not met by the run).
- Hit flash 0x00429760, shock 0x00429830, blast 0x004298C0 (draw approximated as a billboard).

## 4. Ending (world_ending.cpp)

Game mode 6 sub-states (world_player.cpp Mode6Frame -> `World::Impl::EndingFrame`):

- 7 (0x00431550): StartStage with event mode 9, scene = 2 players ? 2 : (players in game != 0), carry with player
  states 7; `endingRoot` keeps StartStage from creating the in-game input / lock-on / attack-order tasks and from
  reseeding (GameIsDemoMode(6, 8) -> srand(0) path). Flags 0x5900 = (keep & ~2) | 0x10; sub-state 8.
- 8 (0x00431660): the ending scenes; Ending_Check (0x00431740) at Stage_End: ranking slot 0x004157F0 on the table
  0x00530048 -> name entry (9) or next mode (title, 3).
- Ending actors: 0x33 state 11 BGM (0x00434260), 0x54 staff roll (0x00431780 -> 0x00431810), 0x55 results
  (0x00431C90 -> 0x00431D50), 0x56 THE END (0x00431BF0 -> 0x00431C10), type 0x65 route branch 0x00436140, score class
  0x00436200 (0x007DCF70).
- 9 (0x00480D90): name entry setup: residency {0x52,1},{0x52,0x79},{0x52,0x7A},{0x50,0x38},{0x50,0x7ED}, BGM
  0x10000006, players to state 8, 32 letters (0x004825B0 / draw 0x00482A00), panels (0x00482310 / 0x00482460), timer
  (0x00482230, digits 0x00482AA0), views 0x00482C40, player tasks 0x00414540 / 0x00414570, then 10.
- 10 (0x00481400): name entry frame, rank insert 0x004811F0, mode 3 when both are done or the timer ends.

## 5. Open items (NOTE / approximations)

- Ranking save 0x0041C610 / 0x0041C490 (saved table 0x009C97A0 not modelled), forbidden-name list 0x00481FB0.
- Ending resource requests type 0xB (0x0041D7D0).
- Final boss creature patterns 0 / 2 / 3; game type 3 paths (0x00425F40).
- Enemy node pre-pass 0x00418660 (env-map uv), actor 0x31 landing-dust angles (0x00401870), env ops 25 / 35 / 36 / 39
  (0x0040AD90), stage-3 boss mode-2 jaw wobble (0x00422C70), title-card light setup.
- Draw differences: the blast billboard, small orb precision (node mv from world x view), sparks / trails, the
  creature, the kind-9 proxy. The camera view of stage 6 first differs at 73634 (about 1.1 units over ~890 frames);
  the ending's camera view is exact.

## 6. Commands

```
world_test game build\scene_capture_auto_st6.txt --script build\script_auto_full2.txt --stage-start 70806 --civilians 0,1
world_test game build\scene_capture_auto_st6_end.txt --script build\script_auto_full2.txt --stage-start 70806 --civilians 0,1 --prefix build\scene_capture_auto_st6.txt
world_test game build\scene_capture_auto_st6.txt --script build\script_auto_full2.txt --stage-start 56007 --prefix build\scene_capture_auto_st5.txt
```
