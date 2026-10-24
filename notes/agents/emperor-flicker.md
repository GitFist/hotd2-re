# Emperor reflective-body flicker (2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> See [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections.

## Cause and original evidence

Emperor's gray core models `0x7B0..0x7CA` and blue reflective twins
`0x7CB..0x7E5` share exact bounding-sphere values and submission matrices.
Their pass-1 sort depths consequently tie. The game submits each core before
its twin (`0x00429040`, and attachment case `0x4C` of `0x00419B40`); their
material alpha values are 0.6 and 0.9. Both retain LESSEQUAL depth testing and
depth writes. They use opposite effective face culling, so a general overlay
depth bias or disabling depth writes is inappropriate.

The original pass-1 comparator `0x004A8A20` compares priority and depth only.
Its VC6 CRT qsort (`0x004ADCF7`, reproduced in `game/crt_qsort.h`) is unstable:
unrelated entries can reverse a tied core/twin pair. The port also constructs
its mixed list with models before sprites, whereas original task submissions
interleave them.

Existing original capture `build/scene_capture_auto_st6.txt` in the main
research checkout contains both initial model submissions and the later
pass-1 model draws. For frame 79950, after the `F 79949` record:

- There are 61 deferred models and 13 translucent sprite submissions.
- Taking deferred model membership from the recorded replay, computing depth
  from captured matrices/material spheres, and sorting the original M/S
  submission sequence reproduces all 61 replayed model IDs in exact order.
- Partitioning those same 74 entries into models then sprites reverses 12 of
  the 27 core/twin pairs. No geometry or light value changes in this comparison.
- Across original frames 79940..79970, 37 core/twin order reversals occur.
  The 13-to-12 sprite change at 79964 reverses 13 pairs; the 12-to-15 change at
  79968 reverses 11. Thus the unstable ordering also exists in the original.

Local reproduction helpers and output are in
`build/graphics-investigation/emperor_source_order.py`,
`emperor_original_pair_flips.py`, and `emperor-source-order-79950.json`.
These helpers read the existing original capture; they do not launch or modify
the original game.

## Change and scope

`ModelRenderer::DrawGame` keeps the original CRT sort, then restores model
submission order within each equal-priority/equal-depth group. Only model
slots are reordered. Sprite slots, sprite tie order, unequal depths,
priorities, material blending, culling, and depth state remain unchanged.

This deliberately stabilizes a native rendering artifact. It does not attempt
to reproduce each frame's arbitrary native tie permutation. An individual
original screenshot may therefore be closer or farther away numerically after
the fix; consistency of reflective-body compositing is the intended behavior.

## Consecutive-frame verification

The stage-6 replay config and input script in the main research checkout are
`build/final-boss-flicker/start.txt` and `build/script_auto_full2.txt`. From this
worktree, with `$research` set to that checkout, capture the fixed build with:

```powershell
& build/src/port/Release/play.exe "$research/game" `
  --config "$research/build/final-boss-flicker/start.txt" `
  --script "$research/build/script_auto_full2.txt" `
  --scene scenes/st1_play.scene `
  --screenshot build/graphics-investigation/emperor-stable-ties-%d.png `
  --every 1 --every-from 79940 --frame 79970 --width 640 --height 480
python build/graphics-investigation/emperor_flicker_metric.py `
  emperor-consecutive-base emperor-stable-ties
```

This is the HOLD YOUR FIRE introduction, with no damage-flash feedback. The
metric compares adjacent RGB frames in the fixed body rectangle
`x=[250,380), y=[100,395)`, taking each pixel's maximum channel difference.
The 31 frames give 30 transitions:

| Measurement | Before | Fixed |
| --- | ---: | ---: |
| Pixel transitions exceeding 25 | 21,838 | 9,479 |
| Pixel transitions exceeding 50 | 5,545 | 2,201 |
| Mean maximum-channel change | 2.424 | 2.003 |
| Largest frame's mean change | 5.463 | 2.327 |

Large jumps decrease by 57-60%. The contact sheet
`build/graphics-investigation/emperor-stable-ties-contact.png` shows the old
large head/chest/shin color changes above the stable result. Reflection and
body motion continue normally. Eleven sparse original-reference frames from
79050..80550 were also compared: four remain identical, and the remaining
differences vary with the original's unstable layer permutation. For example,
full-image mean RGB error at 79950 changes 5.380 to 5.555, while at 80550 it
improves 6.595 to 5.648.

The focused GPU regression is `render_order_test [sdlgpu]`: seven models,
three overlapping tied alpha surfaces, and 0..48 offscreen HUD sprites. The
negative control changed 2,580,480 world pixels as HUD count varied. The fixed
test requires zero changes, authored layer RGB, unchanged original CRT sprite
slots/order and unchanged priority/depth values. It covers depth writes both
enabled and disabled. Run both renderer backends after building:

```powershell
& build/Release/render_order_test.exe
& build/Release/render_order_test.exe sdlgpu
```

Intentional hit colors in `BossHits` and the light-color recovery in
`Actor2DDraw` are preserved. Normal animation, changing environment-map UVs,
specular reflections and moving silhouette edges still change pixels; this
fix does not freeze the boss's shading or animation.
