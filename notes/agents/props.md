# Props / non-enemy actors and the task system (stage-2 agent report)

Sources: Ghidra decompiles + disassembly, `build\scene_capture_actors.txt` (stage-1 demo, frames 2780-3299, K/T/M/E/Z/N lines).
CONFIRMED = read in code and/or matched against the capture; HYPOTHESIS = inferred, not checked.

## 1. Task system

### Block and header (CONFIRMED)
`Task_Create` 0x004A6FA0(fn, size) allocates `Mem_Alloc(size)` (0x004A7400: first-fit heap, block = align16(size)+0x20,
size word at block-0x10, negative while in use: the T dumps show -928 = -(align16(0x378)+0x20)). It zeroes the 0x34-byte header;
callers then run `Task_Clear` 0x004A73D0 (zeroes +0x34..end).

| word | off | meaning |
|---|---|---|
| 0 | +0x00 | function, called once per frame **from the start** (no stack is kept; a task may overwrite it to change state, e.g. 0x0048A3E0 sets 0x0048A920; 3 such switches in the capture) |
| 2 | +0x08 | serial = counter 0x00598AA8++ (initial value 0x10000, never reset); 0 once dead |
| 3 | +0x0C | alive (1), 0 once dead |
| 4 | +0x10 | user pointer (0x004A7260(task, v), used by 5 HUD-ish callers) |
| 5 | +0x14 | dead-list link (head 0x007DF8AC) |
| 6/7 | +0x18/+0x1C | prev / next sibling |
| 8 | +0x20 | parent |
| 9 | +0x24 | "post" list: run after the task by the scheduler, freed with the task by 0x004A70D0. Never non-zero in any T dump (no writer found) |
| 10/11 | +0x28/+0x2C | first / last child (run before the task) |
| 12 | +0x30/+0x32 | u16 / u16; low byte of +0x32 == 0xFF: skip the next call once (scheduler clears the byte) |
| 13 | +0x34 | actor flags (spawned actors: record flags \| 1; props 0x51 / 0x80000001; bit 3 (8) = hit this frame, bits 1/2 = which gun, 0x40000000 = swinging, 0x8000 = no hit target, 0x10 = hit target uses its stored matrix) |

Root = static block 0x007DF878 (13 words), current task 0x007DF8B0, jmp_buf 0x007DF838.

### Scheduler (CONFIRMED, disassembly of 0x004A71A0)
```
Task_Run(t):                       // 0x004A71A0, recursive; per frame Task_RunFrom 0x004A7240(root)
  for c = t.first; c; c = c.next: current = c; Task_Run(c)      // next is read AFTER the call
  current = t
  if setjmp(0x007DF838) == 0 and t.fn:
     if low byte of t+0x32 == 0xFF: clear it   else t.fn(t)
  current = t; for c = t.post; c; c = c.next: current = c; Task_Run(c)
  current = NULL
```
- `Task_Create` makes the new task a child of **current's parent** (root if current is root/NULL) and appends it at the tail.
  Consequence (CONFIRMED in all 518 frame transitions of the capture, every K parent = 0): the task tree is a **flat list under the
  root**; a task created during a frame runs later in the **same** frame; execution order = creation order.
- `Task_Kill` 0x004A7040 (also inlined in 0x00472B10 / 0x00461CD0 / 0x00432D40 ...): unlinks current (prev/next/parent first/last),
  current = parent, pushes it on the dead list (serial = alive = 0) and `longjmp`s to 0x007DF838 (the setjmp of this task's
  Task_Run), so the rest of the task function is skipped. `next` is left intact so the sibling loop continues.
- `Task_FreeDead` 0x004A70D0, called from 0x004ABED0 after the frame is drawn: Mem_Free(post) and Mem_Free(task) for every dead task.
- `Task_InitRoot` 0x004A6F50(init): current = root, dead list = 0, unlink root's post list (0x004A7120), root fn = 0, alive = 1,
  serial = 0, calls init(), returns current. Every caller runs `Mem_Init` 0x004A7310 first (fresh 16 MB heap, root header wiped),
  except state 2 of 0x00460960 (re-init without Mem_Init: old root children survive; HYPOTHESIS that this is intended).
- Capture check: each frame's K list = previous survivors in order + newborn in serial order (tests/task_test.cpp, 518/518).

### Native-port spec (implemented: `src/game/task.{h,cpp}`)
`TaskSystem` mirrors the above exactly: Create (sibling-of-current, tail append, serial++), KillCurrent (unlink, dead list, throws
`TaskEnded` caught in Run = the longjmp), Run (children, fn with 0xFF skip, post list), FreeDead at frame end, Reset.
Deterministic rand order follows from: task order = creation order, event task (created first) runs first each frame, newly
spawned actors run in their spawn frame after all older tasks, then the tasks they create, in that order.

