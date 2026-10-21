# Optional modern lighting and installed visual packs

The native Build Z rendering path remains the default. `GraphicsOptions::perPixelLighting`
enables a separate fragment-lighting path for opaque, lit model batches. It uses the current
game directional, ambient, point and spot lights; it does not invent extra scene lights.
Normals use inverse-transpose WORLD followed by normalization. Diffuse and specular dots are
bounded, degenerate transforms/normals have finite fallbacks, and native fog is applied after
illumination. The simulation, source models, hit geometry and material ordering are unchanged.

Blended pass-1 effects, forced-alpha records, sprites and unlit draws retain their native
paths even when enabled. This preserves the recent smoke, fish-specular, actor and Emperor
ordering fixes. Turning the option off restores the original Gouraud path; there is no
normalization change to native diffuse lighting.

`render_lighting_test` checks native unnormalized/specular behavior, fish-only corrections,
curved-normal interpolation, bounded highlights, point-light range, and exact alpha/unlit/
Original transitions. Run it for D3D11 and `sdlgpu` after building both backends.

`InspectTexturePack` and `DiscoverTexturePacks` in `render/asset_store.h` inspect installed
packs without selecting or modifying them. They recognize the existing color-image layout:

```
pack-root/
  pack.ini                 # optional
  tex/<bank>/<index>.png
  bmp/<file>.png
```

An optional `pack.ini` has `format=1`, `name=`, `author=` and `version=` keys. Legacy packs
without one work. Discovery checks supported PNG headers and image-size bounds; it does not
claim that every pixel stream has decoded. `InspectTexturePack(path, true)` additionally
validates all color images using the game's actual PNG decoder. Missing overrides retain
original assets. Invalid packs can be shown with their error and must not become active.
Packs with `Sound/` are marked as mixed content, because the existing asset override directory
also affects sound. A selection should be explicit and apply on restart to avoid invalidating
renderer pointers and pending prefetch jobs during a frame. `HOTD2_ASSET_DIR` still wins over
the saved setting. The empty path remains Original.

`texture_pack_test` generates its own PNG and temporary directories. It checks legacy and
manifest packs, mixed sound content, unsupported/interlaced/oversized images, optional full
decoding, duplicate paths and stable sorting. No copyrighted textures are added.

## General authored material maps

The general material path uses `pack.ini` with `materials=normal-roughness-v1` and
`materials/tex/<bank>/<index>_nr.png`. Maps must be RGBA8: RGB encodes a unit tangent-space
normal from -1..1 into 0..1, and alpha is perceptual roughness. Normal Y follows the model's
positive texture V direction. Images are stored upright and flipped into the original
texture row convention during loading. Missing/malformed maps retain the original material.
There is no image-derived normal-map generator and no included replacement artwork.

This path is independent of the private David experiment and works by texture-bank/index,
with no actor-name special case. The shader reconstructs a UV basis from the deformed
surface and handles mirrored UVs; degenerate UVs retain the geometric normal. Roughness
controls specular width in the game's existing bounded SDR light/material model. This is
not a complete PBR/metalness pipeline. Material maps opt into per-pixel illumination only
on opaque lit surfaces. Default Off retains native rendering and alpha effects.

`render_material_test` uses generated 1x1 maps and geometry to verify mapped diffuse light,
mirrored UV orientation, flat-map behavior, disabled-map exactness and roughness highlights.
Both backend texture bindings must pass this test before exposing the setting.

## Verified depth effects

`GpuDrawList::opaqueDrawCount` marks the initial native opaque-model prefix. An optional
single-sample prepass can replay that prefix into R32_FLOAT plus its own depth target using
`depth_shaders.h`, retaining the same precise camera-space projection. This avoids treating
multisampled depth like color: SDL does not support depth-target MSAA resolves, nor direct
depth-to-color texture copying. The live depth target stays writable for original blended
draws; the sampled snapshot is independent.

