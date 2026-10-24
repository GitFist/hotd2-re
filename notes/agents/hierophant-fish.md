# Hierophant summoned-fish glare (2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> See [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections.

The second boss's summoned fish reproduce the reported bright body patches with
shooting stopped. They are actor `0x51`, task `0x00438730`, created by
`SpawnFishJump` from Hierophant's `FishWave`. Their animated body models are
`0x1156..0x1169` in `fish.bin`. This is a different family from the earlier
schooling-fish actor `0x42` lighting correction.

## Cause and correction

The PC routine draws the summoned bodies at uniform scale 0.3, with unit authored
normals. Legacy inverse-transpose normal transformation and disabled
`NORMALIZENORMALS` give the shader a normal approximately 3.333 units long. Most
fish materials have specular exponent 5 (power 32), with specular RGB coefficients
from approximately 0.15 to 1.0. This makes the normal/halfway dot product reach
its upper clamp over a broad angular range. A narrow highlight becomes a bright
plateau across the eye, fins and body as the fish turns.

An opt-in `unitSpecular` draw flag now normalizes the normal only for the specular
calculation. The existing diffuse calculation, authored material coefficients and
specular exponent remain in use, preserving smaller glints. `DrawFish` applies
the flag to physical bodies in both branches of control flag bit 4, including
Hierophant's summoned fish and opaque fish bodies. `TaskEscape` applies it only
to its physical-body branch; its bit-4 water-shadow branch retains its previous
lighting behavior.

This deliberately corrects legacy PC rendering; it is not a claim of closer
pixel fidelity to the 2001 executable. Original `0x00439860` and `0x00439C20`
apply the same 0.3 scale. Their `0x0041EBB0(scale)` calls are literal no-ops in
the executable, and the original render-state initialization does not enable
normal normalization.

The opt-in follows `WorldDraw::unitSpecular` through `Scene::BuildFromWorld` and
`ModelRenderer` into the shared shader. Per-draw defaults keep other models on
their existing path. The shadow helper, blood/hit effects and reflection paths
receive no opt-in. No texture, material blend/depth state, geometry, input or
damage behavior is changed by this correction.

## Matched capture evidence

QA files are under `build/graphics-investigation/`:

- Before: `hierophant-before-26600.png` through `hierophant-before-26730.png`.
- Corrected: `hierophant-specular-26600.png` through
  `hierophant-specular-26730.png`.
- Both runs use the same no-shoot input beginning at frame 26594, at 640 by 480.
  Their logs end at frame 26730 in game mode 6, P1 state 5, lives 4, ammo 4,
  score 36890, with 27 model draws and 16 sprites. Player-damage splats later in
  this window occur because the fish attacks; they are not shots at the fish.

All 131 frame pairs were measured. 115 pairs have changed pixels; 16 are
identical. The changing areas track the fish's visible silhouette. Difference
masks and magenta overlays were inspected at small-body and close-up frames:
26650, 26670, 26675, 26678 and 26690. They identify the eye, fins and body surface;
the boss, scenery, HUD and opaque portions of the player-damage overlay remain
unchanged outside that silhouette. The shader change affects color, so the
paired fish geometry and its on-screen bounds remain aligned.

Metrics count pixels whose RGB value differs in each matched pair. Near-white
means `min(R,G,B) >= 230`. Before and after near-white counts are evaluated only
inside that frame pair's changed-pixel mask, then summed over the 131 frames.
This excludes unrelated bright scenery and HUD pixels; the spatial union of
differences across time is not used as a background-contaminated counting ROI.
These are pixel-sample counts, not a measurement of flash frequency.

| Measurement | Result |
| --- | ---: |
| Changed pixel samples, summed across frames | 1,629,092 |
| Near-white samples before | 31,973 |
| Near-white samples after | 1,711 |
| Reduction within the changed-pixel masks | 94.65% |
| Largest changed frame | 26678, 54,095 pixels |
| That frame's inclusive changed bounds | x 0–397, y 87–382 |

The nonzero corrected near-white count and the paired images show that glints
remain. No-hit input also separates this body-lighting result from the separate
legacy muzzle flare and green blood effects investigated in `actor-lighting.md`.

The best presentation pair is **frame 26670**: the complete fish is large and
unobscured, before the orange player-damage overlay. Its changed pixels are
confined to inclusive bounds x 29–232, y 100–318; near-white samples fall from
431 to 12. Frame 26690 is a useful closer eye comparison (755 to 32), while
26678 shows the largest affected body area with some player-damage overlay.

Reproducible numerical output is in `hierophant-fish-metrics.json`; its script
is `measure-hierophant-fish.py`. Selected visual masks are named
`hierophant-diff-mask-<frame>.png` and
`hierophant-diff-overlay-<frame>.png`. The spatial-union mask is useful only for
showing the fish's movement envelope, not for the reported near-white counts.

The GPU regression checks exercise the shared shader correction on both D3D11
and SDL GPU. A separate matched SDL GPU capture at frame 26690 also reduces
near-white pixels within its changed mask from 754 to 32, while preserving the
same gameplay result. The matched gameplay logs confirm the sampled outcome remains the
same; they are not a complete proof of all gameplay-state parity. Bat body
flashing is a separate report and is not covered by this fish-specific fix.
