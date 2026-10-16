# Exactness leftovers (agent "polish3")

Symbols: `polish3_symbols.csv`. Scratch: `<scratchpad>\polish3\` (regress.ps1 = the draws3 suite on this worktree,
rcmp.py, sweeps.ps1 = the six screenshot sweeps, blend_emu.py / mx_test.py / inv_test.py = emulator checks,
session.ps1 = runs of the original under the shared lock).

## Root causes and fixes

| item | root cause | fix |
|---|---|---|
| Stage-3 boss camera in mode 1 (scene 13, 0x004231C0), boss4 neck / hit-frame draws | the rotation blend 0x00412750 (motion blends, Anim flag 1) inverts A with 0x004A8D20; the port's inverse was double precision. Small relative rotations come from differences of near-1 values, so the blended angle was off by one unit (variant 5 nodes 15 / 17 at 3688 in boss4, then the camera turn to the head 1..3 units) | `mx::Inverse` = 0x004A8D20 / 0x004A8B60 in float with the exact x87 operation order, generated from the disassembly (`tools/x87gen.py`) |
| Held-object debris drift on st2_route17 (0x00444D80 / 0x00444EE0) | the same inverse (floor contact 0x00444280) | (same) |
| Attract demo camera / player at 17115, thrown objects | the hands grip 0x00442EB0 took world x view; the original uses the holder's node model-view (+0x28) | `HandsGrip` from `EnemyNode::mv` |
| Final boss node model-views (st6, 2p_auto_b, boss mode 8) | `NodeMV` = world x view | `EnemyNode::mv` |
| Story mode 11 actor 0x24 variant 1 (0x00483030) walking / wrapping its motion 0xEA (883 frames), attract 21900 "red cape" | adv2evtbl.bin is 3542 bytes; the last record's s16 P[16] (0x009781D4) is in its last two bytes. EventMemory dropped the partial word and read a word an earlier table left there (114); the original reads 0, so the actor freezes in its first frame | `EventMemory::Load` merges a trailing partial word |
| Final-boss blast streaks (sweep 84750) | 0x00429C90 scrolls the v of model 0x16B6's inline vertices (-= double 0x0055D1B0, bit 0 set) every blast frame; the edit stays in the loaded model | `BlastUvScroll` (ModelEdit uvs, kept in `waterUv` until the stage start) |
| Stage-2 scene 17 actor 0x44 state 7 (0x00474770) drawn from 20138 instead of 20357 | motion residency: Stage_Start resets every motion state (0x0041D510 -> 0x00412BC0) and loads only the stage's list (0x0045EDD0 -> 0x00412FD0 + flush); other files come from op 0x56 or on demand when an animation fetches a motion (0x00412F50 -> 0x0041D860 + 0x0041D810, synchronous). The port counted "never requested" as resident | `MotionResident` = loaded this stage; the stage list at StartStage; `MotionTouch` in `EnemyMotion` |

Verification: the port's `Inverse`, `BlendRotation` + `MatrixToAngles`, `RotX/Y/Z`, `Translate`, `Scale`, `MulLeft`,
`Transform`, `TransformDir`, `RotAxis`, `DirToAngles`, `DirToAnglesXY`, `AngleBetween` and `TurnToward` were compared
bit for bit with the original functions run in Unicorn (tools/emu.py; FPU control word 0x007F = 24-bit precision,
which Unicorn honours): 0 mismatches in 300..2000 random cases each.

## Numbers (main 979d0be -> this branch; every logic check identical)

| capture | check | before | after |
|---|---|---|---|
| modes_boss4 (stage-3 boss, mode 1) | camera / draws | 8144 / 8159, 8110 / 8149 | 8159 / 8159, 8149 / 8149 |
| st2_route21 | camera / draws | 25422, 23291 | 25437 (first divergence now 43837, stage 4), 23334 |
| st3_route1 | camera / draws | 9197 / 9200, 8822 | 9200 / 9200, 8825 |
| st2_route17 | draws | 9593 / 10118 | 9938 (444D80 / 444EE0 exact; 474770 from 20357) |
| st2_route15 / st2_shutter | draws | 8094, 5473 | 8115, 5475 |
| auto_st4 / st5 / st6 | draws | 11907 / 11915, 14805, 16095 | 11915 / 11915, 14806, 16158 |
| 2p_auto / 2p_auto_b | draws | 63529, 24662 | 63534, 24724 |
| attract loop / menus2_attract: demo | camera / draws | 5366 / 5375, 3422; 3475 / 3484, 3420 | exact, 3470 / 3482; exact, 3468 / 3480 |
| same: story mode 11 | draws | 5253 / 6136, 5254 / 6137 | 6136 / 6136, 6137 / 6137 |
| boss mode 1 / 2 / 5 / 6 / 8 | draws | 7965, 7959, 8143, 8133, 10528 | 7967, 7968, 8149 / 8149, 8134, 10623 |
| misc2_orig2 | draws | 15251 | 15252 |

Sweeps (tools/sweep_compare.py, mean block colour; no frame worse than main): phase 0 1.40 = 1.40, phase 150 2.37 ->
2.31 (84750 blast 0.7), 2P 1.36 = 1.36, attract 1.74 -> 1.63 (21300 / 21900), boss 2 24.65 =, boss 4 1.58 =.
render_compare 9/9.

## Remaining

- **Resolved (agent texres, notes/formats.md "Strip chunk" and "Residency"): not a texture problem.** The texel sum
  is a mod 2^32 sum and equals etc_1.bin #5's exactly (the bound texture is right). The original culls 6047 completely:
  its strips run with D3DCULL_CCW and its triangles are counter-clockwise on screen (forcing CULLMODE none / CW for 6047
  shows the red sky; z, fog or lighting do not). The port's "swap CW / CCW for mirrored worlds" rule was standing in
  for Res_LoadModel's in-memory flip of the strip cull bits (0x00419270), which only the skies of list 0x0057A280
  (6048 / 6049 / 6307 / 6309) get; 6047 is not in it. Sweep: attract 8100 14.4 -> 0.8, 20700 19.4 -> 0.7.
- (old analysis) **Attract 8100 / 20700 (story mode 11, the final boss in its room): the sky dome 6047 (sky type 3).** Root cause
  found with the new probe (`HOTD2_SCENECAP_RSTATE`, run polish3_sky, `build\scene_capture_polish3_sky.txt`): the
  original draws 6047 with the same matrices, fog (red 0x970000, table linear -2000..6000), light and material as the
  port, but the texture bound for it is a 256x256 16-bit surface whose texels are ~0 (texel sum 0x4FEB69 over 8192
  samples): black. The port binds tex\etc_1.bin #5 (mean grey 80). Hiding 6047 in the port reproduces the original
  frame. So in story mode 11 the sky's texture file is not resident (texture file state 0x009C7328, requests type 6
  0x0041CCA0 / type 7 0x0041CD00 = event ops 0x54 / 0x55, 0x0041D6D0); the port has no texture residency model.
- Mode 10 (attract): 482EE0 draws at 23018 / 24003 (one frame each, load timing) and the camera at 25459 / 25460
  (0.0039).
- Demo thrown objects start 1..5 ulps off in x (grip arithmetic; rotations and the camera are exact).
- lighting.md "Light calls 0x0041DC70 around non-skeleton draws": most of that list is already mirrored (fish, actor
  0x24 extras, bats, items); not re-checked one by one.
- The original's wall-clock load timing (draws2.md) still decides single frames of breakables / props (465FB0 at
  29648 in the attract demo, 2p_auto 7406, ...).

## Tools added

- `tools/x87gen.py`: symbolic x87 -> C++ float expressions (24-bit precision control: a float expression tree of the
  same shape is bit-exact). Used for the inverse.
- Hook `HOTD2_SCENECAP_RSTATE=<first>:<last>:<model,...|-1>`: R lines at the strip draws of the listed models (render
  states incl. fog / lighting / cull / z / alpha test, stage-0 texture ops, bound texture size and texel sum,
  material, light 0, first vertex).
- frontend_test `--dump F --dump-to G` (frame range).
</content>
</invoke>
