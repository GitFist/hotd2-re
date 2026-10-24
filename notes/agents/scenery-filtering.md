# Scenery texture shimmer (2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> See [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections.

The reproducible scenery case is the stage-1 stone arch and its fine wall
textures, seen during the ordinary full-game camera movement around frame 5430.
Level-zero point/bilinear sampling aliases when many source texels cover one
screen pixel. Small camera movements then change the sampled details sharply.
The same issue is visible on the distant wall behind the stage-2 flying bats.
This is texture shimmer; it does not establish that every reported scenery pop
or flickering surface shares the same cause.

## Deliberate improvement over legacy sampling

The original PC executable also renders textures without mip levels. This is
not a missing native upload feature:

- Texture creation `0x004AC270` calls `0x004A5F90`, which calls `0x004A5ED0`.
  The last routine clears `DDSCAPS_MIPMAP`, `DDSCAPS_COMPLEX` and
  `DDSD_MIPMAPCOUNT` before `CreateSurface`.
- `0x004A7780` sets texture-stage MIN/MAG filtering to POINT or LINEAR according
  to material TSP bits `0x6000`; it does not select mip filtering.
- The existing native filter trace documented in `notes/formats.md` sampled
  203,620 applications with mip POINT. All 10,466 descriptor entries in the
  420 texture files use the non-mip layouts `1`, `3`, `5`, `9` or `D`.

The port now generates smaller texture levels for 3D minification and interpolates
between them. Authored point/linear magnification is retained, so close-up model
textures keep their original sampling. Sprites retain level-zero sampling,
including HUD glyphs, menus and overlays. `HOTD2_NO_MIPS` is a QA negative control.
Fine distant texture detail becomes smoother; this is an intentional correction
to legacy aliasing, not a claim of closer pixel fidelity to the 2001 renderer.

Mip generation is material-aware. Translucent texture variants use alpha-weighted
RGB, avoiding invisible border colors bleeding into visible edges. Opaque
variants average straight RGB, preserving meaningful color where the material
ignores texture alpha. Both variants preserve level zero exactly. The GPU cache
distinguishes the variants; sprite sampling does not inherit the model sampler.

The bounding-sphere audit did not find evidence for changing scenery culling:
320 stage models and their material spheres, plus 183 section-table model IDs,
enclosed authored vertices within 0.1 world unit. The largest excess was about
0.00048, consistent with float rounding. No speculative culling expansion is
included in this correction.

## Controlled GPU verification

`render_minification_test` exercises the actual model/sprite pipelines on D3D11
and SDL GPU:

- A 256 by 256 one-texel checker is projected to 32 by 32 pixels, then moved
  through 16 subpixel camera-relative phases. The legacy negative control's mean
  changes from 0 to 255. Filtered sampling remains 128 at every phase, for both
  authored magnification modes.
- The same perspective quad enlarges an 8 by 8 glyph to 32 by 32. Turning model
  mip sampling on preserves all RGB bytes for both authored MAG modes.
- A native-size one-pixel HUD glyph remains exact. A minified sprite reusing the
  world's mip-capable checker texture remains at level zero; model sampler state
  cannot leak into it.
- Opaque RGB with alpha zero remains visible when minified. CPU fixtures cover
  alpha-weighted transparent edges, straight opaque RGB, original-level
  preservation, an odd 3 by 5 texture's bottom row and 1 by N dimensions.

These checks pass on both backends.

## Actual matched sequences

All local files below are under `build/graphics-investigation/`:

- `scenery-before-mips-5400.png` through `scenery-before-mips-5460.png` versus
  final material-aware `scenery-filtered-*`: 61 paired frames at 640 by 480.
  Both gameplay logs end at frame 5460 with P1 state 5, lives 3, ammo 4,
  score 3180, 101 model draws and 9 sprites.
- `flying-bat-face-fixed-17290.png` through `flying-bat-face-fixed-17420.png`
  versus final `flying-bat-filtered-*`: 131 paired frames. This comparison holds
  the separate bat-lighting change constant. The final near bat retains its
  visible face/body texture at frame 17340.

`measure-scenery-minification.py` compares adjacent-frame luma changes in fixed
screen regions of static architecture. Before/after use the same camera path and
input. The values include camera motion and rain; they are not registered
world-space measurements or literal counts of flicker. They show the filtering's
effect in this controlled view without claiming that all temporal variation is
an artifact. Luma uses RGB weights 0.2126, 0.7152 and 0.0722.

| Region and frame interval | Mean absolute adjacent-frame luma change | Pixel samples changing by at least 24 luma levels |
| --- | ---: | ---: |
| Left arch, 5400–5430 | 9.14 → 3.41, 62.7% lower | 52,569 → 6,734, 87.2% fewer |
| Right stone wall, 5400–5460 | 16.47 → 9.53, 42.2% lower | 142,328 → 66,512, 53.3% fewer |
| Stage-2 right wall, 17290–17420 | 10.05 → 7.70, 23.4% lower | 369,599 → 234,269, 36.6% fewer |

The left-arch metric uses exclusive bounds `(130,150)`–`(230,300)`, before
characters enter that part of the image. The right stone-wall metric uses
`(390,160)`–`(470,285)`, away from the HUD, water, crosshair and foreground action.
The stage-2 wall uses `(480,40)`–`(610,245)`. Full values, source hashes and sampled
timestamps are in `scenery-minification-metrics.json`.

The best motion evidence is the clean 31-frame left-arch interval. The inline
inspector contains exact lossless crops from `(160,180)`–`(224,276)` for every
frame 5400–5430, with manual frame selection and finite Play/Pause. It also has a
separate leech-lighting case at frame 6620, using `leech-fixed-6620.png` versus
`leech-unit-specular-6620.png`, crop `(295,145)`–`(388,237)`. Mips are disabled in
both leech images, keeping that lighting comparison separate from filtering.
`scenery-leech-visual-provenance.json` records all 64 source/crop hashes; decoded
lossless crops were checked byte-for-byte against the originals.
