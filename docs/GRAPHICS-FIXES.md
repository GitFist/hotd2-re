# Build Z graphics and audio fixes

Build `2026 10 19 Z` combines the graphics corrections, red/green blood selection and optional startup update notification. Both online players must use the same build (compatibility ID `2026101904`).

## Corrections

- Fish and leech bodies use unit specular normals at native scaled sizes, with bounded shader inputs. Includes both Hierophant fish summons and opaque fish-body draws; shadows keep their native lighting.
- Owl-bat face animation avoids the invalid midpoint frame, background flocks hold their last valid body at a missing animation phase, and owl death lighting stays continuous with scene lighting.
- Emperor models keep a stable submission order for exact depth/priority ties. Skin transforms retain the original operation order and normal flags. HUD/sprite ordering stays in its original slots.
- Parallel smoke cards sort by their plane layers. Burning-car smoke uses farthest-first camera distance within marked model slots, retaining native blend/depth writes and unrelated draw slots. The stage-five reuse uses the same correction.
- Material-aware mipmaps reduce minified scenery shimmer. Opaque RGB and translucent coverage use separate filtering; HUD textures keep level-zero sampling.
- Cutscene skipping synchronizes the native event skip flag, preventing Goldman dialogue from restarting during the Strength fight.

## Regression coverage

The spawn audit walks 588 script contexts and 17,168 instructions, including 22 bat references to 21 unique records and 31 fish/control records. Native fixtures cover living/dead/reflected creatures, both player counts, Horde sizes, randomizer controls and both Hierophant summon types. GPU fixtures run on Direct3D 11 and SDL GPU.

Build and run the relevant tests with the extracted game data at `game`:

```powershell
cmake --build build --config Release --target actor_lighting_test bat_face_test bat_variants_test car_smoke_test fish_coverage_test render_lighting_test render_minification_test render_order_test render_smoke_assets_test render_smoke_columns_test render_transparency_test skin_test sound_test blood_color_test pause_menu_test update_check_test --parallel
```

The asset/coverage fixtures take the game-data path. GPU fixtures accept `--renderer d3d11` or `--renderer sdlgpu`; consult each fixture's usage for asset arguments. `update_check_test` is offline unless `--live` is explicitly supplied.

The release package is checked against the extraction manifest and per-file SHA-256 hashes, scanned for private runtime/debug files, and exercised with two clients. These automated same-PC network checks do not replace testing between separate households or physical light-gun/Wii hardware.
