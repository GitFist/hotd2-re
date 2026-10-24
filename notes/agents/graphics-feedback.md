# Graphics and cutscene feedback (build X, 2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> See [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections.

## Confirmed fixes

Skipping the stage-4 Strength introduction used to stop the current Goldman line,
then start the rest of his dialogue during the fight. The skip watcher updated its
native `skip2230` field without updating event global `0x009A2230`, which civilian
actors read. Setting and clearing both representations ends Goldman's task through
the existing script/audio path. No sound backend or music cancellation is changed.
The initial build X used multiplayer compatibility ID `2026101903`. This is
historical build information; current peers must use matching versions under the
current client's compatibility policy.

Smoke plumes contained horizontal gaps with either original or 2x textures. The
first-boss skyline plume (model `0xB40`) and Magician reveal (`0x1AB8`) contain
overlapping transparent cards. Their authored mesh order can put a front card
before a rear card; precise depth then rejects part of the rear card. Multi-mesh
models made entirely of parallel XY alpha cards now sort those planes back to
front. Their vertical centers cannot supply the sort depth: the cards have
different heights, so camera pitch can reverse center ordering. Material depth
tests/writes and the model's position in the mixed model/sprite list are preserved.

The shared shader also allowed specular dot products above one before raising
them to a power, producing white highlights for scaled normals. Native D3D7
`ProcessVertices` reference measurements clamp this input. The shader now matches
that behavior while preserving the original diffuse-light calculation.

Skin deformation also now uses the original engine's matrix operation order and
inline-record flag bits. Four nontrivial transform fixtures executed through the
original `0x00419980` routine provide independent expected outputs, including the
reflected twin's negated normals. This corrects numerical parity; its sampled
Emperor image changes are too small to establish a fix for the reported flicker.

The remaining Emperor flicker was traced separately to unstable ordering of its
equal-depth core and reflective models. Changing HUD sprite counts could reverse
their order. The renderer now retains authored model order within exact
priority/depth ties while preserving original sprite slots and sprite tie order.
Matching no-hit captures show the large blue/gray body jumps disappear; large
pixel transitions decrease 57-60%. This also stabilizes other tied transparent
world surfaces. See `emperor-flicker.md` for the original trace proof and metrics.

Two actor-lighting omissions were also restored from original code: living
schooling fish body/shadow draws, and stage-1 leech water reflections, now select
character lighting and restore scene lighting afterwards. The separate leech
floor shadow retains scene lighting. See `actor-lighting.md` for scoped evidence.

The reporter then identified Hierophant's summoned fish, a separate actor family
from the schooling fish above. These reproduce broad white highlights without
shooting: the inherited 0.3 model scale lengthens inverse-transpose normals to
3.333, widening the specular highlight over the eye and fins. Those physical
fish bodies now use a unit normal only for specular lighting; diffuse lighting,
glints, shadows and hit effects retain their existing behavior. Matched no-shot
captures reduce near-white samples inside the actual changed-pixel masks by
94.65%. This deliberately corrects a legacy artifact. See `hierophant-fish.md`.

A separate inherited flying-bat animation index selected an opaque foot mesh
instead of face feathers once per 30-frame cycle. Clamping only that invalid
midpoint to the last valid face frame fixes the head/outline pop. All other
126 of 131 no-shot frames remain identical. See `bat-face-animation.md`.

The follow-up investigation reproduced three more bat stability problems:
scaled leech bodies had a broad white specular plateau, owl bodies switched to
stronger white lighting on death, and a background flock selected an unresident
effect instead of a bat for one phase of its 20-frame cycle. Leech bodies and
reflections now use unit specular normals; owl corpses retain scene lighting;
the background flock holds its last valid pose during the invalid phase without
altering motion timing. See `bat-lighting-remnants.md` for native references,
actual captures, CPU reference lighting and negative regression controls.

Distant stone textures also reproduced scenery shimmer during ordinary camera
motion. Material-aware generated mip levels now filter minified 3D textures,
while preserving authored close-up magnification and level-zero HUD/sprite
sampling. Opaque RGB is retained even when its unused alpha is zero; translucent
variants filter RGB by coverage. Both the original and port lacked smaller
levels, so this is a deliberate anti-aliasing improvement. See
`scenery-filtering.md` for scope, paired sequences and shader/backend checks.

## Verification

- `sound_test` checks skip-state coherence and the existing sound channel behavior.
  Its optional `--cutscene-replay <config> <input-script> <skip-frame> <end-frame>`
  drives an actual stage and checks that voice stops, does not restart, and leaves
  music and boss gameplay active. Stage-4 skips at 48300 and 48700 pass. The
  unskipped sound trace remains identical.