Soft smoke compares the particle's eye W to opaque scene W reconstructed directly from
reversed projection coefficients. It avoids subtracting sampled reversed depth from 1,
which would lose distant-surface precision. Only structurally recognized layered alpha
cards or explicitly ordered car-smoke submissions with standard alpha blending are eligible.
Sprites, ordinary alpha actors, cutouts and opaque models retain their existing opacity.
The fragment alpha is faded before the original alpha threshold, so invisible faded smoke
cannot write depth and hide later layers. No snapshot-ready flag means exact native alpha.

`render_soft_particles_test` checks real GPU scene intersections at MSAA1/2/4, clear-background
behavior, ordinary-alpha exclusion and exact Off restoration. The setting must remain hidden
until the backend current-frame snapshot path passes these checks. D3D11 and SDL_GPU
both pass those checks at MSAA1/2/4.

Ambient occlusion uses the same independent opaque depth, reconstructs nearby view positions
and geometric normals, and bounds the contact darkening by world distance. Its fixed kernel
has no random noise/history. Backends multiply it into the opaque target at the opaque boundary,
before mixed transparent effects and HUD, preserving individual MSAA color samples. `render_ao_test`
passes flat-wall identity, local contact occlusion, distant-wall rejection, unchanged HUD and
exact Off restoration at MSAA1/2/4 on D3D11 and SDL_GPU.

Primary implementation references:

