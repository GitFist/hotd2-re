# Shooting camera, view building, lock-on table, VM gameplay conditions (Stage 3)

Stage-3 port notes. Builds on `notes/agents/events.md` (VM, command queue, §3.4 shooting camera). Evidence: disassembly
(`tools/disasm.py`, addresses given) and the capture `build\scene_capture_stage3.txt` (attract demo 0, stage 1,
frames 2780–3599 with W/T/K/Y lines). **CONFIRMED** = code + bit-exact replay; **CODE** = read in code, not exercised
by the capture; **HYPOTHESIS** marked.

Port: `src/game/shoot_camera.{h,cpp}` (math mirrors, contexts + Cam_BuildViews, lock-on table, target selection),
`src/game/event_runner.{h,cpp}` (`CameraDirector`: shooting camera state machine, rail, player position),
`src/game/camera.{h,cpp}` (`CameraBank`, float-precision Cam_Curve / Cam_DirToAngles / view). Test:
`tests/event_runner_test.cpp`.

## 1. Results

`event_runner_test game build\scene_capture_stage3.txt` (frames 2788–3599, 812 frames; no oracle, inputs = enemy
dumps + live-enemy count of the previous frame):

| Check | Result |
|---|---|
| Event ops (E), in place | 917 / 917; frames with identical op sequences 812 / 812 |
| `Cam_Evaluate` calls (C: id, time) | 551 / 551; frames 812 / 812 (values: max rel. error 4.6e-7 = print precision) |
| F lines (active context, camera, tick, pos, pitch/yaw/roll) | 812 / 812 each |
| Context 0 vs W 0x009A6000: view +0x00 (16 floats) | **812 / 812 bit-exact, max error 0** |
| … camera matrix +0x40, pos +0xC0, angles +0xCC, target +0xD8 | 812 / 812 bit-exact each |
| Globals (W): state 0x009C6F20, flags 0x009C6F2C..2F, command state, pending, 0x007DCCA8, tick 0x009A6110, ticks left 0x009C6F28, active ctx, mode, sub, camera id, yield | 812 / 812 each |
| Lock-on table rebuilt from enemy dumps vs W 0x009A5EC0 slots 0..3 | 811 / 812 (+1 frame with a non-enemy object, §6) |

Other modes: `--slots-from-w` (slots straight from W): same, all 812. `--oracle` (Stage-2 oracle, kept as a
fallback; automatic when a capture has no W lines): control flow 812/812 but views only 369/812 (the oracle has no
target positions). `--no-inputs` (control): 178/812 op frames, 114/812 evaluation frames, 95/812 views.
Older captures (events, actors, fog, lights, st1demo, enemies_full): PASS (oracle, as before).
`cam_test`: 2730/2730, worst relative error 6.7e-6 → 4.6e-7 (float Cam_Curve); chunk identity unchanged (92.9%).
effects/shooting/props/enemy/env/stage tests unchanged (they use `LookAngles`).

## 2. The FPU runs at 24-bit precision (CONFIRMED)

Direct3D's default FPU setup leaves the x87 precision control at 24 bits: every fadd/fsub/fmul/fdiv/fsqrt result is
rounded to a float significand; fpatan/fsin/fcos results are not (precision control does not apply to them), but the
next multiply rounds them. Evidence: `Cam_BuildViews` re-derives every context's angles from its own matrix
(0x004018E0). With extended intermediates (6479, 13958) comes back as (6478, 13958); with 24-bit rounding it comes
back as (6479, 13958), which is what the capture has (W 0x009A6000 +0xCC, frame 2789; same for 2790, 2793, 2795,
2799). The double-precision port matched 280/812 frames' angles, the float-precision port 812/812.

Ported code mirrors this by doing basic operations in float (SSE float arithmetic rounds identically) in the original
operation order, and keeping only transcendental results in double until the next operation. **Other ports that
assume extended precision (anim, effects, props, shooting, enemy) will see ±1 angle / last-bit differences**; the
math helpers in `shoot_camera.h` (`MtxStack`, `DirToAngles`, `TruncToAngle`) are the reference.
`camera.cpp` now uses them: `LookAngles` → `DirToAngles`, `ViewMatrix` → matrix-stack sequence, `EvalCurve` → float
Hermite in the order of 0x0040414E..0x004041D3.

## 3. Per frame (order of the stage task list)

