# Platform layer: SDL3, GPU backends, input sources, audio (cross-platform readiness)

Goal: the native port (`src/port`, play.exe and the viewers) builds on a thin platform layer so Android / iOS / Linux
become a backend job. `src/game` stays platform-free (unchanged by this work). No game code or data involved: this is
all our own code (renderer, shaders, platform glue).

## 1. Layers and files

| Layer | Library | Files | Platform |
|---|---|---|---|
| game logic | `game`, `game_sound`, `game_hud` | `src/game/*` | any |
| renderer front end | `port_render` | `render/asset_store.*`, `render/model_renderer.*`, `scene/scene.*` | any |
| GPU interface | (header) | `render/gpu_device.h`, shaders `render/hlsl_shaders.h` | any |
| GPU backends | `port_gpu_d3d11`, `port_gpu_sdl` | `render/d3d11_device.*`, `render/sdl_gpu_device.*` | Windows / SDL_GPU |
| input logic | `port_input_core` | `input/player_state.h` (PlayerState, GunSource), `input/aim_map.*`, `input/pointer_guns.*`, `input/touch_guns.*` | any |
| raw input | `port_input` | `input/raw_input.*` (unchanged filtering), `input/raw_input_guns.*` | Windows (also the hook) |
| audio | `port_audio_sdl`, `port_audio` | `audio/audio_output.h`, `audio/sdl_audio_backend.*`, `audio/xaudio_backend.*` | any / Windows |
| platform | `port_platform` | `platform/platform.h`, `platform/platform_sdl.cpp`, `platform/fixed_clock.h` | SDL3 |
| tools | | `tools/play.cpp`, `tools/scene_viewer.cpp`, `tools/model_viewer.cpp`, `tools/snd_play.cpp` | SDL3 |

`input_test.exe` stays a plain Win32 raw-input tool. The hook (`src/hook`, Detours) is only configured for 32-bit
Windows.

## 2. SDL3

- Version **3.4.18** (latest release tag on github.com/libsdl-org/SDL when this was done). Obtained with CMake
  `FetchContent` from the official release tarball
  `https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz`, pinned by SHA256
  `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3`; built as a static library (`SDL3-static`,
  static CRT like the rest). Win32 x86 builds (drivers: video windows/offscreen/dummy, GPU d3d12 + vulkan,
  audio wasapi/dsound/disk/dummy).
- Offline: `-DFETCHCONTENT_SOURCE_DIR_SDL3=<extracted SDL3-3.4.18>` (tested with the x64 build), or
  `-DHOTD2_SDL3_SYSTEM=ON` for an installed SDL3 (`find_package(SDL3 CONFIG)`).
- CMake options: `HOTD2_SDL3` (ON; OFF = no platform layer / tools, libraries + tests only), `HOTD2_SDL3_SYSTEM` (OFF),
  `HOTD2_SDL_GPU` (ON), `HOTD2_ALLOW_64BIT` (OFF; experimental, see 8).
- Entry point: the tools keep a plain `main` (no `SDL_main.h`), fine on desktop; Android / iOS need `SDL_main`.

## 3. Interfaces

**GpuDevice** (`render/gpu_device.h`): `ModelRenderer` does all the game-specific work on the CPU (PVR material
decode, culling, pass 0 / pass 1 order incl. the CRT-qsort pass-1 list, per-draw constants) and submits a
`GpuDrawList` per draw call: draws of two fixed pipelines (Model: triangle list of `game::ModelVertex`; Sprite:
4-vertex strip of `game::TLVertex`) with a D3D7-style `GpuState` (blend enable / D3DBLEND src / dst, D3DCMPFUNC,
z write, point / linear, D3DTEXTUREADDRESS u / v), a texture handle, a vertex stream (static model cache, per-frame
dynamic skinned geometry, sprites) and the index of its 256-byte `ModelConstants` / 16-byte `SpriteConstants`.
Device: `CreateTexture`, `SetStatic/Dynamic/SpriteVertices`, `BeginFrame(clear)`, `Submit`, `EndFrame` (present),
`ReadPixels` (off-screen readback), `TargetWidth/Height`. `ModelRenderer` lost all D3D types: `Init(GpuDevice*)`,
`Draw(w, h, cam)`, `DrawWithMatrix(w, h, vp, unlit)`, `DrawGame(w, h, ...)`, `DrawSprites(w, h, ...)`;
`OffscreenTarget` is gone (device `ReadPixels` + `Platform::SavePng`, which uses `SDL_SavePNG`).

