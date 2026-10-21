# Optional image-clarity pipeline

`GraphicsOptions` is renderer-local cosmetic state. Its default values preserve
Build Z's authored sampling and original final image; simulation, collision,
gameplay, replay and network state are not affected.

Both D3D11 and SDL_GPU/D3D12 implement the same options:

- MSAA Off/2x/4x: colour and reversed-depth attachments share their sample count,
  with a separate single-sample resolve image. Multiple `Submit` calls retain the
  multisample attachment. SDL uses `RESOLVE_AND_STORE` for that reason.
- Anisotropic filtering Off/2x/4x/8x/16x: model materials that already opt into mips.
  The GPU sampler uses anisotropic minification; a shader level-zero point fetch
  retains authored point magnification and wrap/mirror/clamp addressing. Sprites
  retain their authored sampling and remain clamped to level zero.
- Texture filter: authored, nearest magnification, linear magnification. This
  override applies to models only, without changing sprite or mip-chain contents.
- Alpha-to-coverage: only alpha-tested, non-blended models while MSAA is active.
  Existing smoke/additive/translucent draw states are kept. This is a tested
  backend capability, held out of the player-facing menu pending an explicit
  cutout-material classification; shipped pass-1 effects remain blended.
- FXAA: a directional contrast filter of the final image. It also filters HUD/menu
  edges because the original renderer interleaves 3D and sprite submissions.
- SMAA1x: the official SMAA HIGH spatial algorithm, with separate edge, blend
  weight and neighborhood passes and its actual area/search lookup textures.
  It also includes the HUD. The pinned upstream source and lookup provenance
  are retained under `third_party/smaa`.
- Intel CMAA2: the official HIGH conservative morphological compute algorithm
  and candidate/deferred color processing. D3D11 uses GPU indirect dispatch;
  SDL uses bounded launches guarded by the actual GPU candidate counts to avoid
  indirect/UAV buffer aliasing through its public API. All three structured
  working arrays are translated to equivalent raw byte-address accesses because
  SDL exposes raw buffer descriptors. Both use the same translated source and
  manual sRGB decoding on input, with Intel's sRGB output conversion. The pinned
  upstream source remains unchanged, with Apache-2 and Microsoft MIT notices.
  Full-capacity buffers include the exact odd-size quad count. The SDL bounded
  dispatch path supports up to 8.3 million internal pixels.
- AMD FSR1: official full-precision EASU upscaling followed by optional RCAS.
  This runs only when the output is at least the internal size on both axes and
  larger on one. It is a spatial scaler, with no motion/history reconstruction.
  Pinned upstream code and its license are under `third_party/fsr1`.
- Sharpening: bounded local unsharp mask in three strengths.
- Area downsampling: integrates up to 8x8 source samples per destination pixel
  when the internal image is larger than the output. Upscaling retains the sharp
  bilinear path. This is spatial supersampling, with no temporal history.
- Brightness, gamma, contrast, saturation and three colour grades (warm, cool,
  monochrome): neutral branches bypass the adjustment exactly.
- SDR bloom: conservative bright-area glow, Off by default. It is not HDR,
  physically based illumination or a substitute for a future bloom render graph.
- VSync: runtime interval selection; SDL reports an actual VSync fallback if an
  unsynchronized swap-chain mode is unsupported.
- Allow tearing / VRR: D3D11 queries DXGI's tearing capability before creating the
  swap chain and uses the presentation flag only with VSync Off. SDL selects
  Immediate when allowed, Mailbox without tearing when available, otherwise
  reports the actual VSync fallback. This allows VRR-compatible presentation;
  it cannot prove that the monitor/driver has VRR enabled.
- Low latency: D3D11's waitable swap-chain queue is limited to one frame; SDL's
  allowed frames in flight is set to one. Both skip a busy presentation surface
  using a nonblocking readiness check, leaving input/network polling running.
  Capability flags hide unsupported choices, and headless targets reject these
  window-only presentation options.
- Soft particles: a separate single-sample opaque R32 depth prepass gives known
  smoke cards a bounded intersection fade. The depth pass has its own D32 target,
  so MSAA never relies on an unsupported depth resolve. Blended fish, bats and
  foliage are not reclassified as smoke.
- Normal/roughness maps: opt-in material-pack maps are sampled on opaque lit
  surfaces. A missing map disables that draw's flag. The repository supplies no
  replacement art.
- Contact ambient occlusion: depth reconstruction and bounded world-radius
  samples produce a multiplicative pass at the opaque boundary, before smoke
  and HUD. It retains MSAA coverage and alpha rather than copying a resolved
  color image back over multisample geometry.
- HDR output: optional FP16 linear scRGB presentation on a display whose OS HDR
  mode is already enabled. The final pass decodes SDR sRGB and maps reference
  white to 80..400 cd/m2 (scRGB 1.0 = 80 cd/m2). The original RGBA8 art/scene
  remains SDR-authored; this is correct HDR output encoding and adjustable
  reference white, not new HDR scene lighting or a wide-gamut asset upgrade.
  Unsupported/headless physical-output requests fail without changing options.
