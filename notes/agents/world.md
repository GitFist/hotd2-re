# Native world runtime (Stage 3)

`game::World` (src/game/world.h) runs the stage-1 attract demo frame by frame from the stage start (frame 2786 of
`build\scene_capture_stage3.txt`), using only the original data files and the exe image. It ties together the Stage-2
ports (task system, CRT rand, event VM + shooting camera, environment, props, enemies, effects, shooting) and adds
the per-frame world tasks, the walking-enemy AI and the scripted civilian.

**CONFIRMED** = code read + bit-exact (or float-exact) frame-by-frame replay against the capture;
**CODE** = read in code, path not exercised by the demo; **HYPOTHESIS** marked.

Files: `src/game/world.{h,cpp}` (API, stage start, world tasks, hit task, spawns), `world_internal.h` (private
state), `world_enemy.cpp` (enemy create/update, skeleton runtime, hits, pusher), `world_enemy_ai.cpp`
(behaviours 1-6 and 15), `world_civilian.cpp` (actor type 0x10), `mstack.{h,cpp}` (engine matrix-stack ops, 24-bit
float arithmetic). Test: `tests/world_test.cpp`.

## 1. Results

`build\Release\world_test.exe game build\scene_capture_stage3.txt` (frames 2786..3599 = 814 frames). Only input
from the capture: the task serial counter at the stage start (65574; it depends on the title/attract history).

| Check | Frames matching |
|---|---|
| rand state Z (after every frame) | 814 / 814 |
| rand calls Y (value + task serial, in order) | 814 / 814 |
| rand call sites | 814 / 814 |
| task list K (fn + serial, in order) | 814 / 814 |
| event ops E | 814 / 814 |
| camera view (native vs captured) | 814 / 814 |
| lock-on slots 0..3 (W 0x009A5EC0) | 814 / 814 |
| live enemies 0x009C904A / attackers 0x009C7006 | 814 / 814 each |
| enemy hp / behaviour / sub-state / flags (T) | 579 / 579 enemy-frames |
| enemy position (T, 1e-4) | 579 / 579 (facing: 0 mismatches) |
| stage section draws | 814 / 814 |
| per-task draws (model list + W*V vs captured M, abs. tol. 2e-3) | 805 / 814 |

Draw matrices: 114,980 ok, 9 bad. All 9 are float rounding on large view-space values (max error 2.8e-3): frames
3415 / 3520 (one enemy node, model 7850) and 3538..3544 (player tracer model 2936). Per task: civilian 6973/6973,
enemies 21416/21418, player 5272/5279, rain 40600/40600, props / sky / effects / sparks / letterbox all exact.

The other Stage-2 tests still pass with the shared changes (props_test PASS, effects_test PASS, enemy_test OK,
shooting_test all checks passed).

Debug options: `--from/--to`, `--verbose` (first mismatches per check, view errors with angles), `--draws <fn>`
(model lists), `--enemy <serial>` (native vs captured behaviour, motion, tick, flags, position, facing, target,
body point every frame).

## 2. Stage start (frame 2786) - CONFIRMED

0x00426800 sub-state 0 -> rank init 0x00460770 (rank = 0x005679F4[difficulty 2] = 1) -> Stage_Start 0x00460030:
srand(0), Mem_Init, Task_InitRoot(0x00460710) which creates in this order:

| # | serial | fn | role |
|---|---|---|---|
| 1 | 65574 | 0x0045ECB0 -> 0x0045ECC0 | event VM (VM runs from 2788; op 0x4D srand(0) again at 2788) |
| 2 | | 0x0040AD90 | environment interpolation (placeholder: no draws) |
| 3 | | 0x004022B0 | camera (EventRunner::CameraTask) |
| 4 | | 0x004132D0 | sky |
| 5 | | 0x00414F10 | rail / player position 0x009C71E0 (EventRunner::RailStep) |
| 6,7 | | 0x004145C0 -> 0x00413E90 | players 1, 2 (Player_SetState(4): flags 0x10/0x20 -> 0x009C8E80 = 2, route 0x009C8E84 = 2) |
| 8 | | 0x00414F40 | 0x009C7000 = players in game (2) |
| 9 | | 0x00413970 | letterbox |
| 10 | | 0x00480970 | alternative models 0x009A2BB4 (placeholder) |
| 11 | | 0x00401260 | stage section draw |
| 12 | | 0x004136A0 | rain (Rain_Init: 150 rands with no task current = serial 0) |
| 13 | | 0x00408DD0 | lock-on table build (game::LockOnTable) |
| 14 | | 0x004090B0 | attack order (radix sort 0x00409190, ranks +0x131D/+0x131E) |
| 15 | | 0x00406010 | level collision lists (op 0x10/0x11; placeholder) |
| 16 | | 0x004044A0 | hit task (shots vs targets, Hit_Deliver) |
| 17 | | 0x00426960 | demo timer (placeholder) |

then 0x0040C340 and Stage_ResetState 0x0045EDD0 (letterbox 5, sky mode 0, rain 0, flags cleared). After the task
run, rank update 0x004607B0: rank += (players - prevPlayers) * 4 -> 9.

## 3. Frame order - CONFIRMED

`World::Step()` = one Game_Frame: resource queue steps (HYPOTHESIS: 1 request per step, 10 steps per frame; motion
466 is resident at 2790), then Task_Run over the flat creation-ordered list (the event task first; tasks created
this frame run at the end of the list in the same frame; a killed task is unlinked at once), then the rank update.
Each world task is a port of the original task function (table in §2); actors spawned by ops 0x09-0x0C get the
actor-type function (0x009A2280[type], table 0x00593358) and run it on the spawn frame.

