# Data formats

## Directory layout (game/)

| Dir | Files | Contents (as observed) |
|---|---|---|
| `pol/` | 656 | Models. 458 are PRS-compressed (u32 size + PRS); the rest are stored raw. Format of the decoded data: not yet analyzed. |
| `tex/` | 492 | Textures. 335 PRS-compressed; others raw, including Windows BMPs (`BM` header, e.g. `arcade00.bin`). |
| `mot/` | 50 | Motion data. Not PRS. Starts with a table of u32 offsets. |
| `cam/` | 23 | Camera paths (`cp_demo.bin`, `cp_end.bin`, ...). Not PRS. Starts with u32 offsets. |
| `coli/` | 9 | Collision. Loaded raw by `Res_LoadCollision` (0x0048A310). 2 files happen to decode as PRS with matching size; the loader does not call the decompressor, so treat as raw. |
| `evt/` | 11 | Event tables (`comevtbl.bin`, per-stage `*evtbl.bin`). Not PRS. Contain values like `0x0CEB5A..`, likely pointers relocated at load. |
| `sound/` | 802 | `.wav` files (SE, Voice, music). Streamed by the audio thread. |

Loader path patterns from strings: `pol\%s`, `tex\%s`, `etc\%s`, `coli\%s`, `evt\%s`, `Sound\SE\`, `Sound\Voice\`.
Note `etc\` is referenced by the code (Res_LoadEtc, 0x00426980) but no `etc/` directory exists in the installed layout. Unresolved: it may be unused, or the files may live elsewhere.

## PRS-compressed files

```
u32   decodedSize   (little endian)
...   PRS stream    (terminated by a long-copy word of 0x0000)
```

PRS bitstream, from `Lz_DecompressPrs` (0x0040ACD0). Control bits are read LSB-first from control bytes interleaved with the data:

| Bits | Meaning |
|---|---|
| `1` | Literal: copy the next data byte |
| `0 1` | Long copy: read u16 `w`. `w == 0` ends the stream. offset = `(w >> 3) - 0x2000`; size = `(w & 7) + 2`, or when `w & 7 == 0`, size = next byte + 1 |
| `0 0 h l` | Short copy: size = `(h << 1 | l) + 2`; offset = next byte - 0x100 |

Copies are byte-by-byte, so overlapping copies repeat runs.

Implementations: `tools/prs.py` (reference) and `src/game/prs.cpp` (used by the hook and the port). Verified on all 795 PRS files (exact decoded sizes, C++ and Python byte-identical) and live in-game against the original routine.

Open question: how loaders other than `Res_LoadCommon` decide raw vs. PRS (likely fixed per call site or asset list; unverified).

## Textures (tex\*.bin)

A texture file is the decoded file (PRS or raw) with **no header**; every texture's geometry lives in Hod2.exe. From `Res_LoadTexture` 0x0041CDA0, `Res_LoadCommon` 0x00418200, `Tex_Register` 0x004AC980 and `Tex_CreateSurface` 0x004AC270:

| Table | Indexed by file N | Contents |
|---|---|---|
| `0x004D1410 + N*4` | | `char*` file name (`tex\%s`); N=1 is `common.bin` |
| `0x0055B9B8 + N*4` | | pointer to the file's descriptor array |
| `0x0055C0A8 + N*2` | | `s16` texture count |

The name table has 515 valid names (indexes 1–517). The last few entries run past the end of the descriptor and count tables, so readers only accept descriptor pointers inside .rdata/.data and counts from 1 to 4096. 457 of the named files exist in `tex/`.

Descriptor (16 bytes):
```
s16 width; s16 height; u32 format; u32 dataOffset; u32 textureId
format bits 0-2:  pixel format   0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 YUV422 (game rejects: table 0x00571250 maps it to 8)
format bits 8-15: layout         0x01 twiddled square, 0x03 VQ, 0x05 4bpp paletted twiddled,
                                 0x09 rectangle (linear), 0x0D rectangle split into square twiddled blocks
