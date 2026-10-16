# Display settings (play.exe / hotd2.exe)

Port-only feature: fullscreen / window scale / aspect / CRT filter / internal resolution, set from the pause menu
("Display" row) and from the main menu's Options screen ("Display Settings" row after the volume rows). The world code
stays frame-exact (the rows exist only with `WorldConfig::portVolumes >= 0`, which the exactness tests never set).

## 1. Settings (`src/port/play/display_menu.h` `play::DisplaySettings`, saved in `hotd2re_settings.ini`)

| key | values | meaning |
|---|---|---|
| `fullscreen` | 0 / 1 | SDL3 borderless desktop fullscreen (`SDL_SetWindowFullscreen` without a mode) |
| `scale` | 1 / 2 / 3 / fit | windowed client size = that multiple of 640x480; fit = the largest 4:3 client area of the display's usable bounds (`SDL_GetDisplayUsableBounds` minus the window borders). Default 2 |
| `aspect` | 4:3 / stretch | the image is the largest centred 4:3 rectangle of the client area (bars) or fills it |
| `crt` | 0..3 | scanline filter strength (off, 1..3) |
| `render` | 1..4 | internal resolution of the frame (1 = 640x480, the original); the 2D stays in 640x480 space and scales with it |

`--width W --height H` on the command line override the window size for that run (the scale setting is not applied at
start; changing it on a page still resizes). Headless runs (`--screenshot`) never load the settings: they render the
`--width x --height` target as before (default 960x720) with no present pass, so every screenshot check is unchanged.

The volumes / input keys of the same file are unchanged (`play::Settings`, formerly `VolumeSettings`: `music`, `guns`,
`sfx`, `input`).

## 2. Where the pages live

- Pause menu (`src/port/play/pause_menu.*`): rows Resume, Music, Gun Shots, Sound Effects, **Display**, Return to Main
  Menu. Display opens `play::DisplayMenu` as a sub-page (same look: menu background, "Display Settings" heading, rows
  Fullscreen / Window Scale / Aspect / CRT Filter / Render Scale / Back; red = selected). Keyboard: Up / Down, Left /
  Right change a value, Enter / Space = next value (on Back: close), Backspace closes the page; pointer: aiming
  highlights, trigger = next value / Back, reload = previous value / close; start resumes the game from anywhere.
- Options screen (`src/game/world_modes_options.cpp`): port row 14 "Display Settings" after the three volume rows
  (`kPortRows = 4`; the controller / default lines move down four cells). Confirming it plays the sub-screen sound
  0x3016A9 and increments `ModesState::optDisplayRequests` -> `WorldHud::portDisplayRequests`. play.exe sees the count
  grow, opens its own `DisplayMenu` over the screen (the page is the whole frame, as the options screen is in the
  original) and zeroes the world's input while it is open, so the screen does not move; Back returns to it. The
  world knows nothing about the settings themselves.
- Settings are saved when a page or the pause menu is left.

## 3. Rendering (`src/port/render`)

Both GpuDevice backends now always render into an off-screen colour + D32 target of the internal resolution
(`GpuDeviceDesc::width/height`, `ResizeTarget`) and, for a window, run a **present pass** into the swap chain's back
buffer (sized to the client area; D3D11: `ResizeBuffers` when the client size changes): clear black, viewport =
`PresentParams` rectangle (`SetPresent`), one full-viewport triangle (SV_VertexID) with the shared HLSL
`kPresentShaderHlsl` (hlsl_shaders.h):
- "sharp bilinear": texel coordinates snapped so each source texel is sharp and only its edge blends over one
  destination pixel (nearest at integer scales, no shimmer at fractional ones; the old DXGI / blit stretch was plain
  bilinear);
- CRT filter (`crt` > 0): per **game row** (480 whatever the internal resolution) a slight vertical blend with the
  neighbouring rows (5 / 10 / 15 %) and a cosine darkening between the rows (18 / 36 / 54 % at the gap, phase -0.25 so
  that at 2 pixels per row one pixel is bright and one dark), fading out below 2 destination pixels per row, plus a
  small brightness lift (6 % per step).
The headless path (offscreen device, `ReadPixels`) is untouched; D3D11's off-screen texture only gained the
SHADER_RESOURCE bind flag. The sprite half-pixel shift stays 0.5 game pixels at every target size (a half-target-pixel
shift would change the 960x720 headless renders and put the sprite samples on texel-boundary ties at 2x); at render
scale k the 2D layer therefore sits k / 2 target pixels right / down of the exact D3D7 placement.
`ModelRenderer::DrawGame` gets `gpu->TargetWidth() / TargetHeight()` as its viewport. Per-strip cull mode and the
model-load cull flips are untouched. model_viewer (`followWindowSize`) renders at the client size and presents 1:1.

## 4. Aim mapping (`src/port/input/aim_map.h`, `src/port/platform`)

`input::FitImage(windowW, windowH, Aspect)` -> `ImageRect` (pixels) is used by both the present pass and the aim:
play.exe calls `gpu->SetPresent(rect)` and `plat.SetGameImage(rect)` every presented frame. The platform wraps the
raw-input / SDL-mouse source in `MappedGuns`, which re-normalizes the aim from the client area to the image rectangle
with `input::WindowToImage` (bars = off screen -> the arcade's off-screen shot / reload); the touch source gets
`TouchConfig::aspect = Pillarbox43` with the window size instead (its bar taps already reload). Unit test
`tests/display_map_test.cpp` (15 checks: pillarbox / letterbox / stretch rectangles, bar points off screen, agreement
with `ApplyAspect`).

## 5. Checks (this machine, RTX 5070 Ti, Windows 11 125 % DPI)

| check | result |
|---|---|
| `frontend_test game build\scene_capture_modes_options.txt --script build\script_modes_options.txt` | identical to `build\ft_options_base.txt` before and after (47 lines, 0 differences) |
| `python tools/render_compare.py` | 9/9 |
| `sweep_compare.py build\shots_lighting_sweep` vs `play --script build\script_auto_full2.txt --every 300 --frame 97200 --width 640 --height 480` | before 1.42 / 0.914, after 1.42 / 0.914; all 324 renders pixel-identical to the pre-change build |
| headless play 960x720 (default size) frames 800 / 2300, d3d11 and sdlgpu; model_viewer 4 cases | pixel-identical to the pre-change build |
| `pause_menu_test` 58/58, `display_map_test` 15/15, `touch_guns_test` | pass |
| windowed `--run-frames 120` d3d11 / sdlgpu with scale 1 / 2 / fit, render 1 / 3, crt 0 / 2, 4:3 / stretch, `--width 800 --height 500` | run at 60 game frames/s; window captures: 1200x600 shows the 800x600 image with 200-px bars, 1000x500 stretch fills, 1280x960 render 2x + crt 2 and 1920x1440 crt 3 show the scanlines (row means alternate 14.9 / 23.4 at 2x) |

Not verified here: fullscreen (needs a person: toggle it on the page, check the image is centred with bars on a 16:9
display and that the gun / mouse aim lands under the pointer; Esc still quits), the look of the CRT filter at a
fractional window scale, two physical guns with the mapped aim, DPI scaling on another monitor.