## 4. What was added in this stage

- **Walking enemies** (kind 19 "znsam2", variant 2): behaviours 15 scripted walk-in 0x00457220, 1 idle 0x004554D0,
  2 approach 0x00455720, 3 attack 0x00455A40 (+0x00456490 hit tick), 4 back off 0x00455C30, 5 wait 0x00455670,
  6 die on the spot 0x00454D20; helpers 0x00454770, 0x00456650 (bands 0x009A2BE0 / limits 0x009C8E40..48),
  0x00409ED0 (rotates by RotY(ftol(player HEIGHT 0x009C71E4)) - original bug, kept), 0x00409E80 / 0x00409F90,
  0x00455920, 0x00456220 + 0x004563F0 (death motion from the hit direction, splash ticks), hook 0x004569B0,
  hooks 0x00456C50 / 0x00456D10 (variant 6), Enemy_Despawn 0x00455490, Enemy_StartDeathMotion 0x004560B0 (full).
- **Nearest-side target** 0x00409C90 in Enemy_PickTarget (focal * view x / view z < 0 -> player 2).
- **Odd-tick blend start** 0x00411F20: target pose = halfway between frames f and f+1, but only for the root
  position, the root node and the nodes of the skeleton type's top-level list (0x004120C0 -> 0x00412100 does not
  recurse: nodes 1 and 9 for kind 19); other nodes keep frame f+1. Original quirk, reproduced.
- **Sphere pusher** 0x00405B10 / 0x00405760 between registered target spheres (+0x12C, copied at registration).
- **Scripted civilian** (actor type 0x10, kind 0x20 in the demo): create 0x0048A3E0, update 0x0048A920, block
  script 0x0048B9E0 / wait 0x0048B1E0 / look-ahead 0x0048B760, turning 0x0048C850 / 0x0048C990, motion change
  0x0048C310, node draw 0x0048D1F0 (face animation table 0x0056B950), post-root 0x0048D070, parts list
  0x00412440 / 0x004124F0 (anim +0x1170, ids < 0x24 replace node models, >= 0x24 drawn on a node), lifetime
  (0x0045CA60 off-screen test). Control block layout in `CivCtl` (world_internal.h). Script format: words
  {op, args}; a block starts with op 0x2C (end-condition flags) and ends at the next op > 0x2B (0x2C next block,
  0x2D end). Demo script 4: walk motion 0xD1 endlessly toward point 0x0056BEA8, end condition 0x10 (arrival within
  1.0), next block sets flags 0x12080000 (rescue bonus +400, 0x009CA0E8 -= 1, off-screen removal).
- **24-bit FPU arithmetic** in mstack.h (every add/multiply rounded to float, like the game's x87 control word);
  enemies use the 24-bit Cam_DirToAngles of shoot_camera.h. With double intermediates the demo drifted by one
  angle unit (enemy facing at 3410 / 3569 -> camera at 3585).

## 5. Unported pieces (reported through World::Notes(); none changes any check in the demo)

- Level collision meshes (op 0x10 list 0x00991D00 from 3232): floor query 0x004AAA40, push 0x004057F0 /
  0x004AAFF0; collision-mesh objects (flags 0x10, e.g. the animated prop 65610): Hit_TestObjectMesh 0x00404A00,
  floor 0x00405550. FloorHeight returns the ground height 0x009C8E58.
- Enemy motion sounds 0x00452A10 (sound cues only), enemy upper-body track 0x004547C0, hit reactions
  0x004543F0 / 0x00454050 (not reached), kinds 2/3/9/0xC/0xE/0x12 setup, behaviours other than 1-7, 9, 15, 23.
- Civilian: vehicle variant 0x0048B140, child enemies, look modes, attachments 0x0048CD10, shot penalty
  (0x00415430 / 0x004156C0 / 0x0048E080), rescue bonus score, script ops 0x10 (function), 0x13-0x15, 0x19, 0x1D.
- Environment interpolation task 0x0040AD90 (values left to the renderer), demo timer 0x00426960, light /
  texture hooks (0x0041DC70, 0x0041D5D0), Player_TakeHit 0x00415300 (no effect in game mode 5).

## 6. Renderer API (world.h)

`Draws()`: per draw {model, world matrix (without view; `viewSpace` draws are already in view space), task serial,
original fn, draw mode / pass-1 flags, alpha, alternative-model flag, cull flag, skeleton node, up to 4 skin
matrices for skinned attachments}. `Camera()`: active context, position, angles, view and camera matrices.
`Hud()`: letterbox, can-shoot, per player aim / fired / shots. `Enemies()`, `Sounds()`, `EventOps()`, `RandCalls()`,
`Tasks()`, `Notes()`.

## 7. Open items

- Resource queue rate (HYPOTHESIS 10 steps/frame, 1 request per step) - only checked indirectly (motion residency).
- 0x009C6F2D / 0x009C6F2F (civilian wait condition 0x1000) assumed 0; 0x009CA0E4 camera shake and the context-2
  tick 0x009A6458 (behaviour 23 trigger) are not modelled.
- Scripted gameplay captures (`scene_capture_play1/2.txt`, mode 6) are not run yet: World is configured for the
  attract demo (game mode 5, demo auto-gun); mode-6 player input/state paths (0x004169C0 / 0x00417300) are needed.