- **D3D11** (`d3d11_device.cpp`): the previous renderer's D3D11 code moved behind the interface (same shaders, states,
  call order per draw): **bit-identical output** (section 6). Window: swap chain on the SDL window's HWND (fixed back
  buffer stretched by DXGI, or `followWindowSize` for model_viewer); off-screen with WARP fallback.
- **SDL_GPU** (`sdl_gpu_device.cpp`): pipelines cached per (pipeline, blend, depth) key, samplers per filter /
  address, textures uploaded through transfer buffers, dynamic / sprite streams uploaded in a copy pass before each
  render pass (buffer cycling), per-draw uniforms pushed (`SDL_PushGPUVertex/FragmentUniformData`), a 1x1 white
  texture bound for untextured draws. Renders into an off-screen colour + D32 target; window: blitted (linear, stretched)
  to the swap chain (vsync off -> IMMEDIATE / MAILBOX present mode). Shaders: the same HLSL compiled at run time with
  `D3DCompile` to DXBC SM 5.1 with SDL_GPU's D3D12 register spaces (vertex b0 space1; fragment t0 / s0 space2, b0
  space3) and TEXCOORD0..2 vertex semantics (macros in `hlsl_shaders.h`; the D3D11 compile sees the original text) ->
  the device is created for the **D3D12** driver. Vulkan / Metal: see 8.

**GunSource** (`input/player_state.h`): `Player(i)` -> `PlayerState` (aim normalized over the game image, onScreen,
held / pressed / released button bits: trigger, reload, start), `BeginFrame()` once per game frame. play.exe's input
code is unchanged otherwise (state-9 trigger = start, off-screen = -1, `game::MakeWorldInput`).
- `RawInputGuns` (Windows, **default**): wraps `input::RawInput` unchanged (one device per player, null-handle
  reports ignored); WM_INPUT / WM_INPUT_DEVICE_CHANGE reach it through `SDL_SetWindowsMessageHook` (SDL still dispatches
  them). The relative-mouse "aims at the Windows pointer" rule moved here from play.cpp unchanged.
- `PointerGuns` (`--input mouse`): SDL mouse events; P1 at the system pointer (queried per frame, so outside the window
  = off-screen like the raw path); `multiDevice` joins each SDL mouse id as its own player (only useful where a
  platform reports per-device absolute positions, see 5). Enter = start.
- `TouchGuns` (`--input touch`, default on Android / iOS): tap on the game image = trigger at the touch point (held
  while down, aim follows the finger, first tap joins P1); tap outside the image (`TouchConfig::aspect = Pillarbox43`:
  the bars of a wide phone screen) / a second finger / a long press (0.5 s, finger still) = reload (off-screen shot);
  three fingers = start. Desktop testing: `SDL_HINT_MOUSE_TOUCH_EVENTS=1` is set, so the left mouse button is a finger
  (click = shot, click-and-hold = reload), Enter = start. Unit test `tests/touch_guns_test.cpp`.

