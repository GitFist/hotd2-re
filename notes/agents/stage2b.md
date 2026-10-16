# Stage 2 (continued): bats, carried objects, route signs, komono

Continues `stage2.md`. Check: `world_test game build\scene_capture_auto_st2.txt --script build\script_auto_full.txt
--stage-start 14659` (capture and options: stage2.md section 1). Symbols: `stage2b_symbols.csv`.

**CONFIRMED** = ported and exercised frame-exact against the capture; **CODE** = ported from the code, not exercised.

## Progress (first divergence of the task list K / rand Z after each step)

| Step | K | Z |
|---|---|---|
| start (stage2.md) | 17260 | 17272 |
| bats (actor 0x43) | 17650 | 17650 |
| bat +0x121 = 0 (cleared memory: lock-on slots 0/1), civilian rescue bonus + wait condition 0x1000 | 17700 | 17800 |
| actor 0x24 variant 3 (0x004831E0) | 17825 | 17801 |
| enemy behaviour 33 (thrower) | 18128 | 18269 |
| item drops slot 1 / 3 | 18274 | 18269 |
| actor 0x31 behaviour 1 (flinch) | 18274 | 18277 |
| actor 0x31 behaviour 31 start, choices 0x1F / 0x20, footprints 0x5A, landing block 0x4B | 18317 | 18318 |
| komono 14 (route sign) / 17 | 18610 | 19458 |
| komono 12 | 18782 | 19458 |
| enemy behaviour 37 + carried object | 18984 | 19458 |
| civilian script op 0x1D (voice line) | 19289 | 19458 |
| komono 19 / 20 / 50 / 58 / 60, smoke 0x00465860, light task (event op 0x17) | 19543 | 19543 |

Next: scene 0x19 (19542): actor type 0x40 (ctor 0x0043BD30, a pack of creatures, task 0x0043BEF0 -> 0x0043C440, death
0x0043DA20) uses the second skeletal animation system 0x0040F520 / 0x0040F860 (skeleton table 0x004E0430, motions
0x004E05CC) which is not ported anywhere.

## Notes on the ports

- Bats: `src/game/world_st2_bats.cpp`. The bat block is 0x2A0 bytes; +0x121 (target) is 0 from the cleared task memory
  (only variant 0 / two players pick one), which puts both stage-2 bats in lock-on slots 0 / 1. The variant-0 hit test
  reads the tick of the ACTIVE camera context (`0x009A6110[ctx * 0x69]`): context 0 = the event camera tick; context
  2's +0x110 (0x009A6458) is not modelled (0 used, noted). State 2 reads +0x3C, never written (0).
- The death code has hard-coded scene geometry per variant (floors, walls, stairs); variant 0 / 1 exercised.
- Carried object (`world_st2_carry.cpp`): the pose is computed from the carrier's node MODEL-VIEW matrices (node world
  x view) and the object is drawn in view space, as the original. The second atan2 of 0x00442EB0 is scaled by the
  NEGATIVE constant 0x0055CB20 (the decompiler hides it). The carrier and the object share a `CarryLink`
  (shared_ptr) so the carrier can read the object's hp after the object task is gone (the original reads freed memory).
  The floor bounce 0x00444280 is only ported as a contact test (noted when the object reaches the floor).
- Komono generic ctor 0x00461CF0 cases: 0xC (12), 0xE (14), 0x13 (19), 0x14 (20), 0x3A (58), 0x3C (60, radius 2).
- Route: komono 14 / 19 write the camera route 0x009C88A4 (`runner->camera.routeBranch`) at spawn and when shot.

## Shared-code changes (small)

- `world_internal.h`: `EnemyTask::f135C`, `EnemyTask::carry`, `Impl::batAttacker / batSegStep / DropItemSt2 /
  SpawnSmoke / StartLightTask`.
- `world_civilian.cpp`: rescue bonus (+400 to ctl +0x6C or both), wait condition 0x1000 reads the camera flags
  0x009C6F2F / 0x009C6F2D, script op 0x1D plays the voice line.
- `world_scene1.cpp`: actor 0x24 variant 3; DropItem calls DropItemSt2 for slots 1 / 3.
- `world.cpp`: event op 0x17 -> light task; breakable "effect 0x00465860" -> SpawnSmoke.
- `world_st2_actors.cpp`: dispatch of actor 0x43 and komono kinds (SpawnKomonoSt2).
- `world_st2_actor31.cpp`: behaviours 1 / 31, TryBehaviour 0x1F / 0x20 (default: not taken, silently, as the
  original), footprints and the 0x4B landing block.
- `world_st2_enemy.cpp`: behaviours 33 / 37.
