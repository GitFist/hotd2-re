# Actor lighting parity (2026-10-19)

> Historical investigation: referenced captures and measurements are local evidence
> under `build/`, not checked-in fixtures or newly reproduced image comparisons.
> Findings below follow the investigation's chronology; see
> [graphics fixes](../../docs/GRAPHICS-FIXES.md) for the consolidated corrections
> and [bat lighting remnants](bat-lighting-remnants.md) for later bat findings.

Two missing lighting brackets were confirmed against the original program:

- Stage-1 leeches (`0x0043C440`) use character lighting for both their body and
  water reflection. The port restored scene lighting after the body, then drew
  the reflection without selecting character lighting again. The original
  function has a second `0x0041DC70` / `0x0041DCC0` pair around its reflected
  `0x0040F4F0` call (exported function lines 499–501). The separate `0x10D0`
  floor shadow remains in scene lighting.
- Living schooling fish (`0x0042FCA0`, actor `0x42`, `buyo.bin`) use character
  lighting for their body and shadow. Original disassembly calls `0x0041DC70`
  at `0x00430135` and `0x0041DCC0` at `0x00430A66`. The port omitted both.
  Dead/falling variants have no corresponding original bracket and are unchanged.

`actor_lighting_test` replays the real tasks and checks submitted light snapshots
against the environment's character block, with a `1e-6` tolerance for intensity
and RGB and exact angle comparisons. It also checks that the stage-1 floor shadow
retains scene lighting. The leech fixture requires actual mirrored submissions;
the school fixture requires both body and shadow draws.

Example invocations, using the local original assets and input recordings:

```
actor_lighting_test <game> <script_norescue_st1.txt> 6500 6800 0043C440
actor_lighting_test <game> <school-fish-no-shots.txt> 19734 19840 0042FCA0
```

Before the fix, the first fixture found 2,160 bad reflections among 4,320
body/reflection submissions; all 240 floor shadows were correct. The second
fixture found 1,284 bad body/shadow submissions among 1,284. Both report zero
lighting mismatches after the fix.

Local evidence is under `build/graphics-investigation/`:

- `bat-fish-before.exe` preserves the executable from before these two changes.
- `leech-no-shots.txt` replays the original no-rescue stage-1 script through frame
  6569, then releases fire. `leech-idle-6579.png` through `leech-idle-6620.png`
  and the corresponding `leech-fixed-*` files use identical input and renderer
  options. All 42 pairs change; the largest changed area is 633 of 307,200 pixels.
- `school-fish-no-shots.txt` replays the route-21 input through frame 19719,
  then releases fire. `school-fish-idle-19750.png` through
  `school-fish-idle-19840.png` and the corresponding `school-fish-fixed-*` files
  match by game frame. 60 of 91 pairs change; the largest changed area is 1,754
  pixels and the largest RGB channel change is 15 of 255.
- `actor-lighting-image-stats.json` records these pixel comparisons;
  `school-alive-original-disasm.txt` records the original fish routine.

These are scoped lighting-parity fixes. The consecutive captures do not prove
that every reported whole-body white flash is fixed. Stage-3 leaping fish (actor
`0x51`, `fish.bin`) already select and restore character lighting, and the audited
stage-2 flying-bat alive/dead/water-light paths match their original functions.
The original fish blood routines `0x00439DC0` / `0x00439E00` draw models `0x3A`
through `0x52` at the hit point for 25 frames. That feedback remains. Later
isolation below distinguishes this blood from the overlapping white muzzle flare.

## No-hit follow-up and hit controls

The actual stage-2 flying bats (actor `0x43`, live task `0x004460C0`) were checked
separately from stage-1 leeches. `flying-bat-no-shots.txt` preserves the full-game
input through frame 17259, then holds zero buttons. Original capture records the
first bats spawning at 17260 and the first accepted hit in the normal script at
17280. The no-shot replay therefore reaches the encounter before removing fire.

- `flying-bat-before-17290.png` through `flying-bat-before-17420.png` use the saved
  earlier executable. `flying-bat-idle-*` use the current executable, including the
  actor-lighting fixes and equal-depth model-order stabilization. All 131 frame
  pairs are pixel-identical. No unintended whole-body white flash was reproduced
  in this sampled idle window; the bat issue cannot be claimed resolved.
