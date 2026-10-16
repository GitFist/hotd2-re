# Level scripts (event VM), camera command system, game flow

Stage-2 agent notes. Evidence is the handler code (decompile or disassembly, addresses given) unless marked
**HYPOTHESIS**. "Trace" means the runtime captures of the original (`build\scene_capture_*.txt`: E = every opcode
handler call, C = every `Cam_Evaluate`, F = per-frame camera context, K = task list).

Port: `src/game/event_runner.{h,cpp}` (`game::EventRunner`, `game::CameraDirector`, `game::EventHost`), full opcode
table in `src/game/event.cpp` / `tools/evt.py`. Test: `tests/event_runner_test.cpp`.

## 1. The VM

**Evt_Run 0x0045ECC0** (task body; the task is created by `Evt_Init` 0x0045EBC0 → 0x004A6FA0(0x0045ECB0, 0x34)):

```
0x007DCCA4 = running?                  // see below
do {
    op = *script;                      // script = 0x009C7108
    if (gameMode 0x009C8E98 == 5 && demo 0x009A3400 == 2 && op == 0x45 && script[1] == 0x13)
        script += 2, op = *script;     // demo 3 skips "wait flag 0x13"
    0x009C8900 = script;               // last pointer (unused here)
    table0x005931D8[op]();             // handlers advance the script themselves
} while (yield 0x009C8EA0 == 0);
```

- The yield flag is **not** cleared at the start of a frame. A frame's loop is exited only by a handler that leaves
  yield = 1, so the next frame starts with yield = 1 and the first handler called (the wait that yielded) sees
  yield = 1. **CONFIRMED** (code; replayed 917/917 ops).
- Wait idiom (0x41, 0x43–0x47): `if (!yield) { yield = 1; return; }` — reaching a wait mid-frame always ends the
  frame; the condition is first tested on the next frame, and a passing wait clears yield and continues in the
  same frame.
- `running` (0x007DCCA4), computed once per frame: game type 0x009CA08C == 2 → either player's state
  (0x009A5C62 / 0x009A5D92) == 5; otherwise → a player with state 5 and lives (0x009A5C66 / 0x009A5D96) > 0, or
  `0x00413280()` (non-interactive mode: game modes 5, 9, 10, 11, or 6 with sub-state 0x009C90A4 == 8). Waits
  don't pass while nobody plays (continue screen).
- `skipping` 0x009A2D74 (set outside the VM by the cutscene-skip code 0x00434100 / 0x00435B80 area,
  **HYPOTHESIS: start pressed during a skippable cutscene**): waits 0x40/0x41/0x42 pass at once, 0x30 blocks are
  dropped, 0x0D spawns, 0x3A/0x3B sounds suppressed. Enabled/cleared by op 0x2C.

**Evt_Init 0x0045EBC0**: variant 0x009A2BB0 = 0 for game modes 5/9; 5 for game type 1 on event mode 0
(and 0x009A5900 |= 8); 0 for game types 2/3; else 1. Then `script = comevtbl[mode 0x009A1A08][scene 0x009A2BC0]
[variant]`, route branch 0x009C88A4 = 0, wait counter 0x009C8904 = −1, queue indexes/count 0, command state
0x009A1A10 = 2, yield = 0, 0x009A2D7C = 0x009A2D74 = 0, 0x009C8E40.. = {2, 3, 4} (0x00408D60).

**Script tables**: `comevtbl[mode]` is a −1-terminated scene list, each scene a −1-terminated variant list.
Mode → stage table `0x004D1C7C[0x00579928[mode]]`: 0 st1, 1 st2, 2 st3, 3 st4, 4 st5, 5 st6, 6 trn, 9 end,
10 adv, 11 adv2 (7, 8 none). Static sweep: 509 scripts, 14,626 instructions, 72 distinct opcodes, all decodable,
no invalid opcode reached.

## 2. Opcodes (table 0x005931D8, 96 entries, 84 handlers)

Len = bytes (L = −1-terminated list, see `event.cpp`). "Uses" = static count over all scripts. Globals are
written as-is unless stated.