- [SDL depth targets: no multisample resolves](https://wiki.libsdl.org/SDL3/SDL_GPUDepthStencilTargetInfo)
- [SDL texture copy: depth/color restriction](https://wiki.libsdl.org/SDL3/SDL_CopyGPUTextureToTexture)
- [Microsoft depth resource views](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3d11/d3d10-graphics-programming-guide-depth-stencil.md)
- [NVIDIA GPU Gems 3: acquiring depth and soft particles](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles)

## Remaining lighting/content dependencies

The separate local David experiment was inspected read-only. Its material path is explicitly
restricted to David's `znsam2` texture/model banks and adds a second normal/roughness texture,
tangent-frame reconstruction and a different specular model. Its PN-triangle refinement runs
after existing CPU deformation with seam/edge constraints. These are not general-purpose
features to enable for every shipped model. The general normal-map and selected refinement paths
are newly authored and separate; neither that private experiment nor its assets was imported here.

Raster shadows, selected screen-space reflections, volumetric haze/shafts and cinematic focus
now have actual generated-geometry pixel proof on D3D11 and SDL_GPU (see below). Optional native
D3D12 ray queries remain a distinct capability with their own GPU proof requirements.

The repository already has the experimental `HOTD2_ALLOW_64BIT` switch; it is not a certified
x64 release. The original hook remains x86, and a supported x64 port still requires full game,
save, network determinism, asset/renderer and packaging regression coverage. This change does
not alter the supported architecture.

## Render-only temporal history and real geometry motion

Scene submissions carry stable task/model/node/role/occurrence identities. ModelRenderer retains
the previous frame's fully deformed model positions and separate model-view/projection matrices,
so CPU skinning, wave and in-memory vertex edits produce real geometry motion. It never mutates
World, replay records or network hashes. New/changed topology, explicit cuts, camera-path changes,
rollback, render-frame discontinuity and resize reset the affected history. Enabling is explicit;
default Off retains original projection and the mixed model/sprite CRT order.

`temporal_frame.h` defines 32-byte motion vertices and 304-byte independent constants. GPU motion
is current-to-previous displacement in render pixels, positive Y down, without projection jitter.
RGBA16F metadata additionally carries expected previous reversed depth and validity. A separately
retained previous opaque-depth resource rejects actual disocclusion. Native D3D12 also produces
a distinct RG16F texture for vendor SDKs; a four-channel metadata texture is not passed as an alias.
Opaque depth-writing geometry supplies motion; transparent/animated texture draws are explicitly
reactive. HUD sprites never enter motion geometry.

`temporal_history_test` passes stable reordered actor/CPU deformation correspondence, topology and
camera/rollback/frame resets, jitter metadata, Off and the optional pre-HUD boundary. `render_motion_test`
passes static/moving geometry, correct sign/pixel scale, jitter removal, actual disocclusion, cuts and
unchanged color on D3D11/SDL_GPU at MSAA1/2/4 and native D3D12 at its supported MSAA1.

`RequiresSceneProcessing` opts a backend into deferring explicit HUD sprites. Scene models and
scene-effect sprites retain their mixed relative order, then `ProcessScene` runs once, then UI is
drawn at the resulting output dimensions. Ordinary backends keep original ordering. Exact unjittered
current/previous camera matrices are also available in `TemporalCameraInfo` for native extensions.

The optional NGX integration uses a shared device-scoped context, separate feature handles and
parameter maps, signed external feature DLLs and final-owner shutdown. `dlss_frame_generation`
records the real vendor create/evaluate calls into native D3D12 command lists, with separate
generated/retained output resources and an interpolation-disable flag. It validates actual scene
depth, two-channel motion, camera matrices and final color; derives camera planes/FOV/basis from
the supplied projection/view; restores resource states; and never advances simulation or presents.
Runtime frame generation additionally needs the native post-HUD boundary and properly paced
generated-then-retained presentation. A successful adapter build alone is not interpolation proof.

Primary temporal references:

- [AMD: motion vectors, jitter and reactive masks](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/)
- [NVIDIA: official DLSS SDK and programming guides](https://github.com/NVIDIA/DLSS)

## Raster directional shadows and optional scene effects

`NeedsShadowCasters` requests a separate render-only caster packet before view-frustum culling.
Sky, camera overlays, layered cards, forced alpha, non-depth-writing and unlit models are excluded.
Actual deformed AABBs fit shadow depth; a camera-relative texel-snapped 1024-unit receiver region
uses a 1024/2048/4096 R32 color + independent D32 depth target. Surface receivers use 3x3 PCF,
world-texel/grazing-angle bias and bounded normal offset, only for the matching directional light.
The default shader remains exact; the model constant ABI stays 1168 bytes. Native DXR t3 is
separate from the conditional D3D11/SDL raster binding.

`shadow_map_test`, `temporal_history_test` and actual `render_shadow_map_test` pass, including an
occluder outside the camera casting into view, moving/excluded casters and byte-exact Off.
D3D11 and SDL_GPU both pass at MSAA1/2/4.

Optional scene effects run after temporal resolve and before explicit HUD submission. They use
actual current camera metadata even when temporal history is Off. TAA history retains its own
unprocessed result, so artistic effects are not fed back into accumulation. Single-sample scene
intermediates are written back to the original main target before UI, including under MSAA.

* Volumetric haze integrates homogeneous Beer-Lambert extinction along the opaque eye-depth ray,
  with 20/28/36 deterministic samples, bounded density/distance, original fog tint and actual
  directional diffuse color. Shadow-map visibility modulates scattering, producing shafts.
  It can request a visibility map while surface shadow shading remains Off.
* Reflections ray march actual view-space directions against opaque depth. A separate surface
  pass accepts actual material specular metadata, with optional authored normal/roughness maps.
  Screen edges fade and missing/offscreen hits preserve original color. This is SSR.
* Cinematic focus uses centre opaque depth and a depth-aware far-background 12-tap blur. It runs
  only in explicit skippable noninteractive gameplay scenes, never during aiming or pause.
  Foreground silhouettes/focal subject and all subsequent UI remain sharp.

`render_scene_effects_test` passes both backends at MSAA1/2/4: actual occluder-modulated scattering,
material-gated reflection hits, background blur with a sharp subject, immediate aiming exclusion,
HUD identity and byte-exact Off. Chapter-wide target visibility/art tuning remains necessary.
Native D3D12 rejects scene settings until it has corresponding proven passes.

Primary implementation references:

- [NVIDIA GPU Gems 3: depth-aware post-process focus](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-28-practical-post-process-depth-field)
- [AMD: screen-space ray traversal and occluder decisions](https://gpuopen.com/download/GDC_Breaking_Down_The_World_Of_Athia.pdf)
- [SIGGRAPH: volumetric scattering and light visibility](https://www.advances.realtimerendering.com/s2021/jpatry_advances2021/index.html)

## Explicit selected visual mesh refinement

The optional pack convention is:

```ini
format=1
meshes=pn-triangles-v1
refine_models=0x135f,0x03ae
```

This selects original model IDs, not external model files. IDs are bounded to 0..FFFF, at most
512 unique entries in an 8 KiB list; unknown conventions, invalid IDs and malformed selections
disable/reject the pack selection. A mesh-only visual pack can be selected explicitly. No pack
or original model is automatically opted in. `meshRefinement=0` is exact original; 1/2/3 uses
2/3/4 subdivisions per edge, restricted to opaque depth-writing geometry. Forced alpha,
layered cards, sky and camera-overlay exclusions remain in force.

New cubic point-normal geometry is generated after CPU skinning, waves, model edits and optional
render-pose interpolation. The original game model and hit geometry remain unchanged. Shared
smooth edges use canonical shared control points. Boundary, UV-seam, hard-normal, material/cull-run
and nonmanifold edges remain straight; UVs interpolate within the original face. Control-point
displacement and vertex expansion are bounded. Refined bounds drive culling and shadow packets;
topology metadata resets motion correspondence when quality changes.

`mesh_refinement_test` passes actual curved geometry, canonical seam correspondence, locked UV/
hard-normal edges, finite bounded validation and original-source identity. `texture_pack_test`
passes manifest selection/invalid IDs. `render_mesh_refinement_test` passes on D3D11 and SDL_GPU
at MSAA1/2/4: visible curved silhouette, exact expanded triangle counts, model-selection exclusion,
fresh dynamic CPU deformation, alpha exclusion and byte-exact Off. Selected real content still
requires its own art/culling/cost audit before a pack should enable it.

- [Vlachos et al.: Curved PN Triangles](https://drivers.amd.com/developer/CurvedPNTrianglesI3DG01.PDF)
- [NVIDIA GPU Gems 3: generic adaptive mesh refinement](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-5-generic-adaptive-mesh-refinement)

## Direct optional AMD FSR 3.x frame generation

`FsrFrameGeneration` loads an installed signed AMD frame-generation DLL, queries supported
providers and explicitly selects 3.x. Its independent context carries the official API-version
extension and `NO_SWAPCHAIN_CONTEXT_NOTIFY`, leaving presentation with the native backend.
Actual final and matching HUDless RGBA8 textures, reversed depth, two-channel float motion,
unjittered camera basis/range/FOV, render size, jitter and measured frame interval feed
`PrepareV2` and a real frame-interpolation dispatch. Resources return to their documented states.

The generated resource contains actual vendor output; a separate retained resource is an exact
GPU copy of the final rendered frame. Reset, pause, discontinuity and long-gap frames return
retained-only eligibility and a safe generated copy. AMD exposes no NGX-style vendor-disable
resource: the adapter's real four-byte GPU flag explicitly describes these known application
conditions, not vendor telemetry. Native generated-then-retained presentation consumes the same
resource-pair contract as NGX. The current adapter accepts SDR RGBA8 only and rejects unsupported
formats instead of making an untested HDR claim.

`fsr_frame_generation_test` passes on actual hardware using signed provider 3.1.6 through the
native post-HUD callback. A moving actor's generated centroid lies strictly between its previous
and current positions; stationary background and HUD remain stable, retained pixels match the
actual final image byte for byte, and reset/pause frames safely copy the retained image. This
is GPU interpolation evidence; headless tests do not claim a measured presentation/latency gain.

- [Official AMD FidelityFX frame-interpolation API](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/frame-interpolation-api.md)
