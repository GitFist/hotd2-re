# Naomi graphics

Naomi is the default graphics profile for HOTD2 and Typing of the Dead when settings have no saved graphics-mode choice. **Options > Graphics > Graphics mode** selects the PC or Naomi profile for both games. Both profiles share gameplay and networking. Each saves its blood color separately; Naomi starts with Red. Switching back restores the saved PC graphics preferences. Typing uses Naomi replacements for verified shared scenery, enemies and effects, alongside its own word panels, characters and other exclusive artwork.

The profile uses verified cartridge textures and meshes, including higher-resolution scenery/effects and the David/Zeal geometry differences. Per-mesh bindings support differently grouped stage banks and preserve the record addresses used by skinning, waves and model edits. Native 4-bit/8-bit artwork uses the original palette data. It restores standard and local TABLE lighting, specular calculation, quantized fog, authored filtering and level-zero sampling. Optional modern scene effects are suspended while Naomi is selected; display resolution and presentation controls remain available.

Muzzle flashes use the correct warm/blue player artwork, native texture-driven opacity and direct camera-space positioning. Direct3D 11 uses exact per-pixel GPU transparency sorting with opaque-scene depth testing and no translucent depth writes. The resolve runs in bounded screen tiles without dropping layers or changing resolution. Naomi uses a single-sample scene target; saved PC multisampling preferences are restored in PC mode, and post-process AA remains available. The flash calculation stays independent of camera distance and render interpolation. Other renderers retain the bounded CPU sorting path.

Blood restores the separate 58-frame opaque polygonal effect whose PC function was an empty stub, the native 64×64 pool texture, and the original ripple mesh sequence. Existing translucent blood sheets retain their source alpha modes and use native depth/sorting behavior. The additional effect has its own visual lifetime; it does not allocate simulation tasks or consume gameplay randomness.

Release 2.1 also restores the leaping and knife-throwing enemies' death bleeding: five bursts, 20 gameplay frames apart, stopping when the body is removed. This shared routine covers Chapters 1, 2, 4, 5 and 6, pauses with gameplay, and leaves PC rendering and multiplayer simulation unchanged.

Original stage-object bindings restore source geometry where the section tables prove the complete object correspondence. This includes the chapter-five structure whose PC version replaced thousands of frame triangles with flat cutout cards. Regrouped surfaces retain exact native UVs and artwork only when the source correspondence is unambiguous.

## Content

The runtime reads a standalone data bank, not an emulator or cartridge executable. Build the bank offline from the supported USA cartridge and the existing PC data:

```powershell
python tools/import_naomi_visuals.py --rom "PATH\hotd2.zip" --pc-game "PATH\game" --out "PATH\game\naomi_visuals.bank"
```

The importer uses NumPy and verifies the source chip hashes. The bank is discovered beside `Hod2.exe` in the game data directory. An isolated test can instead pass `--naomi-visuals "PATH\naomi_visuals.bank"`; `--graphics-mode pc|naomi` overrides the initial selection. Naomi startup requires a valid bank and reports missing or damaged data without overwriting the saved graphics-mode preference. Unconfigured diagnostic screenshots retain the PC reference unless Naomi or an explicit settings file is requested.

Release 2.0 includes the verified bank in the complete x64 ZIP. Existing clients continue using the compatible client-only updater; after restart, the new executable downloads missing Naomi data once (100 MB) with visible progress, cancellation, a pinned size and SHA-256, and an atomic install. Existing banks and saved PC/Naomi preferences are preserved. The bank is hosted on the companion update release; it is excluded from the client-only ZIP and selective file index.

Release 2.1 includes the corrected Judgment burning-car material and geometry. Its client upgrades the exact verified 2.0 bank to the new bank atomically; cancellation or a failed download preserves the old bank, and modified banks are not overwritten. The separate Naomi Arcade audio bank is also downloaded once on normal launch and included in the complete package.

Scripted/diagnostic launches, explicit bank overrides and `--no-update-check` do not start the automatic download. A cancelled or failed required download can be retried by launching normally, or avoided by installing the complete ZIP. The complete-package tool accepts `--naomi-visuals` and refuses banks that differ from the client's pinned release data.

## Fidelity status

The importer reports native bindings and any missing mesh contexts for the higher-resolution inventory, including additional pool and sky upgrades found through live-draw audits. Unsupported representations and unresolved correspondences outside that inventory still require investigation. Standard/TABLE lighting and fog follow decoded original routines; hardware-wide pixel equivalence, every translucent scene effect and procedural modifier shadows have not been certified. Camera, motion, gameplay and network data retain the existing runtime behavior.

The completed bank supports all 5,372 original texture descriptors, including 334 palette rows, and covers 1,513 verified higher-resolution pairs. The final replay audit found no PC texture fallbacks or transfer-guard failures at six campaign checkpoints. Some equivalent PC geometry remains where only native artwork or unambiguous source UVs are established; this is not a claim of frame-by-frame hardware equivalence.

The original captured arcade regions enable autosorting, which changes effective translucent depth behavior independently of the material's raw depth-write bit. The reference behavior is documented in Flycast's [region decoding](https://github.com/flyinghead/flycast/blob/v2.7/core/hw/pvr/ta_vtx.cpp#L1697), [sorted-translucent depth state](https://github.com/flyinghead/flycast/blob/v2.7/core/rend/dx11/dx11_renderer.cpp#L718) and [texture shading modes](https://github.com/flyinghead/flycast/blob/v2.7/core/rend/dx11/dx11_shaders.cpp#L302). The native full-alpha driver preserves the muzzle's DECAL mode; applying the material alpha again caused the port's excessive transparency.

## Verification

`naomi_visuals_test GAME BANK` checks real texture/model substitutions, Red overlay data, logical sprite sizing, PC restoration and invalid-bank rejection. `render_naomi_lighting_test [sdlgpu|d3d12]` checks actual GPU lighting, fog, sampling and restoration; default is D3D11. The menu tests cover profile persistence and disabled settings. Runtime scene captures are compared with the original Naomi reference captures before extending correspondence rules.

`naomi_flash_sort_test` checks crossing-plane order and interpolated attributes against independent ray/depth calculations. `render_naomi_muzzle_test GAME BANK [sdlgpu|d3d12]` checks flash compositing, opaque occlusion, native geometry across camera positions, both players, rapid-shot overlap and PC restoration on the actual GPU.

`naomi_deformation_test GAME BANK` verifies native materials through actual skinned attachments and the PC inline-record wave walk. `render_naomi_table_test [sdlgpu|d3d12]` checks colored local lights, attenuation, spotlight cones, scale handling and native RGB packing. `render_naomi_blood_test GAME BANK [sdlgpu|d3d12]` compares real nonplanar blood geometry with an independent per-ray compositing calculation.

Validation covers assets, camera math, GPU sorting and rendering on D3D11, SDL GPU and D3D12. The PC reference capture is byte-identical to the unmodified installed release under the same conditions; the polygonal-blood replay preserves 2,700 PC/network frames and passes pause/lifetime checks.
