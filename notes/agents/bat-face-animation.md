# Flying-bat face animation boundary (2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> See [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections.

The original PC draw routine `0x00447C20` selects a face frame from
`seedCounter % 30`. It folds values above 14 with `30 - phase`, leaving
phase 15 equal to 15. Adding the face base `0xBF8` selects `0xC07`.

The actual assets establish the boundary: `0xBF8..0xC06` are 15 translucent
face/feather frames using texture 9; the next model, `0xC07`, is an opaque
foot using texture 2. This is an original indexing error, rather than a
missing texture or a lighting-state leak. The wing-pose and foot animation
families otherwise have consistent material/texture references.

`world_st2_bats.cpp` now clamps only the folded midpoint 15 to 14. This holds
the last valid face frame, preserving the remaining phases, the 30-frame
cycle, and the existing dead-state override. The original sequence
`0..14,15,14..1` becomes `0..14,14,14..1`.

## Regression and capture evidence

`bat_face_test <game> <script> <first> <last>` checks the actual asset-family
boundary, then replays living bats through all 30 phases. It checks the
14/15/16 midpoint neighborhood and unchanged neighboring phases 13/17.
Using `build/graphics-investigation/flying-bat-no-shots.txt` at
17290..17420:

- An independent negative control linked against the pre-fix game library
  fails at frame 17294, task 68940: face animation selected foot `0xC07`.
- The fixed test passes 262 live face draws, including 10 midpoint draws.
- Existing CPU traces show both live bats selecting `0xC07` at 17294,
  17324, 17354, 17384, and 17414. No shots occur after frame 17260.
- Comparing `flying-bat-idle-N.png` and `flying-bat-face-fixed-N.png` in
  `build/graphics-investigation/` changes exactly those five frames; all
  other 126 frames are byte-identical. Changed pixel counts are respectively
  85, 146, 19, 2133, and 53.

The clearest visible example is frame 17384. Changed bounds are
`x=414..511, y=378..441` in the 640x480 image. The lossless crops
`bat-face-before-crop-17384.png` and `bat-face-fixed-crop-17384.png` use
rectangle `[402,524) x [366,454)` and show the restored feather outline,
partly behind the existing HUD. The comparison helper is
`build/graphics-investigation/bat_face_metric.py`.

```powershell
& build/Release/bat_face_test.exe "$research/game" `
  build/graphics-investigation/flying-bat-no-shots.txt 17290 17420
python build/graphics-investigation/bat_face_metric.py
```

This establishes and fixes a periodic face/outline pop. It does not establish
that every reported whole-body white flash is this defect. Flying-bat body
normals and WORLD scales are unit length in this replay; its main body has
specular disabled, so the summoned fish's scaled-normal highlight problem
does not apply. Normal wing motion, player-damage overlays and intentional
shot feedback remain unchanged.
