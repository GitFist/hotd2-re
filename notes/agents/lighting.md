# Lighting, environment and screen-level differences (agent "lighting", 2026-10-05)

## Ground truth used

- `build\scene_capture_lighting_env.txt`: a light replay of `script_auto_full2.txt` from boot to 97400 with the new
  hook option `HOTD2_SCENECAP_ENV=1` (V lines: both env blocks, the light globals, 0x009A2BB4 / 0x009C8D4C / the light
  table ambient and every enabled light slot, one line whenever something changed; written also before
  `HOTD2_SCENECAP_FROM`).
- `build\shots_lighting_sweep\shot_<frame>.bmp`: the original every 300 frames (new hook option
  `HOTD2_SCENECAP_SHOT_EVERY=N`), same run (324 shots, frames 300..97200).
- `build\shots_lighting_misc\`: older shots kept from game\ (church 2280 from `script_auto_st1 --direct`, the stage-2 bar
  19340..19700 from `script_auto_full`).
- New hook option `HOTD2_SCENECAP_LIGHTS=2`: L lines also list every other enabled D3D light as the runtime holds it.

Tools:
- `tools/env_compare.py <capture with V lines> <port env log>`: the port's env per frame
  (`HOTD2_PLAY_ENVLOG=<file> play.exe ...`) against the V lines.
- `tools/sweep_compare.py <shot dir> <render dir>`: per frame block colour difference, colour bias per channel, edge
  correlation; renders from `play.exe game --script build\script_auto_full2.txt --screenshot <dir>\o_%d.png --every 300
  --frame 97200 --width 640 --height 480`.

## Root causes and what was ported

1. **play.exe lit and fogged the whole game with the stage-1 demo environment** (`scenes\st1_play.scene`: fog, light
   colour, angles fixed at the stage-1 values). The world now owns the environment and the renderer takes it per frame
   and per draw (`World::Lights()`, `World::Env()`, `WorldDraw::light`, `Scene::BuildFromWorld`).
2. **Env interpolator 0x0040AD90 and the env ops** (`game::Environment`, `src/game/environment.*`):
   - 9 channel records per block {active, value, target, step} (0x009C89E0 block 0, 0x009C8920 block 1), stepped by
     0x0040B0F0 every frame and copied to the block (0x0040B0A0..0x0040B390, fog colour channels truncated);
   - op 0x21 / 0x25 = 0x0040B650 (step per frame), 0x23 / 0x27 = 0x0040BA90 (over N frames: |target - value| / N),
     0x22 / 0x26 = 0x0040C1F0 (stop), 0x24 = Env_SetChannel on block 1, 0x19 = block-1 angles;
   - op 0x17 light-angle task 0x0040E3B0 (rotation blend 0x00412750 of Rz Rx Ry from / to, angles re-derived) now
     moves 0x009A3558 / 0x009A355C;
   - boot values: the blocks are initialised data (fog colour 0, near 65535, far 65536, colour 1, intensity 0.7);
     **Env_InitBlock 0x0041DBA0** puts both blocks back to these values in Cam_Reset 0x004021D0 (every stage start
     through the camera task init 0x00402260, the game over / map / ranking / title paths). The env writes at
     0x00460250..0x0046032F (`Environment::StageDefaults`, colour (1, 0.2, 0.1)) did not run once in the whole game.
   - title card 0x004342E0: block-0 angles from the active camera, intensity 0.7 (0x0040C2C0).
   - Result: `env_compare.py`: 97198 / 97200 frames equal (both blocks, every field, light table ambient and light
     count); the two left: one frame of a stage-5 light slot (allocated one frame late) and one yaw unit at 69522.
3. **Light globals per submission** (`world_light.cpp`): Mdl_Submit 0x004A7E50 snapshots colour / intensity /
   direction (pass 1 keeps the copy). Set by the env task (block 0 colour, intensity), the camera task 0x00401F40 (block-0
   direction), **Env_ApplyActorLight 0x0041DC70** (block 1: intensity 0x009A5C2C, angles 0x009A59F8 / FC, colour
   0x009A5C20..) and **Env_ApplyLight 0x0041DCC0** (back to block 0), 0x0041DC10 (explicit values), 0x004AA0A0 /
   0x004AA070. Nearly every caller of 0x00411090 wraps it in 0x0041DC70 / 0x0041DCC0 (58 call sites), so
   `World::Impl::AnimDraw` does it by default (`animLight`); exceptions: the player's death body 0x004151D0 (none),
   0x00428F70 / 0x0042D930 / 0x00497F80 (0x0041DC10 values). Also the hydra heads / body (0x004219E0 is always wrapped)
   and the bats / leeches / bat water.
4. **Boss 5 light 0x0047FC50**: in behaviour 9 (and 10 after sub-state 3) both blocks' angles point from the drawn
   node's view-space point (node +0x68, index 0x007DCF2C) to node 8's, through the camera matrix; colour
   (1, 0.781, 0.565); a hit flash +0x1334 makes the colour (1 or 0, 0, 0) by parity.
5. **The second light set** (alt submits, key bit 0x04000000, 0x004AA8B0): AMBIENT = 0x009C71D4.. (op 0x16) and the
   enabled slots of 0x009A1A20 (stride 0x74: D3DLIGHT7 + enabled +0x68 + index +0x70), rebuilt every frame by the task
   0x00480970 (created by 0x004809D0) when 0x009A2BB4 != 0 (op 0x14). Used in the whole game at 38419..40094
   (stage-3 boss arena), 44928..47309 (stage 4) and 56009..56483 (stage 5 start). Slot writers:
   - player flashlights 0x00480AC0 (slots 1 / 2) when 0x009C8D4C == 1 (op 0x15): spot from the camera through the aim
     point, white, att0 0.5, theta = phi = 0.3927;
   - actor 0x2B 0x004380B0 (flickering point light), 0x00438230 / 0x004383C0 (spots, phi never set = 0).
   The renderer evaluates point / spot lights per vertex with D3D7 attenuation and the spot cone (up to 8).
6. **Renderer (both backends, shared HLSL)**:
   - D3D7 lighting per vertex (Gouraud), as the fixed-function pipeline does;
   - **specular**: Draw_InitStates 0x004A7630 sets D3DRENDERSTATE_SPECULARENABLE = 1; Mdl_Draw puts the material's
     words 16..18 and power 1 << word 9 into the D3DMATERIAL7 (power 0 when word 9 <= 0); light specular = diffuse
     (0x004AA120); local viewer; added after the texture stage. This was the hydra's missing sheen (frames 39300 / 75000:
     13.3 / 19.4 -> 1.6 / 1.4);
   - normals by the inverse transpose of WORLD without renormalising (NORMALIZENORMALS is never set);
   - mipmaps: none exist. Tex_CreateDDSurface 0x004A5F90 -> 0x004A5ED0 clears DDSCAPS_MIPMAP / COMPLEX and
     DDSD_MIPMAPCOUNT before CreateSurface, so level 0 only is exact (open item closed).
7. **Pause overlay** (0x0040E500): model 0x93E through the alpha path with 0.5 at view-space z -0.0005 with the near
   plane at 1e-5; the world now submits it while paused (`PauseFrame`) and `Scene::BuildFromWorld` scales that draw about
   the eye to z -1 (same image) because the port's near plane is fixed. Whether the original shows the frozen scene
   behind it is still not verified (no paused screenshot).

## Numbers (original shots vs play.exe, block colour difference 0..255)

| | before (main) | after |
|---|---|---|
| whole-game sweep, 324 frames (mean) | 18.21 | 3.74 |
| frames flagged (colour > 12 or edges < 0.5) | 191 | 15 |
| stage-2 bar 19340..19700 (11 shots, mean) | 11.0 (bias R +5 / B -9) | 1.1 |
| stage-1 church 2280 (`--direct`) | 25.3 | 0.9 |
| Goldman screen 48600 | 17.3 | 1.5 |
| hydra 39300 / 75000 | 13.3 / 19.4 (with env fixed) | 1.6 / 1.4 |
| boss 5 67800..69300 (light 0x0047FC50) | 18.8..32.9 (env already fixed) | 2.1..4.7 |
| final-boss orbs 83400 (env-map uv) | 15.9 | 15.5 (orbs now shiny; trail copies differ) |

## Also ported

- Env-map uv pass Mdl_DrawEnvMapped 0x00418660 -> 0x004AA400 (`WorldDraw::envMap`, applied in `Scene::BuildFromWorld`):
  uv of the vertices of strips with ctrl 0x100 = ((1 - n.col0) / 2, (n.col1 + 1) / 2) of the model-view. Marked at
  the final-boss orbs / trails / beams, the creature nodes 0x0042C6D0, actor 0x2D nodes 0x00429040 and the enemy node
  models 0x1C71.. Not marked yet: Attach_DrawModel 0x00419B40 variants, 0x0042AA57, 0x0042B1B0, 0x0042D35F, 0x00472686.
- Orb trails 0x0042BF30: the oldest copy has a negative alpha factor (invisible in the original; the port drew it
  opaque because -1 means "no alpha path"): clamped to 0.

## Open in this area

- Leech / bat seam normals (Anim2_DrawSlot 0x0040F960, cache 0x007C17B0 / 0x007C18D0): on the first draw
  (+0x1358 == 0) segments < 6 record the normals of the vertices listed at 0x004E1DB1; later seam vertices of
  segments 2..6 take the cached normal - but the code writes all three cached components to normal.x (z | 1 wins), so
  only normal.x changes. Needs `game::ModelEdit` to carry a normal-x override (bats agent's struct).
- Stage-5 opening 56009..56483 (light table with actor-0x2B spots whose phi is never set): at 56400 the original is
  fully dark, the port shows the sphere (12.8). Rejecting phi < theta spots only gives 11.5, so it is not the spot;
  `HOTD2_SCENECAP_LIGHTS=2` (all enabled D3D lights at each draw) was added for that capture, not run (Hod2 lock busy).
- Light calls 0x0041DC70 around non-skeleton draws not yet mirrored: 0x0043990B.. (0x00439860, with colours
  0x004AA0A0(0.1)), 0x0043A47E, 0x0043D24D / 2DC / 91B, 0x0043EA58, 0x0044879F, 0x00472603 / 0x00472A0D, 0x00476AD7,
  0x00483A62 / B70 / D01, 0x0049A3AE.. (tutorial); 0x004AA0A0 colour flashes of 0x00425780, 0x00450A30, 0x004755B0,
  0x0047E130, 0x0047F4A0, 0x00487CA0, 0x0048FB40, 0x00498090.. (boss / select screens).
- Pause: whether the original keeps the frozen scene behind the overlay (the port's paused frames have no 3D).

## Remaining sweep differences outside this area (frame: block colour / edges)

- 15000 (103.7 / 0.65), 30300 (133.8 / 0.34): stage 2 / 3 chapter card: the original is black behind the card, the
  port draws the stage (camera / what is drawn before the stage camera starts).
- 67200 (10.2): stage-5 book pages: the original shows open pages where the port shows the cover (animation / model).
- 69600 (19.5): boss-5 death: the original has a large red blast around the boss (Boss5_BlastTask 0x00480810 effect),
  the port small particles.
- 80400 / 81000 / 84000 (9..14, port brighter): final-boss orb trails: the port shows more / more opaque trail
  copies than the original (trail alpha values or count, 0x0042BF30).
- 82200 / 82800 / 83400 (11..15): final boss phase-2 effects (sparks / creature), positions differ.
- 88200..91800 (13..41, edges 0.03..0.75): ending / staff roll: camera and scene content differ (91200 edges 0.03).