**AudioOutput** (`audio/audio_output.h`) = `game::SoundBackend` + `Init(gameDir)` + `Name()`. `game::SoundDriver` is
unchanged. SDL3 backend (**default**, `--audio-backend sdl`): one `SDL_AudioStream` per channel, each on its own
logical device (`SDL_OpenAudioDeviceStream` on a master logical device opened at F32 / stereo / 44100 like the
original's primary buffer), so pause / resume keep the exact position; fed from the whole PCM data chunk by a get
callback that wraps for loops and flushes the resampler at the end of one-shots. Same semantics as the XAudio2 class
(play rewinds a playing static buffer, resumes a paused one; stop rewinds; duplicate shares the PCM). XAudio2 stays
available (`--audio-backend xaudio2`).

**Platform** (`platform/platform.h`): `Init(Options)` (window or headless: no SDL video), `PollEvent` (tool-level
events: quit, keys, mouse; feeds the gun source), `Seconds` / `Delay`, `Guns()`, `CreateGpu(renderer, w, h,
followWindowSize, syncInterval)`, `CreateAudio(kind, gameDir)`, `SavePng`. `FixedClock` (`fixed_clock.h`) is play's
60 Hz accumulator unchanged (0.25 s stall cap). Common tool options: `--renderer d3d11|sdlgpu`,
`--input auto|raw|mouse|touch`, `--audio-backend sdl|xaudio2` (`ParseCommonArg`).

## 4. Tool changes

play.exe: window / events / clock / input / audio through the platform layer; `--run-frames N` (quit after N game
frames, prints game frames per second and presented frames: interactive smoke test). Game logic, script replay,
HUD / world sprites, the front end (World::Boot, port::FrontScreens, `--direct`, the mode select's EXIT), `--serial` /
`--credits` / `HOTD2_PLAY_ZLOG` (main) untouched; the keyboard pad bits (Up / Down / Enter -> P1 0x10 / 0x20 / 0x8) now
come from the platform's key events, independent of the gun source. scene_viewer / model_viewer /
snd_play: same options; behaviour as before (model_viewer back buffer follows the window, viewers vsync).
Paths use '/' (works on Windows; the sound backend converts the tables' '\\').

## 5. Multi-mouse findings (SDL 3.4.18 source, `src/video/windows/SDL_windowsevents.c`, `SDL_windowsrawinput.c`)

- SDL3 reports per-device mouse ids (`which` = the raw-input device handle) **only in relative mouse mode**: then a
  raw-input thread registers RIDEV mouse input for its own message window. Otherwise WM_MOUSEMOVE / button messages
  are sent with `SDL_GLOBAL_MOUSE_ID`: one pointer.
- In relative mode, absolute devices (light guns reporting `MOUSE_MOVE_ABSOLUTE`) are converted to relative deltas
  against **one shared** last position (`data->last_raw_mouse_position`, all devices), or sent as pen motion -> two
  absolute guns would corrupt each other's aim; there is no per-device absolute position.
- Raw reports with a null device handle (SendInput, automation) get mouse id 0 and are not filtered (only
  touch / pen-sourced messages are).
- Relative-mode events only go to the window with keyboard focus; our registration uses RIDEV_INPUTSINK.
- Raw-input registration is one target window per usage page per process: enabling SDL relative mode would take the
  mouse registration from `RawInput`, and disabling it removes it (RIDEV_REMOVE). The port therefore never enables
  relative mouse mode.
=> SDL3 cannot replace our raw input for two guns on Windows; `RawInputGuns` stays the Windows default (physical
two-gun test still pending). SDL's per-device ids could serve two relative mice in relative mode, but not light guns.
(Linux X11 XInput2 / Wayland: not evaluated; motion positions are the shared cursor there too.)

## 6. Verification (this machine: RTX 5070 Ti laptop, D3D11 + D3D12)

Baseline = main's play.exe (8f511e7, front end included) built separately; new = this branch merged with it.
| Check | Result |
|---|---|
| play headless D3D11, `--direct` and boot mode: `script_p1_reload` frames 2000 / 2300 / 3100 (+ boot frames 300 / 800: logos, front-end bitmap screens), `script_auto_st1` 14300 | **identical**, all 10 images (mean abs diff 0, 0 differing pixels, RGBA incl. alpha) |
| render_compare.py (model_viewer) | 9/9 pass; cpp_*.png identical to the pre-change renders |
| scene_viewer title_cam29 t=800, st1_demo_cam42 t=3100 (D3D11) | identical |
| world_test play2scene1 + script_p1_reload | output byte-identical to main's world_test |
| play headless **SDL_GPU (D3D12)** vs main (D3D11), frames 2000 / 2300 / 3100 / 14300 (direct and boot alike) | mean abs RGB diff 0.094 / 0.079 / 0.040 / 0.018 (0-255 scale); 2-10 % of pixels differ, almost all by 1 LSB; boot frames 300 / 800 identical |
| scene_viewer --simple SDL_GPU vs D3D11 | identical; title t=800: max diff 1 LSB |
| touch_guns_test, gun_input_test, input_test_unit | pass |
| sdl_audio_test (disk driver): play / end / loop / pause / resume / stop / duplicate / release | pass |
| SDL audio output vs the WAV (BLOOD01, 16 kHz -> 44.1 kHz): correlation 0.996, peak ratio 0.996, silence after the end; loop of vo_RELOAD over 2.4 periods: correlation 0.99993 (seamless) | ok |
| x64 build (`HOTD2_ALLOW_64BIT`, SDL from `FETCHCONTENT_SOURCE_DIR_SDL3`): play 4 frames / world_test | identical to 32-bit |
| `-DHOTD2_SDL3=OFF` | configures and builds (libraries, tests, hook, input_test; no SDL tools) |
| play windowed `--run-frames 300/120`, d3d11 / sdlgpu, raw / mouse / touch, sdl / xaudio2 audio | starts, renders, 59.98 game frames/s (2 s runs; 5 s runs show the 0.25 s start-up stall cap), exits cleanly |

SDL_GPU differences analysed: (a) sprite text shifted by one texel in places — the 2D sprites sample **exactly on
texel boundaries** (D3D7 integer coordinates + half-pixel shift), so the point-sampling tie goes either way depending
on the driver's attribute interpolation (coverage is identical: checked with solid-colour sprites; the shaders'
DXBC math is identical between vs_5_0 and vs_5_1); a constant UV bias fixes one scene and breaks another, so it was not
kept. The same tie behaviour can differ between GPUs even under D3D11. If deterministic 2D sampling is wanted, snap
sprite UVs to texel centres in the shader for **both** backends (changes D3D11 output by design; coordinator's
call). (b) 1-LSB rounding in lit / fogged shading (driver shader compilers).

## 7. Needs a human check

- play.exe window: crosshair cursor, mouse aiming at the pointer (`--input mouse` and the raw-input mouse path), the
  window resize / DXGI stretch, DPI scaling (SDL3 declares per-monitor DPI awareness, the old Win32 window was
  DPI-unaware: on a scaled display the window may now be physically smaller).
- Sound by ear (`--audio`, SDL default vs `--audio-backend xaudio2`): mono SEs (SDL duplicates mono to both channels
  at full level; XAudio2's default mono->stereo matrix not compared), BGM loop points, pause / resume on the start
  button.
- Raw input under SDL (registration works; clicks need real devices since synthetic input is filtered by design),
  two physical guns (still pending as before).
- `--input touch` with mouse emulation (click = shot, hold = reload, Enter = start) and on a real touch screen.
- `--renderer sdlgpu` windowed: present / vsync-off behaviour, window resize.

## 8. Remaining for Android / iOS / Linux

- **64-bit**: `-DHOTD2_ALLOW_64BIT=ON -A x64` (hook / Detours skipped) builds everything (game, port, tools, tests)
  with no errors and no pointer-truncation warnings; x64 play.exe headless frames 2000 / 2300 / 3100 / 14300 are
  **pixel-identical** to the 32-bit build and x64 world_test output is byte-identical (MSVC x64, SSE floats). Next:
  make 64-bit the norm for the port (keep 32-bit for the hook). ARM (Android / iOS clang) may contract a*b+c into FMA:
  build `src/game` with `-ffp-contract=off` and re-run world_test there, since the world logic is compared exactly.
- **Shaders for Vulkan / Metal**: compile `hlsl_shaders.h` offline to SPIR-V (DXC `-spirv`, which the Windows SDK DXC
  lacks; Vulkan SDK or SDL_shadercross) and MSL (SDL_shadercross / SPIRV-Cross), check them in as generated sources
  (our shaders), select by `SDL_GetGPUShaderFormats` in `SdlGpuDevice::CreateShaders`. GLES devices without Vulkan
  would need another backend (or SDL_GPU only).
- **Entry point / lifecycle**: `SDL_main` (or SDL's main callbacks) for Android / iOS; app suspend / resume (pause the
  game, recreate the swap chain), touch-only UI (no Esc), orientation (landscape lock).
- **Presentation**: letterbox the 4:3 image on wide screens in the SDL_GPU blit (currently stretched) and set
  `TouchConfig::aspect = Pillarbox43` so bar taps reload.
- **Data access**: game files via `SDL_IOStream` / Android assets instead of `std::ifstream` (AssetStore, ExeImage,
  sound backend, scene files); case-sensitive file names on Linux / Android (the tables use the original case).
- **Input**: second player on touch (split screen halves?), game controllers (SDL gamepad -> aim with a stick),
  Linux multi-mouse via evdev if two guns are wanted there.
- CI for a Linux build (SDL_GPU Vulkan + shader blobs) to keep `src/port` portable.
