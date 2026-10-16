# Stage 2 (continued, part 3): scene 0x1B to the stage end, the stage-2 boss

Continues `stage2.md` / `stage2b.md`. Check (capture and options: stage2.md section 1):

```
world_test game build\scene_capture_auto_st2.txt --script build\script_auto_full.txt --stage-start 14659
```

**Result: every logic check is exact for all 15141 frames 14659..29799** (rand state / calls / sites, task list,
event ops, live enemies, attackers, enemy hp / position, lock-on slots, sounds, game mode / sub-state, letterbox,
rank, hit shake, players, aim, gun records, continue timers, credits), i.e. the rest of stage 2, the boss, the stage
clear, the stage end and, through the in-run stage chain, the first 64 frames of stage 3. Remaining differences:
camera view 15076 / 15141 (one angle unit at 22151..22233: the civilian 69782's body point is ~6e-5 off, the old
node-matrix ulp issue, also seen in stage 3 at 32523), players' point / angles from 18450 (older, rail rounding),
draws (rounding, capture model-id aliases).

Symbols: `stage2c_symbols.csv`. **CONFIRMED** = ported and exercised frame-exact; **CODE** = ported, not exercised.

## Progress (first divergence of the rand state Z / task list K after each step)

| Step | Z | K |
|---|---|---|
| start (merged main c90dffe) | 22127 | 22127 |
| enemy kinds 2 / 3 (weapon meshes in the hands, group count 0x009C8A74), behaviour 20, shot vs node mesh 0x004048A0 | 23643 | 23652 |
| civilian script op 0x10 function 0x0048DA90 (look at the children) | 23643 | 23652 |
| behaviour 31 | 25873 | 23979 (boss) |
| the boss (actor 0x14), water level (actor 0x16), komono 18, fish jump 0x00438640 | 29305 | 25323 |
| actor 0x25 command 17 (task 0x00484EA0) | 29305 | 26509 |
| boss nodes keep the draw-time model-view (hit test at 26509) | 29309 | 28932 |
| actor 0x25 ctor resets the task function after its first run | 29309 | 29309 |
| actor 0x21 kill counted as a rescue (stage clear shows 6) | exact | exact |
| event op 0x2B hit-rate bonus (players at 29267), stage chain keeps the stage-end scene (events at 29738) | exact | exact |

## The boss (`src/game/world_st2c_boss.cpp`)

Actor type 0x14: 0x00475E90 makes the spawn task the boss (block 0xBC at +0x1310, 0x007DCF20), task 0x00476150 with
the state table 0x00596218 (21 states, all ported; the capture uses 1, 5..17 and 19). Per frame: shots (0x00476270),
state, skeleton + draw (0x00476AD0), arena walls (0x00477CD0 on the corners from the record), phases (0x00477E60),
view position (0x00409B70 with +0xC = 6), aggression (0x00477FF0), removal at camera P s16 0x30 / tick P s16 0x32.

- Damage only through the chest opening: a shot on node 1 must pass within 3.5 of node 1 * (0, 4, 1) and enter the
  5.5 sphere at node 1 * (0, 3, 0) inside the yaw / pitch window of the current chest-model frame (+0x88 - +0x8A ->
  0x00596480). The chest model cycles 0x2EE..0x315 with the timing rows 0x005965C0[+0x94] (+0x94 goes up 2 per hit,
  down 1 per landed attack). Damage 0x00596697[players + aggression * 2] (16 at aggression 15 with one player).
- **The node matrices are model-view in the original** and the hit test / world points run at the start of the next
  frame: model-view of the last draw (`Boss2Task::drawView`) times the current camera matrix. Using the current view
  rejected/accepted shots at the edge (26509: distance 3.4937 vs 3.5).
- The boss's fish: 0x00438640 creates a fish task (stage 3's 0x00438730, `world_st3_fish.cpp`) that jumps at once
  (0x00438850); kinds 1 (state 10) / 2 (state 11); water level 0x007DCBB0 = -24.9.
- Water level 0x00442390 = actor 0x16's record y (the wave objects 0x004420C0 / 0x004421B0 of 0x004422D0 are not
  spawned in stage 2).
- Health bar / name card: main's `SpawnBossBar` (0x00435E50) and `SpawnNameCard` (0x00437A70, added next to it in
  world_st1b_boss.cpp) so play.exe draws them; splashes through `SpawnExplosionAt` (0x0043FCA0, world_st3_vehicle.cpp).