| Op | Handler | Len | Name | Semantics | Uses |
|---|---|---|---|---|---|
| 00 2A 34 3C 4C | 0x0041EBB0 | – | invalid | empty function: script not advanced, yield unchanged → the VM spins forever | 0 |
| 01–04 | 0x00408820 | L | route1_spawn | if route 0x009C8E84 == 1: run 0x00577650[op] (01→0x004088A0 as 09, 02→0x00408990 as 0A, 03→0x00408AA0 as 0B, 04→0x00408C40 as 0C); else skip list | 15 |
| 05–08 | 0x00408860 | L | route2_spawn | same for route 2 (05→09 … 08→0C) | 48 |
| 09 | 0x004088A0 | L | spawn09 | per record: task(0x009A2280[type], 0x13F4), flags, pos/rot, state, byte 0x24→+0x1F4, byte 0x25→kind (formats.md "Actors") | 349 |
| 0A | 0x00408990 | L | spawn0a | per record: task(type, 0x13F4), flags 0, state = rec[1] | 81 |
| 0B | 0x00408AA0 | L | spawn0b | per record `Actor_SpawnRecord0B` 0x00408A20 | 538 |
| 0C | 0x00408C40 | L | spawn0c | per record `Actor_SpawnRecord0C` 0x00408BC0 | 103 |
| 0D | 0x00408B70 | L | spawn0b_noskip | as 0B unless skipping | 2 |
| 0E | 0x00408D10 | 24 | set_table_2be0 | 0x009A2BE0[i*3..] = a, b, c (4 entries, defaults 0x004C4CD0 {25,38,51}… restored by 4D; meaning unknown) | 0 |
| 0F | 0x00408C80 | L | set_list_8e40 | values → dwords 0x009C8E40.. | 27 |
| 10 | 0x0045F130 | L | set_ptr_1a14 | 0x009A1A14 = &list | 116 |
| 11 | 0x0045F160 | L | set_ptr_34d8 | 0x009A34D8 = &list | 0 |
| 12 | 0x00408CC0 | L | set_list_8e40_r2 | as 0F, then 0x009C8E40 += 1 if 0x009C8E80 == 2 | 3 |
| 13 | 0x0045F190 | L | set_params_13 | [0] → 0x009A5FE0; *[1] → 0x009A1A0C; *[2] → 0x009A21BC; *[3] → 0x009A21B4; *[4] → 0x009A2444 | 8 |
| 14 / 15 | 0x00480CE0 / 0x00480D00 | 8 | set_2bb4 / set_8d4c | (s8) arg → 0x009A2BB4 / 0x009C8D4C | 52 / 52 |
| 16 | 0x00480D20 | 16 | set_71d4 | 3 dwords via pointers → 0x009C71D4/D8/DC | 39 |
| 17 | 0x0045F2A0 | 16 | light_angle_task | 0x0040E370(a, b, c): task 0x0040E3B0 with the current env +0x18/+0x1C angles and target (a, b), c (**HYPOTHESIS: light-angle interpolation over c frames**) | 10 |
| 18 / 19 | 0x0045F240 / 0x0045F270 | 12 | light_angles(_b1) | `Env_SetLightAngles`(block 0 0x009A3540 / block 1 0x009A59E0, pitch, yaw) | 181 / 95 |
| 1A | 0x0045F2D0 | 8 | set_8e58 | *ptr → 0x009C8E58 (player eye height when 0x009C70F4 == 1, 0x0040C470) | 212 |
| 1B 1C 1D | 0x0045F300/320/340 | 8 | set_* | arg → 0x009A2C34 / 0x009CA090 / 0x009C8E50 | 167/189/146 |
| 1E / 1F | 0x0045F360 / 0x0045F380 | 8 | set_8a78 / set_a0f4 | (s16) → 0x009C8A78; (u8) → 0x009CA0F4 | 124 / 291 |
| 20 / 24 | 0x0040B3E0 / 0x0040B630 | 12/20/8 | env_set(_b1) | `Env_SetChannel` 0x0040B3F0(block 0 / 1): immediate (formats.md "Environment") | 1188 / 120 |
| 21 / 25 | 0x0040B640 / 0x0040BA80 | 16/8 | env_interp(_b1) | 0x0040B650: interpolation record (0x009C89E0 / 0x009C8920, 16 B per channel {active, current, target, step}): target from the arg, step given | 0 / 0 |
| 22 / 26 | 0x0040C2A0 / 0x0040C2B0 | 8 | env_stop(_b1) | 0x0040C1F0: clear channel record(s) | 0 / 0 |
| 23 / 27 | 0x0040C1D0 / 0x0040C1E0 | 16/8 | env_interp_frames(_b1) | 0x0040BA90: target, step = (target − current) / frames, negated if negative (×−1 from 0x004C4C64); sub 2–5 targets are ints, 0/1/6–10 pointers | 634 / 48 |
| 28 | 0x00401670 | 8 | section_preload | `Stage_LoadSection`(s, cur) (or (cur, prev) if s == cur) | 287 |
| 29 | 0x00401630 | 8 | section_switch | prev 0x009C7004 = cur 0x009A2224; cur = s; `Stage_UnloadSection`(prev, cur) | 314 |
| 2B | 0x0045FE40 | 4 | player_bonus_2b? | per player in state 5 with +0x? > 0x13: 0x004156C0(i, 0x00567990[...]) | 6 |
| 2C | 0x0045FD90 | 8 | skippable | arg ≠ 0: 0x009A2D7C = 1, 0x009A2230 = 0; arg 0: 0x009A2D7C = 0, skipping 0x009A2D74 = 0 | 116 |
| 2D | 0x0045FDD0 | 8 | voice | 0x00435B80((s16) arg): voice/line table 0x0058B6B8 / 0x00589DB4 (+0x009C7000 character offset outside modes 10/11) | 234 |
| 2E | 0x0045FE00 | 4 | sound_stop_if_skipping | if skipping: Snd 0x80000002 | 52 |
| 2F | 0x0045FE20 | 8 | set_5c48 | (s16) → 0x009A5C48 | 12 |
| 30 | 0x0045F7F0 | (n/16)·4+8 | cam_queue | §3. If skipping: skip. Else ring 0x009C9060[0x009CA108] = &block, write index +1 & 15, pending 0x009A2C8C += 1; type 0x21 also clears 0x009A34F0 | 1192 |
| 31 | 0x0045F870 | 8 | cam_start | 0x00403BB0(1, sub); mode 0x009C6F08 = 1, sub 0x009C6F10 = arg; state 0x009C6F20 = 0; 0x009C6FD8 = 0; 0x009C6F33 = 0; clear bit 0 of 0x009A5EBC/0x009A5D8C; ctx0 command = nop; command state = 1; pending −= 1 | 262 (all sub 3) |
| 32 | 0x0045F900 | 8 | cam_start_ready | yields until both players have a non-zero 0x00579CE0[state·5] or lives > 0, then as 31 (without the 6FD8/6F33 resets) | 7 |
| 33 | 0x0045F9F0 | 12 | cam_set_state | command state 0x009A1A10 = a; pending += b | 125 |
| 35 | 0x0045FA20 | 8 | cam_roll | 0x009A21B0 = arg (roll channel enable in `Cam_Evaluate`) | 8 |
| 36 | 0x0045FA40 | 8 | set_70f4 | 0x009C70F4 (1 = player eye height from 0x009C8E58) | 0 |
| 37 | 0x0045FA60 | 8 | rail_force | 0x009CA098 (1 = rail always advances) | 4 |
| 38 | 0x0045F700 | 8 | sound | `Snd_PlayGameSound` 0x0041CFD0(id) | 237 |
| 39 | 0x0045F720 | 16 | sound_repeat | 0x00435EE0(id, count, interval): plays now and count−1 more times every interval frames (task 0x00435E90; stops when skipping) | 14 |
| 3A / 3B | 0x0045F750 / 0x0045F780 | 8 / 16 | *_noskip | as 38 / 39 unless skipping | 13 / 0 |
| 3D 3E 3F 5B 5C | 0x0045FEC0 / EB0 / ED0 | 16 / 8 / 4 | nop | skip | 66/13/13/0/0 |
| 40 | 0x0045FA80 | 4 | wait_queue | not skipping: pending ≠ 0 → yield; pending == 0 && !running → return (no advance; spins if reached mid-frame); else yield = 0, advance. **No forced frame end** | 586 |
| 41 | 0x0045FAC0 | 8 | wait_camera | wait idiom; then (running) arg == 0: ticks remaining 0x009C6F28 < 1; arg ≠ 0: arg < ctx0 tick 0x009A6110 | 735 |
| 42 | 0x0045FB30 | 8 | wait_frames | first call: counter 0x009C8904 = n, yield. Later: `if ((c < 0 \|\| --c < 0) && running)` continue (c = −1). Passes on the (n+1)-th frame after the first call | 271 |
| 43 | 0x0045FBC0 | 8 | wait_7006 | wait idiom; 0x009C7006 ≤ arg && running && camera settled 0x009C6F2D | 16 |
| 44 | 0x0045FC10 | 8 | wait_enemies | wait idiom; live enemies 0x009C904A ≤ arg && running && settled && 0x007DCCA8 > 0 → 0x007DCCA8 = 0, continue; else 0x007DCCA8 += 1 (needs ≥ 1 failed check) | 221 |
| 45 | 0x0045FC80 | 8 | wait_flag | wait idiom; flag byte 0x009C7200[arg] && running | 38 |
| 46 | 0x0045FCD0 | 8 | wait_a0e8 | wait idiom; 0x009CA0E8 ≤ arg && running && settled | 34 |
| 47 | 0x0045FD20 | 4 | wait_cam_ready | wait idiom; running && (0x009C6F2F \|\| 0x009C6F2D) && 0x009CA93C == 0 | 9 |
| 48 | 0x0045FD70 | 8 | set_flag | 0x009C7200[arg] = 1 (256 bytes, cleared by 0x0045EDD0) | 269 |
| 49 / 4A / 4B | 0x0045F3E0 / 0x0045F470 / 0x00408AE0 | to −2 | pp_* | skip 0x009C9118 lists, run one list (Res_RequestPolFile / Res_RequestUnloadPolFile / Actor_SpawnRecord0B), skip to −2 (**HYPOTHESIS: 0x009C9118 = player count/character index**) | 0 |
| 4D | 0x0045EEC0 | 4 | stage_reset | §3.6; also 0x009A5C50, 0x009A5C48, 0x009C70F4, 0x009C8A74 = 0, 0x009A2BE0 table defaults, 0x004157D0 / 0x009C71C4 (once per demo), appends the scene to the visited log 0x009A5920[mode·16 + 0x009A5C30++] (0xFF-terminated) | 118 |
| 4E | 0x0045EFF0 | – | halt | yield = 1, never advances | 1 |
| 4F | 0x0045F000 | jump | next_variant | variant += 1; if comevtbl[m][s][v] == −1: next scene from 0x00597890[mode][scene] {s16 kind, a, b, c}: kind 0 → a, kind 1 → {a,b,c}[route branch 0x009C88A4], kind 2 → scene+1; variant = 1. If comevtbl[m][scene] == −1 (end of the mode): `Stage_End` 0x0045ED90 (0x009C71C0 = 1, …), scene reset, script = comevtbl[7][0][0]. Else script = comevtbl[m][s][v], 0x009C88A4 = 0 | 508 |
| 50–56 | 0x0045F3A0 … 0x0045F640 | 8 | resources | request ring 0x007DA220 (formats.md). 52/53 remap pol 0xBE/0xBF to character files when game type 1 (0x009A2242 / 0x009A2256) | 777/356/1279/712/208/169/78 |
| 57 | 0x0045F660 | 8 | request_type9? | 0x0041D790: request type 9 | 157 |
| 58 | 0x0045F680 | 4 | flush_all | `Res_FlushRequests` 0x0041D970 | 53 |
| 59 | 0x0045F690 | 4 | flush_lt8 | 0x0041D9D0: process queued requests of type < 8, keep the others | 185 |
| 5A | 0x0045F6A0 | 4 | flush_8_10 | 0x0041DA70: process types 8–10, keep the others | 0 |
| 5D / 5E | 0x0045F6B0 / 0x0045F6E0 | 12 / 8 | snd_load/free_pack | PC stubs (OutputDebugString "SS_SndLoadPack/FreePack") | 4 / 0 |
| 5F | 0x0045F7C0 | 20 | bgm | 0x0041D450: Snd(0x80000000) (stop), Snd(arg c) | 29 |