- `flying-bat-idle-tasks.txt` records the current replay's task list per frame.
  Frames 17260–17420 contain 322 live-bat task instances, zero death tasks
  (`0x00448210`), and zero hit-flash tasks (`0x00430BD0`). The player can still be
  attacked; orange damage-overlay splats are player damage, not bat hit flashes.
- `flying-bat-hit-17275.png` through `flying-bat-hit-17315.png` use the unmodified
  normal shooting script. Frames 17281 and 17290 show white starbursts at aimed
  locations. These are overlapping muzzle flashes, as isolated below. Separately,
  the original bat routine calls `0x00430C50` on an accepted hit;
  it creates `0x00430BD0`, which draws models `0x3A..0x52` for 25 frames. The body
  then switches `0xBBF` to `0xBC0`, omits dead wing parts and emits 40 scraps.
  This path contains no full-body material-whitening command. Port references:
  `world_st2_bats.cpp` lines 535–598, 611–619 and 892–925.

The schooling-fish no-shot sequence was also repeated with the current renderer:

- `school-fish-stable-19750.png` through `school-fish-stable-19840.png` are all
  pixel-identical to the earlier `school-fish-fixed-*` frames. The later model
  tie stabilization adds no change to this sequence. The actor-lighting fix is
  the small, independently measured correction described above.
- `school-fish-idle-tasks.txt`, frames 19734–19840, contains 642 living-school
  task instances, zero dead-fish tasks (`0x00430C80`) and zero hit-flash tasks
  (`0x00430BD0`). No unintended whole-body white flash was reproduced here.
- `school-fish-hit-19750.png` through `school-fish-hit-19840.png` retain the
  normal route-21 shooting input as a control. Original disassembly calls
  `0x00430C50` at `0x0042FCD7` when the hit flag is set, producing the same
  25-frame blood effect. The original screenshot
  `shots_routes2a_vis/shot_19800.bmp` also contains an overlapping white starburst.

Thus, missing leech-reflection and school-fish lighting is fixed and tested. The
broader reported flying-bat whole-body flash was not reproduced in these windows.

## Blood textures and muzzle-flare isolation

The white starburst must not be attributed to the blood merely because it
appears on a hit. A temporary diagnostic suppressed only the two muzzle-model
`Draw` calls in `PlayerFlashes` (`world_player.cpp` around lines 1148 and 1151).
All actor, blood, tracer and task-state updates remained active. At identical
frame 17281, the white rays disappear completely:

- Normal: `build/graphics-investigation/bat-hit-drawproof-17281.png`
- Muzzle draws suppressed: `build/graphics-investigation/bat-hit-no-muzzle-17281.png`
- The normal draw log includes blood model `0x3B` from task `0x00430BD0`, plus
  the separate pass-`0xC` muzzle models. Blood was retained in the diagnostic.

The temporary source guard was removed and the clean application rebuilt. A
local diagnostic executable, `hit-flash-diagnostic.exe`, remains under the QA
directory and enables that isolation with `HOTD2_DIAG_HIDE_MUZZLE=1`.

Raw texture decoding independently rules out a missing default green-blood tint:

- Blood models `0x3A..0x52` use `common.bin` descriptor indices 1–25, global
  texture IDs 159–183. Their decoded textures contain green RGB and no white
  visible texels.
- `scr_blood_red.bin` and `scr_blood_green.bin` intentionally replace those same
  25 global IDs. The 25 common textures are byte-for-byte identical after RGBA
  decoding to the green replacements. The original boots with selector
  `0x009C7024 = 1`, choosing green.
- `0x0041EBB0`, called after scale by the original effects, is literally `RET`
  in the executable. It is not an omitted color or lighting setter.
- Audit data and exported textures: `build/graphics-investigation/hit-asset-audit/`.

The reporter subsequently identified the second boss's summoned fish. Those
are actor `0x51` (`SpawnFishJump`, task `0x00438730`, `fish.bin`), rather than the
schooling actor `0x42` tested above. It required the separate Hierophant replay
documented in `hierophant-fish.md`.
Its models `0x1156..0x1169` have unit normals and substantial specular materials
(mostly exponent 5, power 32), then are drawn at scale 0.3. With the legacy
inverse-transpose normals and normalization disabled, this increases normal
length to approximately 3.333. That is a separate lighting cause, now reproduced
and corrected with a scoped unit specular normal. See `hierophant-fish.md` for
matched no-shot captures; the texture audit and muzzle diagnostic alone do not
establish that fix.
