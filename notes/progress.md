# Progress

## Game files available (2026-10-02)

The missing-game-files blocker is resolved. `game/Hod2.exe` and all 2,047 files listed in the installer are present (546,307,866 bytes total), with their directory layout reconstructed from the MSI Directory, Component, and File tables. Each file's size was checked against MSI metadata and each copied file's SHA-256 was checked against the extracted CAB member.

- Source: the pre-existing `Downloads/The_House_of_the_Dead_2_Win_ROM_EN/.../Game Files/House Of the Dead 2` CloneCD image. This was not a new physical-disc rip.
- `original/` retains byte-identical `.img`, `.ccd`, `.cue`, and `.sub` files plus a derived `House Of the Dead 2.iso`. The ISO contains the 2,048-byte payload from each of 248,470 validated MODE1/2352 sectors; the raw image and subchannel file remain available.
- `original/disc/` contains the extracted disc, `original/cab/` contains the unpacked CAB members, and `original/disc/msi-tables.json` records the installer layout. SHA-256 manifests are `original/media-manifest.json` and `original/game-manifest.json`.
- `game/Hod2.exe`: x86, image base `0x00400000`, entry point `0x004AD3B5`, linker 6.0, timestamp 2001-05-09. SHA-256: `c6b4116788b7f68c56860fb9cc8a94bf984e620907031e4bc3db43623dbe579a`. It matches the executable in the disc root.
- `tools/pe_info.py` completed successfully; report: `ghidra/hod2-pe.json`. Imports include DDRAW, DINPUT, and DSOUND. No known copy-protection signatures were detected by that script; this is not a runtime compatibility test.
- Ghidra headless import, auto-analysis, export, and save completed successfully. Project: `ghidra/hotd2.gpr`, program: `/Hod2.exe`. `ghidra/export-hotd2/functions.csv` contains 1,891 indexed functions: 1,874 decompiled and 17 thunks. The script's broader 2,048-function total also counts external functions, which its iterator does not export. Decompiled files are in `ghidra/export-hotd2/functions/`; log: `ghidra/analyze-hotd2.log`.
- The next work is semantic subsystem mapping and validation of the generated decompilation, followed by hooks/port work. The subsystem table below has not been marked complete by the automated analysis alone.
- Files were extracted without executing the legacy installer or game. MSI registration, registry values, disc checks, and playability have not been tested. Game files and generated analysis remain excluded from Git by the existing ignore rules.

## Milestone 1: entry points mapped, first hook live (2026-10-02)

Evidence comes from `ghidra/xrefs/` (`tools/ghidra/ExportXrefs.java`: import callers, string xrefs, call graph), decompiled C, and disassembly (`tools/disasm.py`). Every address is in `notes/symbols.csv`, with status `identified` (backed by API or string evidence), `hypothesis` (inferred from position or call pattern), or `hooked`.

**Toolchain:** MSVC 6 with static CRT (Rich header, linker 6.0). Engine is H.I.C.'s "HicGameLib"/"HGL" wrapper around statically linked D3DX 7 utility code (`CD3duContext`, `CDDrawDeviceList`, `CHelInfo`). No `.reloc` section, so the image always loads at 0x00400000.

**Startup chain:** `CRT_WinMainCRTStartup` 0x004AD3B5 → `App_WinMain` 0x004C2C80 (single-instance mutex `HicGameLibMutex`) → `HGL_Run` 0x004A65F0 with 5 callbacks:
- `Game_PreInit` 0x0049E420
- `Game_Init` 0x0049E4A0
- `Game_Frame` 0x0049E220
- `Game_Shutdown` 0x0049E750
- `Game_WndProcHook` 0x0049E780