## 3. Camera command system

### 3.1 Tasks and order (CONFIRMED by the K task list in the stage-1 demo)

`0x0045ECC0` Evt_Run → `0x0040AD90` env interpolator → **`0x004022B0` camera task** → `0x004132D0` →
**`0x00414F10` rail step** (`jmp 0x0040C370` → `jmp [0x009C7080]`) → … → `0x00426960` demo timer → actors.
So in one frame: script ops, then the camera, then the rail, then enemies (enemy changes become visible to the
script and camera next frame).

Camera task 0x004022B0: 0x009C6F2F = 0; mode function 0x00576B10[mode 0x009C6F08] (1 → 0x004023D0,
2 → 0x004023E0, 0/3 → nop); context functions 1–3 (always nop in the scripts seen); `Cam_BuildViews`, which
ends by **committing** the requested mode/sub (0x009C6F0C / 0x009C6F14 → 0x009C6F08 / 0x009C6F10).

- 0x00403BB0(mode, sub) sets the request and calls the sub-mode setup 0x00576C14[mode·9 + sub];
  0x00403BD0 also sets the current mode/sub. Setups: (0,0) 0x00403970; (1,1) 0x00403980; (1,2) 0x004039A0;
  (1,3) 0x004039D0 **active context 0x009C6F00 = 2**, rail 0x0040C380; (2,4) 0x00403A00 rail 0x0040C430;
  (2,5) 0x00403A30 rail 0x0040C4C0; (2,6) 0x00403A60 rail 0x0040C770; (2,7) 0x00403A90 rail 0x0040C8A0
  (they also set player draw/aim function pointers 0x009A5CDC.. / 0x009A5E0C..). Others → 0x00402710 (`jmp $`).