1. `Evt_Run` (VM). 2. env interpolator. 3. **Camera task 0x004022B0**: 0x009C6F2F = 0, 0x0059C988 = 1; mode function
(1: queue + context-2 mirror 0x004023F0; 2: queue); contexts 1–3 (nop); **Cam_BuildViews 0x00401F40** (views + mode
commit). 4. 0x004132D0. 5. **Rail step** 0x0040C370 (rail pose, players' position 0x009C71E0). 6. … **lock-on table
task 0x00408DD0**. 7. actors (enemies register for lock-on in their update).

### 3.1 Shooting camera 0x00402650 (state machine as events.md §3.4, CONFIRMED)
- Lock-on test reads slots 0..3 of 0x009A5EC0 only.
- **State 3, 0x00402890**: pos = rail pos (1/16 lerp if 0x009C6F33); look-at 0x009C6FA8 = `Cam_TargetLookAt`
  0x00403050; target = `Cam_RotateToward`(pos, look-at, target, 1, step 0x009C6F36); `Cam_LookAngles` (ctx roll);
  c = `Math_SignedCos2`(look-at − pos, target − pos). No slot (0x0059C988 == 0): |c| > 0.99999 → 0x009C6F2F = 1,
  step = `*(s8*)*0x00576C0C` = 12. Slot: step = 0 if 0x009C904A == 0 and style 2, else `Cam_TurnStepFromAngle`
  0x00403B00 = table 0x00576C04[0x009C6F38][min(angle(target, look-at), 0x1FFF) >> 7] (table 1 = 0x00576B44: 64 up
  to index 25, then 63, 56, 50, 43, 36, 29, 22, then 16). The step used in frame N is computed at the end of N − 1;
  `Cam_ShootInit` sets 0x200 (first tracking frame barely turns).
- **State 2, 0x00402740** (style 0/2): settled (0x009C6F2D) → `Cam_Evaluate` into ctx 0, `Cam_LookAngles(roll 0)`.
  Else (0x009C6F2C = 1 on the first frame): `Cam_SmoothToRail` 0x00402E60 (evaluate → 0x009C6F90, pos 1/16 there),
  `Cam_TurnToRail` 0x00402F80 (evaluate → look-at = rail target, `Cam_RotateToward` with step 12), alignment test
  |c| > 0.99999 → 2C = 0, 2D = 1, 2F = 1. Settling frames 2937, 3030, 3242 come out of the computation.
- Style 1 (0x00402860), state 6 (empty), state 7 (`Cam_ShootMoveTo` 0x00402A40), sub 7 (`Cam_ShootFollowRail`
  0x00402E00): ported, CODE.

### 3.2 Cam_RotateToward 0x00403C00 (CONFIRMED)
`(a, b) = 0x00401790(target − pos)` (b = atan2(y, x), a = −atan2(z, |xy|): the frame in which pos→target is +x).
`c` = angle of the look-at about that axis (atan2(z, y) in `T(−pos) Rz(−b) Ry(−a)`); `d` = atan2(y, x) of the
look-at in `T(−pos) Rz(−b) Ry(−a) Rx(−c)`; `e = d·num / (num + den)` (idiv); result = `(100 cos e, 100 sin e, 0)`
transformed by `T(pos) Rz(b) Ry(a) Rx(c)` (stack order). So the view direction turns num/(num+den) of the angle
toward the look-at inside the plane of both, and the stored target is 100 units out (W shows |target − pos| = 100
while tracking). With num = 1 the per-frame turn is 1/(1 + step).

### 3.3 Cam_BuildViews 0x00401F40 (CONFIRMED)
For the **active** context first: `Cam_LookAngles(pos, P, roll)` with P = (0, (float)0x009CA0E4, −1000) in its own
camera frame `[I T(pos) Ry(yaw) Rx(pitch)]` — the angles are re-aimed every frame (0x009CA0E4: **HYPOTHESIS** hit
shake from 0x00415270, 0 in the demo). Then for **all four** contexts: camera matrix +0x40 = `[I T Ry Rx Rz]`,
`Cam_NormalizeAngles` 0x004018E0 (pitch/yaw from the matrix's +z axis via `Cam_DirToAngles`, roll from its +x axis),
view +0x00 = `[I Rz(−roll) Rx(−pitch) Ry(−yaw) T(−pos)]` with the normalised angles. The round trip moves angles by
±1 where truncation is close and is not idempotent (e.g. ctx0 yaw 1316 → 1315 at frame 2880, a frame in which nothing else writes context 0); context 2 (active in event mode) differs
from context 0 by the re-aim (F 2789: 6478 vs W ctx0 6479).

### 3.4 Lock-on table (CONFIRMED, `game::LockOnTable`)
- 0x009A5EC0 = **16** slots {u8 on, obj*}; candidates 0x005A4DC8 = 14 × {key, obj}, count 0x009CA93C.
- `Target_Register` 0x00408EC0 (from `Actor_UpdateViewPos` 0x00409B70 in every enemy update, not in the spawn
  frame; skipped when obj+0x34 & 0x10000): key = ftol(|obj+0x40 − players 0x009C71E0| · 10).
- Table task 0x00408DD0 (runs after the camera): radix sort 0x00408F30 (stable, low 16 bits of the key), clear 16
  slots, nearest first: an object targeting a player (obj+0x121 ≠ −1) takes the first of slots 0/1 with a null
  pointer, the others slot 2 + rank; obj+0x120 = slot; count = 0.
- The camera reads the objects' +0x100 (body point, already +yOffset from 0x00409B70) live. So: registration in frame
  N − 1, slots in frame N (after the camera), camera use in frame N + 1. Demo: single enemies (targeting player 1) in
  slot 0; the 3-enemy group (no target player) in slots 2, 3, 4 → the camera looks at the midpoint of the two nearest.
- `Target_Remove` 0x004092B0 / direct slot clears on death keep the pointer; `Target_AddDirect` 0x00408E80 puts an
  object in the first free slot 2..13.
- Players' position 0x009C71E0: rail sub 4/6/7: rail pos with y − 15 (or 0x009C8E58 when 0x009C70F4 == 1); event
  mode (0x0040C380): row 3 of `[ctx0 camera matrix, T(0, −15, 0)]`.

### 3.5 Corrections to earlier notes
- `Cam_TargetLookAt` 0x00403050 is not a mean of the slots: slots 0/1 (player-targeting objects) first, the one that
  targets a player wins, midpoint only if both or neither do; then slots 2+3 midpoint, 2, 3; none → rail look point.
- `Cam_CmdPreset` 0x004032E0 pose = pos + pitch/yaw/roll (24 bytes to ctx +0xC0), not pos + target (port fixed).
- In W 0x009C6F2C (4 bytes) the order is 2C returning, 2D settled, 2E style, 2F aimed: frames 2881–2882 have
  2F = 1 (aligned with the rail look point while no slot is set yet), not 2C.
- Rail sub 6/7 also run `Cam_LookAngles` on the rail block (0x009C70CC..) — used by sub 7 (0x00402E00 copies it).
- Look table (type 0x60) storage: entry i = (tick args[2i] at 0x009C6FE0 + 8i, camera args[2i+1] at 0x009C6FDC + 8i).

## 4. Inputs (interface) and API

`EventHost` (event_runner.h) is the interface to other subsystems; values are read when the original reads them:
`LiveEnemies` 0x009C904A, `Count7006` 0x009C7006, `CountA0E8` 0x009CA0E8, `Busy93C` (0x009CA93C ≠ 0 =
`LockOnTable::Count()`), `LockOnSlots` (slots 0..3 with the objects' +0x100 / +0x121 = `LockOnTable::Slots`),
`ShakeY` 0x009CA0E4, `GameRunning` 0x007DCCA4, `RailAdvance`, `PlayersReady`, `EvaluateCamera` (only without a
`CameraBank`), `CameraAlignedOverride` (oracle tests only). Removed: `TargetLocked`, `CameraAligned` (now computed).

World runtime per frame: `runner.RunEvents()`; env; `runner.CameraTask()` (→ `camera.ActiveView()`, `camera.ctx[i]`);
…; `runner.RailStep()` (→ `camera.playerPos`); `lockOn.Build()`; actors (`lockOn.Register(&target, camera.playerPos)`
in each enemy update, `Release`/`Remove` on death). `runner.Frame()` = the three runner steps. Attach the camera
files with `runner.camera.SetCameraBank(&bank)` (`CameraBank::Load(exe, read cam\<name>)`).

## 5. Symbols

`notes/agents/shoot_camera_symbols.csv` (46 rows: 37 functions ported/refined, 9 data).

## 6. Open items

1. Frame 3232: slot 2 holds a non-enemy object for one frame — task 65612 (fn 0x0048A920, created that frame, flags
   0x10081, i.e. `Actor_Civilian` 0x0048A3E0 family), so a constructor path calls `Target_AddDirect` 0x00408E80
   (**HYPOTHESIS**; callers 0x0048FD90, 0x004917E0, …); the next rebuild clears it. No effect on the camera (event
   mode); not modelled by the test.
2. Not exercised by the demo (CODE only): styles 1/2 (0x009C6F2E is never set non-zero here — who writes it? only
   0x004031E0 clears it in the static refs), states 6/7, sub 5 and 7, look tables in shooting mode, slots 0+1 together
   (two players), 0x009C6F38 ≠ 1, 0x009CA0E4 ≠ 0, roll channel 0x009A21B0, 0x009C6F1C = 5, `Target_AddDirect`.
3. `0x009C6F38` table 0 is 0x0059C9A8 (zeros in the file; written at run time?).
4. Player orientation (0x009C71EC.., 0x00401C50) and the 0x00402460 player placement are not ported (players only).

Captures wanted (same W list as stage3 plus 0x009CA0E4, 0x009C8E8C, 0x009C6F36, 0x009C6F38, 0x009CA93C, 0x005A4DC8
(112 bytes), 0x009A5EC0 (128 bytes = all 16 slots), 0x009C71E0 (24 bytes); E/C/F/K/T(4533F0, 48A920)/W):
- attract demos 2 and 3 (stage 2 scene 28, stage 4 scene 5) — route ops, type-0x60 look tables, sub 6/7 rails;
- a short real game (mode 6) where a player gets hit and shoots enemies targeting both players (slots 0+1, shake).