- `render_transparency_test [sdlgpu]` checks both card orders, offset card centers,
  camera pitch, distant translated origins, blending, and opaque occlusion.
- `render_lighting_test [sdlgpu]` checks scaled and unscaled normals against native
  D3D7 lighting measurements. The old shader returned 255 instead of 20 in the
  bounded-specular negative control.
- `render_depth_test [sdlgpu]` retains the rooftop precision and sprite/model
  occlusion checks.
- `render_order_test [sdlgpu]` varies 0..48 unrelated HUD sprites against three
  overlapping tied surfaces and four offscreen models. It checks unchanged world
  pixels, authored layer color, original sprite slots/order and priority/depth
  groups, with depth writes on/off and both separate/mixed sprite priorities.
  The old sort changed 2,580,480 world pixels; both fixed backends change zero.
- `actor_lighting_test` replays actual tasks: 4,320 body/reflection draws and 240
  floor shadows for stage-1 leeches, and 1,284 schooling-fish body/shadow draws.
  Both now have zero light mismatches; the old paths had 2,160 and 1,284.
- `skin_test <game>` verifies the original-output transform fixtures and the
  existing real-asset skin definitions and vertex-offset mapping.
- `bat_face_test` validates the real asset-family boundary and replays all 30
  live animation phases: 262 face draws, including 10 corrected midpoint cases.
- The fish-only specular GPU fixtures pass on D3D11 and SDL GPU. They remove an
  angled scaled-normal highlight, retain an aligned glint and native diffuse
  response, and check that the flag does not leak between draws or scenes.
- `render_minification_test [sdlgpu]` reproduces a projected texture changing
  between black and white under subpixel motion. Both corrected backends remain
  at RGB 128; close-up model sampling, one-pixel HUD glyphs, opaque zero-alpha
  RGB and transparent-edge colors retain their required behavior.
- The extended actor replay verifies 62 live/dead owl bodies, 7,560 leech body
  and reflection draws, 420 floor shadows and 1,284 schooling-fish submissions
  with zero light/scope mismatches. Ambient-flock replay verifies all 7,830
  resident bodies, correcting 390 absent phases while retaining 360 complete
  20-frame cycles.
- The initial 325-frame smoke/skin screenshot sweep leaves 288 frames identical.
  Most changed frames contain tiny skin/lighting corrections; the largest changes
  repair the two smoke scenes. Original and 2x textures were also checked directly
  at frames 10650 and 65850. Local QA output is under `build/qa/`.
- The subsequent 325-frame sweep with model-tie and actor-lighting fixes reaches
  frame 97150 with the same score 150060; 248 frames remain identical to the
  preceding fixed build. Changed frames include the intended Emperor compositing,
  tied hit-effect layers and actor lighting. HUD sprite order checks pass.
- Two clients using a local relay reach frame 3000 with matching state hashes,
  including 140 deliberately dropped packets. This is a same-PC regression check.
  It predates the final fish-specular and bat-face changes.
- The fish/face 325-frame sweep reaches frame 97150 with score 150060. All sampled
  images remain identical to the previous fixed build; the separately captured
  fish and bat encounters demonstrate the narrowly timed corrections.
- The subsequent bat/scenery build again reaches frame 97150, P1 state 8,
  lives 3, ammo 6 and score 150060, with 325 sampled images. Original textures,
  the 2x pack and SDL GPU were also checked directly in the reproduced scenes.
  The packaged executable, staged executable and ZIP member hashes match; ZIP
  CRC and release privacy checks pass. `build/dist-scenery-x-20261019/verification.json`
  records the final package hashes and scoped evidence.

## Remaining investigation

The reported Emperor blue/gray compositing flicker is now reproduced and fixed.
The reporter's unspecified scenery location and GPU are unavailable. Shared
tie-order stabilization, distant-texture filtering and the background-flock
correction address demonstrated instability, without establishing that every
possible scenery pop has been reproduced. Fine distant textures intentionally
become smoother; close-up sampling and the HUD remain unchanged.

Hierophant's summoned-fish washout and the periodic flying-bat face pop are now
reproduced and corrected. The continuation additionally fixes the leech white
highlight, owl death light jump and cyclic background bat disappearance. These
separate causes must not be attributed solely to the face-index bug. A diagnostic at frame 17281
removes the white starburst by suppressing only the two player muzzle-model draws;
blood remains green and present. Thus the sampled white rays must not be called
white blood or a bat-body material effect. Production shot effects are retained.

An additional consecutive-frame original Emperor capture was attempted in an
isolated game folder with a documented memory-only boss-unlock fixture. The legacy
executable repeatedly stalled before frame zero, so no new Emperor comparison is
claimed. Test processes and the task-mounted disc image were cleaned up.