```
These are the Dreamcast PowerVR texture formats. Twiddle = Morton order with **y in the even bits, x in the odd bits** (the game steps x with `(x + 0x55556) & 0xAAAAA` and y with `(y + 0x2AAAB) & 0x55555`).
- **VQ:** a 0x800-byte codebook (256 entries × 4 texels, order (0,0) (0,1) (1,0) (1,1)) followed by one byte per 2×2 block, in twiddled order.
- **Rect-twiddled:** square blocks of `min(w,h)`, stacked vertically if `w < h`, otherwise side by side.
- **4bpp paletted:** 4-bit indices, twiddled; non-square textures are split into square blocks along the long axis, like 0x0D. Palette:
  - 50 *static* banks of 16 ARGB1555 colours in the exe. Table at 0x0057A010: 12-byte entries `{s16, s16 count=0x10, u32 colours*, s32}`, copied to 0x00810200 by `Tex_LoadPalettes` 0x004ACCA0.
  - Bank per texture = `Tex_GetPaletteBank(file, index)` 0x0041C9E0, a 73-case switch of constants, per-index tables and formulas. `tools/emu.py` gets exact values by running the game's function in a CPU emulator.
  - Out-of-range banks become -1, which means bank 0. Files loaded by `Res_LoadCommon` (`common.bin`) never set a bank, so they use bank 0.
  - Entries are converted to the texture's surface format: 565 = `(v & 0xFFE0) << 1 | (v & 0x1F)`; 4444 = alpha from bit 15, then the top 4 bits of each 5-bit channel; 1555 unchanged.
- **Orientation:** texel data is stored **bottom-up**. `Tex_CreateSurface` copies rows in stored order, so the game's surfaces hold the images upside down and the model UVs compensate. Decoders reproduce the game's layout exactly; only the PNG export flips, for viewing. Evidence: digit and text textures in `scr_common.bin` read correctly only after flipping.

**Residency (surfaces and file states).** Surfaces live in a table indexed by the global texture id: surface
0x00810A0C[id], refcount 0x00810A08[id] (-1 = permanent: created while 0x00810A04 = 1), size 0x00810A00[id]. Mdl_Draw
binds 0x00810A0C[word 8] (NULL for untextured materials); Spr_MakeRecord 0x004ACD20 skips a NULL surface.
- Model textures are created with their model, from the texture file of the same index as the pol file: Res_LoadModel's
  step 0x00418A00 reads `tex\<0x004D1410[file]>` in full and Tex_RegisterModelTextures 0x004AC980 creates the
  materials' surfaces (or adds a reference); the model release 0x004191A0 -> 0x004ACAB0 drops them. A drawn model
  (Mdl_Draw 0x00418560 skips unloaded slots) therefore always has its textures.
- The first loader of an id keeps the surface (later loads only add references), so shared ids show the first file's
  texels: of 1431 shared ids, 39 differ, all in scr_blood_red.bin (0x1BA; 4764 is the same image once scr_common's
  palette bank is applied); the boot loads 0x0057A384[0x009C7024 = 1] =
  scr_blood_green.bin (0x1BB), whose texels equal the model files'.
- Texture-file requests (type 6 0x0041CCA0 = steps 0x0041C890 / 0x0041C950 / 0x0041CC10, op 0x54; type 7 0x0041CD00,
  op 0x55) and Res_LoadTexture 0x0041CDA0 (boot only) load whole files for sprites (0x004ACB50 / 0x004ACC10). File
  state 0x009C7328 (12-byte entries 0x009C7320, 0x1BD files): 0 / 3 loading / 4 loaded; Spr_Draw 0x0041C630 draws only
  sprites whose file is in state 4.
- 0x0041D510 -> 0x00418690 (every stage / mode start, also at the end of the boot): 0x00418720 zeroes all file states
  and model slots, then common.bin (1), scr_common.bin (0x147) and both blood files get state 4, and 0x004AC920 releases
  every non-permanent surface (slot NULL; refcounts are left as they are, the next creation sets 1).
- Res_LoadCommon 0x00418200 ends with 0x004192C0 on list 0x0057A268 (common.bin models 2353..2363): TSP
  `(& ~0x4000) | 0x2000` (bilinear; the file has them point-sampled).

Raw format values seen across all files (15 distinct): 0x301, 0xD01, 0x101, 0x302, 0xD02, 0x102, 0x902, 0x500, 0x901, 0x502, 0x100, 0x501, 0xD00, 0x300, 0x900.

Implementations: `tools/tex_export.py` (reference + PNG export), and `src/game/texture.cpp` with `src/game/exe_image.cpp` (port). Verified visually on `common.bin` (effects and sprites with alpha) and on `bg_adv00.bin` VQ textures (building facades, brick, roof tiles), and by `tex_test` (C++ vs Python RGBA SHA-256 for every texture).

## Models (pol\*.bin)

Evidence: `Res_LoadCommon` 0x00418200, `Res_LoadModel` 0x00418820, `Res_LoadPolFile` 0x00418C20, `Mdl_Draw` 0x004A7EF0 / `Mdl_DrawAlt` 0x004A8440, `Mdl_DrawStrip` 0x004A59C0 / `Mdl_DrawList` 0x004A5A40, `Tex_RegisterModelTextures` 0x004AC980.

**File** (decoded PRS buffer, or the raw file; 230 of the files are PRS):
```
header: (count + 1) entries { u32 start (bit 31 = flag), u32 rootOffset }
        the last entry's start == file size (true for all 650 files with models), which defines count
model k occupies [start_k, start_{k+1}); its root is at rootOffset_k
```
`Res_LoadModel` reads only one model's block from a raw file, chosen by global model id:
- `0x004E83B4[id]` gives the file number.
- `0x004D0EF4[file]` gives the name.
- `0x004E794C[file]` is the file's id list; `0x004E803C[file]` is its count.

The exe lists can be longer than the header (padding entries with id 0) or shorter (8 files differ). The header is authoritative for structure. 331 files aren't in the exe name table at all, and 324 of those still have valid model headers.

**Model root**
```
+0x00 u32 (usually 1)
+0x04 u32 flags        observed: 1, 3, 5, 7. Flag 0x10 selects an alternate vertex path (14-word inline
                       entries) in Mdl_Draw; no shipped model uses it.
