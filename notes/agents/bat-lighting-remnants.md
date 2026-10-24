# Bat lighting remnants (2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> See [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections.

The remaining investigation found two brightness causes and one missing-frame
cycle inherited from the PC program. These changes intentionally improve visual stability; they are
not claims that the corresponding original code was translated incorrectly.
The earlier missing reflection light bracket and owl face-frame correction are
documented in `actor-lighting.md` and `bat-face-animation.md`.

## Stage-1 leech highlights

The physical leech segments are drawn at uniform scale 0.75 with normalization
disabled in the original fixed-function lighting. Their inverse-transpose
normals consequently reach length 1.333333. The shiny variant's materials have
specular exponent 5 (power 32) and specular RGB 1. A scaled normal lets
`dot(normal, halfVector)` reach one over a broad angular range, producing a
flat white highlight instead of a small reflection.

The CPU replay applied actual per-submission seam position and normal-X edits
before evaluating lighting. Across no-shot frames 6579–6620 it inspected 43,824
referenced vertices. Of 20,189 light-facing specular vertices, 4,285 reached a
specular factor of at least 0.95. Normalizing only the specular normal reduces
that to 38; the summed specular factor falls from 4,634.6 to 594.2. Diffuse
lighting, seam edits, materials, geometry and animation are unchanged.

`BatSkelDraw` now opts physical body submissions into the existing
`WorldDraw::unitSpecular` path, including mirrored bodies and fading/dead body
variants. Floor/water shadows and hit effects stay outside that path.
`actor_lighting_test` checks that actual leech bodies/reflections are opted in,
the separate model `0x10D0` floor shadow is not, and the original light snapshots
and reflection edits still match. Against the saved pre-tag game library the
new scope assertion fails; the renderer's angled-normal and flag-reset fixtures
are documented in `hierophant-fish.md`.

Matched GPU captures use the same no-shot input, with mipmapping disabled in
both runs: `leech-fixed-6579.png` through `leech-fixed-6620.png` versus
`leech-unit-specular-*`, all under `build/graphics-investigation/`.

- All 42 pairs change, totaling 22,996 changed pixel samples.
- In the mask of old near-white pixels (minimum RGB channel at least 200) whose
  brightness drops by more than 16, 9,126 old pixels become 443 still near-white
  pixels, a 95.1% reduction.
- Frame 6620 changes 1,952 pixels within inclusive bounds x305–372/y165–239;
  the same near-white count falls from 829 to 7. Texture detail becomes visible
  in the previously broad white front-body reflection.

## Flying owl death lighting

Living owl task `0x004460C0` draws in scene lighting. The original death function
`0x00448210` explicitly calls character lighting (`0x0041DC70`), draws with
`0x00447C20`, then restores scene lighting (`0x0041DCC0`). The port previously
matched that behavior. At the sampled encounter this switches intensity 0.5,
RGB (1, 0.9, 0.77) to intensity 0.7, white, on the first death-task update.

Live body `0xBBF` and dead body `0xBC0` share the same feather/skin materials:
texture references 3 and 7, ambient scale 0.75, diffuse/alpha 1, exponent 0.
The dead body additionally has green wound materials using textures 10 and 11;
it does not replace the skin with a white material. Both bodies have unit raw
normals and unit world scale. Normalizing normals cannot correct this jump.

In normal shooting frames 17265–17295, the CPU lighting reference found zero
RGB-clipped vertices among 6,532 living-body vertex references. Dead-body
submissions had 1,577 clipped references out of 2,912 (54.16%). Replacing only
their light snapshot with the simultaneous living owl's scene light, while
keeping the exact dead geometry, materials and pose, reduces that to 6/2,912.
At frame 17281 the same-pose count is 110/182 versus 0/182; at 17282–17284 it is
106/182 versus 0/182 each frame. These counts refer to pre-texture diffuse RGB
saturation, not to an equivalent count of fully white screen pixels.

