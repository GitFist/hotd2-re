# Original campaign Randomizer (port only)

Entry: modern main menu Randomizer (cursor 9), shared online lobby Randomizer (cursor 5).
The underlying game type remains Original (1), including its section geometry, weapons,
effects, items, chapters and routes. The original inventory select is bypassed only for this mode.

`world_randomizer.cpp` owns a deterministic xorshift32 stream seeded by the synchronized
game-start counter. Its state, chapter, equipment and encounter counters enter the network
state hash. It is reconstructed by normal room-input recovery replay. There is currently no
manual seed-entry UI; the displayed seed identifies the run. Guns consume the original RNG,
so merely shooting more cannot change the randomizer's next roll on the same route.

Each chapter chooses a different weapon from pistol, shotgun, machine gun and grenade
launcher for each reserved player. A second slot draws a compatible Original bonus from
damage, ammunition and shot-effect items using the original compatibility table. Standard
Original mode still uses its manual item selection and saved inventory. Randomized pickups
equip the shooter's gun/bonus and refill that player's magazine; the partner's ammo and
weapon cooldown are preserved. Unattributed rescue/prop rewards go to the first active slot.

Ordinary type-0x30 zombies with behaviours 1, 2, 14 or 15, no special actor/retreat flags,
and zero `lo` draw from six grounded enemy kinds (basic, shirt, brute, heavy, chainsaw,
speedy). An encounter draws 1..3 actors; extra actors stop when occupied/pending slots reach
10, reserving space in the original's fourteen-slot actor registry. Original walking parameters
and flags are retained. Bonuses stand five units either side of the original spawn. Bosses,
civilian behaviour, riders, cinematic lunges, jumps and special scripted behaviours are untouched.
Replacement models are loaded explicitly and retained while alive across section releases.
Chainsaw replacements receive the original shared coli0.bin hand-weapon mesh (0x009903E0)
instead of interpreting the displaced zombie's parameter word as a collision address.

Items chosen through ItemPick and civilian rewards use the randomized pool; fixed prop
rewards are randomized on collection. The chapter roll and pickups never consume inventory.
Saving Original records/rewards is suppressed for the run, including the final-boss unlock.
Returning to title or lobby clears the mode and restores the inventory from the save block.

Build M (2026 10 14, id 2026101402) requires the same client on both PCs. No server protocol
change is needed. Tests live in `tests/randomizer_test.cpp` and `tests/network_room_test.cpp`.

## Build M: dialogue and scene appearances

The user's choice is appearance swaps while keeping scene actions, never hostile conversions
of civilians. Eligible humanoid civilians and scene actors retain their original skeleton,
animation, collision, root motion, event scripts and rescue role. AnimDraw executes their
original callbacks, then replaces only the render submissions with a compatible 16-bone
costume. The pool is 29 complete civilian/agent outfits plus six zombies. Civilian hair, hats
and head variants come from authored spawn-record parts lists; agent outfits include their
skinned attachments. Head-look orientation and scene devices/briefcases/muzzle flashes carry
over. Active player weapon bodies, bosses and incompatible rigs are excluded. Resource
requests keep replacement meshes resident when the original section unloads assets.

Spoken voice IDs map to 335 unique recorded lines with valid subtitle groups. Voice() and
SoundCall() use the same original sound ID as their mapping key, so captions match the
replacement recording. Silent text, music, effects and stop commands keep their purpose.
Long replacement lines may be interrupted by the next scripted line; their subtitle is
cleared with the voice channel, leaving scene/camera timing unchanged.

Both mappings hash the run seed in separate cosmetic domains and never advance either
gameplay RNG. A character/line keeps its replacement through the run and recovery replay.
The voice pool is derived from immutable tables at RandomizerBegin. The reportedCostumes
mask only suppresses repeated diagnostic notes and is not simulation state.

M verification: 64 cosmetic seeds covered all 335 lines and all 35 costumes; every dialogue
WAV exists and parses. Tests check matching subtitles/audio, compatible rigs, resident meshes,
unchanged actor state and RNG, and exclusions. The existing 128-seed equipment/encounter tests,
9,000 matched gameplay frames, and 600 frames in each later chapter pass with the new features.
An 18,000-frame unskipped scene replay reaches seven character swaps and 18 dialogue swaps.
Screenshots in build/randomizer-cosmetics include civilian-to-chainsaw and agent-to-civilian
scenes. This is visual/scene coverage of part of chapter 1, not every scene or every costume.
Generate the replay with randomizer_test.exe game build/randomizer-cosmetics/scenes.txt.

Final M delivery is build/dist-randomizer-m-20261014: the update is 2,111,491 bytes and the
complete package is 443,681,851 bytes. CRC and all 2,047 complete-package file hashes pass;
personal saves/rejoin tickets and the optional graphics pack are excluded. The update keeps
existing settings, saves and graphics. Executable SHA256:
2826888c2aabe7c2e0cd2adca6c7145e490b06538c70b19ae7d42e23e421f9f4.
The final real-client relay/rejoin test passed, with matching dialogue/costume selections
as well as world hashes. Evidence:
<local-temp>/hotd2_rejoin_join_n7sjnmjr/verification.json.

## Previous build L verification (2026-10-14)

- Release build: hotd2, play, randomizer_test and network_room_test succeeded.
- Randomizer test: 128 seeds, all six chapter equipment rolls, all four weapons, six zombie
  kinds, all three group sizes, compatibility, script exclusions and inventory isolation passed.
  Two independent worlds matched for 9,000 gameplay frames; chapters 2 through 6 each loaded
  scenery and matched for another 600 frames. Returning to the room cleared the mode.
- Shared-room mode selection / host authority / return / replay test and pause-menu tests passed.
- Original Network-menu reference run matched. Original-mode reference run matched simulation,
  RNG, events, camera and enemy state through 15,000 frames. Its draw comparison retained 89
  discrepant frames (130 matrices); no baseline comparison was made for those rendering differences.
- Solo screenshots checked menu fit, seed / equipment captions, scenery and mixed zombie models.
- The final real-client relay test selected Randomizer, reached mixed encounters, killed P2's
  process, let P1 continue, and rejoined P2 from a changed endpoint. Player slots, final world
  hashes and the final 101 frame logs matched, with no desync or invalid collision-mesh notes.
  Evidence: <local-temp>/hotd2_rejoin_join_nta_t19t/verification.json.
- The chainsaw regression now checks its shared hand mesh and rejects invalid mesh notes
  during shot preview and live simulation. The corrected solo run also produced none.
- Distribution: build/dist-randomizer-20261014-final contains the 2.1 MB update and 444 MB complete
  package. Archive CRC and all 2,047 packaged-file hashes passed; no personal save or rejoin ticket
  is included. The optional graphics pack is excluded from the complete package; the update
  preserves an installed graphics pack.

These checks do not cover a complete campaign on every route, balance across all seeds, or
play between two households. Manual seed entry and configurable randomization presets are
not implemented.