- Mode 1 (event camera): 0x004023D0 = queue processor, then if sub (before the commit) == 3 and 0x009C6F1C == 0,
  context 2 = copy of context 0 pos/target + look angles (F lines show context 2 active).
- Mode 2 (shooting): 0x004023E0 = queue processor only; active context 0.

### 3.2 Queue processor 0x00402320 (CONFIRMED, disassembly)

```
loop:
  ctx0.fn(0)                                     // [0x009A610C]
  if read 0x009A34D0 == write 0x009CA108:
      if state 0x009A1A10 in {1,2}: ctx0.fn = nop
      return
  next = false
  if state in {1,2}:
      block = ring[read++ & 15]
      ctx0.fn = 0x005776EC[type >> 4][type & 15]  // type = block word 1 (= the 0x30 length argument)
      ctx0+0x184.. = 8 words from block+8         // always 8, even past a short block
      next = (state == 1)
  state = 0
  if next: goto loop                             // a finished command starts the next block this frame
  return                                         // state 2 (idle) starts it but runs it next frame
```

Command state: 0 running, 1 finished (start next now), 2 idle (start next frame; set by init). Commands finish
with `state = 1, pending −= 1` (ctx 0). **Pending 0x009A2C8C** counts queued + running commands; op 0x40 waits
for 0. Type 0x21 never finishes by itself: op 0x31 / 0x32 do the `state = 1, pending −= 1`.