## 2. Demo actors (stage-1 attract)

Frame 2788: event ops 0x09 (rec 0x0097A174), 0x0B (0x0097A114, 0x0097A144), 0x09 (0x0097AE54), 0x0B (0x0097AB58, 0x0097ABA0)
create spawn actors 65591-65596; each runs in the same frame, creates one task (65597-65602) and kills itself (CONFIRMED: serials
and fns reproduced by props_test).

| serial | spawn | task | verified |
|---|---|---|---|
| 65597 | type 0x41 kind 45 (op 0x09, state 7) -> generic komono ctor 0x00461CF0 | 0x0046DAB0 decor | 4096 draws, max err 2.3e-4; phase |
| 65598/65599 | type 0x44 state 15 (0x00473770) | 0x00465FB0 breakable, sub-kind 0 (pot, skeleton 5, motion 475) | see below |
| 65600 | type 0x41 kind 4 (0x00462E10), sub-kind 1 (skeleton 17, motion 466) | 0x00465FB0 | |
| 65601/65602 | type 0x44 state 1 (0x00472BD0) | 0x00473CF0 hanging prop (6095/6096) | 1024 draws, max err 1.5e-4; rot/frame every frame |
| 65610 | type 0x44 state 3 (0x00472E00), frame 3031 | 0x00474120 animated prop (6126) | 268 draws, max err 1.5e-4 |
| 65611 | 0x004666B0 when 65599 is shot (3172) | 0x004667A0 hit spark (sprite 0xE25, 14 frames) | not ported |
| 65612 | type 0x10 (op 0x0C, 3232) | itself, fn -> 0x0048A920 | survey only (see 3) |

