# Remaster asset pack (2026-10-06)

An optional pack of upscaled textures and 48 kHz sounds for the port, under a strict "original look" brief: sharper
and cleaner, never restyled (no invented detail, no colour shifts, hand-painted look kept). The default game stays
byte-identical: without `HOTD2_ASSET_DIR` / `assets=` nothing changes and `tools/render_compare.py` is 9/9 with the
loader built in.

Nothing derived from Sega's data is committed: the pack lives in `remaster\` (gitignored). Only the scripts
(`tools/remaster.py`, `tools/remaster_filters.py`, `tools/remaster_audio.py`), the loader and these notes are in git.

## Enabling the pack

* `HOTD2_ASSET_DIR=<dir>` for any port tool (play, hotd2, scene_viewer, model_viewer), or
* `assets=<dir>` in `hotd2re_settings.ini` (play.exe / hotd2.exe; the environment variable wins when both are set).

Layout the loader reads (`src/port/assets/asset_override.{h,cpp}`):

| Path in the pack | Replaces | Loader |
|---|---|---|
| `tex\<tex file stem>\<index>.png` (also `%03d` / `%04d` names) | descriptor `index` of `tex\<stem>.bin`, any size, upright PNG | `AssetStore::LoadOverride` (`src/port/render/asset_store.cpp`), own PNG reader `src/port/render/png_read.cpp` |
| (loading, 2026-10-06/07) | overrides of a texture file are decoded on worker threads when the game loads the file (`AssetStore::PrefetchFile / PrefetchPolFile / PrefetchTexFile`, called by play.cpp on `WorldHud::texFiles` 0 -> 3 / 4, on `WorldHud::polFileLoads` (pol-file / model loads and the stage's section objects), at boot and at the mode select for stage 1; GPU uploads four per frame via `DrainPrefetched`); `png_read.cpp` inflate is table-driven (`tests/png_test.cpp` + `tools/png_hashes.py` check it against Pillow) | `AssetStore::Decode` -> `TakePrefetched` |
| `bmp\<file>.png` | the BMP front screens (`hod2title00.bin`, `segalogor_*`, mode icons ...; the file name without its `tex\` directory) | `FrontScreens::Bitmap` (until 2026-10-06 it looked under `bmp\tex\`, so these overrides never matched; the menu tiles are now written by `tools/menu_background.py`) |
| `Sound\...\x.wav` (the game's own relative path) | the WAV | `port::SoundPath` in both audio backends (SDL, XAudio2) |

The PNGs are stored upright (as `tools/tex_export.py` writes them); the loader flips to the game's bottom-up row
order. `RgbaImage` carries the game's descriptor size as `logicalWidth / logicalHeight`: sprite quads
(`Scene::MakeSpriteDraws`) and BMP blits size themselves by it, model UVs are normalized, so any resolution works.
Both GPU backends only receive a bigger RGBA image (`CreateTexture`), nothing else changed; D3D11 and SDL_GPU renders
of a pack texture agree (mean abs diff 0.006 / 255 on `znken.bin` model 3).

## Pipeline (`python tools/remaster.py <stage>`; `HOTD2_REMASTER_OUT` moves the output, default `<repo>\remaster`)

0. `python tools/tex_export.py --out remaster\src_png --hashes remaster\tex_hashes.txt` (reference decoder, 10,466 PNGs
   + `tex_hashes_banks.txt` with the emulated palette bank per pal4 texture).
1. `inventory` -> `remaster\inventory.json`: every texture with file, index, id, size, layout / pixel format, palette
   bank, alpha use (opaque / binary / gradient, transparent and partial fractions), colour count, wrap-seam continuity;
   who uses it: pol models (material refs of all 650 pol files -> pol files, mesh count, TSP blend / address / filter
   bits, UV range) and the sprite tables (2,979 sprite ids -> file slot 0x0057A5BC / texture id 0x0057D448); skeleton
   types whose nodes draw models from the file. Also the 72 untabled `tex\` files (47 BMP screens), the 802 WAVs
   (rate, bits, channels, frames, chunk layout) and the pol counts.
2. `classify` -> `remaster\classes.json`: class, alpha kind and edge mode per axis (`wrap` when a referencing
   material wraps and the model UVs cross the texture edge, `mirror` for mirrored materials, else `clamp`; for
   unreferenced textures the image's own seam continuity decides).
3. `pilot` -> `remaster\preview\*.png`: 24 textures over the classes, each as a contact sheet of the original
   (nearest x2 / x4) against the candidate methods; `preview\index.txt` lists them.
4. `batch [--scale 4,2] [--class a,b] [--method M] [--dry-run] [--force] [--limit N] [--file X]` -> `remaster\tex\`
   (4x, the primary) and `remaster\tex2x\` plus `remaster\manifest.json` (per texture: class, edge modes, method,
   source SHA-256 of the decoded RGBA, output size). Resumable: a texture is skipped when its manifest entry has the
   same method and source hash and the file exists.
5. `bmp [--scale 2]` -> `remaster\bmp\*.png` (the 47 BMP front screens, lanczos_clamp).
6. `audio [--rate 48000] [--dry-run] [--force]` -> `remaster\Sound\...` + `remaster\sound_manifest.json`.

Run times on this machine: inventory 50 s, batch about 25 minutes for 20,932 PNGs, audio 20 s.

## Inventory summary

* Texture table: 420 files (first entry per name), 10,466 textures, all decoded. Users: 7,448 by models, 2,979 by
  sprites, 39 by neither (`scr_blood_green.bin`, the boot-time swap for the red blood sprites).
* Alpha: 6,344 opaque, 1,534 binary (ARGB1555 colour key), 2,588 gradient (ARGB4444).
* Formats: VQ 3,183 (rgb565 2,237 / argb4444 852 / argb1555 94), rect-twiddled 2,780, twiddled 2,146, rect 1,067,
  pal4 1,290. Sizes: 128x128 2,273, 64x64 1,722, 128x32 1,006, 32x32 701, 256x256 665 ... (nothing above 256).
* Untabled `tex\` files: 72, of which 47 are Windows BMPs (title / logo / mode screens, 64x64 to 512x512) and 25 are
  texture files whose exe table entries are unusable (the game cannot register them either).
* Sounds: 802 PCM WAVs, all 16-bit: BGM 38 (22,050 Hz stereo), voice 468 (466 at 22,050 Hz, 2 at 44,100 Hz, stereo),
  SE 296 (11,025 / 16,000 / 22,050 / 44,100 Hz, mono and stereo). 72.6 minutes, 384 MB. 761 carry a trailing LIST
  chunk, 2 an `smpl` chunk.
* Models (out of scope for the pack, counted): 650 pol files, 18,027 models, 82,494 meshes, 555,052 strips,
  3,853,568 vertices.

## Classes (`remaster\classes.json`)

| Class | Count | Rule |
|---|---|---|
| `ui_pixel` | 2,408 | sprite-table texture (or `scr_*` file) with few colours for its size: fonts, digits, icons, frames |
| `ui_soft` | 610 | sprite-table texture with many colours / gradient alpha: logos, painted backdrops, blood splats |
| `sky` | 120 | used by the sky model files `st<N>_1[b].bin` |
| `skin` | 1,301 | used by models drawn at skeleton nodes (enemies, bosses, players, civilians) |
| `effect_add` | 75 | only translucent materials, additive blend (`*/one`): flashes, glows |
| `effect_alpha` | 1,583 | only translucent materials with alpha blend, `eff_*` files, gradient-alpha common.bin textures |
| `surface` | 4,369 | everything else referenced by an opaque model material: level chunks, props, vehicles |

Edge modes: 5,936 textures clamp on both axes, 2,324 wrap on both, 1,565 wrap in u only, 641 in v only (model v
ranges are normally [-1, 0] because the texels are stored bottom-up; a range only counts as tiled when it crosses an
integer boundary).

## Pilot: methods per class (sheets in `remaster\preview\`)

Candidates (`tools/remaster_filters.py`): `nearest`, `scale2x` (EPX), `xbr` (xBR 2x, ffmpeg's FILT2 rules with
Hyllian's luma-weighted distance; 4x = two passes), `catmull` (Catmull-Rom), `lanczos_clamp` (Lanczos-3 whose output
is clamped to the min / max of the 2x2 source texels around each pixel: no ringing, no new extrema),
`lanczos_sharp` (lanczos_clamp + unsharp 0.35 / sigma 0.6*scale, clamped again). Everything is alpha-aware: colours
under alpha 0 are first bled outwards from the opaque neighbours (so neither the filter nor the GPU's bilinear
sampling drags the key colour into edges), binary alpha is re-thresholded at 0.5 so colour-keyed edges stay hard and
the alpha test (ALPHAREF 1) cannot grow them. Tiled axes wrap, mirrored axes pad symmetrically, the rest replicate
the edge texel; a shift-equivariance test across the seam passes for every method.

| Class | Chosen | Why (sheet) |
|---|---|---|
| `ui_pixel` | `xbr` | `ui_pixel_scr_common_057_x4.png` (PRESS START BUTTON, pal4 colour key): xBR gives clean rounded outlines at exactly the original's colours; Scale2x makes blobby corners; Lanczos blurs the key edge. `ui_pixel_scr_lisence_009_x4.png` (anti-aliased rgb565 text): xBR keeps the strokes crisp where Lanczos softens them. |
| `ui_soft` | `lanczos_clamp` | `ui_soft_scr_dc_option_019_x4.png`, `ui_soft_scr_blood_green_029_x4.png`: painted content; xBR turns gradients into posterised blobs, the clamped Lanczos keeps the painting and adds no halos. |
| `sky` | `lanczos_clamp` | `sky_st1_1_062_x4.png` (poster, argb4444): soft content; sharpening would exaggerate the VQ blocks. |
| `skin` | `lanczos_sharp` | `skin_zskamere_002_x4.png`, `skin_hito_*`: the mild clamped sharpen gives scales / cloth folds definition without halos. |
| `effect_add` | `lanczos_clamp` | `effect_add_eff_boss6_006_x4.png`: xBR destroys the glow falloff; plain Lanczos keeps it. |
| `effect_alpha` | `lanczos_clamp` | `effect_alpha_*`: same reasoning, soft alpha preserved. |
| `surface` | `lanczos_sharp` | `surface_st1_03_001_x4.png` (bricks), `surface_st2_06_003_x4.png`, `surface_etc_komono_buru_000_x4.png`: the sharpened clamped Lanczos is the closest to "the same texture, cleaner"; Catmull-Rom is visibly softer. |

Not chosen anywhere: `scale2x` (too blobby), `catmull` (softer than Lanczos without any benefit once ringing is
clamped away).

## Batch result (`remaster\manifest.json`)

All 10,466 textures at 4x (`tex\`) and 2x (`tex2x\`), RGBA PNG, plus 47 BMP screens at 2x (`bmp\`). Swap which
scale is primary with `batch --primary 2` (or copy `tex2x` over `tex`). The manifest records the method, the edge
modes and the SHA-256 of the decoded source texels (from `tex_hashes.txt`), so a changed source or method re-runs
only the affected textures.

## Audio (`remaster\Sound\`, `remaster\sound_manifest.json`)

`tools/remaster_audio.py`: polyphase windowed-sinc resampling (Kaiser beta 9, 32 zero crossings per side at the
lower rate, cutoff 0.95 x the lower Nyquist; the kernel is applied with `scipy.signal.upfirdn`, no other processing),
16-bit output by rounding, no gain change, no dither, same channel count. Self-check: a 1 kHz tone keeps level
(ratio 1.00003) and frequency, an impulse keeps its time.

What it does and does not do:
* 22.05 kHz sources stay band-limited to 11 kHz; the pack adds no content above the source's Nyquist. 16 kHz /
  11 kHz SEs likewise.
* It replaces the audio device's real-time rate conversion with an offline high-order one and gives the mixer one
  uniform rate. Both backends open their master at 44,100 Hz (the original's primary buffer rate), so a 48 kHz pack is
  still converted once more on the way out; `audio --rate 44100` writes a pack at the master rate instead (no second
  conversion) and is the better choice for exactness.
* 510 files clip somewhere: 44,156 samples in all (0.02 % of the material), concentrated in SEs that are already
  hard-limited at full scale in the original (`damage3_22.wav` 19 %, `damage4_22.wav` 14 %, `explo1_22.wav` 5 %).
  These are inter-sample overshoots of a band-limited reconstruction between the original's clipped samples, not a
  level change; any resampler (including the device's) produces them. Per-file counts are in the manifest. A
  `--headroom` scale was deliberately not added (the brief says no normalisation).

## Known limitations

* Fonts and other `ui_pixel` sprites are drawn 1:1 in the 640x480 space; the 4x xBR version only shows when the window
  is larger than 640x480 (point sampling at 1:1 reproduces the original exactly either way).
* Colour-keyed edges: the key outline is re-thresholded at 0.5, so it follows the interpolated edge; at 4x this is
  smoother than the original staircase, which is intended, but a few one-texel features (digit serifs) round off.
* VQ textures (3,183) carry 2x2 codebook blocks; every upscaler magnifies that structure faithfully. Smoothing it away
  would be a restyle and was not done.
* Translucent sprites and models are sampled with the original's point filter where the material says so
  (`TSP & 0x6000 == 0`); a 4x texture then shows 4x point sampling, not bilinear. That is the original's filter state
  and was kept.
* The second blood file (`scr_blood_green.bin`) and the 25 unusable texture files are upscaled but never drawn.
* 3D geometry, motions, cameras and event data are out of scope (unchanged).
* The pack is 1.9 GB of 4x PNGs, 0.7 GB of 2x PNGs, 3 MB of BMP screens and 793 MB of WAVs; load time of a stage
  grows with the PNG decoding (the reader is a straightforward inflate; a stage's textures load in well under a
  second).
* Checked end to end: `model_viewer` (D3D11 and SDL_GPU) and `scene_viewer scenes\title_cam29.scene` with
  `HOTD2_ASSET_DIR` set draw the pack textures at the original placement and sprite size (title layout identical,
  mean abs pixel difference 5.7 / 255 from the sharper textures only); `batch --dry-run` after a full run reports
  20,932 outputs up to date.

## Recommendations

* AI upscaling (Real-ESRGAN or similar) would help one class substantially: `skin` and `surface` textures that are
  photographic (bricks, stone, wood, cloth, faces) - the hand-painted look is mostly photo-sourced there, and a
  conservative model (anime models are wrong for this) with a low denoise strength would recover texture that no
  interpolation can. It would hurt `ui_pixel` and the effects. Needs the user's approval and a download; the pipeline
  takes external results as drop-in PNGs (same `tex\<stem>\<index>.png` layout; the batch skips files whose manifest
  entry it did not write only with `--force`, so keep external results in a separate pack directory or let the
  manifest be regenerated).
* Hand touch-ups worth doing (from the sheets): the title logo strips (`scr_title.bin`, `ui_soft`), the licence /
  copyright text (`scr_lisence.bin`), the HUD digits (`scr_common.bin` 0..11), the 47 BMP screens (only 2x here; they
  are the first thing seen).
* A 44.1 kHz audio pack (`audio --rate 44100`) for exactness with the 44.1 kHz masters.
* The three loader lookups accept any size, so a hand-made 8x UI pack works without code changes.

## Files changed

* `tools/remaster.py`, `tools/remaster_filters.py`, `tools/remaster_audio.py` (new), `.gitignore` (`remaster/`).
* `src/port/assets/asset_override.{h,cpp}` (new lib `port_assets`), `src/port/render/png_read.{h,cpp}` (new),
  `src/port/render/asset_store.{h,cpp}` (`LoadOverride`, `RgbaImage::logicalWidth / Height`),
  `src/port/render/front_screens.cpp` (BMP override), `src/port/scene/scene.cpp` (one line: sprite quads use the
  logical size), `src/port/audio/audio_output.h`, `sdl_audio_backend.cpp`, `xaudio_backend.cpp` (`SoundPath`),
  `src/port/play/pause_menu.{h,cpp}` (`assets=` key), `src/port/tools/play.cpp` (include + one line),
  `src/port/CMakeLists.txt`.