`HGL_CreateMainWindow` 0x004A6370 reads the `[HGLCORE]` config and runs `HGL_CheckDisc` 0x004A6830, then `HGL_MessageLoop` 0x004A6500 calls `Game_Frame` once per frame at 60 fps. Ghidra had not created the 5 callbacks as functions (they're only referenced as data); `tools/ghidra/DecompileAt.java` created them.

**File I/O:** separate loaders per folder (`pol\`, `tex\`, `etc\`, `coli\`, `evt\`). Assets are a u32 size followed by Sega PRS; `Lz_DecompressPrs` is 0x0040ACD0. The format is documented in `notes/formats.md`. The custom heap is `Mem_Alloc` 0x004A7400.

**Rendering:** DirectDraw 7 via D3DX 7 (`DirectDrawCreateEx` in D3DX code at 0x004BC26E / 0x004BC773 / 0x004BDDB0). `Game_Frame`'s windowed path Blts a 640×480 back buffer. Per-frame draw entry point is presumed to be `Game_Draw` 0x0049EB50 (hypothesis).

**Input:**
- `Input_Init` 0x0041E2D0 reads Hod2.ini `[Controller]`, opens `\\.\SEGAJOY.VXD` (Sega PC light gun driver), then sets up DirectInput 7.
- `Input_Poll` 0x0041E530 fills 13-dword records for each device.
- Mouse aim comes from `Input_GetMouseClientPos` 0x0041EA80 (GetCursorPos in client coordinates).
- VxDs can't load on NT-family Windows, so the gun path is dead on modern systems. This is the main motivation for the port's raw-input layer.

**Audio:** `Snd_Init` 0x004ABF50 sets up DirectSound at 44,100 Hz, 16-bit, via `Snd_CreateDirectSound` 0x004A2F50 (DSOUND ordinal 1). `Snd_Command` 0x004ABF80 is a packed-command dispatcher; `Snd_OpenStream` 0x004A3EF0 is a streaming thread over `sound\*.wav`.

**First hook (live):**
- `Lz_DecompressPrs` was reimplemented in `src/game/prs.cpp` and hooked through `hotd2_hook.dll`.
- **Offline:** `prs_test`, 795/795 files byte-identical to the reference.
- **Runtime:** the game was launched via `hotd2_launch.exe` with the disc ISO mounted as a virtual drive (no disc-check patch) and `HOTD2_SHADOW=1`. It ran for 45 s, responsive. Our decoder handled 9 real loads (108 KB – 1.9 MB), all byte-identical to the original routine run side by side.
- The hook refuses to install unless the exe's PE timestamp is 0x3AF8ED51.

**Uncertainties / not yet verified**
- Callback slot meanings for `HGL_Run` are inferred from where they're stored and called. Slots 1 and 2 (PreInit/Init) aren't confirmed by a direct call site yet.
- Signatures marked `?` aren't determined yet.
- How loaders other than `Res_LoadCommon` choose raw vs. PRS.
- `etc\` is referenced by the code but missing from the installed tree.
- Game display mode during the runtime test wasn't captured (no screenshot); only process liveness and the hook log were checked. Gameplay past the attract/demo loads hasn't been exercised.
- Whether DirectInput/mouse aim works on Windows 11 is untested.

## Milestone 2: input mapped, two-player raw-input hook (2026-10-02)

**Input architecture** (new symbols in `notes/symbols.csv`):
- `Input_Poll` 0x0041E530 runs a per-player switch on device mode (P1 `0x00588E24`, P2 `0x007DC698`, resolved from Hod2.ini by `Input_ResolveDeviceModes` 0x0041E440).
- Each player has a gun slot (P1 = 0, P2 = 6):
  - aim: `0x007DBD50 + slot*8` (int x, y)
  - fired flag: `0x007DC120 + slot*4`
  - off-screen-shot flag: `0x007DB7C8 + slot*4`
  - 0x34-byte button record: `0x007DC1B8 + slot*0x34` (+8 held, +0xC ~held, +0x10 pressed, +0x14 released)
- Game code reads aim through `Input_GetGunPos` 0x0041E280 and latches shots with `Input_LatchShots` 0x0041EB30.
- Mode 5 (mouse as gun) is the only pointer path that works on NT-family Windows, and it supports a single system cursor.

**Hook (`src/hook/input_hook.cpp`, opt-in `HOTD2_RAWINPUT=1`):**
- Runs the original poll, then writes mode-5-equivalent state for each player who has joined with their own raw-input device (first trigger pull = P1, second = P2). Trigger while aimed off-screen counts as a reload.
- Players who haven't joined are untouched.
- Raw input runs on its own thread with a hidden receiver window, mapped onto the game window's client area.

**Runtime check:**
- Game launched with both hooks (PRS shadow + raw input): ran 40 s, responsive. Raw-input thread attached to the game window, and PRS shadow stayed 9/9 identical.
- **Not verified:** actually joining and shooting. Synthetic input (SendInput, automation) has no device handle and is ignored by design, so this needs a test with a physical mouse or light gun.

**Open questions**
- The game window covered the full 2048×1280 desktop with fullscreen-exclusive DirectDraw. A window capture came back black (expected for exclusive mode), so the visual state wasn't confirmed.
- Aim is written in client pixels, the same units as the original `Input_GetMouseClientPos`. Whether the game rescales client pixels to its 640×480 space in fullscreen hasn't been confirmed. Next step: decompile the callers of `Input_GetGunPos` (0x0040CE70, 0x00416940, 0x004169C0).

## Milestone 3: aim scaling resolved, windowed debug mode (2026-10-02)

**Aim coordinate space (evidence)**
- `Hud_UpdateCrosshair` 0x004169C0: in gun modes 5/6 the crosshair is `Input_GetGunPos` used directly. Otherwise the pad cursor is converted with `(cx + 320.0, 240.0 - cy)` (float constants at 0x004C49CC / 0x004C49C8). So the screen space is **640×480 pixels, top-left origin**.
- `Game_UpdateShots` 0x0040CE70: copies gun aim into the shot records (0x009C8800, u16 x/y, stride 0x28) with no rescale. `0xFFFF,0xFFFF` marks an off-screen shot.
- `Hud_SetCrosshairFromGun` 0x00416940: same, direct use.
- Conclusion: the game expects aim in 0–639 × 0–479 and does no client-to-game scaling of its own. The original mouse path (`Input_GetMouseClientPos`) is only right when the client area is exactly 640×480.

**Display facts**
- `Game_PreInit` calls `HGL_SetDisplayModeByIndex(11)`, which is 640×480, 16 bpp, fullscreen (mode table at 0x005988F0).
- `HGL_CreateMainWindow` sizes the window to a 640×480 client area from frame and caption metrics.
- The `[HGLCORE] FullScreen` key sets flag 0x4, but nothing reads that flag (I checked every reference to 0x007DEB80). There's no ini switch for windowed mode.
- Measured from the game process itself:
  - **Windowed:** client is 640×**477**, because Windows 11's frame metrics differ from what the game assumed.
  - **Fullscreen** (earlier run, measured from outside): window 2048×1280 on a 2048×1280 desktop. The 640×480 mode is emulated and stretched by Windows/GPU.
- In both cases client pixels ≠ game pixels.

**Corrections to the raw-input hook**
1. Aim is now `normalized position × (0x007DE634, 0x007DE638)` (640×480), clamped to [0, 639] × [0, 479]. Before, it was written in client pixels, which overshoots by a factor of 3.2 on a 2048-wide stretched fullscreen and is about 0.6% off in a window.
2. When a player joins and their device mode isn't 5 or 6, it's switched to 5. `Hud_UpdateCrosshair` only follows gun aim in modes 5/6. Mode 6 (the Hod2.ini default for P1) is kept.
3. Aim is now written even when the device points off-screen (clamped). Off-screen shots are unchanged: trigger off the image or reload sets the off-screen flag, and `Input_LatchShots` turns that into aim `0xFFFF`.
4. Optional `HOTD2_ASPECT=4:3` for absolute light guns when the GPU keeps a 4:3 image with side bars: points on the bars count as off-screen. The default is stretch. In fullscreen the bar geometry comes from the monitor rect; in a window, from the client rect.
5. The coordinate math is now pure functions (`src/port/input/aim_map.*`, `src/hook/gun_state.*`). `RawInput::ProcessMouse` is the single place where reports without a device handle are rejected; that filtering is unchanged.

**Windowed debug mode** (`HOTD2_WINDOWED=1`, hook on `HGL_SetDisplayModeByIndex` 0x004A6200)
- Maps fullscreen mode indexes 8–13 to their windowed twins 0–5, using the game's own windowed present path.
- **Verified:** the game renders in a 656×516 window (attract demo with P1 HUD and "PRESS START BUTTON"), and the window can be captured with `PrintWindow`. This is now the way to inspect the game visually.

**Tests**
- `input_test_unit`: **55 checks, 0 failures**. Covers:
  - absolute-to-screen, screen-to-normalized and normalized-to-game conversion, including 2048×1280 → 640×480
  - windowed offset, virtual desktop, 4:3 pillarbox and letterbox
  - null-handle rejection, join order and the third-device limit, per-player independence, relative clamping
  - edge flags across frames, off-window absolute aim, unplugging and rejoining
  - mode-5 fired/off-screen rules, trigger off-screen = reload, start button bit
- `prs_test`: 795/795.
- **Runtime** (windowed + raw input + PRS shadow): 30 s, responsive, PRS 9/9 identical, geometry logged from inside the game.

**Physical verification still required**
- During the run, the log recorded `P1 joined via raw input (device mode was 6)`, triggered by a physical pointing device (synthetic input can't join). That shows real device reports reach the hook in the running game.
- **Not yet verified on hardware:** that the crosshair tracks the device, that shots land where aimed, that two separate devices aim independently, and that off-screen reload works. These need a person with two physical devices.
- Whether `HOTD2_ASPECT=4:3` or stretch matches a particular GPU's fullscreen scaling needs a check with an absolute light gun.

## Milestone 4: texture format decoded, port texture loader (2026-10-02)

**Format** (full spec in `notes/formats.md`)
- Texture files have no headers. Each file's descriptors (width, height, format, offset, id) live in exe tables indexed by file number: names at 0x004D1410, descriptors at 0x0055B9B8, counts at 0x0055C0A8.
- The texel layouts are the Dreamcast PowerVR's: twiddled, VQ, 4bpp paletted, rectangle and rectangle-twiddled, in ARGB1555 / RGB565 / ARGB4444. 15 distinct format values occur.
- Paletted textures use 50 static ARGB1555 banks (table 0x0057A010). The bank comes from `Tex_GetPaletteBank` 0x0041C9E0, which I evaluate exactly by **running the game's own function in a CPU emulator** (`tools/emu.py`, Unicorn), rather than hand-transcribing its 73-case switch. The emulated results match the constants in the decompiled switch.
- Texel data is stored bottom-up; the game's surfaces hold it that way.

**Code**
- `tools/tex_export.py`: reference decoder, PNG export, hashes.
- `src/game/texture.cpp`: the port's decoder (layouts, palette conversion, RGBA).
- `src/game/exe_image.cpp`: PE reader for the exe tables.
- `tests/tex_test.cpp`: C++ vs Python SHA-256 of every texture's RGBA.

**Verification**
- Visual checks:
  - `common.bin`: effects and sprites with correct alpha.
  - `bg_adv00.bin`: VQ building and brick textures with no 2×2 block artifacts.
  - `scr_common.bin`: digits, life torches, "PLEASE WAIT", the 512×32 "PRESS START BUTTON" banner. These show the orientation and the non-square paletted layout are right.
- Bug found and fixed: non-square paletted textures are split into square twiddled blocks, as the game's loop does. Before the fix, the 512×32 banner read out of bounds.
- `tex_test`: **10,466 textures across 420 files, all byte-identical between C++ and Python** (RGBA SHA-256), including 1,290 paletted ones. 0 failures.
- `bank_test`: the native `game::GetPaletteBank` (`src/game/palette_bank.cpp`, transcribed from the decompiled switch) matches the emulated original on **180,544 / 180,544** (file, index) pairs, including indexes past the existing textures and negative indexes.

**Coverage and limitations**
- 420 texture files have table entries. 23 named files have a texture count of 0, so nothing references them (for example `dc_ctrl.bin`, `eff_3.bin`).
- 49 files in `tex/` aren't in the table at all; these are plain BMPs (`arcade00.bin`, `empireBlack_00.bin`, …), the PC-specific screens loaded elsewhere. Not mapped yet.
- YUV422 textures (pixel format 3) are rejected by the game itself, and none occur in the descriptors.

## Milestone 5: pol/ model format, native model loader (2026-10-03)

**Traced:** the on-demand model loader (`Res_LoadModel` 0x00418820), the whole-file loader (0x00418C20), the common loader, the two draw routines (`Mdl_Draw` 0x004A7EF0 / 0x004A8440), the strip and list draw calls, and texture registration and release. The full spec is in `notes/formats.md`. In short:
- Each file has a header of `{start, root}` entries, terminated by an entry whose start equals the file size.
- Models are chunk streams: 20-word material chunks (PowerVR TA words, bounding sphere, texture, D3D material) followed by strips or triangle lists of 32-byte vertices, with optional back-referenced shared vertices.
- **Vertex layout confirmed by the engine's own D3D call:** FVF 0x112 = XYZ | NORMAL | TEX1.
- **Texture link:** ~~common.bin stores descriptor indexes; all other files store global texture ids (39,307 of 39,307 resolve)~~. **Corrected in Milestone 7:** every file stores a descriptor index into the same-named texture file. The "global id" check was too weak, because small indexes fall inside the id range by coincidence.

**Code**
- `tools/pol_parse.py`: reference parser, stats, OBJ export, digests.
- `tools/pol_render.py`: offline textured z-buffer renderer.
- `src/game/model.{h,cpp}`: native loader with strict structural validation.
- `tests/model_test.cpp`: C++ vs Python digests.
- `src/hook/model_check.cpp`: runtime check against the original renderer (`HOTD2_MODELCHECK=1`).

**Mistakes found and fixed along the way**
- Vertex-reference offsets are **signed**. Reading them as unsigned caused 6,537 false failures.
- The model count must come from the file header, not the exe lists, which are padded with id 0 or don't match in 8 files.

**Verification**
- `model_test`: 650 files, **18,027 / 18,027 models byte-identical** between C++ and Python. That's 82,494 meshes, 555,052 strips and 3,853,568 vertices, with 0 parse failures.
- **Against the original engine** (windowed attract demo, ~75 s): every strip `Mdl_Draw` submitted to D3D was looked up in the C++ parse of that model, read from game memory.
  - 427 models: 5,141,227 exact matches, 118 matches after re-parsing (the game modified that data at runtime), **0 unmatched**, 0 parse failures.
  - 40,923 strips went through the engine's UV-rewrite scratch path and weren't compared.
- **Visual:**
  - `znken.bin` body parts render with coherent textures: shoes, coat, torn shirt with skin, green-blood wound variants, exposed chest.
  - Level chunks `st1_03` and `st3_01` show recognizable architecture (tiled roofs, walls). From an outside camera they're hard to judge in detail.

**Unresolved / not covered**
- 6 `.bin` files with no model header and no PRS (listed in formats.md).
- The flag-0x10 alternate vertex format (unused by shipped files).
- What modifies the 118 strips at runtime.
- How `TSP & 0x180000` maps to draw passes, and the exact PVR-to-D3D state conversion (0x004A7780). Not decoded yet.
- **Characters aren't assembled.** Parts are separate models; the hierarchy and animation (`mot/`) are the next dependency.

## Milestone 6: native D3D11 model rendering (2026-10-03)

**Code**
- `src/port/render/asset_store.*`: loads models and textures through the native decoders only (PRS, `ParseModel`, texture tables, `GetPaletteBank`, palette banks). Texture refs: ~~common.bin by index, everything else by global id~~. Corrected in Milestone 7 to the same-named texture file.
- `src/port/render/model_renderer.*`: D3D11 renderer (triangulated strips, point-sampled wrapped textures, alpha test at 0.5, lighting from the normals, no culling) plus off-screen PNG readback.
- `src/port/tools/model_viewer.cpp`: interactive viewer (orbit, zoom, PgUp/PgDn to step models, A for all) and a deterministic `--screenshot` mode.

**Verification** (`python tools/render_compare.py`)
- **6 / 6 cases pass**: common debris, two zombie parts, two level chunks, and an entire boss file (37,421 triangles, 484 textured meshes).
- In every case, triangle and texture counts match the reference exactly. Silhouette IoU is 0.951–0.989, and 1–23% of foreground pixels differ by more than 24 levels, all from point sampling on small or distant triangles.
- Side-by-side images were inspected by eye.
- Bugs found and fixed: HLSL default column-major cbuffer packing (now `row_major`), and the WIC PNG encoder needing BGRA.
- The interactive window starts and responds.

**Not yet matching the original renderer**
- Material diffuse/ambient/specular, PVR blend and pass state (TSP & 0x180000), strip cull-mode bits, the UV address mode 2 (mirror) path, fog, and lights aren't applied. The renderer currently matches the offline reference (texture colour plus fixed lighting), not the game's look.
- No scene or camera from game data yet; models are drawn in local space.

## Milestone 7: skeletons, motions, posed characters; texture-link correction (2026-10-03)

**Correction to Milestone 5:**
- Model texture fields are descriptor indexes into the **same-named texture file**, not global ids. Seen at runtime: faces rendered on Harold's thighs.
- Logged in-memory fields (`0x40000000 | id`) of 389 drawn models all equal the same-named table's descriptor ids.
- Fixed in `tools/pol_render.py` and `port::AssetStore`. The `pol_X → X` pairing for untabled files is inferred, with 80,529 of 80,689 indexes in range.
- `render_compare.py` now compares 4×4 block-averaged images, because the now-correct high-frequency textures (ivy, cobblestone) made raw pixel diffs meaningless.

**Animation:**
- Mapped 64 exe skeletons (static node trees with per-node model ids and offsets), the mot file layout (30 Hz frames, root position plus s16 XYZ angles per node), and the pose composition (`Rx Ry Rz T M_parent`, D3DX row-vector conventions).
- Found both draw paths: player-side `0x0040F520` and enemies `0x004110D0`.
- Spec in `notes/formats.md`.

**Code**
- `tools/skel_pose.py`: reference posing and rendering, plus `--dump`.
- `src/game/anim.{h,cpp}`: skeletons, motion files, `EvaluatePose`.
- `tests/anim_test.cpp`.
- `src/hook/anim_check.cpp` (`HOTD2_ANIMCHECK=1`).
- `model_viewer --skeleton T --motion M --tick K`: posed characters natively. Interactive: Left/Right step, Space plays.
- `port::AssetStore::ModelById`; the renderer takes per-item transforms and texture tables.

**Verification**
- `anim_test`: 64 of 64 skeletons; 6,028 C++ vs Python node matrices agree (worst relative error 4.7e-6).
- **Engine** (attract demo): **26,086 enemy node matrices in steady-state frames match `EvaluatePose` with 0 mismatches** (worst 1.8e-5). Blended frames differ as expected; they're counted separately.
- `render_compare.py`: **9 / 9**, including 3 posed characters (Harold, znassb, znnick), with matching triangle counts and 0–0.2% of blocks differing.
- Visual: correctly textured, plausibly posed zombies.

**Not covered yet**
- Motion blending and odd-tick node interpolation.
- The player-side skeleton path (not exercised by the attract demo).
- Per-node special cases in `Anim_NodeTransform` (slots 8/9) and the enemy angle sets.
- Which motions belong to which enemy (chosen by enemy AI code, not mapped).
- Scenes and stage layouts (placing level chunks and enemies), cameras (`cam/`), and the `evt/` event tables.

## Milestone 8: cameras + first faithful scene (title flythrough) (2026-10-03)

**Capture tooling** (ground truth from the original):
- `src/hook/scene_capture.cpp` (`HOTD2_SCENECAP=1`) logs per frame: the camera context, every `Cam_Evaluate` result, every model draw (id plus model-view matrix), and device view/projection (`HOTD2_SCENECAP_TRANSFORMS=1`).
- `HOTD2_SCENECAP_SHOTS=f1,f2,...` saves screenshots of the original at those frames.
- `HOTD2_KEEPACTIVE=1` keeps the game running unfocused by subclassing its window and dropping deactivation, so captures don't need the user's focus. An earlier capture had stalled for exactly that reason.
- The attract demo is deterministic: two runs gave identical camera values, with frames aligned within ±1.

**Format and code:** camera files, Hermite curves, look angles and view composition are traced (spec in `notes/formats.md`).
- `tools/cam.py`: reference implementation plus validation.
- `src/game/camera.{h,cpp}`: port.
- `tests/cam_test.cpp`: validates the port **against captured engine output**.
- `scenes/title_cam29.scene`: our own data file (ids only).
- `src/port/scene/*` and `scene_viewer.exe` (interactive playback at 60 fps, or `--screenshot` at a camera time).
- `ModelRenderer::DrawWithMatrix` (game view-projection, unlit).

**Verified**
- 2,730 of 2,730 engine camera evaluations reproduced.
- Static level chunks sit at identity under the derived view in 100% of single-camera, effect-free frames, which confirms the camera-to-view chain and placement against independent data.
- Native title-scene renders match original screenshots at 4 camera times; see the `scene_compare.py` numbers in formats.md, with controls.

**Hypotheses and gaps (not verified)**
- The title scene's content is taken from the capture, not yet loaded from the game's stage/event tables.
- The cloud layer's rotation rule is fitted.
- Camera shake and offsets (camera 30, 64 draws) and camera-transition blending (camera 42, 532 draws) aren't modelled.
- `Cam_EvaluateAngles` (0x004042D0) wasn't exercised by the capture, which has no evaluations in frames ~1309–2788.
- 2D overlays (logo, text, HUD sprites) aren't rendered.
- Textures are point-sampled where the original filters. Lighting and material state, fog and alpha blending modes are still not reproduced. The scene viewer draws unlit, matching the full-brightness look of the original stage textures here, but that's not verified per material.
- Particles (`stage1.bin`) and placed props (`komono_*`) aren't in the scene yet.
- Still open from earlier: animation blending; the two exceptional motion files (`komono_niwa.bin`, `komono_st2.bin`); the player-side skeleton path.

## Milestone 9: event VM foundation (2026-10-03)

**Traced** event loading and relocation, script selection, the VM loop, the opcode table, and the resource-request queue with its per-type step tables. Spec in `notes/formats.md`.

**Tools**
- **Event trace from the original:** `HOTD2_SCENECAP_EVENTS=1` wraps the 96 opcode table entries (writable data, no code patching) and logs `E frame opcode address next-words`.
- **Kept trace:** `build\scene_capture_events.txt`. The attract demo's events run only from frame 2788 (stage-1 demo); the title and instruction scenes are hard-coded.
- **Code:** `tools/evt.py` (reference, trace validation, resource list), `src/game/event.{h,cpp}` (`EventMemory`, `DecodeEvent`), `tests/evt_test.cpp`.

**Verified against the original's trace:** 917 of 917 opcodes at their traced addresses in the relocated `st1evtbl.bin` image; 105 of 105 sequential instruction lengths. The resource opcodes' arguments resolve to real files: enemy, prop, water and item `pol`/`tex`/`mot` files.

**Not yet**
- Executing the VM in the port (waits, camera opcodes, the list/spawn opcodes 07/09/0B/0C, environment channels).
- **Stage initialization**, which loads the level chunks, `stage1.bin` and `common.bin` (`Res_LoadStage` 0x00418490 and its tables).
- **Chunk visibility** (which loaded chunks are drawn when).
- Enemy/prop placement records.
- 12 of the executed opcode names are hypotheses (marked `?`).
- Opcode `0x30` blocks: the length formula is confirmed, but their purpose isn't.

## Milestone 10: data-driven stage scene + game material state (2026-10-03)

**Scene from game data**
- Traced the stage section tables and event ops 0x28/0x29, and the 0x30 command queue with its type-64 camera segments. Spec in `notes/formats.md`.
- `scenes/st1_demo_cam42.scene` builds the stage-1 demo from data:
  - level chunks from the section table (section 7, selected by `st1evtbl.bin`)
  - camera 42 from the script's type-64 blocks
  - sky models from its 0x50 requests
- `src/game/stage.{h,cpp}`; `scene` files gained `stage S section N`.
- **Verified:** all level-chunk draws of the original are in the selected section (`stage_test` 5,392 / 5,392). Camera blocks match the evaluations frame for frame.

**Materials:** decoded `Mdl_ApplyPvrState` and the two draw passes. `ModelRenderer::DrawGame` reproduces:
- per-mesh blend factors, depth function, depth write, alpha test (≥ 1/255 in the translucent pass), linear/point filtering, wrap/mirror/clamp addressing and alpha source
- opaque pass first, then a translucent pass sorted back to front

The scene viewer uses it by default (`--simple` gives the old path).

**Comparison with the original** (`python tools/scene_compare.py --mask stage scenes\st1_demo_cam42.scene 2820 2860 3100 3200`; shots and capture: `build\scene_capture_st1demo.txt` + `game\shot_*.bmp`):

| | Edge correlation | Colour difference (mean abs, 8×8 blocks) | Controls (t+60) |
|---|---|---|---|
| Game materials | **0.73–0.88** | 4.8 / 15.9 (frames 2820 / 3200) | −0.03–0.06 |
| Simple path | 0.46–0.74 | 20.8 / 23.7 | |

The title scene with game materials: 0.70–0.82 (previously 0.62–0.72).

The visible "dark band" in the first stage render was translucent overlay meshes drawn opaque. Fixed by the material state.

**Still missing in the stage scene:**
- props (`komono_*`, e.g. the clay pot), enemies, rain (`stage1.bin`), effects, 2D HUD and text
- D3D lighting and materials, fog, strip culling, per-model translucent sort
- camera holds between blocks (gameplay-driven waits)
- the sky rotation rule (still the fitted hypothesis)

## Milestone 11a: lighting (2026-10-03)

**Tracing:** found the game's two lighting setups (`0x004AA120`: a single directional light plus the `AMBIENT` render state; `0x004AA8B0`: a light table) and the environment light chain. Event op `0x18` sets pitch/yaw in the environment block `0x009A3540`; `0x0040E0B0` turns that into a world and view direction; `0x004AA0E0` sets the D3D light direction to its negation.

**Capture:** `HOTD2_SCENECAP_LIGHTS=1` logs device light state at each `Mdl_Draw`. In the stage-1 demo it's constant apart from direction: LIGHTING on, `AMBIENT` = `0xB2B2B2`, light 0 directional with diffuse 0.98 and ambient 0.3.
- **877 captured light states equal −(environment direction · view)** for op `0x18` (pitch −4608, yaw 2048).
- Enemies and characters use another light (1,313 states), not traced. Static models occasionally inherit it.

**Port:** `ModelRenderer::DrawGame` implements D3D7 fixed-function diffuse plus ambient lighting (material ambient = word 10 × diffuse, clamped, modulated with the texture; specular off). Scene files gained `light env <pitch> <yaw>` and `light_color`.

**Result on the stage-1 demo** (frames 2820 / 2860 / 3100 / 3200):
- Edge correlation 0.77–0.91 (unlit materials: 0.73–0.88).
- Mean colour difference **2.4 / 3.3 / 3.9 / 7.1**, against 14.6–23.0 for the simple path.

**Unverified:** where the light colours and ambient come from in code (captured constants only), whether `SPECULARENABLE` is ever on, fog, and the character light.

## Stage 3: integration (in progress, 2026-10-03)

**Shooting camera (agent, merged):** `game/shoot_camera.*` plus the event-runner changes.
- `event_runner_test` reproduces the stage-1 demo **without the oracle**:
  - ops 917/917, frames 812/812;
  - `Cam_Evaluate` calls 551/551;
  - rendered view bit-exact against W 0x009A6000 in 812/812 frames;
  - 12 camera/VM globals 812/812.
- Inputs are the previous frame's enemy dumps and live-enemy count. Control without inputs: 178/812.
- **FPU finding:** the game runs the x87 FPU at **24-bit precision** (Direct3D's FPU setup). add/sub/mul/div/sqrt round to float; fpatan/fsin/fcos don't. Camera code was switched to float precision (`cam_test` worst error 6.7e-6 → 4.6e-7). Other ports may need the same (±1 angle errors).
- Report: `notes/agents/shoot_camera.md`.

**World runtime (agent, merged):** `game/world*.cpp`, `mstack.*`.
- Deterministic native simulation of the stage-1 demo from the stage start. `world_test` against `build\scene_capture_stage3.txt`, frames 2786–3599 (only input taken from the capture: the first task serial):
  - rand state 814/814; rand calls (value, site, task) 814/814;
  - task list 814/814; event ops 814/814;
  - camera view 814/814; lock-on slots 814/814;
  - live enemies and attackers 814/814;
  - enemy hp/state and position 579/579 enemy-frames;
  - draws 805/814 frames (114,980 matrices ok, 9 just over the 2e-3 tolerance: rounding on large values).
- Covers walking-enemy behaviours 1–6 and 15, the full death motions, the scripted civilian (type 0x10), and 24-bit float math everywhere.
- Unported (reported by `World::Notes()`): collision meshes, enemy motion sounds, civilian extras, env interpolator and demo timer bodies.

**Rendering the world (coordinator):** scene directive `world <table> <start> <serial>` (`scenes/st1_demo_world.scene`) feeds every world draw and the camera to the renderer. `scene_compare --world` against the clean reference shots, 3D layer:

| Frame | 2820 | 2860 | 2900 | 2950 | 3000 | 3100 | 3200 | 3280 |
|---|---|---|---|---|---|---|---|---|
| Edge correlation | 0.911 | 0.919 | 0.933 | 0.937 | 0.933 | 0.883 | 0.873 | 0.879 |
| Colour difference | 1.4 | 1.6 | 2.0 | 1.7 | 2.2 | 1.4 | 1.8 | 1.5 |

Controls (world 60 frames later) are −0.01 to 0.31. Before (static scene, no actors) frame 3200 was 0.782 / 4.4.

**Skinned enemy attachments ported:** `game/skin.*` (definitions 0x0052ED08, vertex occurrence tables 0x0041A320, deformation 0x00419980 / records 0..3 per 0x004198B0) plus dynamic per-frame geometry in the renderer. `skin_test`: all 85 definitions' occurrence offsets land on vertex records, relative to the model block start. World comparison: frames 2900/3000 3D edges 0.933 → 0.937. Still not applied: the alt model select 0x004185E0. Traced: it is the normal submit with key bit 0x04000000, which makes `Mdl_Submit` set the **second light set** (0x004AA8B0: AMBIENT 0x007E8138, up to 16 D3DLIGHT7 at 0x007E7AA8 with enables 0x007E8128, multi-light flag 0x007E813C; reset 0x004AA650/0x004AA830, setters 0x004AA6D0/0x004AA6F0/0x004AA720/0x004AA780, reached through function pointers) instead of the environment light. Unused in the stage-1 demo: one light state in all 3,311 logged draws, and no alt draws in the world.

**Sound:** `scene_viewer --audio` (interactive) plays the world's sound commands through `game::SoundDriver` + XAudio2. Not yet checked by ear. `--dump-sounds` lists them: over the stage-1 demo, 150+ commands (P1/P2 gunshots GUN4/GUN5, blood, zombie and civilian voices, the script's splash/door sounds), all resolving to existing files.

**Captures for gameplay** (scripted input, from the stage start with tasks/rand/sprites/watch):
- `build\scene_capture_play1full.txt`: no reloads, attacked to game over.
- `build\scene_capture_play2full.txt`: reloads, kills, score to ~1700, lives lost at 2583/4059/4246, game over 4519.
- `build\scene_capture_attract_loop.txt`: full attract loop to frame 31000, including demo 2 (stage 2, scene 28, frames 15488–17230) and demo 3 (stage 4, scenes 5/7, 28043–29785).

**Gameplay HUD (agent, merged):** `game/hud.*` (library `game_hud`). Every 2D layout drawn during play is ported as a function of `game::HudState`: crosshair, ammo/RELOAD, lives/skip prompt, credit line, PRESS START, CONTINUE, GAME OVER, chapter card, route map, subtitles, mode select and the tutorial screens. Each `HudState` field names the address it mirrors.
- `hud_test` on `play1full` / `play2full`: 34,000+ frames exact, 5 differ (values the capture lacked).
- The new capture with those values (`build\scene_capture_play2hud.txt`) confirmed the assumed defaults (device mode 6, credits 5 with coin state 2, 0x009CA08C = 0). The 1,764-frame lives difference was a test gap: the capture lacked `9A32A0`, and the test now falls back to 0x009A5C40, which is equal.
- Remaining differences across the three captures:
  - lives, frame 903: the skip prompt is missing on the first frame after the cutscene starts; unexplained.
  - subtitles: one frame where the skip-watcher task 0x00435F20 runs after the subtitle task.
  - tutorial reload, frame 5918: passes once 0x007DD158 is captured.
- Follow-up capture `build\scene_capture_play2hud2.txt` (same script). It adds 9A32A0, E lines, and K/T lines for the skip-watcher 0x00435F20 and the subtitle tasks, for ordering the watcher against the subtitle task. Results unchanged: lives 2543/1, subtitle 548/1, tutorial reload 270/0.
- No score digits are drawn during play in this version.

**Game mode 6 in the world (agent, merged):** `world_player.cpp`, `world_actors.cpp`. `SetInput` / `MakeWorldInput`, mode-6 `StartStage`, `GameMode()`, per-frame `WorldHud`.
- `world_test --script` on play1, frames 901–3336 through game over: every logic check exact on every frame:
  - rand, tasks, event ops 2436/2436; camera 2435/2435;
  - player state / lives / score / ammo / aim 2436/2436;
  - enemies 1016/1016;
  - draws 2225/2251 rendered frames.
- play2 is exact until 1767, where scene 1 spawns objects not ported yet (komono kinds 7/9/38/39/40/44, actor types 33/24).
- Remaining draw differences: actor 0x22 rounding; the body shadow drawn one frame too long on the sub-mode 1/3 frame (cause unknown).
- Not ported: enemy motion sounds.

**Two players, continue, game over (agent, merged):** `world_gameover.cpp`, two-player join/continue in `world_player.cpp`, enemy root interpolation 0x00410920 with 24-bit float steps. Report `notes/agents/twoplayer.md`, symbols merged (40 new; 2 name conflicts kept as before, `notes/agents/symbol_conflicts.txt`). `world_test --script`:
- `build\scene_capture_2p.txt` (P2 joins at 1400, active at 2430): every check exact through 2964; at 2965 the scene-1 objects (not yet ported) diverge.
- `build\scene_capture_2p_continue.txt` (P1 continues with start at 4547-4560): logic 5099/5099 frames, camera 5098/5099 (frame 5562: one enemy body point, unexplained).
- play1 game over (mode 7, to the title): logic 517/517, camera 515/516, draws 296/302 (differences explained in `twoplayer.md`).
- Unexercised: the two-player rail midpoint. Still wanted captures: both players starting on the title, a two-player game over, a one-player whole-game continue countdown, one player's countdown running out while the other plays.

**Scene 1 (agent, merged):** `world_collision.cpp` (level collision meshes coli0/coli1: floor 0x004AAA40, sphere push, shots blocked by the level, collision-mesh objects), `world_scene1.cpp` (komono 7/9/38/39/40/44/69, door, breakable-prop items), `world_npc.cpp` (actors 0x24/0x25, rats 0x52), `world_enemy_beh.cpp` (behaviours 13/34/35/40/45, decapitation, child civilians, civilian penalties). Report `notes/agents/scene1.md`; 53 symbols merged (5 name conflicts kept, `symbol_conflicts.txt`). After merging with the two-player branch (one merge fix: the decapitation's neck blood now goes through `SpawnNodeBlood` so it follows the neck node), `world_test --script`:
- play2scene1 (reload run to game over at 4520): rand, tasks, event ops, enemies (2939 enemy-frames), players, aim 3620/3620; camera and gun record 3619/3619; draws 3564/3567 (two enemy nodes just over the 2e-3 tolerance, the mode-7 switch frame); mode 7: 77/77.
- 2p and 2p_continue: every logic check 5099/5099 (before: 2p diverged at 2965); draws 1996/2018 and 3392/3415.
- stage-1 demo and play1 unchanged.
- Capture artifact: the hook's model-id cache (`scene_capture.cpp` ModelId, pointer -> id) logs a model loaded at a freed model's address under the old id (3958 as 672, 5467 as 47); world_test reports these as aliases. Hook fix pending.
- Not ported (in `World::Notes()`): enemy motion sounds, civilian voice, actor 0x25 commands 9-12/16/17, actor 0x24 variants 1-5, behaviour 35 jumps, item slots 1/3/8, water splash 0x00408740.

**Auto-play captures (agent, merged):** `src/hook/auto_play.*`, `HOTD2_INPUT_AUTO=1` (aims at the hit system's own target list 0x0059D8E8, head first, never at civilians; reads game memory only, no rand). It logs the applied input to `game\input_auto.txt`, which replays exactly (`HOTD2_INPUT_SCRIPT`: Z/K/Y/N/E/U lines identical over 14,800 frames). Scripted input, not device input. Report `notes/agents/autoplay.md`.
- `build\scene_capture_auto_st1.txt` + `build\script_auto_st1.txt`: all of stage 1 (route scenes 1 @903 ... 9 boss @9326, boss tasks 9476..14657), no lives lost, stage 2 at 14660. `build\script_auto_full.txt`: a whole game to stage 3's serpent boss (game over ~45610).
- world_test on it: exact through 3606 (rand to 4866); first gaps: sound commands at 1344, players' angles 3229, actor 0x25 script op 9 at 3607, civilian rescue bonus 5488. Agents now on stage 1 scenes 2-9 + boss, and on stage 2 (mid-game stage start from a capture).

**Menu modes 3 (agent "menus", branch):** training lessons 3 / 4 / 7 / 8 / 9 exact and 5 / 6 logic-exact against new captures (komono 68, the mount alone = actor 0x23 variant 2, actor 0x20 / 0x21 / 0x31 type-2 branches, vehicle motion 9 = the juggled target), Boss mode bosses 1 / 3 / 4 / 6 exact and 5 logic-exact (stage-3 boss mode-1 tasks, vehicle motions 5 / 8), special weapons 0x00414B90 (+ reload 0x00414E40, tracer extras), story modes on the original's frames (mode-11 title card 0x00434DA0, actor 0x50 captions, the parked car), attract demo 2 exact (held object throw 0x00442B90 state 5 -> 0x00443DC0), komono 73 / 75 / 76, the network lobby without a network (mode 15), play.exe menu tour / ranking save verified end to end. New capture options HOTD2_NOSAVE (no save writes) / HOTD2_POKE (+ frontend_test / play --save-patch). Open: lesson 2 (komono kind 0, stage-route object), boss 2 (stage-2 scene 39 actors 0x15 / 0x17), boss 7 (stage-5 boss camera precision), story-mode NPC actors. Details `notes/agents/modes.md` section 8.

**Menu modes 2 (agent, merged 2026-10-04):** Boss mode end (stage end 0x004603B0 types 2 / 3, boss-time name entry via the arcade name entry with the boss table 0x00480F70, forbidden names, save 0x0041C610, back to the select, rush), Training (lesson select actor 0x6D, lesson flow 0x00497360..0x00497DB0, grades 0x00498ED0, lesson actors 0x1A / 0x1B, type-2 branches), Original mode (item select actor 0x6E, items 0x0048A0D0 / 0x00415FE0 / 0x0048A140, damage factor, lives cap / magazine, pickups and drops), bosses outside stage 1 in Boss mode (in-scene stage switch). frontend_test, every logic check exact: boss0 7159/7159 (to 7999), train0 6159/6159, train01 5159/5159, orig 14199/14199 (to 14999). Open: special weapons 0x00414B90, training lessons 2-9 (no capture), the name-entry letters / panels not visible in play.exe (draw list matches: renderer / asset side), bosses 2-4 in Boss mode reach unported paths. Details `notes/agents/modes.md` section 7.

**Renderer: per-strip cull mode (coordinator, 2026-10-04):** Mdl_Draw 0x004A7EF0 sets D3DRENDERSTATE_CULLMODE per strip from its control word (table 0x00598B20 = {none, none, CCW, CW}, bit 0x80 keeps the current mode). The port drew everything with culling off; the name-entry screen (boss mode and the arcade ending) sits the camera inside a room model (etc_2.bin, model 56) whose outward-facing walls then hid the book and letters. Meshes are now split into runs by cull mode and both backends cull (D3DCULL_CW = D3D11 / SDL_GPU front faces). Stage renders vs the reference shots unchanged (6.02 / 14.22 / 25.42), render_compare 9/9. `HOTD2_CULL_OFF=1` disables it for comparisons. Not modelled: the cull state carried over from the previous model (a model starts with "none").

**Texture residency / sky culling (agent texres, 2026-10-05):** Attract 8100 / 20700 (story mode 11): the original culls the sky 6047 completely; the listed skies 6048 / 6049 / 6307 / 6309 are visible only because Res_LoadModel flips their strip cull bits in memory (0x00419270). The renderer now applies that flip per draw (`WorldDraw::cullFlip`: listed model loaded on its own, slot state 4; listed models always where slots are not tracked) instead of a det(world) rule. Texture residency itself (formats.md "Residency"): model textures come with their model, so a drawn model always has them; texture-file requests (ops 0x54 / 0x55) only gate sprites (Spr_Draw needs file state 4), now tracked in the world (`texFiles`, game modes 5 / 6 / 10 / 11, Stage_Start / story / game-over / name-entry loads, stage-clear 0x16A, item caption files 0x0048CBF0 / 0x0048DEE0) and applied in `Scene::MakeSpriteDraws` (`HOTD2_NO_TEXRES=1` turns it off, `HOTD2_PLAY_TEXLOG=<file>` lists skipped sprites); Res_LoadCommon's bilinear edit of common.bin 2353..2363 (0x004192C0) in AssetStore::ModelById. Sweeps: attract 1.63 -> 0.98 (8100 14.4 -> 0.8, 20700 19.4 -> 0.7), p0 30600 / 30900 and p150 30450..31050 and 2P 29100 / 29700 slightly better, no frame worse; world_test / frontend_test suite identical; render_compare 9/9. Hook: `HOTD2_RS_OVERRIDE=<model>:<frame>:<state>=<value>[/...]+...` (device render states around a model's strip draws in one frame), R lines now end with `| cache <0x007E7914>`, `HOTD2_SCENECAP_RSTATE_ALL=1` (every strip, no dedupe), `R!` lines when DrawPrimitive fails.

**State after the parallel rounds (2026-10-06):** every logic check (rand, tasks, events, camera, enemies, players, lock-on, sounds) is exact on every capture of the regression suite.
- Coverage:
  - every arcade route scene (stages 1-6; endings 4 / 5 / 6; game over in stage 6);
  - whole two-player games;
  - a whole Original-mode game (item select → ending → title) and its type-1 branches;
  - Training 2-9, Boss mode 0-8, weapons, story modes, attract demos, the network lobby.
- Command lines: routes.md sections 4 / 5, modes.md 8.2 / 9, twoplayer2.md, misc2.md, orig3.md, draws2.md.
- Draws: 98-100% of frames per capture.
- Screenshot sweeps (mean block colour vs the original): phase 0 1.40, phase 150 2.31, attract 0.98, 2P 1.35.
- Known not-reproducible:
  - the original's wall-clock resource loading: the idle callback 0x0049E730 makes single-frame residency differences at load points;
  - reads of uninitialised or freed memory: fish blood-pool yaw, stage-1 boss dust ring, final-boss trail history, stage-4 boss wound mark 0x00492210 after the boss block is freed.
- Bugs of the original reproduced exactly:
  - the stage-5 boss stagger soft-lock (2P);
  - the Boss2 rematch walking home along the arena wall if hit during its walk-in.
- Sky culling, corrected: the original flips the strip cull bits of models 6048 / 6049 / 6307 / 6309 when Res_LoadModel loads them alone (0x00419270, list 0x0057A280). The earlier det(world) rule is gone.
- Still open: the user's physical two-device / light-gun test; the mobile port. (Original mode closed by orig5: notes/agents/orig5.md.)

**Port features for play.exe / hotd2.exe (2026-10-06; all behind WorldConfig::portFeatures / portVolumes / portModernMenu, off in every exactness test):**
- hotd2.exe: the GUI build (no console), the original's icon from the local Hod2.exe at configure time.
- Pause menu in the options screen's look (`src/port/play/pause_menu.*`, `game/menu_text.h` shared with the options screen): Resume, Music / Gun Shots / Sound Effects volumes (per-category gain in both audio backends), Display, Return to Main Menu with Yes / No (`World::RequestTitle`).
- Options screen: the volume rows after the sound tests and a Display Settings row (rows 11..14; Default resets the volumes).
- Display settings (`src/port/play/display_menu.*`, GpuDevice present pass in both backends): fullscreen, window scale, 4:3 / stretch, CRT scanlines, render scale 1..4x, Main Menu modern / classic. In a game the Display page shows the paused scene (dimmed by the original's overlay) so changes show live.
- Settings file `hotd2re_settings.ini`: volumes, `input=` (touchpads: mouse), display keys, `assets=` (asset pack). (The `menu=modern|classic` key and the Display page's Main Menu row were removed on 2026-10-06: the modern menu is always on in the port.)
- Hitches with the asset pack (2026-10-06, user report: the frame froze every few seconds while changing settings in a game): every pack texture was decoded on the main thread the first time it was drawn, by a bit-at-a-time inflate (headless frame-2500 render: +1.5 s with the pack). Now `png_read.cpp` uses a 9-bit lookup table, a 64-bit bit buffer and direct writes (bit-exact vs Pillow, `png_test` 447/447; ~4x faster), and `AssetStore::PrefetchFile` decodes a loaded texture file's overrides on up to 4 worker threads as soon as `texFiles` shows the file loading (play.cpp), the main thread taking the result (or waiting for one in flight / decoding a still-queued one itself). 2026-10-07, second report (the leeches from the gutter froze the frame): `texFiles` only covers sprite files; the model textures come with their pol files, so `WorldHud::polFileLoads` now reports the queue's pol-file / model loads (ops 0x52 / 0x50) and the section objects of a loaded stage / title table (`NoteSectionPolFiles`), play.cpp prefetches those files, warms the front end at boot and stage 1 at the mode select, and uploads prefetched images to the GPU four per frame. Windowed scripted run (`--run-frames 5200`, HOTD2_PLAY_TIMING): before, 7 frames of 100-970 ms through stage 1; after, 2 frames of 30 / 46 ms. Peak working set with the pack: 636 MB through stage 1, 1082 MB over a full headless game (script_auto_full, 45622 frames; the pack's raw 4x textures are up to 176 MB per file, 8 GB for everything, and decoded textures are never evicted). play.exe / hotd2.exe link with /LARGEADDRESSAWARE (4 GB). Future work if memory becomes a problem: evict a file's textures (CPU and GPU) when `texFiles` drops it, as the original does.
- In-game Display page (2026-10-06): the paused scene behind it is no longer dimmed by the pause overlay 0x93E (user request: see the scene clearly while changing settings). The pause menu's own rows keep the options screen's tiled background.
- Main-menu background (2026-10-06): `tools/menu_background.py` composes the pack's `bmp\hod2title*.bin.png` from a stage-1 screenshot (frame 2424, the first two zombies) plus the title logo / SEGA marks. Fixed on the way: `FrontScreens::Load` looked for `bmp/tex/<file>.png`, the pack writes `bmp/<file>.png`, so no BMP override had ever matched.
- Original-mode item select: back to the mode select with reload / Backspace.
- Scene Select: mode-select entry opening a boss-select-style page (`world_scene_select.cpp`) that starts an arcade game at any chapter.
- Modern main menu: an even left-hand list, the baked labels in-painted out of the background at load (`FrontScreens::CleanMenuBackground`), pointer selection.
- Asset remaster pack (`tools/remaster.py`, notes/agents/remaster.md): all textures 2x / 4x per class, audio resampled, loaded through `HOTD2_ASSET_DIR` / `assets=`; the pack lives in the ignored `remaster\` folder.

**User-reported play.exe bugs (2026-10-05):**
- **Black sky after per-strip culling:** the dome (Sky_Task, S(1.2,1.2,-1.2)) is drawn by the original with CULLMODE CW and stays visible, although its triangles are clockwise on screen under the captured VIEW diag(1,1,-1). Measured in the running game with the new hook diagnostic `HOTD2_SCENECAP_CULL=<frame>` (V lines: device CULLMODE plus WORLD / VIEW mirror flags per model at Mdl_DrawStrip / Mdl_DrawList). In effect, mirrored WORLD matrices are culled as if they were unmirrored. The port swaps CW / CCW when det(world) < 0. **Corrected (texres, 2026-10-05):** D3D culls by the true screen winding; the listed skies (0x0057A280: 6048 / 6049 / 6307 / 6309) have their strip cull bits flipped in memory by Res_LoadModel (0x00419270, single-model loads only), and that is what the port now models (`WorldDraw::cullFlip`, no determinant rule). The sky 6047 is not flipped and the original culls it completely (attract 8100 / 20700 fixed). Title scene_compare colour difference is back to 2.2 / 3.9 / 4.3 / 3.7 (36-65 with the bug); render_compare 9/9. Mirrored draws in the game: skies 6048 / 6049, the opening car's wheels, common.bin k14-24 at 4284. The cull-mode cache 0x007E7914 never resets, but the port's per-model start state never mattered for these draws.
- **CONTINUE? 9 left on screen** (agent): play.exe drew it from 0x009A2BB8, which keeps its value after a continue. The HUD now records what Continue_ScreenTask 0x00460530 / the per-player prompt 0x00414280 actually draw each frame. New world_test / frontend_test check "CONTINUE sprites": auto_st3 600/600.
- **Camera stuck after a leaping zombie's lunge** (agent): actor 0x31 behaviour 8 (wait for a free target, 0x0044B3E0) was a no-op, so the waiting leaper walked away through the walls and Cam_TargetLookAt followed it. Behaviours 8 / 11 / 17 ported. New capture `build\scene_capture_leaper_wait.txt` + `script_leaper_wait.txt`: all logic checks exact 901..8799.
- **Chainsaw zombie stuck in the doorway** (agent): enemy behaviour 14 (walk out, 0x00457160) not ported. It is on the no-rescue route (scene 1 → 10 → 3), which no earlier capture took. New capture `build\scene_capture_norescue_st1.txt` + `script_norescue_st1.txt`. It diverges at 6045: actor 0x11 and komono kinds 0 / 6 are not ported (alternate-routes agent).
- **Invisible leeches** (stage 1 scene 3, cellar window, ~6898..7160): they are actor 0x40 ("bats" in the code, group type 0, mol.bin models). Their skeleton draw (second animation system 0x0040EAE0 / 0x0040F4F0, vertex deformation 0x0040F960) is not ported, so only their shadows 0x10D0 show; the stage-2 bats likewise. Agent running.
- The garden urn in stage 1 scene 1 is level geometry, not a breakable prop. The original's task-drawn props there are a crate, a door and two gates only.
- Parallel agents (2026-10-05): bats / leeches draw, alternate routes, lighting / env interpolator / overlays plus a whole-game screenshot sweep, menu modes, draw exactness, sounds / NPC leftovers. Rules: `notes/agents/coordination.md`.

**ONE CONTINUOUS NATIVE GAME = THE ORIGINAL (stage3c, merged 2026-10-04):** `world_test game build\scene_capture_<X>.txt --script build\script_auto_full2.txt --prefix build\scene_capture_auto_st1.txt` with X = auto2_st3boss / auto_st4 / auto_st5 / auto_st6 / auto_st6_end: every logic check AND the camera exact from the stage-3 boss fight through stages 4-6, the final boss, the ending and name entry to the title (97157); the earlier part (same input to 38081) is exact via auto_st1 / st2 / st3. Root causes: 24-bit x87 matrix math in the original's operation order (Translate / Transform / both multiplies / angles / blends / normalize), the skeleton chain also built in model-view from the active view (hit points, body point), player cutscene callbacks 0x00415E40 / 0x00415F60 / 0x00415F90, vehicle-frame civilian targets, lock-on +0x121 = 0, credits cleared at the ending's title switch. Draw matrices still differ in places (draws come from the world-space chain). Report `notes/agents/stage3c.md`.

**Menu modes (agent, merged, stopped early for a machine shutdown):** map in `notes/agents/modes.md` (Original = game type 1, Training = game type 2 / trnevtbl.bin, Boss = game type 3 / boss select 0x00425010, Rankings = mode 8, Options = mode 12, Network = mode 15). Save data: one 0xF4C-byte block scrambled into four "data" files (pol\bg_adv19.bin, tex\scr_tod_itm_itamidome2.bin, pol\komono_0.bin, pol\tv2.bin); the port reads them read-only and writes its own `hotd2re_save.bin`. Ported + exact vs captures (frontend_test): Rankings (2410/2410 sprites, draws 2060/2060), Options (2101/2101), Boss mode select + the stage-1 boss fight (rand / tasks / events / camera exact). play.exe: every menu entry starts its mode; Training / Original stall at their (unported) select screens; boss / training stage end, name entry, other bosses, special weapons / items: next (modes.md section 6). Captures `build\scene_capture_modes_*.txt`; script d-pad bits 8/16/32/64 (script lines only).

**Stage 6, final boss, ending (agent, merged) - the arcade game is complete stage by stage:** stage-6 objects (lift, bridge, slide, debris / figures / flames, enemy kinds 9 / 0x12, behaviour 8), the final boss (actor 0x2D variant 1, 8 orbs, the second-phase creature), the ending (mode 6 sub-states 7..10: event mode 9 ending stage, staff roll, results, THE END, name entry with rank insert) to the title. `world_test ... auto_st6 --stage-start 70806 --civilians 0,1`: every logic check exact 70806..86999; with `--prefix build\scene_capture_auto_st6.txt` on `scene_capture_auto_st6_end.txt`: exact 86900..97157 (title on the original's frame), camera 10257/10257. New WorldCarry civilian counters, world_test `--vfrom`, `--civilians`. Not ported: ranking save 0x0041C610 / 0x0041C490, forbidden-name check 0x00481FB0, creature patterns, game type 3. Report `notes/agents/stage6.md`, 97 symbols merged.
- **Gap found by a continuous run of `script_auto_full2.txt` from the game start:** it game-overs in the stage-3 boss fight (38082..44107 of that script was never captured; the stage-3 boss port came from the old script, where no head took damage). Capture requested (`build\scene_capture_auto2_st3boss.txt`); every other part of the game is verified stage by stage.

**Stage 5 complete, captures of stages 5 / 6 / ending (agent, merged):** `scene_capture_auto_st5.txt` (55900..70899), `scene_capture_auto_st6.txt` (70700..86999), `scene_capture_auto_st6_end.txt` (86900..97299, the ending and the title switch at 97200), one deterministic replay of `script_auto_full2.txt`. Ported: actor 0x2B lights, actor 0x2D variant 0 + skeleton-0x4C attachments (`Kind4CTask`, world_st5.h; the final boss is 0x2D variant 1), the car / wreck (0x0048F190), riding enemies, behaviours 10 / 32, the Judgment rematch variants, the stage-5 boss (actor 0x32 -> 0x0047C960, 12 states, damage on node slots 4/6/11/13, projectiles 0x0047EFA0), sky types 8/10/11. `world_test ... auto_st5 --stage-start 56007`: every logic check exact 56007..70899 (camera 14669/14893); `--prefix` now combines with `--stage-start` (stage 4 run into the stage-5 capture: exact 55900..70899). auto_st4 is now exact on every check including the task list (11993/11993). Report `notes/agents/stage5.md`.

**Stage 4 complete (agent stage4b, merged):** the boss (actor 0x19: ctor 0x004917E0, task 0x004919D0, 24 states 0x00597298; damage on node 2 or a material-0x3D mesh hit), carried objects (world_st4_held.cpp, `world_held.h`), vehicle motions 2/3/4/7/9, node-mesh hits with the previous frame's model-view x the current camera, gameplay camera 0x00403710 clears the targets taken, SpawnRecord params 0x50 bytes. `world_test ... auto_st4 --stage-start 44107`: every logic check exact 44107..56099 (stage end, in-run stage-5 start at 56007 included); tasks 11902/11993 (stage-5 actors 0x2B / 0x2D left to the stage-5 agent); draws 10438/11809. Report `notes/agents/stage4b.md`.

**Auto-play through the whole game, stage 4 started (agent, merged):** auto_play.cpp targets boss weak points (stage-3 heads only on the head node while the jaw is open, Boss2 node 1, stage-5 boss nodes 4/6/11/13, final boss node 1 via the 0x00428F00 point, orbs first; skeleton walk to type 85, depth 64). `build\script_auto_full2.txt` finishes the game with no continue (identical to script_auto_full to 38081): stage 4 Stage_Start 44107 (boss 48032), stage 5 56007 (Boss2 rematch 62071, boss 0x0047C960 65136), stage 6 70806 (heads 74409, final boss 76610, down 84086), ending ~87000, title 97200. Captures: `scene_capture_auto_st4.txt` (44000..56099), `scene_capture_auto2_k.txt` (K/Z/E/U/W 43500..79699). Hook: HOTD2_INPUT_AUTO_FROM, HOTD2_INPUT_AUTO_DEBUG, HOTD2_SCENECAP_ALLOCS (A lines = every Task_Create). Stage 4 (`--stage-start 44107`, no new carry): behaviours 17 / 30 (+ generic jumps), actor 0x46 v0, actor 0x31 beh 18, komono 75, node slots 16..31 for shots; exact 44107..47354 (next: weapon-mesh hit test 0x004048A0 uses the previous frame's model-view x the current camera). Thrown weapons (behaviour 33, coordinator): attract demo 2 rand exact to 17189. Report `notes/agents/stage4.md`.

**Front end (agent, merged):** `World::Boot` + the game-mode machine (Game_UpdateMode 0x004608A0, Mode_Commit 0x0040E860, next-mode table 0x004E1014): boot logos (16), title (3), mode select (4, bitmap menu + credits), attract demos (5), story modes (11 / 10), how-to-play (9), game (6), game over (7) back to the title; `port::FrontScreens` draws the bitmap screens. play.exe now boots like the original (`--direct` = old behaviour). `frontend_test` from boot: play1full mode switches on the original's frames (3@605, 4@701, 6@900, 7@3336, 3@3853, 9@4334), title 481/481 (tasks, rand), mode select 199/199 sprites, game 2436/2436, how-to-play 165/165; 2p 5099/5099 from boot. Not exact / unported: menu entries for Original / Training / Boss modes, Rankings, Options, Network (selectable, noted); mode 11 actor 0x00434DA0 and mode 10 actor 0x00499A80 (story modes end 20 frames early); demo 2 (thrown weapon 0x0045A4F0) and demo 3 (stage 4). Report `notes/agents/frontend.md`, 61 symbols merged (Mode_Select, Mode_HowToPlay renamed).

**Stage 2 complete (agent stage2c, merged) and continuous runs (coordinator):** the rest of stage 2 (enemy kinds 2/3 with weapon meshes, behaviours 20/31, shots vs node meshes 0x004048A0, civilian op 0x10), the boss (actor 0x14, all 21 states; damage only through the chest opening, hit test with the previous draw's node matrices), water level actor 0x16, komono 18, fish jumps, actor 0x25 command 17, event op 0x2B hit-rate bonus. `world_test ... auto_st2 --stage-start 14659`: every logic check exact 14659..29799 (camera 15076/15141). Report `notes/agents/stage2c.md`, symbols merged.
- `world_test --prefix <capture>` runs a capture's game start uncompared up to the main capture's first frame, so a later stage reached inside one run (ChainStage) is checked. It found the last in-run difference: the level collision files were loaded once per World (coli1.bin kept in stage 2, so walls did not block shots); they are now reloaded per stage, and per-stage globals (push object, boat / vehicle / bat / bird pointers and indices, fish slots, water level) are reset at Stage_Start.
- **One continuous native run from the game start (901) matches the original on every logic check through stage 1, stage 2, stage 3, the stage-3 boss, five continues, game over and the route map to the title switch (46375)**: `world_test game build\scene_capture_auto_st2.txt --script build\script_auto_full.txt --prefix build\scene_capture_auto_st1.txt` (14400..29799 exact) and the same with `scene_capture_auto_st3.txt` (29700..45612 + game over 45613..46375 exact). play.exe follows the same path (`--serial 65551` aligns task serials with the captures; not needed for the logic).

**Stage-3 boss, continues, game over (agent, merged):** the serpent boss (actor 0x45: intro head / body, five heads with neck chains, rocks, name card 0x00424900, sparks), actor 0x44 state 14, mode-6 sub-states 0xB / 0xC (no credits) into mode 7, lock-on +0x121 = 0 for birds / heads, model-view chains for head hit points, 24-bit 0x00401790. `world_test ... scene_capture_auto_st3.txt --stage-start 29736`: every logic check exact 29736..45612 (stage 3 + boss + 5 continues) and 45613..46375 (game over + route map, to the title switch); camera 15780/15876 (civilian alignment ulp at 32426, idle-motion odd-tick angles); draws 12291/15696. Report `notes/agents/stage3b.md`, 26 symbols merged.

**World 2D in play.exe (coordinator, 2026-10-04):** `WorldHud::sprites` carries the 2D sprites world tasks submit: the boss health bar (0x00435C80 -> retained list 0x0041C800, flags | 0x700), the boss name card titles (0x00437AC0, fade from tick 0x50; capture: id 0xBA at (310, 84), 10362..10581) and the stage-clear screen (0x00434EF0: 17 background tiles, the view-space result text, rescued / bonus counts, score digits 0x004362E0, hit rate 0x00436620). Actor 0x62 (0x00435930) now loads pol 0x7C (the result text, ids 0x165F..) and the rescued civilians' pols, so those draws pass the residency check: on `scene_capture_auto_st1.txt` the clear task draws its 32 models per frame like the original, and matching draw matrices rose 1,761,805 -> 1,777,134 (bad 57,726 -> 42,397). `WorldHud::subtitles` (live voice-line tasks 0x00435AA0: group / line) are drawn with `game::SubtitleSprites`; `WorldHud::titleSerial / titleChapter` (actor 0x60) drive `game::ChapterCard` in play.exe (sprite counts match the capture at 1180 / 1250).

**Stage 3 (agent, merged 2026-10-04; includes the stage-2 agent's work up to its last merge):** capture `build\scene_capture_auto_st3.txt` (29700..46999); `world_test ... --stage-start 29736`: rand / tasks exact 29736..38081 (to the stage-3 boss, actor 0x45, unported), camera 1-2 angle units off from 32523 (civilian alignment ulp). Ported: players' boat, water surfaces, floating crates, wakes / splash rings, fish (0x51), the vehicle (0x13) with riders, flocks (0x46), behaviours 24/38, actor 0x20 v2, actor 0x44 state 11. Report `notes/agents/stage3.md`; its 66 proposed symbols are NOT yet merged into symbols.csv.
- Stage-2 continuation (agent stopped at the usage limit; its WIP committed and merged, deduplicated against stage 3: one wake 0x00408740, box walk 0x00449020, splash ring 0x004567C0 and behaviour 33/37 port each): bats (actor 0x43), komono 8/19/21/30/37 falls, actor 0x24 wait, actor 0x33 states 1/5, behaviours 11/29, death-jump variants 4..6. Current: stage 2 (`--stage-start 14659`) exact to 22126, stage 3 (`--stage-start 29736`) to 38081 (boss: actor 0x45), stage 1 901..14799, older captures unchanged. stage2b / stage3 symbols merged.

**Stage 1 complete (agent, merged) and the in-run stage switch (coordinator):** scenes 2-9, the boss (rider actor 0x22 variant 1 + mount actor 0x23, name card, health bar), the stage clear (actor types 0x61 / 0x62) and the stage end (0x0045ED90, mode-6 sub-states 5 / 6); bats, held objects (behaviour 37), civilian attachments, komono 31/52, actor types 0x12 / 0x28 (`world_st1b*.cpp`, report `notes/agents/stage1b.md`, 87 symbols merged, 12 name conflicts kept, 2 hypotheses renamed: Boss1_NameCard, Stage_ClearTask). The next stage now starts inside the same run: `World::Impl::ChainStage` (world_st2.cpp) builds the carry from the world's own state (`MakeCarry`), reloads the stage table (`LoadStageTable`, Evt_LoadTable 0x00413160) and runs Stage_Start on the sub-state-1 frame. `world_test ... scene_capture_auto_st1.txt --script build\script_auto_st1.txt`: rand, rand calls, tasks, event ops, camera, sounds, players, aim, enemies **13899/13899 frames (901..14799), i.e. all of stage 1 and the first 140 frames of stage 2 in one run**; players' angles 13890 (one-unit rail rounding at 3843); draws 11511/12787 (actor 0x22 rounding, capture model-id aliases). play.exe now plays from stage 1 into stage 2 (headless with `script_auto_full.txt`: score 18210 at the stage-1 end, as in the original). Not drawn yet: boss bar / name card sprites, stage-clear result models.

**Stage 2 (agent, merged):** mid-game stage start: `WorldConfig::carry` / `ApplyCarry` (`world_st2.cpp`), `world_test --stage-start F` reads the carried state from the capture (player state 5 -> 2, lives, score, rank and its timer, route record, hit timers, komono-40 "unbroken" bytes set once per game, never-reset tracer rolls; full table in `notes/agents/stage2.md`). Capture `build\scene_capture_auto_st2.txt` (14400..29799, replay of `script_auto_full.txt`; hook options `HOTD2_SCENECAP_FROM`, `HOTD2_SCENECAP_MODELS=0`). Ported: rider + vehicle, leaping zombies (actor 0x31), actors 0x27 / 0x53 / 0x33 / 0x44 stage-2 states, komono 35/37/40 v1-5/66/70/77, behaviours 21/27, 32 enemy node records (skeleton 0x1A has 19 nodes). `world_test ... --stage-start 14659`: exact 14659..17271 (next: bats, actor 0x43). Regressions unchanged after the merge (play2scene1 3620/3620 logic, 3564/3567 draws; play1, 2p, demo as before). 82 symbols merged (5 conflicts kept). Now running: stage 1 (scenes 2-9 + boss), stage 2 from the bats on, stage 3 (capture + start + port).

**play.exe vs the original's screenshots:** play.exe's script path lagged one frame and skipped the start frame's input, so its replay diverged. It now uses world_test's input logic and the captures' seed (902) by default (`--seed`). With `script_p1_reload.txt` it reproduces the original's player state at 2000 / 2300 / 3100 exactly, and the renders match the clean shots `build\ref\play2scene1_*.bmp` closely (mean abs pixel difference 6.9 / 14.2 / 25.4; visible differences: the credit line (free play vs 5 credits), shot-flash brightness).

**HUD additions (coordinator):** state-10 "please wait" 0x8E9 (0x00414690), state-6 GAME OVER 0x43E outside mode 7 (0x00416900), PAUSE label (0x0040E500). `hud_test` now handles pause: no player task runs, and the HUD blink counter 0x005A4D44 holds (traced: it is only updated by PRESS START). 2p / 2p_continue: wait 517/517 each, pause 2900 / 1500, credit lines exact (2 differences before). Remaining 2p differences: P1 crosshair one frame early after P2 joins (6 / 7 frames, test input model), the rejoin frame 4561.
- PAUSE in play.exe: the pause function sets the near plane to 0.001 (0x004194C0) before the label; with the gameplay near plane 0.8 the label was clipped. Not drawn: the dark overlay (model 0x93E, alpha 0.5). Unverified: whether the original shows the frozen scene behind it (no screenshot).

**play.exe (coordinator): playable stage 1, opening area.**
- Raw-input devices (first trigger joins P1, a second device P2; left button trigger, right reload, middle start), mode-6 world, gameplay HUD, `--audio` sound.
- The world's game-over sequence runs (`cfg.gameOver`): CONTINUE countdown, mode 7 with the "GAME OVER" sprite effects, the route map, then a new game when the original would return to the title. A trigger pull on an idle player presses start (P2 joins mid-game). PAUSE label drawn; the pause overlay model 0x93E, state-10 "please wait" and per-player GAME OVER sprites are not.
- Renderer fix: frames with sprites but no 3D models (the GAME OVER letters) were skipped entirely.
- Headless script replay (`--script --screenshot --frame`) shows, at the original's frames:
  - 1500: the intro with letterbox and HOLD YOUR FIRE!;
  - 2600: the zombie attack with RELOAD prompt and torches, 2 lives;
  - 3100: CONTINUE? 9;
  - 3620: GAME OVER; 3760: the route map.
- Playable through scene 1 (library, door, decapitations); the headless reload run reaches 3100 with score 1560. Beyond scene 1: untested. The port's config is free play (the original run had 5 credits).
- **User test, 2026-10-03, one physical mouse:** crosshair follows the pointer, reload works, sound is fine, speed is correct. This needed two fixes: a fixed 60 Hz clock instead of one frame per display refresh, and a relative mouse aiming at the Windows pointer. **Still pending:** two physical devices at once (independent aim, P2 join), and light guns.

**Next captures and agents (running):**
- `build\scene_capture_play2scene1.txt`: reload run, frames to 4599, full dumps of the scene-1 task functions. The scene-1 objects agent works from it.
- `build\scene_capture_2p.txt` and `build\scene_capture_2p_continue.txt`, scripts `build\script_2p*.txt`:
  - P2 presses start at 1400 and becomes active at 2430 (state 10 in between);
  - in the continue run, P1 continues with start at 4547–4560;
  - used by the two-players / continue / game-over agent (merged, above).

**Coordinator:**
- **Renderer:** cached per-model geometry; fitted per-mesh visibility rule (pass-1 lists 41/43 stage, 95/95 title).
- **Blink phase traced:** boot frame counter.
- **Port gun input glue:** `src/port/play/gun_input.*` (device → mode-6 gun record → `Player_Trigger`), `gun_input_test`. Physical devices untested.
- **Scripted input for captures:** `HOTD2_INPUT_SCRIPT` in the hook; same writes as a joined device; *not* device input. Used to record real gameplay:
  - `build\scene_capture_play1.txt`: mode-6 game, no reloads. Two enemies attack; lives 3→0 at 2579/2824/3058; game over at 3335.
  - `build\scene_capture_play2.txt`: off-screen reloads, 7 enemies, frames to 5999.
  - Scripts: `build\script_p1_*.txt`.
## Stage 2: parallel subsystem decompile (2026-10-03)

**Setup:**
- Six agents, each in its own git worktree, did static analysis only. The coordinator ran all live captures and checks.
- Prepared for them: a static data-xref index (`ghidra\xrefs\datarefs_all.csv`, 21,719 refs) and 616 functions created from pointer tables (2,555 decompiled in total).

**Agent reports:**
- One report per subsystem in `notes/agents/<area>.md`: enemy, props, events, shooting, effects, sound.
- Proposed symbols merged into `symbols.csv` (606 rows). Name alternatives are listed in `notes/agents/symbol_conflicts.txt`.

**Subsystems and verification:**

| Area | Port | Test and result |
|---|---|---|
| Task system | `game/task.*` | `task_test`: 518/518 frame transitions match a flat, creation-ordered root list |
| Props | `game/props.*` | `props_test`: 26,597 prop draws match (max 2.4e-4), including the breakable-prop break and the hanging prop's flag-26 motion. Prop skeletons differ from character skeletons (explains the "unexplained" komono motions). |
| Enemy | `game/enemy.*` | `enemy_test`: 7/7 shot events, death jump and fall exact, 238/238 corpse twitches. Lifecycle of behaviour 23 traced; damage table traced. |
| Shooting | `game/shooting.*` | `shooting_test`: demo auto-gun `Player_UpdateAim` 0x00415710 (random fire and aim from `rand()`) reproduced in 513/513 frames; 7/7 enemy hits, 1/1 prop hit. Shot ray, hit test and hit delivery traced. |
| Effects / RNG | `game/rng.*`, `game/effects.*` | `effects_test`: RNG replay hits the captured state in 520/520 frames; rain 25,600/25,600 draws; sky 1,024/1,024; billboard effects 90/90. All 538 rand sites listed in `notes/agents/effects_rand_sites.csv`. |
| Event VM | `game/event_runner.*` | `event_runner_test`: 917/917 ops and 812/812 frames, 551/551 camera evaluations. Needs a capture-derived **oracle** for gameplay conditions (enemy visibility, back-on-rail); without it, 150/812 frames. |
| Sound | `game/sound_table.*`, `game/sound_driver.*`, `port/audio` | `sound_test`: id → file tables (324 SE, 38 BGM, 463 voice). XAudio2 playback not yet checked by ear. |

**Coordinator work (renderer and captures):**
- **Pass-1 list:** `pass1_test` reproduces the order of 2,678/2,678 captured mixed lists (CRT qsort + comparator).
- **Model deferral in the renderer:** minimum depth over skipped meshes, plus frustum culling as in `Stage_DrawSection` 0x00401260.
- **Per-object priority:** stage draw mode 2 → priority 7.
- **Port vs game pass-1 lists (`tools/pass1_compare.py`):** title 48/48 frames identical. Stage demo 4/23, because the port defers sky 6048 and the game never does (open).
- **Sky:** scenes now use the traced sky task (`sky_task`). Title 3D-layer colour difference fell from 10–27 to 2.6–5.0.
- **Per-layer `scene_compare`** against the clean reference shots in `build\ref`:
  - title: 3D edges 0.870 / 0.786 / 0.866 / 0.887 (controls 0.03–0.30); whole frame 0.94–0.96;
  - stage: 3D edges 0.910 / 0.873 / 0.877 / 0.782.
- **Lesson:** heavily instrumented runs shift screenshots, so reference shots are taken in light runs only.
- **New captures:**
  - `scene_capture_actors.txt` (task dumps, rand callers);
  - `scene_capture_enemies_full.txt`;
  - `scene_capture_pass1.txt`;
  - `scene_capture_randlog.txt`: ordered rand calls (Y lines) and watched globals (W lines) the agents asked for.

**Corrections to earlier notes:**
- The demo seed is 0, from `srand` via `Game_SeedRandom` 0x004157D0 at stage start and op 0x4D; the "state 1" seen earlier was the post-seed default.
- The fitted sky rule (yaw 4322 + 4/frame, scale 1.2) was wrong: it's the sky table, scale (1.2, 1.2, −1.2).
- Camera "transition blend" doesn't exist: those frames are return-to-rail double evaluations plus 1/16 view smoothing (0x00403C00, not ported).
- 0x00413E90 tasks are the per-player tasks, not a stage effect.

**Open:**
- Demo input source: the events agent found demo table 0x00589828 → `etc\key_122.bin` (missing from `game\`), but the stage-1 demo's shots are rand-driven and verified. Check whether the key file drives other demos.
- **Sky 6048 pass-1 deferral in the stage demo.** The port culls 6048's overhead dome mesh (material sphere r 1770, centre about 2000 above the camera) against the exact top frustum plane; the game never does. Yet the game does cull other meshes (needed for 6139's depth to match). So D3D's ComputeSphereVisibility is looser than exact plane distances; the rule is still to be found by testing candidates against the captured P lists. Debug: `HOTD2_DEBUG_CULL=1 scene_viewer ...` prints culled meshes.
  - **Resolved (fitted, hypothesis):** the per-mesh test ignores spheres that cross the near plane, which can only be culled when entirely behind it. With that rule the pass-1 lists match in 41/43 stage-demo frames (exact test: 4/23) and 95/95 title frames. The 2 remaining frames are a depth-order swap of 6091/6911; depth differences up to ~37 remain in a few entries.
- **Renderer refactor:** each model's geometry is now uploaded once and drawn with per-item world matrices, ready for per-frame world draws. Outputs are unchanged.
- The event runner still needs real gameplay conditions instead of the oracle (enemy visibility / shooting camera 0x00402650, rail alignment).
- Next: native integration (Stage 3). Event runner + task system + props + enemies + demo auto-gun + effects in one deterministic replay, checked per frame against the captured RNG state and draws.
## Milestone 12: 2D sprites (2026-10-03)

**Traced:**
- The sprite pipeline from definition to TL strip (`Spr_Draw` → `Spr_MakeRecord` → `Spr_Submit` → `Spr_DrawRecord` → `Draw_TLStrip`) and the pass-1 sort (VC6 CRT `qsort` + comparator).
- The title layout (`Title_DrawLogo` 0x004960A0) and PRESS START (0x004067D0 / `HUD_Init` 0x004066D0).

**Port:**
- `game/sprite.*` and `game/crt_qsort.h`.
- `AssetStore::TextureById`.
- `ModelRenderer::DrawSprites`, a TL pipeline with the game's ISP/TSP state.
- Scene directives `overlay title` and `overlay press_start phase P`; `scene_viewer --frame F --no2d`.

**Checks:**
- `sprite_test` vs `build\scene_capture_sprites.txt` (new capture flag `HOTD2_SCENECAP_SPRITES`):
  - records: 0 unexplained;
  - quads: 17,947 within 1.5e-8;
  - texture sizes: 17,947 equal;
  - layouts: 5,181 equal.
- `scene_compare --mask none` (whole frame, overlays drawn):
  - title 650/800/950/1050: edges 0.938/0.930/0.930/0.944, colour difference 5.0/10.7/6.9/4.6 (without 2D: 0.31–0.36 / 25–31);
  - stage 2820/2860/3100/3200: edges 0.920/0.875/0.910/0.863, colour difference 1.4/2.4/1.8/4.4. The PRESS START blink is predicted correctly in all four frames (on/off/on/on).

**Open:**
- Equal-key order with translucent models in the list.
- The other TL draw paths (0x0049E100 / 0x0049EBE0).
- HUD/credit layouts and the transition frames (1087–1099, `scr_common` glyph animations).
- (Resolved later) The blink phase is traced: the counter is the boot frame counter, which is capture frame + 1 at draw time.
## Milestone 11c: environment, fog (2026-10-03)

**Traced:**
- Event op 0x20 channels (`Env_SetChannel` 0x0040B3F0) for fog near/far/colour, light colour and intensity.
- How the engine turns them into D3D7 state: `Fog_SetRange` / `Fog_SetColor`, and `0x004AA120` with the constants 1.4, 0.3 and 255.

**Port:**
- `game::Environment` executes ops 0x18/0x20.
- `scene` directive `env <table> <from> <to>` runs the script's own env ops, which replaces the hand-copied light constants.
- `ModelRenderer` gained per-material linear fog on eye depth.

**Checks:**
- `env_test` against the original's captured G/L states (`build\scene_capture_fog.txt`): env block 2/2, fog 1/1 steady state, light colours 3,311/3,311 (including 640 title states against the defaults).
- `scene_compare --mask stage` at 2820/2860/3100/3200: edge correlation 0.910/0.873/0.877/0.782; block colour difference 1.4/2.5/1.7/4.4 (2.5 at 2860 is unchanged by fog). Without fog (`--nofog`): 1.6/2.5/2.8/5.8.
- Texture filter state checked in the running game (`HOTD2_SCENECAP_FILTER`, `build\scene_capture_filter.txt`): it always matches the material bits.

**Open:**
- Op 0x21 (interpolated env change).
- Writer of the title's fog values (env near 65535 / far 65536; not an event op).
- Writer of env +0x38.
- Mipmaps.
- Enemy light direction.
- Title scene: now `env default` (seeded colour/intensity, angles 0, no fog; light states verified 640/640). `scene_compare` 650/800/950/1050:
  - edge correlation 0.759/0.725/0.813/0.876 (was 0.715/0.695/0.774/0.815);
  - colour difference 16.3/26.8/17.8/10.1 (was 22.2/29.0/21.8/19.5);
  - the rest of the difference is 2D logo/text outside the mask.

## Milestone 11b: actor spawn records (2026-10-03)

**Traced:**
- The actor type table (56 types from list `0x00593358`).
- Spawn records used by event ops 0x09 / 0x0B / 0x0C: type, flags, world position, rotation, state, kind, params.
- The prop ('komono') kind dispatch.

**Verified:** all three drawn type-0x44 props in the stage-1 demo are drawn exactly at their record positions (the initial placement; their actors then move them).

**Finding:** prop groups (`komono_3` / `komono_6`) and enemies are built by actor code (sub-kind tables, `rand()`), not by placement data. A faithful stage scene with props and enemies needs that **gameplay code ported** (or the RNG reproduced), which is a separate milestone. The scene viewer therefore still shows level geometry, sky and lighting only. Next candidates: 2D sprites/HUD/text (`Game_Draw` sprite list); fog (`FOGENABLE` from TSP bit 23 plus fog parameters); enemy actors.

| Subsystem | Mapped | Decompiled | Hooked | Ported | Notes |
|---|---|---|---|---|---|
| Startup / main loop | ✅ entry points | partial | | | HGL callbacks identified |
| File I/O / archives | ✅ loaders + PRS | PRS | ✅ PRS | PRS (shared lib) | |
| Rendering (D3D) | entry points, display modes, textures, models, draw path | Tex_CreateSurface, palette bank, Mdl_Draw chunk walk | windowed debug; model check (opt-in) | textures, palettes, models, D3D11 viewer | material/PVR state, scenes next |
| Input (mouse / gun) | ✅ devices, slots, aim | poll paths | ✅ Input_Poll (raw input, opt-in) | raw-input layer | needs physical-device test |
| Audio | entry points | | | | DirectSound 44.1 kHz |
| Enemy AI / animation | skeletons, motions, enemy draw path | EvaluatePose | anim check (opt-in) | skeletons, motions, posed characters | blending, AI motion choice next |
| Level scripts / cameras | cameras, event VM, resource queue | camera eval, event decoder | scene capture, event trace (opt-in) | camera paths, event decoder, title scene | VM execution, stage init, spawns next |
| UI / menus | | | | | |
| Game state / scoring | | | | | |

## Log
- 2026-10-02: Project scaffolded. Waiting on disc + USB DVD drive.
- 2026-10-02: Tooling built and self-tested on a 32-bit system exe: pe_info, Ghidra headless pipeline (import/analyze/export), hook DLL + launcher (injection confirmed), GhidraMCP rebuilt for 12.1.4.
- 2026-10-02: Port platform layer: raw-input multi-device pointer input (src/port/input), input_test.exe for two-gun/two-mouse verification.
- 2026-10-04: Cross-platform readiness (notes/agents/platform.md): SDL3 3.4.18 platform layer (FetchContent, pinned), GpuDevice interface with D3D11 (bit-identical to before) and SDL_GPU / D3D12 backends, gun sources (raw input = Windows default, SDL mouse, touch), SDL3 audio backend (default; XAudio2 kept). play / viewers: `--renderer d3d11|sdlgpu --input auto|raw|mouse|touch --audio-backend sdl|xaudio2`. Experimental x64 build identical to 32-bit.
- 2026-10-08: Horde Mode (port only, notes/agents/horde.md): modern menu entry, stage-1 street take-over (VM halted, rail held), barrel row, timed rounds of the roster's zombies with a port behaviour on the engine's motion / attack tables, money, the store between rounds (original-mode weapons, power ups, magazines, lives, barrel repair / reinforcement, CPU agents as the player skeletons), co-op in the world sim. frontend_test / pause_menu_test unchanged.