- Temporal AA: a pre-HUD resolve with dense geometry motion, retained prior
  opaque depth, RGB neighborhood clamping and bounded history feedback. Current
  and previous conservative reactive masks reject animated alpha/scene effects.
  Stable actor identities include previous CPU-deformed positions, not just
  object transforms. Frame gaps, topology changes, rollback, resize and camera
  cuts invalidate history. Missing real motion/coverage fails the resolve and
  retains the current native scene; zero/static fake motion is never substituted.
  The D3D11/SDL capability is distinct from native D3D12 vendor reconstruction.

`ConfigureGraphics` applies between frames. Invalid values and unsupported MSAA
counts fail without changing options or targets. Replacement colour, depth,
resolve and readback resources are fully allocated before replacing live ones.
The caller presents the error and may explicitly select a lower setting.
`EffectiveGraphics` exposes accepted state. Headless capture exercises MSAA,
samplers and final-image effects through the same shaders used by the window.

`render_graphics_test` checks GPU pixels for MSAA coverage and split-submit
retention, FXAA edge smoothing and flat preservation, exact Off restoration,
transaction guards, anisotropic short-axis detail, point magnification and
negative-UV addressing, sprite isolation, colour effects, cutout coverage and
target resizing. It accepts `sdlgpu` to run the same checks on SDL_GPU.

`render_presentation_test` uses actual hidden platform windows to check capability
reports, accepted/rejected settings, queue readiness, VSync, resize, returning to
normal presentation and shutdown, on both APIs. It does not change fullscreen
display modes or claim a measured end-to-end latency improvement.

`render_material_test`, `render_soft_particles_test` and `render_ao_test` exercise
real GPU output, including MSAA 1/2/4, opaque/transparent classification, nearby
and far-depth exclusions, HUD preservation and exact Off restoration.

`render_hdr_test` reads an actual FP16 GPU target and compares black, the sRGB
transfer-function breakpoint, midtones and white to the scRGB reference at
80/200/400 cd/m2. The diagnostic does not enable HDR or change a monitor. A
passing diagnostic does not prove the visual calibration of a physical display.

`render_motion_test` checks actual GPU motion vectors, jitter cancellation,
actor reordering, prior-depth disocclusion and unchanged color at MSAA1/2/4.
`render_taa_test` checks temporal edge stabilization, flat identity, cuts,
animated reactive coverage, HUD isolation, missing-metadata rejection and Off.
The generated jitter fixture's RGB temporal variation drops from 280704 to
76821 at 1x, 283968 to 47235 at 2x and 281904 to 37321 at 4x on both backends.
These are fixture results, not a claimed percentage improvement for the game.

`render_cmaa_test` checks dense full-screen input at 64x64, odd 127x95 and
256x192, repeated counter/list reset, flat color identity and exact Off. Both
backends produce the same output hash `6459db7beface7c2`. The shared graphics
test also verifies CMAA2 smooths a high-contrast diagonal.

Vendor reconstruction and frame-generation SDK paths remain separate ongoing
engine work. No spatial scaler is represented as a temporal algorithm.
Binary redistributions must include `THIRD_PARTY_NOTICES.txt`.

## Later display and performance additions

Independent phosphor mask (aperture/slot/shadow), restrained CRT glow and radial
curvature are default Off. `CrtWarpUV` matches the actual present shader so the
runtime can map every pointer through the same display geometry. Real GPU tests
on both APIs verify image/aim agreement, black curved corners, independent mask
patterns, glow and byte-exact Off. Output-relative resolution helpers support
match-output and 1.5x/2x supersampling with bounded dimensions; calibration cards
provide black-level, grayscale/gamma and pixel-grid references.

D3D11 GPU-budget feedback uses actual timestamp/disjoint queries in a bounded
eight-slot ring. Timing starts at the first scene submission, after CPU scene
assembly, and ends before the Present wait. Polling never waits for the GPU and
rejects disjoint/stale samples. `render_gpu_budget_test` proves actual completed
samples, excludes an inserted CPU pause, and checks resolution hysteresis,
warmup, bounds and stale-sample rejection. SDL_GPU's public API has no timestamp
query interface, so that backend explicitly reports GPU timing unavailable.

Compiled DXBC is persisted with exact source, entry point, shader profile,
macros, compiler flags and compiler-DLL identity in the cache key. The bounded
file payload has a checksum and DXBC signature, writes replace atomically, and
corruption/I/O errors fall back to genuine compilation. Driver-specific pipeline
objects are still created fresh. `shader_cache_test` exercises real compiled
blobs, cache hits, key separation, corruption and unwritable-cache fallback.