`BatDeath` now selects scene lighting before drawing, preserving the living
owl's lighting through death. The final scene-light restore remains. Blood,
debris, muzzle feedback, wound textures, death geometry and physics are unchanged.
The temporary diagnostic environment guard was removed after the GPU comparison.

The GPU comparison uses identical normal shooting input, mipmapping disabled,
and only the two muzzle-model draws suppressed in both runs to expose the body:
`owl-death-light-before-17280.png` through `17300.png` versus
`owl-death-light-fixed-*`. Frame 17280 is identical; the following 20 frames
change only the affected owl/death pieces. Total changed pixel samples are
11,455. The largest frame, 17287, changes 685 pixels in bounds
x302–361/y110–134. All 23 old near-white pixels in the above >16-brightness-drop
mask lose their white clipping. The owls are small in this capture; this proves
the scoped death-brightness reduction, not the disappearance of every possible
reported living-bat flash. Green blood remains visible in both captures.

The real-replay regression uses:

```
actor_lighting_test <game> <script_auto_full2.txt> 17265 17295 004460C0
```

It checks scene intensity, RGB and direction on both `0xBBF` living and `0xBC0`
dead body submissions and requires both variants. The saved pre-fix library
fails with 15 mismatches among 62 body draws; the first mismatch is frame 17281,
dead model `0xBC0`, intensity 0.7 instead of 0.5.

## Background flying-creature blink

The 30 background creatures spawned by kind `0x53`, motion `0xD9`, use
`TaskBird24` (`0x00483F00`, original update `0x00483F90`). Their valid wing poses
are models `0x21F..0x231` in `zabat_hod1.bin`. The original counter increments
before testing the old model, permitting `0x232` for one frame each 20-frame
cycle. That model belongs to `eff_1.bin`, not the creature; it is not resident
in this scene, so the body disappears for that phase.

The original attract-loop capture has 30 live tasks over frames 19077–19337:
7,830 expected body submissions, 7,440 actual, and exactly 390 missing phases.
Task 66174 draws `0x230` at 19087, `0x231` at 19088, nothing at 19089, then
`0x21F` at 19090. The gap repeats every 20 frames.

A current-port CPU `World::Boot` replay with zero input independently reaches
the same flock at frames 6522–6782. It has the same 7,830 expected, 7,440 actual,
390 missing draws. Tracked task 65738 draws `0x230` at 6526, `0x231` at 6527,
nothing at 6528, and `0x21F` at 6529. Other flock members have different initial
phases, so the first missing body is already frame 6526, task 65761.

The fix holds the final valid pose `0x231` only for the submitted invalid
`0x232` phase. The internal counter, vertical bob reversals and 20-frame motion
cycle are unchanged. This removes a background disappearance; it is not
evidence of a combat owl whole-body white material.

```
bat_face_test <game> - 6522 6782 --ambient
```

This optional mode of the existing test checks the actual asset families,
requires a body submission for every live flock task, exercises all 19 valid
poses and held boundaries, and checks that wraps still span exactly 20 frames.
Its negative control against the saved pre-fix library fails on the real
missing task at 6526. Probe source/results are under
`build/graphics-investigation/background-birds-probe/`; the negative test log is
`build/graphics-investigation/lighting-analysis/ambient-negative.log`.

## Evidence and remaining scope

`build/graphics-investigation/bat_lighting_metric.py` produces
`bat-lighting-image-stats.json` and lossless owl comparison crops. CPU CSVs/logs
are in `build/graphics-investigation/lighting-analysis/`: `leech-exact-edits.*`,
`shot-live-dead.*`, `shot-live-dead-scene.*`, and the negative regression logs.

The live owl's main body has no specular term; all 45 animated wing-frame
assets have matching valid materials and no out-of-family frame beyond the
separately fixed face midpoint. Its unit-scale normals do not exhibit the
leech/fish amplification. No arbitrary whole-body whitening command was found.
Leech material-cache edits were checked against the original inline records:
the persistent seam normal writes remain finite and shorten normals instead
of amplifying them. The authored leech diffuse change at world Z -515 is
unchanged. These audits limit the claims to the reproduced causes above.