### 3.3 Block types (0x005776EC; sub-tables overlap, so some types alias)

| Type | Fn | Args | Effect |
|---|---|---|---|
| 0x10 | 0x00403830 | flag | bit 0 of 0x009A5EBC and 0x009A5D8C = flag; done |
| 0x11 | 0x004036B0 | sub | 0x00403BD0(requested mode, sub); done |
| 0x12 | 0x00403780 | i | player functions 0x009A5CE0 / 0x009A5E10 = 0x00579E90[i]; done |
| 0x13 | 0x00403250 | n | wait n frames (0x00403290 counts down, done at 0); n = 0 → 0x00420810 (empty: never done) |
| 0x14 | 0x004037E0 | ctx | active context 0x009C6F00 = ctx; done |
| 0x15 | 0x00403930 | – | 0x009C6F33 = 1 (gameplay camera position smoothing); done |
| 0x20 (0x16) | 0x004032E0 | n, pose | context pos/target = pose 0x00576CF0[pose] (24 B); 0x009C6F2D = 1; done after n frames (n = 0: never) |
| 0x21 (0x17) | 0x00403710 | sub, – | **enter shooting mode**: 0x009A2BA0/A4 = 0, state 0x009C6F20 = 3, 0x00403BB0(2, sub), 0x009A5900 \|= 1, fn = 0x00576B20[sub] (4 → 0x00402460, 6 → 0x00402580, 7 → 0x00402DB0, 5 → null), state 0 (never done; ended by op 0x31). Scripts use sub 4 (178×), 7 (132×), 6 (84×) |
| 0x40 (0x18, 0x22) | 0x00403360 | start, end, cam, flags | **camera segment**: start == end → 0x004033B0 (one pose at start; done). flags & 2 → 0x00403490 **deferred**: 0x009A2D78 = cam, rail tick 0x009C70AC = start (−1 → ctx tick + 1), rail end 0x009C70B0 = end, 0x009C70B8 = flags; done. Else 0x00403510: camera 0x009A2D78 = cam (not with flags & 4), tick = start (−1: continue from 0x009A6110; flags & 4: use the deferred start/end), fn = `Cam_PlayStep` and call it. Flags seen: 0 (564×), 2 (196×) |
| 0x60 (0x19, 0x23, 0x41) | 0x004038A0 | t0, c0, t1, c1, t2, c2 | look table: 0x009C6FD8 = 1, entries (tick, camera) indexed by route branch 0x009C88A4, used by 0x00403DB0; done |

**Cam_PlayStep 0x004035E0**: `tick 0x009A6110 = ctx+0x144; Cam_Evaluate(0x009A2D78, tick) → ctx pos/target/roll;
look angles; ticks remaining 0x009C6F28 = end − tick; if end ≤ tick: ctx+0x144 += 1, done; else ctx+0x144 += 1`.
One tick per frame, evaluated at start..end inclusive (end − start + 1 frames); the first tick is evaluated in the
frame the block starts. **CONFIRMED** (segments 0..90, 91..300 frame by frame).

### 3.4 Shooting (gameplay) camera, mode 2