- Death state 19: placed at (227.3, 23, -2121.9), falls back into the water, motion 0x2C placed by 0x0045CE70 (node 1
  kept), bobs on the water, uncounted 0x78 frames later. States 18 / 20 (other phases) are CODE.
- Not ported (Notes): the leg IK / floor follow of 0x00476AD0 (only while +0x34 & 0x20000 is clear; never in the
  capture), the foot globals 0x009A3500..0x009A353C (read by 0x00441D17, water drawing), the 0x9A3500 foot modes of
  the motion rows; the legs are drawn with their animated matrices (IK angles 0).

## Other ports

- Enemy kinds 2 / 3 (Enemy_SetupKind 0x00452FD0): hands (nodes 5 / 8) get flags 0x51, radius 0, mesh P word 4;
  0x009C8A74 counts the group (first one sound 0x4D17A9 / 0x1F25A9). `World::Impl::GroupCount()` is the runner global
  (op 0x4D clears it); world_enemy_ai's 0x00456600 uses it (the old `c8a74` member is gone).
- Shot vs node collision mesh 0x004048A0 (world.cpp TaskHit + `ShotNodeMesh`): the node's world matrix as the
  object matrix; the hit gets node flags | 0x50 and its impact effect (Hit_RecordImpact).
- Behaviours 20 (0x00457780) and 31 (0x00458CE0) in `world_st2c_enemy.cpp`.
- Civilian op 0x10 with 0x0048DA90: per-frame 0x0048DAB0 turns the civilian towards its children (step 0x80); in the
  look-ahead 0x0048B760 the function is called and +0x5C reset (the scan continues).
- Actor 0x25 command 17 (0x004842A0 case 0x11): task functions 0x00484DF0 / 0x00484EA0 / 0x00484F90, effects a = 2 / 3.
  The ctor 0x004840D0 sets the function to 0x004842A0 after the first run: a script ending (-1) in that run gets one
  more 0x004842A0 frame.
- Actor 0x21 (vehicle rider) killed: 0x009CA0EC / 0x009C89C0[stage] / 0x009C8EC0 = kind 0x36 (the stage clear lists it).
- Event op 0x2B (0x0045FE40): players in state 5 with > 19 shots get 0x00567990[(hits * 100 / shots) / 10].
- `ChainStage` keeps the scene the stage end left (op 0x4F past the mode's last scene: 0 after stage 1, 7 after stage 2).
- Komono 18 (task 0x00468E50): the lightning light's lifetime (the light itself is renderer state).

## Shared-code changes (small, additive)

`world_internal.h` (stage-2c block: SpawnStage2c, st2Water, SpawnNameCard, SpawnFishJump, SpawnExplosionAt,
PlayerBonus, EnemySetupKind2, EnemyBehaviourSt2c, GroupCount, ShotNodeMesh), `world_enemy.cpp` (SetupKind 2 / 3 hook,
behaviour hook), `world_enemy_ai.cpp` (GroupCount), `world.cpp` (node-mesh shot test), `world_civilian.cpp` (op 0x10),
`world_npc.cpp` (command 17, ctor quirk), `world_st2_actors.cpp` (SpawnStage2c hook, rider rescue count),
`world_st1b_boss.cpp` (SpawnNameCard), `world_st3_fish.cpp` (SpawnFishJump), `world_st3_vehicle.cpp`
(SpawnExplosionAt), `world_st2.cpp` (ChainStage scene), `tests/world_test.cpp` (first event-op mismatch printed),
CMakeLists (one target_sources line).

## Regressions (after merging main 18033ff)

auto_st1 901..14799 exact; auto_st3 `--stage-start 29736` 29736..45612 exact (rand / tasks / events / players; camera
from 32523 as before); play2scene1 3620 / 79; play1full 2436; 2p 5099; stage-3 demo 814 - all unchanged.

## Not ported / open (World::Notes on the stage-2 run)

actor 0x25 wait 4/4 (distance, unverified); actor 0x31 landing dust angles (0x00401870); actor 0x33 state 1 mesh
target (unchecked); bat skeleton draw (0x0040F4F0); bat water deformation (renderer); civilian head look modes 1 / 6
(0x0048D1F0, node 2 draw only); env ops 25 / 35 / 36 / 39 (interpolator 0x0040AD90); komono 18 light 0x0040DD90;
player +0x80 callback 0x00415E40; title card light setup (renderer); the boss leg IK (above).