Render-pose interpolation is separate from temporal antialiasing. It keeps two
consecutive simulation ticks keyed by stable actor/node identity, uses quaternion
rotation and signed scale/translation interpolation, and blends fully deformed
positions/normals in render-owned dynamic model copies. UV/material animation,
HUD and view-space overlays remain current. Repeated presentations of one tick
reuse the same endpoints. Mode/world/camera changes, frame gaps, rollback,
camera jumps/turns, local actor teleports and changed topology have conservative
reset/fallback policies. Sheared or degenerate matrices keep the current authored
pose. Capture is bounded to 262144 dynamic vertices per tick; excess geometry
retains its current deformation. No interpolation state enters World or netplay.

The optional feature adds up to one 60 Hz tick (16.7 ms) of visual latency; it
does not make the simulation run at 120 Hz. `render_pose_test` proves fixed tick
pairs, quaternions, reflected/nonuniform scales, deformation, topology/cuts,
stable identities and Off. `render_pose_gpu_test` proves actual quarter/midpoint
raster positions for rigid/deformed geometry at MSAA1/2/4 on both APIs, immediate
HUD and exact Off. `scene_pose_test` covers six chapters with unchanged gameplay
camera, frame and RNG, repeatable sub-tick geometry and native restoration.

## Independent output-resolution HUD

`outputResolutionHud` is an explicit, default-Off D3D11/SDL option with its own
capability flag. The existing pre-HUD boundary resolves scene TAA and depth
effects at internal resolution, then spatial AA, FSR1, area downsample, sharpening
and SDR bloom into a separate output-sized RGBA8 target. A fresh single-sample
D32 attachment supports ordered explicit UI there. World sprites and transparent
models retain their internal scene order. `TargetWidth/Height` continue reporting
scene input dimensions; `HudTargetWidth/Height` report the accepted UI dimensions
after the boundary. Output dimensions come from the present image rectangle,
including headless callers that set `PresentParams.w/h` explicitly.

Color adjustment, CRT scanlines/mask/curvature/glow and optional scRGB conversion
run once on the composed image. Spatial reconstruction and sharpening never run
over the extracted UI. Allocation/invalid-size failures keep the internal HUD and
expose `HudCompositionError` for the runtime; valid later resizes recover. Output
targets support dimensions up to 8192 with a bounded 16-million-pixel allocation.
Off never extracts UI and restores the original mixed pass-1 image exactly.

`render_hud_output_test` compares actual GPU pixels with a full-resolution,
single-sample UI reference at even and odd output sizes, for MSAA1/2/4 on both
APIs. It verifies recovered fine checker detail, unchanged internal world sprites,
pre-UI spatial AA/FSR/sharpen/bloom, TAA metadata dimensions, once-only color/CRT,
FP16 scRGB transfer/paper-white mapping, failed resize fallback/recovery and Off.
The reference is intentionally single sampled like the output UI; comparing a
multisampled fine checker can introduce fractional-edge coverage before CRT glow.

## Bounded state and pipeline warmup

`WarmupGraphics` creates real common GPU objects between frames, with no dummy
draw or dispatch. D3D11 warms 23 depth/blend state objects and 36 samplers. SDL
warms 39 main raster PSOs, 55 with independent output HUD, or up to 73 with the
tested selected depth/motion/shadow/effect paths, plus 36 samplers. These are
bounded common states; unusual authored material combinations may still create
objects lazily. The call reports actual created/resident counts and CPU API
creation time, not GPU time or a measured frame-latency improvement.

Repeated calls are cache-idempotent. Color changes retain reusable state/PSO
objects. Anisotropy changes invalidate sampler policy only. SDL main raster
PSOs are invalidated on a global MSAA change; actual alpha-to-coverage and the
single-sample output-HUD path are explicit key bits. Scene/HUD targets remain
RGBA8, with distinct SDR/scRGB present pipelines. D3D11 blend cache keys contain
all blend/coverage inputs, and sampler keys contain filter/address/mip state.

`render_warmup_test` proves real object creation, bounded counts, idempotence,
zero additional common objects on an actual first-use draw, exact pixels against
an unwarmed device, active-frame rejection, color-only cache retention and exact
sampler invalidation after AF changes. All seven MSAA/HUD/effect cases pass both
APIs. HUD, graphics/MSAA/filter and TAA GPU regressions pass after the lifecycle
change. Native D3D12 currently reports this optional warmup API unsupported.

Relevant API contracts:

- <https://wiki.libsdl.org/SDL3/SDL_GPUColorTargetInfo>
- <https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsSampleCount>
- <https://wiki.libsdl.org/SDL3/SDL_GPUMultisampleState>
- <https://wiki.libsdl.org/SDL3/SDL_GPUSamplerCreateInfo>
- <https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkmultisamplequalitylevels>
- <https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains>
- <https://wiki.libsdl.org/SDL3/SDL_SetGPUAllowedFramesInFlight>
- <https://github.com/iryoku/smaa>
- <https://github.com/GPUOpen-Effects/FidelityFX-FSR>
- <https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range>
- <https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition>