Start functions (called once, then they install the step): 0x00402460 (sub 4; also places the players at the
camera), 0x00402580 (sub 6), 0x00402DB0 (sub 7, installs 0x00402E00). All first run **0x004031E0**:
`0x009C70BC = (float) tick 0x009A6110; active context = 0; 0x009C6F2D/2C/30/31/32/2E = 0;
ticks remaining = 0x7FFFFFFF; 0x009C6F36 = 0x200` and set the rail look point 0x009C70D8 = camera transform of
(0, 0, −30).

**Step 0x00402650** (each frame, from the queue processor):
```
targets = any byte 0x009A5EC0 + 8k (k < 4)          // lock-on slots; 0x00403050 aims at their +0x100 positions
if style 0x009C6F2E in 0..2:
    n = style == 2 ? 0x009C7006 : 0x009C904A
    state 0x009C6F20 = (n == 0 && !targets) ? 2 : 3
if 0x009CA094 == 1: state = 6
if state != 2: 0x009C6F2D = 0x009C6F2C = 0
if 0x009C6F30 || 0x009C6F32: state = 7
0x00576CBC[state](0)            // 2 → 0x00402720, 3 → 0x00402890, 6 → 0x00420810 (empty), 7 → 0x00402A40, others jmp $
tick 0x009A6110 = (int) 0x009C70BC
```
- **State 3 (enemies or targets): 0x00402890** — position = rail position 0x009C70C0 (1/16 smoothing toward it
  if 0x009C6F33), look-at 0x009C6FA8 from the targets (0x00403050), view rotated toward it by 0x00403C00 with
  the step 0x009C6F36. No `Cam_Evaluate` (unless the look table is on and ticks remaining < 0). The camera
  "holds" while enemies are alive.
- **State 2 (clear): 0x00402720 → 0x00576CE0[style]**: style 0/2 → 0x00402740, 1 → 0x00402860.
  0x00402740: `b = (2D ? 2 : 0) | (2C ? 1 : 0)`. b ≥ 2 (settled): `Cam_Evaluate(cam, tick)` straight into the
  context (one C line per frame). b = 0: 2C = 1, then as b = 1: (0x00403DB0 if the look table is on)
  0x00402E60 (`Cam_Evaluate(cam, tick)`, position = (p + 15·pos)/16 via 0x00401E60), 0x00402F80
  (`Cam_Evaluate(cam, tick)` → look-at, view rotated by 0x00403C00), then the alignment test 0x00401DF0 =
  signed cos² between (look-at − pos) and (target − pos) > 0.99999 (0x004C439C) → 2C = 0, **2D (settled) = 1**,
  2F = 1. So "return to rail" = two C lines per frame until aligned.
- **0x009C6F2D (settled)** gates ops 0x43, 0x44, 0x46 (and 0x47 with 0x009C6F2F): the script continues only
  after the camera has turned back to the rail. 0x007DCCA8 (op 0x44) adds one more frame.
- State 7 (0x00402A40): scripted move toward 0x009C6FC0 / 0x009C70C0 (no `Cam_Evaluate`). State 6: empty.

**Rail step 0x0040C370 → [0x009C7080]** (after the camera task):
- sub 4 (0x0040C430 → 0x0040C470): rail position = context 0 position (stand still); ticks remaining = −1.
- sub 6 (0x0040C770 → 0x0040C790) / sub 7 (0x0040C8A0 → 0x0040C8B0): if rail tick < end (sub 7: ≤ end):
  rail tick += 1 when `0x009CA098 == 1 || 0x00413280() || (0x009C8E8C == 0 && 0x009C8E80 != 0)`;
  0x009C70BC = (float) rail tick; `Cam_Evaluate(0x009A2D78, rail tick)` → rail pose 0x009C70C0 / 0x009C70D8;
  player position from it. Always: ticks remaining = end − rail tick.
- sub 5 (0x0040C4C0 → 0x0040C5C0): player faces 0x009C7084; button 0x9C9028 & 0x20000 starts a 30-frame
  move (not decoded). Mode 1 sub 3 (0x0040C380): player position from the camera matrix with a (0, −15, 0) offset (not checked). Mode 1 sub 1
  (0x0040C9C0): no camera effect.

So in sub 6 the camera moves along the deferred segment while the player can shoot: the rail evaluates tick k in
frame f, the camera evaluates `0x009A6110` = the rail tick of the previous frame (C lines `(42,303),(42,305)` …).
**0x009C6F28** (ticks remaining): `Cam_PlayStep` (end − tick), rail sub 6/7 (end − rail tick), rail sub 4 (−1),
0x004031E0 and op 0x4D (0x7FFFFFFF). **0x009A6110** (tick): `Cam_PlayStep`, 0x004033B0, 0x00403510,
0x00402650 / 0x00402E00 ((int) 0x009C70BC), 0x00403DB0, op 0x4D (0).