+0x08 f32 sphere[4]    centre xyz, radius
+0x18 chunk stream, terminated by a 0 word
```
**Material chunk** (bit 31 of word 0 set), 20 words:

| Word | Meaning |
|---|---|
| 0–3 | PowerVR TA words: parameter control, ISP/TSP instruction, TSP, texture control. Converted to D3D render states by 0x004A7780. TSP & 0x180000 selects the draw pass. |
| 4–7 | bounding sphere, for `ComputeSphereVisibility` (IDirect3DDevice7 vtable +0x84) |
| 8 | texture: `0xFFFFFFFF` = none. Otherwise a **descriptor index into the texture file named like the pol file** (`pol\X.bin` → `tex\X.bin`; for the untabled `pol\pol_X.bin` files, `tex\X.bin`, which is inferred). At load it becomes `0x40000000 \| descriptor.id`. **Runtime evidence:** the in-memory fields of 389 drawn models all equal the same-named table's descriptor ids. Static coverage: 80,529 of 80,689 textured materials are in range; the exception is `znmachob.bin` (160 materials), which has no texture table. An earlier claim that non-common files store global ids was wrong: small indexes fall inside the id range by coincidence. |
| 9 | specular exponent n: power = `1 << n` if n > 0 |
| 10 | ambient scale (ambient = scale × diffuse) |
| 11–14 | diffuse A, R, G, B |
| 16–18 | specular R, G, B |
| 0x13 | byte length of the strip chunks that follow this material |

**Strip chunk** (bit 31 clear): `u32 ctrl, u32 count`, then vertices.
- ctrl `0x08`: triangle list (count × 3 vertices), else strip.
- ctrl `0x20`: vertex references.
- ctrl `&3`: cull mode (render state 0x16, values from table 0x00598B20 = {none, none, CCW, CW}). Mdl_Draw sets it only when `ctrl & 3` differs from the cached last control word 0x007E7914, so the device state is always `table[cache & 3]` (Spr_DrawRecord 0x004A7AB0 sets none and clears the cache). D3D culls by the screen winding, mirrored world matrices included. **In memory the bit can be flipped:** Res_LoadModel's single-model step (0x00418B5B..0x00418B68) runs 0x00419300 (every material's TSP `| 0x800000`, fog off) and 0x00419270 (every strip `ctrl ^= 1`: CW <-> CCW) on the models of list 0x0057A280 = {6048, 6049, 6307, 6309} (the sky domes drawn mirrored with S(1.2, 1.2, -1.2)); a pol-file load does not. The sky 6047 (type 3) is not in the list and is culled completely. Captured: `build\scene_capture_texres_c4500.txt` (R lines with `| cache`, 6048's strips run with cache 0x52 / 0x73 where the file has 0x53 / 0x72).
- ctrl `0x40`: shade mode (render state 9).
- ctrl `0x80`: keep the current states.

**Vertex**: 32 bytes `x y z nx ny nz u v`. Confirmed by `Mdl_DrawStrip`/`Mdl_DrawList`, which call `DrawPrimitive(TRIANGLESTRIP/TRIANGLELIST, FVF 0x112 = XYZ|NORMAL|TEX1, verts, count)`.

**Reference entry** (ctrl 0x20): if bit 0 of the first word is set, it's an inline 32-byte vertex. Otherwise it's `{u32 flag, s32 offset}`, and the vertex is at (entry end + offset). The offset is signed and points backwards; every reference lands inside its model's block.

**Coverage**
- 650 files, **18,027 models, 82,494 meshes, 555,052 strips, 3,853,568 vertices**. All parse with strict checks (material lengths, reference bounds, termination).
- The C++ loader (`src/game/model.cpp`) and the Python reference (`tools/pol_parse.py`) agree byte-for-byte on all 18,027 models (`model_test`).
- In the running game, every strip the original renderer submitted was matched against the C++ parse of that model in memory (`HOTD2_MODELCHECK=1`): 5,141,227 exact matches, 118 more after re-parsing modified memory, **0 unmatched**.

**Unresolved**
- 6 `.bin` files have no model header and aren't PRS: `komono_0.bin`, `pol_boss2_hane_hod1.bin`, `pol_komono_suimonie.bin`, `pol_znonoo.bin`, `tv2.bin`, `bg_adv19.bin`. The last two are read and written raw by the attract code (0x004A0540 / 0x004A05C0).
- `files.txt` is a Shift-JIS directory listing from the developers' machine (`E:\Hod2\Image\pol`).
- Characters are stored as separate per-part models. Assembling them needs the hierarchy and motion data (`mot/`), which isn't decoded yet.
- The flag-0x10 vertex format and the in-game modification of the 118 strips aren't analyzed.

## Skeletons (exe) and motions (mot\*.bin)

Evidence:
- **Player-side path:** `Anim_DrawSkeleton` 0x0040F520, node walkers 0x0040F680 / 0x0040F710 / 0x0040F7B0, `Anim_NodeTransform` 0x0040ED30, `Anim_RootTransform` 0x0040EFB0.
- **Enemy path:** `Enemy_DrawSkeleton` 0x004110D0, `Enemy_RootTransform` 0x00410920, `Enemy_NodeWalk` 0x004114C0.
- **Common:** `Anim_GetFrame` 0x00412F50, `Res_LoadMotionFile` 0x00412C10, `Res_ReadMotionFile` 0x00412D40, `Res_SetupMotionSlots` 0x00412D90. Matrix stack ops: 0x004A9D80 translate, 0x004A99F0 / 0x004A9AE0 / 0x004A9BD0 rotate X/Y/Z.

**Skeletons** are static data in the exe:
- `0x004E0430[t]` holds the root `Node*` for each of 64 types; `0x004E0724[t]` holds the node (rotation slot) count.
- Most types are 16-node humanoids: root → torso (head, two 3-node arms) and hips (two 3-node legs).
```
Node: u32 modelId (global model id drawn at the node, 0 = none); f32 offset[3]; u32 flags; s16 slot; s16 childCount; Node* child[childCount]
```
Model ids resolve to a file through `0x004E83B4[id]` and to a position through that file's id list; e.g. skeleton 0 → `harold.bin`, 1 → `znassb.bin`.

**Motion files:**
- Name `0x004D1B00[n]` (n = 1 is `ans.bin`), motion ids `0x004E2B14[n]`, count `0x004E2BDC[n]`.
- Content: `u32 offset[count]`, then each motion = `u32 header` + frames stored at **30 Hz**.
- The length table `0x004E07D0[id]` is in 60 Hz ticks, so stored frames = `(ticks - 1) / 2 + 2`.
- Frame = `align4(12 + 6 * nodes)` bytes: `f32 rootPos[3]`, then `s16 rot[nodes][3]` (x, y, z; 0x10000 = 360°, i.e. × 2π/65536 = `_DAT_004C4370`).
- **1,056 of 1,058 motion entries** (49 files) fit this exactly. 920 are 16-node humanoid motions. The 2 that don't are `komono_niwa.bin` / `komono_st2.bin` props. (`anim_test`, counting unique motion ids rather than entries, reports 1,051 of 1,058.)

**Pose** (row vectors; every engine op pre-multiplies the current matrix):
```
root: M = Rx(r.x) * Ry(r.y) * Rz(r.z) * T(rootPos)          (slot of the root node)
node: M = Rx(a.x) * Ry(a.y) * Rz(a.z) * T(offset) * M_parent
Rx = [[1,0,0],[0,c,s],[0,-s,c]]  Ry = [[c,0,-s],[0,1,0],[s,0,c]]  Rz = [[c,s,0],[-s,c,0],[0,0,1]]  (D3DX conventions)
```
Odd ticks interpolate: the root position is lerped at 0.5 (`_DAT_004C43AC`) towards the next frame, and angles take half the shortest delta. Node-angle interpolation in the engine uses the previously applied angles and isn't verified.

**Verification**
- `anim_test`: 64 of 64 skeletons load; C++ vs Python on 6,028 node matrices, worst relative error 4.7e-6.
- **Engine** (`HOTD2_ANIMCHECK=1`, attract demo): enemy node matrices relative to the torso, compared with `EvaluatePose` at the enemy's motion and stored frame (enemy obj +0x20, +0x18; matrices at nodeBuffer + slot·0x90 + 0x28). **26,086 compared in steady-state frames, 0 mismatched**, worst 1.8e-5.
- Frames where the enemy is blending (obj+0x37 & 0x21) differ, as expected. Per-node blending (`0x00411D70` / `0x00411700`) isn't modelled.
- The player-side path (0x0040F520) didn't run in the attract demo, so it's unverified at runtime.

## Cameras (cam\*.bin) and view

Evidence: `Cam_LoadFile` 0x00403E70, `Cam_Evaluate` 0x004041E0, `Cam_EvaluateAngles` 0x004042D0, `Cam_Curve` 0x004040F0, `Cam_LookAngles` 0x00403AC0, `Cam_DirToAngles` 0x004016B0, `Cam_BuildViews` 0x00401F40, `Cam_PlayStep` 0x004035E0.

**Tables:** name `0x004D1BC8[n]` (path format `cam\%s` at 0x005773D0, n = 1 is `cp_demo.bin`; 23 files), camera ids `0x004C470C[n]`, count `0x004C476C[n]`, file of camera id `0x004C479C[id]` (s8).

**File:** `u32 offset[count]; u32 0xFFFFFFFF;` then the curve area, which starts at `4 + 4·count`. Camera k's block at `offset[k]` is `s32 channel[8]`, each a word index into the curve area:
- 0–2: position
- 3–5: target
- 6: roll, used only when `0x009A21B0 != 0` (0 in every capture)
- 7: not read by `Cam_Evaluate`

**Curve:** `u16 n` (key count padded to a power of two with copies of the last key), `u16 log2(n)`, then keys `{f32 time, value, outTangent, inTangent}`. The key is found with the game's binary search (start n/2, step n/4, log2(n) − 1 halvings). Between keys k−1 and k, with h = tₖ − tₖ₋₁ and s = t − tₖ₋₁:
`v = (3s²/h² − 2s³/h³)·vₖ + (2s³/h³ − 3s²/h² + 1)·vₖ₋₁ + (s³/h² − s²/h)·inₖ + (s³/h² − 2s²/h + s)·outₖ₋₁`

**Time:** integer frames (60 Hz) since the camera started. `Cam_PlayStep` evaluates at the context tick (+0x144) and increments it every frame until the end tick (+0x148).

**Angles:** with d = pos − target:
- yaw = `(s16) trunc(atan2(dx, dz) · 65536/2π)`
- horizontal distance = `dz / cos(yaw)`, or `dx / sin(yaw)` near ±90°
- pitch = `−(s16) trunc(atan2(dy, horizontal) · 65536/2π)`

**Contexts:** 4 camera contexts at `0x009A6000 + i·0x1A4`: view +0x00, camera matrix +0x40, pos +0xC0, pitch +0xCC, yaw +0xD0, roll +0xD4, tick +0x144. Active index `0x009C6F00`; camera id `0x009A2D78`.

**View:** `V = T(−pos)·Ry(−yaw)·Rx(−pitch)·Rz(−roll)`, loaded as the base of the engine matrix stack, so model matrices are `world · V`.

**Device transforms** (captured with `GetTransform`):
- D3D VIEW = `diag(1, 1, −1, 1)`
- PROJECTION = `[2.00065, 2.667533, Q = 1.0001, −Q·zn = −0.80008]`: near 0.8, far ≈ 8000, vertical field of view ≈ 41.1°, 4:3. One matrix for the whole capture.

**Placement (captured):**
- Level chunk models (`st<N>_0x*.bin`) are drawn at **identity** world placement.
- Sky models (`st<N>_1.bin`) are translated to the camera position. Model 6048 additionally has scale 1.2 and a yaw that advances about 4 units per frame. That yaw rate is a **fitted hypothesis**; the code isn't traced.
- `stage1.bin` model 83 is drawn about 100 times per frame with varying positions and scales (a particle effect); `komono_*` props are placed instances. Neither source is traced yet.

**Verification** (capture `build\scene_capture_run1.txt`: attract demo, 3,600 frames; cameras 29, 30, 42):
- `cam_test`: 2,730 of 2,730 captured `Cam_Evaluate` results match (worst 6.7e-6).
- Static chunk draws come out at identity in 92.9% overall and **100% in single-camera frames without effects** (camera 29: 2,315/2,315).
- Exceptions:
  - Camera 30, frames 2058–2059 (64 draws): a camera offset applied after evaluation (y +12.8 / +5.6), likely a shake. Not modelled.
  - Camera 42 frames with 2–3 evaluations per frame (532 draws): **not a blend** (corrected, Stage 2). They are return-to-rail evaluations of the shooting camera 0x00402650 plus the rail's own; the rendered view is smoothed 1/16 per frame with a rotation (0x00403C00), not ported. See notes/agents/events.md.
- `tools/scene_compare.py`: native renders vs original screenshots at frames 650 / 800 / 950 / 1050 (camera 29 t = 44 / 194 / 344 / 444). Edge correlation outside the 2D overlays is 0.62–0.72, against 0.03–0.28 for t+60 controls. Two capture runs gave identical camera values, with frame alignment within ±1.

## Event scripts (evt\*.bin)

Evidence: `Evt_LoadCommonTable` 0x00413070, `Evt_LoadTable` 0x00413160, `Evt_Relocate` 0x00413120, `Evt_GetScript` 0x0045EB90, `Evt_Init` 0x0045EBC0, `Evt_Run` 0x0045ECC0, opcode table 0x005931D8, resource queue (request functions 0x0041D5D0–0x0041D750, ring buffer 0x007DA220, processor `Res_ProcessRequest` 0x0041D5A0, type table 0x00588C20).

**Loading**
- `comevtbl.bin` loads at **0x00977200**. The stage table `0x004D1C7C[0x00579928[mode]]` (e.g. `st1evtbl.bin`) loads at **0x00977400** (`PTR_DAT_005798EC`).
- Every word in the Dreamcast range `0x0CE80000–0x0CEFFFFF` is relocated by `+0xF3AC1A00`, so DC `0x0CE80000` becomes PC `0x00941A00`. That range also covers game globals.
- **Confirmed by trace:** all 917 executed opcodes sit at their traced addresses in the relocated image of `st1evtbl.bin`.

**Script selection:** `script = comevtbl[mode 0x009A1A08][scene 0x009A2BC0][variant 0x009A2BB0]` → `0x009C7108`. Opcode `0x4F` advances the variant.

**VM:** `do { table[*script](); } while (!yieldFlag 0x009C8EA0);`. Handlers advance the script pointer themselves. There are 96 opcodes with 84 distinct handlers.

**Lengths:**
- **Fixed:** 4 bytes for 40, 4D, 4F, 58, 59. 8 bytes for 1A–1F, 28, 29, 31, 38, 41, 42, 44, 48, 50–56. 12 for 18. 16 for 3D.
- **−1-terminated lists:** 07, 09, 0B, 0C, 0F, 10.
- **20 (environment channels):** 20 bytes if sub-op 5 or 9, else 12.
- **30:** `(n/16)·4 + 8` bytes, where n is the first argument.
- 105 of 105 sequential executions agree (`evt_test`).

**Semantics (confirmed from handler code unless marked ?)**
- **50–56:** queue resource requests in the 64-entry ring at 0x007DA220 (16 bytes: type, ?, id, state):
  - 50: load model by global id (type 0/1, step table 0x0057A29C → `Res_LoadModel`)
  - 51: unload model (type 2)
  - 52: load pol file by index (type 3 → `Res_LoadPolFile`)
  - 53: unload pol file (type 4)
  - 54: load texture file by index (type 6; the demo loads `scr_item_07`/`08`/`17`)
  - 55: unload texture file (type 7, 0x0041CD00: state 0, one release per texture 0x004ACC10)
  - 56: load motion file (type 8 → `Res_LoadMotionFile`/`Read`/`SetupSlots`)
- **58 / 59:** process queued requests.
- **20:** set an environment channel of block 0 (`Env_SetChannel` 0x0040B3F0), immediately (current and interpolation state). **21** (0x0040B650) is the interpolated variant (not decoded). Channels: see "Environment" below.
- **18:** environment parameter (`0x0040E140`). **1B–1F:** set globals.
- **31:** start camera (`0x00403BB0(1, id)`, id → 0x009C6F10). **4D:** camera reset. **41:** wait for camera ticks remaining (0x009C6F28). **40, 42, 44:** other waits.
- **38:** play sound (`Snd_PlayGameSound`). **48:** set flag 0x009C7200[arg]. **4F:** next variant.
- **07, 09, 0B, 0C:** call a function per list entry, with pointers into event data. Probably object/enemy spawn lists (?), not decoded.

**Stage-1 demo** (`st1evtbl.bin`, attract frames 2788+). It loads the skies 6048/6911 and these pol files: `znsam2`, `znonoo`, `znebi2` (enemies), `komono_3`, `komono_st1b`, `komono_6` (props), `water`, `dolam`, `frog_gold`. It also loads `etc_1` models, `scr_item` textures and the `komono_st2` motions (one of the two motion files with the unexplained layout). Later it loads `hitoc`, `hito_kao_boy`, `zstin` and `komono_man` motions, and unloads earlier sets.
- Drawn but **not** requested by events: the level chunks (`st1_03b`, `st1_03c`, `st1_05`), `stage1.bin` particles and `common.bin`. These are loaded by stage initialization (`Res_LoadStage` 0x00418490 → `Res_LoadCommon` / `Res_LoadStageTextures`), which isn't traced yet.

## Stage sections, camera blocks, material state

**Stage sections** (`Stage_SelectTables` 0x004014C0, `Stage_LoadSection` 0x00401510, `Stage_UnloadSection` 0x004015A0)
- Tables per stage s (0-based; s = 0 is stage 1): sections `0x00576A2C[s]` (`0x00576A8C[s]` when `0x009CA08C == 1`), objects `0x00576A5C[s]` (`0x00576ABC[s]`).
- Section = 0x18 bytes: up to 12 s16 object indexes, −1 terminated. Object = `{s16 modelId, s16 ?}`.
- Event **0x29** `section` makes it current: it unloads objects of the old section that aren't in the new one, and requests the new ones. **0x28** `section` preloads.
- Request rule: if the model's file number (`0x004E83B4[id]`) is below `0xE7`, the whole pol file is requested, otherwise just the model.
- **Verified:** in the stage-1 demo (`st1evtbl.bin`, section 7), every level-chunk draw of the original is in the selected section (`stage_test`: 5,392 / 5,392 in the last capture, 9,036 / 9,036 in the earlier one).
- The original draws all six section-7 chunks every frame, twice (once per pass); culling of 6094/6097 appears in ~37% of frames.

**Command queue (event 0x30):** pushes the block pointer onto a 16-entry queue (`0x009C9060`, pending count `0x009A2C8C`). The block's first word is its type and determines its size.
- **Type 64 = camera segment** `{64, startTick, endTick, cameraId}`. Verified: blocks 0..90, 91..300 and 301..315 of camera 42 were evaluated on consecutive frames, one tick each (frames 2789–2879, 3031–3240, 3241–3254).
- Between blocks the camera holds while the script waits on gameplay (frames 2880–3030).
- Type 33 is an unknown block kind.

**Material → render state** (`Mdl_ApplyPvrState` 0x004A7780; D3D7 render-state numbers):

| Source | State |
|---|---|
| ISP 31–29 | `ZFUNC` via `[NEVER, GEQUAL, EQUAL, GREATER, LEQUAL, NOTEQUAL, LESS, ALWAYS]` (0x00598B00, reversed because DC compares 1/w) |
| ISP 26 | `ZWRITEENABLE` = !bit |
| TSP 31–29 / 28–26 | `SRCBLEND` / `DESTBLEND` (0x00598AB0 / 0x00598AD0: zero, one, other colour, inverse, src alpha, inverse, dst alpha, inverse) |
| TSP 23 | `FOGENABLE` = !bit |
| TSP & 0x180000 | `ALPHATESTENABLE` = `!= 0x80000`, with global `ALPHAREF` 1 and `ALPHAFUNC` `GREATEREQUAL` (0x004A7630) |
| TSP 18/16 (U), 17/15 (V) | address mode = `[wrap, mirror, clamp, mirror][flip \| clamp<<1]` |
| TSP 7–6 | `COLOROP` modulate; `ALPHAOP` modulate, or SELECTARG1 (texture alpha) for instruction 1 |
| TSP 14–13 | MAGFILTER/MINFILTER POINT if 0, else LINEAR; MIPFILTER stays POINT |
| strip ctrl & 3 | `CULLMODE` via `[none, none, CCW, CW]` |

**Passes:**
- Pass 0 (`0x004A7A10`, `ALPHABLENDENABLE` 0): `Mdl_Draw(obj, 0)` draws meshes with `TSP & 0x180000 == 0x80000`.
- Pass 1 (`0x004A88E0`): sorts the deferred models by the depth `Mdl_Draw` computed, enables blending and draws the rest with `Mdl_Draw(obj, 1)`.

The state is only changed when the TSP differs from a cached copy (reset to 0x20800000 per frame by `Draw_InitStates` 0x004A7630), so stale device state could in principle leak. **Checked** with `HOTD2_SCENECAP_FILTER`: across 203,620 applications in the stage-1 demo, the device filter always equals what the material's own bits imply (bits 0 → POINT/POINT/mip POINT, bits 1 → LINEAR/LINEAR/mip POINT).

**Port:** `ModelRenderer::DrawGame` (lighting and fog included). Not reproduced yet: strip culling, the per-model (not per-batch) translucent sort, and mipmaps: none exist (0x004A5ED0 clears DDSCAPS_MIPMAP / COMPLEX and DDSD_MIPMAPCOUNT before CreateSurface), so sampling level 0 is exact. Draw_InitStates 0x004A7630 also turns SPECULARENABLE on (material words 16..18, power 1 << word 9) and never sets NORMALIZENORMALS.

## Environment: fog and light colour

Event ops write env block 0 at **0x009A3540** (the scene light and fog) and block 1 at **0x009A59E0** (the actor light: Env_ApplyActorLight 0x0041DC70 around nearly every skeleton draw, back to block 0 by 0x0041DCC0). The engine turns them into D3D state every frame. Port: `game::Environment` (`src/game/environment.*`), the light globals per submission in `src/game/world_light.cpp`; details and the whole-game verification in notes/agents/lighting.md (2026-10-05).

| Event | Effect |
|---|---|
| op 0x18 `pitch yaw` | env +0x18 / +0x1C light angles (0x0040E140) |
| op 0x20 sub 0 / 1 `ptr` | +0x30 fog near / +0x34 fog far (float read from the pointer) |
| op 0x20 sub 2 / 3 / 4 `v`, sub 5 `r g b` | +0x24 / +0x28 / +0x2C fog colour (ints) |
| op 0x20 sub 6 / 7 / 8 `ptr`, sub 9 `p p p` | +0x240..+0x248 light colour |
| op 0x20 sub 10 `ptr` | +0x24C light intensity |

**Corrections (2026-10-05, whole-game capture with `HOTD2_SCENECAP_ENV`, notes/agents/lighting.md):** the blocks are initialised data (fog colour 0, near 65535, far 65536, colour 1, intensity 0.7, angles 0) and Env_InitBlock 0x0041DBA0 restores exactly that in Cam_Reset 0x004021D0 (every stage start, the game over / map / ranking / title paths); the 0x00460250 writes below never ran in a whole game. Block-1 ops: 0x19 angles, 0x24 set, 0x25 / 0x27 interpolate, 0x26 stop. Interpolation: 0x21 / 0x25 (0x0040B650) step per frame, 0x23 / 0x27 (0x0040BA90) over N frames, 0x22 / 0x26 (0x0040C1F0) stop; channel records `{active, value, target, step}`. The port matches 97198 / 97200 frames of a whole game.

**Defaults (superseded, see above)**
- `0x00460250` writes fog colour (255, 0, 0), near −600, far 1800 and light colour (1.0, 0.2, 0.1).
- `Env_InitInterp` 0x0040AE60 starts the per-frame interpolator task (0x0040AD90 → 0x0040B0A0 / 0x0040B0F0). It seeds the 16-byte channel records `{active, value, step, ?}` at 0x009C89E0 (block 1: 0x009C8920): colour 1.0 (0x0057969C) and intensity 0.7 (0x005796AC). The fog records come from 0x007C1750 (BSS).
- The title runs with no env ops, and its captured state matches the seeds: angles 0/0 (world light (0,0,1), 639 states), AMBIENT 0xB2B2B2, diffuse 0.98, ambient 0.3.
- **Not traced:** how the seeds replace the 0x00460250 colour, and the title's fog (env near 65535 / far 65536 → `FOGSTART` 131070, i.e. no fog).
- `0x0040C1F0` (handler not identified) clears channel records.

**Fog**
- `FOGCOLOR` = env +0x24..+0x2C (0x0040D5B0 → 0x004ABDD0).
- `FOGSTART` / `FOGEND` = 2·min / 2·max of near and far (0x0040C2E0 → 0x004ABDF0), applied only when env +0x38 (s16) != 0.
- Captured: `FOGTABLEMODE` LINEAR (3), `FOGVERTEXMODE` NONE, `RANGEFOGENABLE` 0.
- Per material: `FOGENABLE` = !TSP bit 23.
- The projection is W-friendly, so the table fog runs on eye depth.

**Light** (0x0041DCC0 → `Light_SetIntensity` 0x004AA070, `Light_SetColor` 0x004AA0A0, built in 0x004AA120). With intensity i and colour c:
- `AMBIENT` = 0xFF000000 | trunc(255·i·c) per channel
- light diffuse = specular = 1.4·i·c
- light ambient = 0.3·c

**Stage-1 demo** (st1evtbl.bin 0x0097A224..0x0097A28C, frame 2788):
- light pitch 0xEE00, yaw 0x800
- fog colour (0x29, 0x28, 0x18), near 39, far 261 → D3D 78..522
- colour (1, 1, 1), intensity 0.7 → AMBIENT 0xB2B2B2, diffuse 0.98, ambient 0.3

**Verified:** `env_test` replays the env ops of the original's event trace through `game::Environment` and compares every captured state:
- env block: 2/2 states equal;
- D3D fog: 1/1 steady state (frames 2788–3300) equal, plus 1 state within a frame of the change;
- light colours: 3,311/3,311 equal (640 title states against the defaults, 2,671 after the script ops).

The title (frame 607) had fog 131070..131072 (effectively off); that title-script op wasn't decoded.

## 2D sprites

Screen-space quads in a 640×480 space, drawn through the model pass system. Port: `src/game/sprite.*`, `src/game/crt_qsort.h`, `ModelRenderer::DrawSprites`.

**Chain:** `Spr_Draw` 0x0041C630(def) → `Spr_MakeRecord` 0x004ACD20(def, texture id) → `Spr_Submit` 0x004A7A40 → `Spr_DrawRecord` 0x004A7AB0 → `Draw_TLStrip` 0x004A5A00 (DrawPrimitive, triangle strip, FVF 0x1C4).
- `Spr_DrawSimple` 0x0041C6D0(id, x, y, depth, sx, sy, rot, flags) fills alpha 1 and blend −1.
- Translucent records (key bit 31) are copied into the pass-1 list with key `| 0x007E78B8` (the list priority, set by 0x004A7A00), sorted together with translucent models, and drawn there. Others are drawn immediately.

**Definition (14 words):** `id, x, y, depth, sx, sy, (4 unused), rot, alpha, blend, flags`.
- Texture: file slot `0x0057A5BC[id]` (index into the tex name table 0x004D1410; drawn only when slot state `0x009C7328[slot*12] == 4`) and global texture id `0x0057D448[id]` (descriptor `textureId`).
- Flags:
  - bits 0–1 / 2–3: anchor x / y (1 = left/top, 2 = centre, 3 = right/bottom; 0 = top-left)
  - 0x10 / 0x20: flip u / v
  - bits 8–10: ZFUNC index (0 → 4)
  - 0x800: fog on
  - 0x2000: RGB = env light colour (0x007E7998)
- Translucent unless global `0x00810A20 & 1`, blend 0, or (blend != 2 and alpha > 1). Opaque sprites use alpha 1. Colour = `trunc(255·alpha) << 24 | 0xFFFFFF`.

**Vertices:**
- Half-size = texture size × scale × 0.5, rotated by `rot`·2π/65536.
- Corners in anchor units `(1,3) (3,3) (1,1) (3,1)` (0x00598B78). Uvs: `(u0,v0) (u1,v0) (u0,v1) (u1,v1)`, with v = 0 at the bottom because textures are stored bottom-up.
- rhw = 1/depth; z = (1.0001·depth − 0.80008)/depth (the projection terms 0x007E7970 / 0x007E7980).

**State:**
- ISP = zfunc << 29.
- TSP = 0x20818000 (opaque: ONE/ZERO, fog off, clamp) or 0x94918000 (translucent: SRCALPHA/INVSRCALPHA, alpha test).
- Bit 0x800000 is cleared by flag 0x800 (fog). Point sampling. Clipping is off while drawing.

**Pass-1 order:** CRT `qsort` (VC6, 0x004ADCF7, not stable) with comparator 0x004A8A20: key bits 0–3 ascending, then depth descending.

**Layouts traced**
- `Title_DrawLogo` 0x004960A0:
  - logo strips 0xAB1+i at (36 + 128·i, 28), i = 0..4;
  - 0x8FE (372, 379) flags 0xA;
  - 0xA5A (320, 454) flags 0xE;
  - 0x231 (218, 426);
  - all depth 1, priority 0xF.
- PRESS START (0x004067D0): definition 0x005A4CE8 from `HUD_Init` 0x004066D0 (depth 0.9999, flags 5), id 0x900 at (160, 334), drawn while `(counter 0x005A4D44 & 0x20) == 0`. The counter starts at 0 in `HUD_Init` and accumulates the frame counter 0x009A5C40. That is `((capture frame + 1) & 32) == 0`, because the counter is the boot frame counter (traced, see below).

**Verified** (`sprite_test`, `build\scene_capture_sprites.txt`, 3,300 frames):
- 17,997 definitions → records: every drawn record is explained; 2,431 frames have identical sets. 5 scene-change frames left some definitions undrawn.
- 17,947 quads within 1.5e-8 of the captured vertices (1,361 bit-identical).
- All 17,947 runtime texture sizes equal the static descriptors.
- 5,181 captured title and PRESS START definitions equal the ported layouts.

**Not reproduced:**
- Draw order among equal keys when translucent models share the list: sorting the sprites alone reproduces 285 of 2,431 frames.
- Other 2D paths: 0x0049E100 / 0x0049EBE0, which draw TL strips without a record (3,576 Q lines).
- HUD/credit layouts (0x004068A0, 0x00406920 and others).
- (Resolved) Blink counter: `HUD_Init` runs once at boot, so the counter equals the boot frame counter 0x009A5C40 (not reset at attract mode changes; checked frames 0–3299 in `build\scene_capture_hud.txt`). When the HUD draws in capture frame N it is N + 1, which explains the earlier "phase 1".
## Actors spawned by events (props, enemies, items)

**Actor type table:** `0x009A2280[0x70]`, filled at stage start by `Actor_InitTypeTable` 0x0040AC90 from the `(type, function)` pair list at **0x00593358** (56 types, terminated by a negative type; the default is a no-op). Examples: 0x41 → 0x00461CD0 (prop "komono" kinds), 0x44 → 0x00472B10, 0x10 → 0x0048A3E0. Actors are coroutine tasks (`0x004A6FA0` creates a task with a function and size; the bodies `longjmp` back to the scheduler).

**Spawn records** (pointer lists in event data):
- Ops **0x0B** (`0x00408A20`, actor size 0x13F4) and **0x0C** (`0x00408BC0`, 0x1314) per entry:
```
u32 type; u32 flags (actor+0x34 = flags|1); f32 pos[3] (+0x40); s32 rot[3] (+0x64); u16 lo; u16 state (+0x11C/+0x11E);
u32 params... (pointer stored at +0x1390 / +0x130C)
```
- Op **0x09** (`0x004088A0`): same header; byte 0x24 → +0x1F4, byte 0x25 → **kind** (+0x130C).
- Type 0x41 dispatches on kind via table 0x00593580 (158 entries). For example, kind 4 → 0x00462E10 (prop group: sub-kind tables 0x00593DB8 / 0x00593DC0, fixed model ids for some sub-kinds, `rand()` for others); kind 45 → 0x00461CF0.

**Verification** (stage-1 demo capture):
- All three drawn type-0x44 records (models 6095 and 6096 from `komono_st1b`; 6126) are drawn **exactly** at the record position (distance 0.00) in some frame. The actor then moves them (6095/6096 sway; 6126 moves), so the record is the initial placement.
- Two type-0x44 records hold "2" in the model slot (different sub-layout; not drawn).

**Not decoded:**
- Prop kinds (random choices, sub-object tables), which produce the `komono_3` / `komono_6` instances.
- Enemy actors (`znebi2`; they use the second light).
- The other 54 actor types.
- Actor rotation order for `rot[3]` (all demo records have small or zero values).

## Stage-2 subsystem reports

Detailed formats and code paths live in the agents' reports. Highlights:
- `notes/agents/events.md`: all 96 event opcodes, the camera command queue, game modes.
- `notes/agents/enemy.md`: enemy object layout 0x13F4, behaviours, damage.
- `notes/agents/props.md`: task system, prop skeleton/motion layout, actor types.
- `notes/agents/shooting.md`: gun records, shot ray, hit list, demo auto-gun.
- `notes/agents/effects.md`: RNG sites, rain, sky table, billboard effects.
- `notes/agents/sound.md`: sound id format and tables.

## Hod2.ini

```
[Controller]      PLAYER1_INDEX / PLAYER1_DEVID / PLAYER2_INDEX / PLAYER2_DEVID   (Input_ReadConfig)
[Flush Setting]   FLUSH_FRAME, FLUSH_POWER, SCREEN_LIGHT, G_ENABLE, AUTORELOAD   (Game_Init: gun screen flash)
```

`[HGLCORE]` keys read by `HGL_ReadCoreConfig` (0x004A6C70) into flag bits at 0x007DEB80:
AllDevicePass=0x1, DispFps=0x2, FullScreen=0x4, SmallTexure=0x8/0x10, DispVersion=0x20, CDCheckPass=0x40, UseZBuffer=0x80, UseHEL=0x100.
The ini path it reads (buffer at 0x007DF3E0) has not been confirmed.
