# Stage 4 (continued): the rest of stage 4, the stage-4 boss, the stage end

Continues `stage4.md`. Check (captures / env: stage4.md section 2):

```
world_test game build\scene_capture_auto_st4.txt --script build\script_auto_full2.txt --stage-start 44107
```

**Result: every logic check is exact for all 11993 frames 44107..56099** (rand state / calls / sites, event ops, live
enemies, attackers, enemy hp / position, lock-on slots, camera view, sounds, game mode / sub-state, letterbox, rank,
shake, players, aim, gun record, continue timers, credits, players' point) except the task list from **56009**: the
stage-5 start actors 0x2B (task 0x004383C0) and 0x2D (0x00428CD0) are placeholders (stage 5, ported by the stage-5
work). The stage end (0x0045ED90 at 56004, sub-states 5 / 6, Stage_Start of stage 5 at 56007, in-run chain) is exact.
Draws 10438 / 11809 frames (was 1271). Output kept in the main checkout: `build\wt_auto_st4b.txt`.

**CONFIRMED** = ported and frame-exact against the capture; **CODE** = ported, not exercised. Symbols:
`stage4b_symbols.csv`.

## 1. Divergence progress (first logic divergence)

| frame | cause | fix |
|---|---|---|
| 47355 | Hit_TestNodeMesh 0x004048A0: the node keeps its model-view of the last evaluation (previous frame's view) and is multiplied by the current camera matrix | `World::Impl::camViewPrev` (view before the camera task), `ShotNodeMesh` uses world x prevView x camera |
| 47715 (sites) | behaviour-33 rand sites of 0x00458F00 swapped (kind 1 is 0x00459011, kind 0x14 0x00458F27) | |
| 47763 (lock-on) | the gameplay camera command 0x00403710 clears the targets taken 0x009A2BA0 / 0x009A2BA4 | `EventHost::TargetsReset` (event_runner) -> `World::Impl` |
| 47766 (camera) | a shot-down thrown weapon is still in a lock slot this frame; the camera reads its +0x121 (-1 after 0x00456520) live | lock record synced in every state (world_thrown.cpp) |
| 48125 / 48129 | vehicle motions 2 / 3 / 4, actor 0x19 = the boss, carried object type 2 | world_st4_vehicle.cpp, world_st4_boss.cpp, world_st4_held.cpp: rand exact to the end at the first build |
| 53577 | the boss is removed at camera P s16 0x40 / tick 0x42: `SpawnRecord::params` held only 0x40 bytes | params 0x50 bytes (props.h / props.cpp) |
| 56009 | stage 5: actor types 0x2B / 0x2D | (stage-5 work) |

Draw fixes: the boss's floor shadow (0x00411090 = 0x004110D0 + 0x0040A590), komono 40 variant 9 extra model (model +
0x96 at a fixed world point by +0x1B8, 0x0046C570), skeleton 0x17 attachments (below), item slot 5 billboard
(0x004A9F70 clears the model-view rotation: drawn in view space; world_scene1.cpp).

## 2. The boss (`src/game/world_st4_boss.cpp`)

Actor type 0x19: ctor 0x004917E0 (the spawn task becomes the boss), task 0x004919D0, control block 0xA4 bytes
(0x007DD0AC, `Boss4Ctl`). Record: params byte 0 skeleton, byte 1 first state (< 2: rides the vehicle 0x009A2C88, drawn in
its frame), words 1..15 node collision meshes (flags 0x51, radius 0), s16 0x40 / 0x42 removal camera tick.

- Per frame: shots 0x00491B40, relocation 0x004928D0, wall checks 0x00492350 / 0x004922C0, state 0x00597298[+4],
  footsteps 0x00492460, draw 0x00492620, phase check 0x00492790, view pos 0x00409B70(+0x70), camera sweep 0x00493090,
  arena walls 0x00493330 (+0 & 2), aggression 0x004934D0, engine sound 0x004935E0, removal.
- Shots: node 2 (the head) damages by (s8)0x005704D7[players + level * 2] (13 at level 14 with one player); a node mesh
  hit of material 0x3D (the impact record 0x009A2C40, now kept by `ShotImpact` in `impactPos / impactN / impactMat`)
  takes 1 hp and leaves a wound mark (0x004920C0 -> task 0x00492210, model 0x3CD on the node for 0x78 frames, frozen
  once the boss is dead); material 0x35 nothing; other hits effect 0x33. Head model by damage 0x005709A0[-ftol(bar *
  -9)]. A head hit flinches (state 0x14, or 0x15 = knocked back when node 15 or 12 is above ground + 10); the hp
  threshold of the phase (+0x24 = hpMax * 0x00570490[phase]) makes it invulnerable (+0x34 0x100) until the next arena.
- Phases 0..8 (stage 4; phases 9..17 = the second intro 0x00493B40, CODE): 0x00492790 raises the phase by hp, requests
  the camera sweep 0x00570538[phase] (0x00493090: context 0 on a camera path, gameplay camera held by 0x009CA094,
  0x009C70AC / 0x009C70B0 / 0x009C70BC written, turned to the lock-on look-at) and 0x004928D0 places the boss at the
  next arena position at the phase's camera tick (points 0x00570640).
- States seen in the capture (trace with `HOTD2_BOSS4_DEBUG=1`): 0 (intro on the truck, name card 0x005972F8, jumps off
  at flag 0x1E, bar 0x00435E50 when the letterbox opens), 4, 5, 7, 8, 9, 11, 15 (swipe, Player_TakeHit kind 8), 18
  (takes one of the two objects on its body and throws it), 19 (crouch / leap), 20, 21, 22 (death, flag 0x20). CODE:
  1..3, 6, 10 (turn end not ported: Note), 12, 13, 14, 16, 17; 23 (pad-controlled debug move) is a Note.
- Health bar / name card: the existing `SpawnBossBar` / `SpawnNameCard` (world_st1b_boss.cpp), i.e. WorldHud::sprites
  for play.exe; `bossBar` / `bossActive` written by the boss.

## 3. Carried object type 2 (`world_st4_held.cpp`, `world_held.h`)

`HeldTask` moved from world_st1b_held.cpp into `world_held.h` (+ `body`, `stuck`, `lock`). State 18 creates it
(0x00494F70) on task 0x00443200 (holder node 8 x T(-0.5, -2, 0)); at the throw tick state 4 -> 0x00442B90 (arc at a
point 15 in front of the view in 50 frames) -> 0x00443B90 (flight, lock-on candidate); close to the view it sticks
(0x00444160, CODE) and hits the target (kind 1); shot to pieces (0x004423F0 type 2, `HeldType2Break`) it is deflected
(angle to a point beside the view) and flies off (0x00444F70) until off screen. The capture's object was shot down.

## 4. Other ports

- Vehicle motions (`world_st4_vehicle.cpp`, hooked from world_st3_vehicle.cpp): mode 2 / 9 0x004408A0 (the boss's truck
  on path 0x175, doors 0x952 / 0x953), mode 3 0x00440AD0 (fade to 0.7 and back, draw mode 9, Task_Kill), mode 4 / 7
  0x00440C20 (path 0x176 with fire and shake, prop skeleton 0x15 motions 0x1CD / 0x1CC, model 0x954 at tick 0x38E).
- Skeleton 0x17 (actor 0x31) attachments: 0 = skin (as the other kinds) drawn translucent with +0x138C
  (`EnemyTask::Alpha138C`, Actor31Task overrides it); 1 = the tail 0x0041A020 (8 segments 0x0052EA38, nodes 16..23,
  models 0x1FC2..0x1FC9, each with its node matrix; the vertex chaining is not modelled).

## 5. Shared-code changes (small, additive)

`world_internal.h` (stage-4 block: camViewPrev, TargetsReset, impact record, SpawnBoss4, VehicleMotionSt4,
AnimAttach17; `EnemyTask::Alpha138C`), `event_runner.h/.cpp` (`EventHost::TargetsReset` from Cmd::Gameplay),
`world.cpp` (camViewPrev), `world_st2c_enemy.cpp` (ShotNodeMesh matrix), `world_st2_enemy.cpp` (rand sites),
`world_thrown.cpp` (lock sync), `world_collision.cpp` (impact record), `world_st1b_held.cpp` (HeldTask -> header,
type-2 break hook, three exports), `world_st3_vehicle.cpp` (motion hook, draw mode), `world_enemy.cpp` (kind 0x17
attachment hook), `world_st2_actor31.cpp` (Alpha138C), `world_actors.cpp` (komono 40 variant 9), `world_scene1.cpp`
(item slot 5), `props.h/.cpp` (params 0x50 bytes), `world_st4_actors.cpp` (type 0x19 dispatch, AnimAttach17),
CMakeLists (one target_sources line).

## 6. Regressions (unchanged logic and draws)

auto_st1 901..14799 exact; auto_st2 (prefix) 14400..29799 exact; auto_st3 (prefix) 29700..45612 exact (game over to
46375); play1full 2436 exact; 2p 5099 exact; stage-3 demo 814 exact. Draw counts identical to before on all of them.

Frontend `frontend_test game build\scene_capture_attract_loop.txt --resync --from 28045 --to 29786` (demo 3 = another
part of stage 4): rand 937 / 1742 (first 28981), task list 322 / 1742 (first 28366: a serial off by one after
unported attract actors), camera 913 / 1742; not checkable yet (actor 0x24 variants / kinds 83 / 85, komono 73 / 76,
actor types 36 / 45 / 80, actor 0x31 behaviours 8 / 11, sky types 8 / 10 / 11 ...).

## 7. Not ported / open (World::Notes on the stage-4 run)

- stage-4 boss light 0x0041DC70 / 0x0041DCC0 (renderer state); boss state 10 turn end (0x00494730); state 23.
- civilian head look modes 1 / 6 (0x0048D1F0: node 2 re-aimed in node 1's frame, clamps, steps 0x0048D9B0; writes the
  node-2 matrix) - the remaining 0x0048A920 draw differences (418 frames).
- actor 0x25 motion props 0x32A / 0x34C / 0x35D (0x00485790) - 0x004842A0 draws (152 frames).
- actor 0x31 landing dust angles (0x00401870), env ops 25 / 35 / 36 / 39 (0x0040AD90), title card light setup.
- carried object throw states 5 / other (0x00442B90), stuck object 0x00444160 (CODE).
- stage-clear / name-card 2D draws (0x00435760 / 0x00434EF0 / 0x00437AC0 / 0x00426A70), as on stages 1..3.
- stage 5 start: actor types 0x2B / 0x2D.