### 3.5 Transitions / "blend"
There is no separate blend between cameras. What looked like transition frames with 2–3 evaluations
(formats.md: camera 42, frames ~3242–3254) are the shooting camera's return-to-rail evaluations plus the rail
step's evaluation, and the rendered view is the smoothed (1/16 per frame) position plus the 0x00403C00 rotation,
which matches neither evaluation. Porting 0x00403C00 / 0x00403050 is what's needed to render those frames.

### 3.6 Op 0x4D (stage reset, camera part)
tick 0x009A6110 = 0; 0x00403BB0(1, 3) (active context 2); clear player bit 0; ticks remaining = 0x7FFFFFFF;
0x009C6F38 = 1; 0x009C6FD8 = 0; 0x009C6F3C = 1; 0x009C6F33 = 0; 0x009CA094 = 0; roll enable 0x009A21B0 = 0.

## 4. Game flow

- `Game_Frame` 0x0049E220 → `Game_Update` 0x0040E730: frame counters (0x009A32A0, 0x009A5C50, 0x009A2BAC);
  per player start-button joins; 0x004184C0; `Game_UpdateInput`; 0x0040E500 (pause toggle: 0x009A1A00, PAUSE
  sprite 0x8E7/0x8E8) unless 0x009C90A0 is set (then 0x0040E980 clears the pause state); if 0x009A1A00 == 0 and
  0x009C90A0 == 0: **`Game_UpdateMode` 0x004608A0**; 0x00418550;
  0x0040D5B0 (fog colour); 0x004043A0; **`Mode_Commit` 0x0040E860**; 0x004043C0 (start button → title).
- **Game mode 0x009C8E98**, sub-state 0x009C90A4 (0 = init, 1 = running), frames in mode 0x009A5C44.
  `Game_UpdateMode` (first: `Actor_InitTypeTable` when sub-state 0) dispatches: 3 0x00495EB0 title; 4 0x00496200
  (after start on the title; **HYPOTHESIS** menu/mode select); 5 0x00426800 attract demo; 6 0x0045FEE0 game
  (sub-state table 0x005679A8); 7 0x00460960; 8 0x0041A5E0; 9 0x004996C0 (event mode 8, no stage table;
  **HYPOTHESIS** instructions); 10 0x0041F9B0 / 11 0x0041FB00 (event modes 10/11 = advevtbl / adv2evtbl,
  **HYPOTHESIS** story/advertise sequences); 12 0x004869E0; 15 0x0049F380; 16 0x0041E1F0. Then 0x004067D0
  (PRESS START).