All draws: world W premultiplied onto the view V (M line = W * V; V = the stage task 0x00401260's draw, identity world).
props_test: 3284/3284 task identities (serial+fn) and states, 26597 draws ok (max |W*V - M| 2.4e-4), 0 bad, 32 skipped (below).

### Hanging prop 0x00473CF0 (task 0x378) - CONFIRMED
Ctor state 1 (0x00472BD0), params = record+0x24: u16[0] kind -> +0x290, u16[2] model -> +0x28C, s32 @8 hit id -> +0x14C (-1 = not
shootable), s32 @0x10 amplitude -> +0x1DC, s32 @0x14 phase -> +0x1E8, s8 @0x20 trigger flag -> +0x2A0, s8 @0x21 kill flag -> +0x2A4;
pos -> +0x19C, rot[1] -> +0x1D0, scale (1,1,1) +0x1A8, +0x2C0 = 1.0, flags |= 0x51.
Per frame:
1. kill flag set (0x009C7200[k] == 1): hit id -1 -> Task_Kill, else Actor_Remove 0x00409CC0. Stage 1 + flag 0x77 -> remove.
2. Model 0xA60 / 0x1866 special cases (flags 0x36/0x23/2/4, sounds 0x1F16A9/0x2116A9/0x361BA9, 0x00420810) - ported, untested.
3. Trigger flag set and frame (+0x2A8) < 60 (130 for kind 4): kinds 0/2/3 read s16 triples 0x005960B4[kind][frame]
   (a, b, c), others u16 0x005960C8[kind][frame] (b only). rz = base_z + c; amp > 0: rx = base_x + a, ry = ftol(b * +0x2C0),
   else rx = base_x - a, ry = -ftol(...); frame++. Demo: flag 26 set by op 0x48 at 3232, 60 frames of kind 3 (0x00595E48).
4. Else, when not swinging and hit bit 8 set: start swing (phase 0, base = ry). Swing: phase += 0x1000;
   ry = base - ftol(sin(phase) * (model 0x1A46 ? 1024 : -1024)) * amp; ends (ry = base) when phase > 0xFFFF (16 frames).
5. Matrix: push; T(pos); RotY(+0x1D0); RotZ(+0x6C); RotY(+0x68); RotX(+0x64); Scale (+0x1A8.., or (1.05,1,1) for 0x1816/0x1817)
   -> **W = S Rx Ry Rz Ry(base) T**; draw via 0x00418560 (0x004185E0 when 0x009A2BB4 && mode != 1 && path != 0x46); store
   matrix to +0x150; Hit_Register 0x00405160 if hit id != -1.

### Breakable prop 0x00465FB0 (pots / crates; task 0x378) - CONFIRMED (draws), shot handling partly
Ctors: type-0x44 state 15 0x00473770 (skipped on some stage/sub combos; params: u16[0] lifetime, s8 @4 item slot, s32 @8 -> +0x2A0)
and komono kind 4 0x00462E10 (lifetime = record state, item slot = record byte 0x24). Both: sub-kind = record rot[2], rotY =
rot[1], row 0x00593DB8[sub] (12 B: skeleton, motion, sound, hit radius, hit height; 11 rows), fixed static models for sub-kinds
2/3/8/9 (0x17A9/0x19E8/0x17AA/0x17AB), item counter 0x009C7010[slot] = **rand() % rot[0] + 1** when slot > 0 and rot[0] > 1
(the 2 rand calls at 0x00473912 in frame 2788).
Per frame: lifetime (area 0x009A2BB0 changes > +0x11C -> remove), camera-path removals (path 0x29/time 0x14A in stage 0,
path 0x2F/time 0x96), shot (bit 8 while frame 0): score (0x004650F0, rand() only when both gun bits set), break sound, frame = 1,
spark task, item drop when the slot counter reaches 0; flags &= ~0xE; shake (when +0x2C0 > 0.01: two rand() % 151 offsets,
+0x2C0 *= 0.85); frame++ while > 0, at ticks-2: remove if slot not 0/4, else hold.
Draw (when motion resident, 0x009A37E4[motion*4] == 2): T(pos + shake) RotY; intact static model: +T(0,0.8,0); otherwise the prop
skeleton (0x0040DD90; alt select 0x0040DFA0 for slot 4 / stage 3). Shadow for sub-kinds 0,3,6,7,10 (model 0x10D0, S(10,1,10)) /
4,5 (0x10D1, S(8,1,8)) at (x, ground 0x009C8E58 + 0.2, z). While intact: hit point (x, y + height, z) -> view space +0x70, register.
Verified: 21209 draws incl. 65599's break animation (3172-3242, death at 3243 = ticks 74 - 2 exceeded, slot 2).

### Prop skeletons (0x0040DDC0 / 0x0040DE50 / 0x0040D9D0) - CONFIRMED
Separate from the character skeletons: roots 0x004D5390[t] (29 types), node {u32 model, s16 index, s16 childCount, Node* child[]},
count 0x004D5404[t], interpolation mode 0x004D5440[t]. Motion slot 0x009A37E0[id*8]; frame = (count-1) positions (12 B) then
(count-1) rotations (6 B), stride (18*count - 15) & ~3 (this explains the "unexplained" komono motion layout). Mode 0: one frame
per tick; modes 1/2: frame tick/2, odd ticks lerp position by 0.5 and rotations by half the wrapped delta (next = frame 0 at the
last tick). Node: **M = Rx Ry Rz T(pos) * parent**; root (index 0) neither transformed nor drawn. Model 0x10CE gets a random scale
((rand() % 11) * 0.01 + 0.27) per draw. **Not ported:** mode 2 with all three angle deltas > 0x3000 takes a matrix-slerp path
(0x00412750); 32 of 21241 node draws in the capture (65599's break) use it and are skipped by the test.

### Decor kind 45 0x0046DAB0 - CONFIRMED
Draws 11 table entries (x,z at 0x005949F8, models 0x00594A50, y = 65, RotY 0x145E for the first 5 / 0x3C4D) - the first 5 only once
+0x2A0 is set (camera path 0x2A at time 500) - plus two 0x1735 at fixed positions. Dies after `state` (7) area changes or when
0x009A2BC0 == 0xE. Vertex wave on loaded models 0x1731..0x1734 (k = 0..3), vertices with flag bit 0:
`v[+8] = v[+4] * -0.1 * sin(((5k - trunc(v[+4])) * 0x200 + phase) * 2pi/65536)`, phase (+0x1D0) += 0x200 per model (CONFIRMED
phase in T dumps; the mesh edit itself is not ported).

### Animated prop (type 0x44 state 3) 0x00474120 - CONFIRMED (static part)
Ctor 0x00472E00: skeleton 0xB, motion 0x1D6 (komono_man.bin, 148 ticks), model override 0x17EE (0x197C if stage != 0), trigger/kill
flags params bytes 0x20/0x21, hit id params s32 @8 (here a pointer 0x00991B30), +0x124 = 5.0. Per frame: frame++ while the trigger flag
is set (up to ticks-2), sounds 0x1116A9 at frames 0x41/0x58/100/0x6E; when 0x009A4694 == 2: T(pos), prop skeleton with model
override storing node 1 (+0x338 -> +0x150), then +0x64/+0x68/+0x6C are read from the motion frame at slot (trigger flag - 1)
(looks like a bug: flag index used as node index; values unused by the draw), Hit_Register. Flag 30 never set in the capture, so
only frame 0 is verified.

## 3. Survey

### Actor types (0x00593358, 56 pairs) - HYPOTHESIS unless noted
0x10 civilian/NPC (enemy-style skeleton 0x00411090, scripted sub-records via 0x00408A20, shooting it costs the player 0x004156C0(p,-100); demo 65612) ·
0x11 enemy-skeleton actor (0x0043A990 ground) · 0x12/0x13 small objects via 0x005926A8 · 0x14 enemy-skeleton actor (0x00596408) ·
0x15 spawner (task 0x004418C0) · 0x16/0x17 task-heap utilities (0x007DCC4C) · 0x18 enemy 0x30 variant (calls 0x00452DA0) ·
0x19 enemy-skeleton actor · 0x1A pol-loading effect (rand) · 0x1B record spawner (0x0B/0x0C sub-records) ·
0x20-0x25 enemy kinds (enemy skeleton, state fns) · 0x26 multi-state actor (6 states) · 0x27/0x28 camera-angle actors ·
0x29 simple model + kill on camera time · 0x2A immediate no-op kill · 0x2B/0x2C trivial · 0x2D enemy with sub-task ·
0x30 enemy (demo znebi2, other agent) · 0x31/0x32 enemies · 0x33 multi-state event actor (demo 65603 -> task 0x00433AC0 -> effect 0x407A70) ·
0x40 task spawner · **0x41 komono props (kind ctor table)** · 0x42/0x43 spawners with rand · **0x44 placed props (state table)** ·
0x45 state machine (0x0041FDB0..) · 0x46 large enemy/boss part (rand 7) · 0x47 spawner · 0x48 enemy-skeleton · 0x50 4-state actor ·
0x51-0x56 misc (0x53 enemy-skeleton) · 0x60 model 5936 + env light · 0x61 item/pickup (models 5728/5729, sprite) · 0x62 resource loader ·
0x63-0x66 small HUD/flag actors · 0x6C/0x6D boss loaders (tex/pol/mot requests) · 0x6E boss (models 5215/5216).

### Type 0x44 states (0x00595AB8, 18 entries) - CONFIRMED tasks created, roles HYPOTHESIS
0 -> 0x00473B90 · **1 -> hanging prop** · 2 -> row of hanging props (kind 2, models 0x1794+n, positions along RotY) ·
**3 -> animated prop 0x00474120** · 4 -> hanging prop with scale/alt params layout · 5 -> 0x00474240 (model 6103, creates hanging prop) ·
6 -> 0x00474470 (prop skeleton, hit) · 7 -> 0x00474770 (hit) · 8 -> 0x004748C0 (0xD14 block) · 9 -> 0x00474EA0 · 10 -> 0x0046A1C0 ·
11 -> 0x004753F0 · 12 -> 0x004755B0 (model 5936, hit) · 13 -> 0x004757F0 (hit) · 14 -> 0x004758E0 · **15 -> breakable 0x00465FB0** ·
16 -> 0x0046A580 (rand x10, sub-task 0x0046AD20) · 17 -> 0x00474F30.

### Komono kinds
0x00593580 is **two tables**: constructors [0..78] (kind -> ctor, called once by 0x00461CD0) and 0x005936BC = per-frame task
functions [0..78] used by the generic ctor 0x00461CF0 (CONFIRMED: kind 45 -> 0x005936BC[45] = 0x0046DAB0).
Ctor groups: 0x00461CF0 generic (50 kinds: 5-14, 18-21, 25, 27, 28, 30-36, 41, 43, 45, 49, 51, 53, 54, 56-60, 62-64, 67, 69-78;
switch per kind, rand for 8, 0x22, 0x24, 0x2B, 0x3E, 0x43, 0x47 ...), no-op 0x0041EBB0 (2, 15, 22, 23, 46), dedicated ctors for
0, 1, 3, 4 (breakable group), 16, 17, 24, 26, 29, 37-40, 42, 44, 47, 48, 50, 52, 55, 61, 65, 66, 68.

## 4. rand() in prop code (determinism)
Item counters at prop creation (0x00473770 / 0x00462E10 / generic ctor cases), break shake (2 per frame while shaking), shooter
choice when both guns hit (0x004650F0), model 0x10CE scale per draw, 0x0046A580 / 0x00468640 / 0x0046C570 ... (not ported).
In the demo capture only 0x00473912 (x2, frame 2788) fires from prop code.

## 5. Open questions
- 0x009C8E58 (shadow ground): -9.76 from 2788, -15.5 from 3232 (when the type-0x10 actor spawns at y -15.5); writer not traced.
- Big-angle matrix interpolation 0x00412750 (prop skeleton mode 2).
- Task +0x24 post list: no writer found; scheduler and free code support it.
- C-line fields assumed = camera path 0x009A2D78 / time 0x009A6110 (used for prop removal rules; never decisive in the capture).