- Mode changes: `Mode_Request` 0x0040E850 (0x007C17A0 = mode), applied by `Mode_Commit` (sub-state 0, many
  resets; players' state = 9 unless mode 6/7). `Mode_Next` 0x0040E960: next = 0x005798F0[0x009C911E][mode]
  (all three rows 0x004E1014): **0→10, 3→9, 9→5, 5→11, 11→10, 10→3, 6→3, 7→3**. So the attract loop is
  title (3) → 9 → demo (5) → 11 → 10 → title. Start in modes 5/9/10/11 (once 0x009A5C44 > 2) → title; start on the
  title → 4; a player join (0x00414FC0) → 6. **CONFIRMED** by code; the capture shows title → (mode 9,
  camera 30) → demo at 2788, consistent.
- **Attract demo (mode 5, 0x00426800)**: demo index 0x009A3400 (wraps at a −1 entry). Table 0x00589828
  (20 B): {s16 event mode, s16 scene, s16 frames, s16 key offset, s16 bgm, s16 –, s32 bgm id, char* key file}:
  0 = (0, 4, 1893, key_122.bin), 1 = (1, 28, 1744, key_2132.bin), 2 = (3, 5, 1744, key_405.bin).
  Init: 0x009CA08C = 0, 0x009A1A08 = event mode, `Evt_LoadTable`, 0x009A2BC0 = scene, 0x007DC90C = 1,
  0x007DC910 = frames, input playback from `etc\<key file>` (0x00426980, PRS), `Stage_Start` 0x00460030(…, 1).
  Each frame: run the stage tasks (0x004606D0); the demo timer task 0x00426960 decrements 0x007DC910 and
  clears 0x007DC90C below 4; then demo index += 1 and `Mode_Next`.
- **Stage_Start 0x00460030**: resets, preloads, requests, then creates the stage task 0x00460710 which creates (in
  this order) Evt_Init, Env_InitInterp, the camera task 0x00402300, 0x004134D0, 0x00414F20 (rail/player),
  0x00414ED0, 0x00414FB0, 0x00413950, 0x004809D0, 0x00401470, 0x004138A0, 0x00408D90, 0x00409080, 0x00404480,
  demo timer (mode 5); then 0x0040C340 (rail reset) and `Stage_ResetState` 0x0045EDD0 (counters 0x009C904A /
  0x009C7006 / 0x009CA0E8 = 0, flags 0x009C7200 cleared, light angles 0, 0x009CA098 = 0 …).
- Script selection for the demo: mode 0, scene 4, variant 0 → 0x0097A208; 0x4F → variant 1 (0x0097A360),
  2 (0x0097A4AC), 3 (0x0097A608) …
- **0x007DCCA4** = "game running" (§1). **0x009C6F2D** = shooting camera settled on the rail.
  **0x007DCCA8** = frames op 0x44 has failed. **0x009A2D74** = cutscene skip in progress.

## 5. Native runner spec (implemented)

`EventRunner::Start(mode, scene, cfg)` = camera init 0x00402260 + Evt_Init. `EventRunner::Frame()` =
`RunEvents()` (Evt_Run, exact yield semantics, every handler call appended to `trace`), `camera.Frame()`
(camera task: queue processor, gameplay camera step, mode commit), `camera.RailStep()`. All handler effects
owned by other subsystems go through `EventHost` (spawn records, sounds, voices, BGM, resources, sections,
non-immediate env ops) and all gameplay inputs come from it (`GameRunning`, `LiveEnemies`, `Count7006`,
`CountA0E8`, `TargetLocked`, `CameraAligned`, `RailAdvance`, `PlayersReady`, `EvaluateCamera`). Env ops 0x18 /
0x20 go to `game::Environment`. Plain global stores land in `EventRunner::globals` (keyed by address).
Endless-loop cases of the original (invalid opcodes, op 0x40 with nobody playing mid-frame, null camera
handlers) stop the runner with an error instead.

**Stage 3 update:** the items below are now ported and the oracle is no longer needed — see
`notes/agents/shoot_camera.md` (shooting camera, 0x00403C00, Cam_BuildViews, lock-on table; bit-exact views
812/812 frames on `build\scene_capture_stage3.txt`). `CameraAligned` / `TargetLocked` were replaced by the computed
test and `EventHost::LockOnSlots`.

Not ported (Stage 2; positions only, not control flow): 0x00403C00 view rotation, 0x00403050 target look-at,
0x00402A40 scripted move, the rail look point (0, 0, −30), player placement. The alignment test is therefore a
host query.

## 6. Verification

`event_runner_test <game> <capture>` replays the stage-1 demo from frame 2788 with this oracle (from the
capture): the shooting camera sees enemies in frames 2881–2897, 2970–2983, 3303–3599; the alignment test passes
at 2937, 3030, 3242.

| Capture | Ops (E) | Frames with identical op sequence | `Cam_Evaluate` (C) | F: context / camera / tick |
|---|---|---|---|---|
| scene_capture_events.txt (2788–3599) | 917 / 917 | 812 / 812 | 551 / 551 | 812 / 812 each |
| scene_capture_actors / fog / lights / st1demo / enemies_full (2788–3299) | 612 / 612 | 512 / 512 | 548 / 548 | 512 / 512 each |

Control (`--no-oracle`): 150 / 812 frames match, 265 / 551 evaluations. `evt_test` 917 / 917 and 105 / 105
lengths with the full table; `env_test`, `stage_test`, `cam_test` unchanged.

## 7. Open questions / captures wanted

1. Per-frame game mode trace (0x009C8E98, 0x009C90A4, 0x009A1A08, 0x009A2BC0, 0x009A2BB0, 0x009A3400) from
   boot through one full attract loop (≥ 12,000 frames) to confirm the 3 → 9 → 5 → 11 → 10 → 3 cycle and what
   modes 9/10/11 show; with E lines (modes 10/11 should run advevtbl/adv2evtbl scripts).
2. Per-frame 0x009C904A, 0x009C6F20, 0x009C6F2C/2D, 0x009A5EC0..0x009A5EDF and ctx0 target (0x009A60D8) in the
   stage-1 demo, to turn the oracle into checked values and to port 0x00403C00 (rendering of the
   return-to-rail frames).
3. A capture of demos 2 and 3 (stage 2 scene 28, stage 4 scene 5) with E/C/F lines for a second and third
   replay (route ops 0x01–0x08, type 0x60 look tables, 0x4F scene transitions).
4. `etc\key_*.bin` (demo input) is read from `etc\`, which is missing in `game/` — where does the running game
   find it (disc)?
5. Meaning of 0x009C9118, 0x009C8E84 (route; set by gameplay when a path is chosen), 0x009A2BE0 table, op 0x2B.
