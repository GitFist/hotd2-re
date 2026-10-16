# Build R: Randomizer cutscene crash investigation

Reported location: Goldman behind the desk after Chapter 2. This is the opening scene 7 of event table 2.
No local crash dump or Windows application error report was available. The desk scene's simulation and D3D11
rendering passed 64 seeds, so no character substitution was removed or changed.

The remaster texture cache had no retirement path: AssetStore retained decoded RGBA images, the renderer retained
texture handles and geometry, and both GPU backends retained every uploaded texture across chapters and new games.
The installed client is 32-bit with LARGEADDRESSAWARE. A render/prefetch replay of successive chapter starts crashed
with the old caching behavior at 3,606 MiB private memory (exit 0xC0000409). The same replay without the remaster passed.
This establishes a reproducible memory-pressure failure, not proof of the precise cause of the user's unrecorded crash.

`Scene::PrepareWorld` now retires the renderer, scene model and decoded asset caches together when the game mode or
chapter changes, or the world rewinds. Called between GPU frames and before the new world's texture prefetch in
play.cpp. AssetStore cancels queued work and joins the old workers before clearing their results; a later prefetch
starts fresh workers. GPU textures and cached geometry are discarded before their CPU image pointers. SDL waits for
pending GPU work; D3D11 unbinds the old shader resource. Active texture-file prefetch tracking is reset at the same time.
This is rendering-only and changes neither Randomizer seeds nor lockstep inputs. Build R forces matching client versions.

Validation:

- `randomizer_scene_test game remaster build/randomizer-r/desk-after.ppm`: real offscreen D3D11 renderer, Randomizer,
  each chapter's resource prefetch and opening sequence, including the desk conversation. Six chapter starts pass at
  1,587 MiB peak private memory. This is not a playthrough of every encounter/route.
- With `HOTD2_TEST_KEEP_CACHE=1`, the same test reproduces the previous failure. The desk frame is pixel-identical
  with and without the cleanup. Measurement script/results: build/randomizer-r/verify_memory.py and
  memory-verification.json; screenshots and logs are in the same ignored folder.
- Optional fourth argument to randomizer_scene_test is a seed; this runs only the desk scene. 64 original-texture
  seeds passed the render path, in addition to 64 simulation runs during diagnosis.
- Existing randomizer_test passes: 335 voice lines, 35 costumes, 128 equipment seeds, 9,000 matched two-world frames
  and 600 matched frames in each later chapter.
- The actual play.exe passed title/menu/gameplay transitions using the remaster on D3D11 and SDL_GPU. The retained
  old scene replay selects Horde under the current menu coordinates; this checks cache lifecycle, not Randomizer UI.
- `tools/menu_pointer_smoke.py`: two isolated clients create/join a local relay room, select Original equipment,
  and reach frame 2200 with the same final world hash 9A4D766C. No desync. No live VPS changes.

Installed executable SHA256: B56A00709AA520669FE52C1A53F4C5AE5BB2933FF7025C18307767C39E5ACE57.
Previous Q executable retained beside it as hotd2-q-before-randomizer-fix-20261015.exe. Packages are in
build/dist-randomizer-r-20261015: the small randomizer-fix update and the complete local ZIP. CRC and all 2,047
complete-package manifest hashes verified; settings, saves and the optional remaster are preserved by the update.

Both online players should update to `2026 10 15 R` (2026101505). No server or firewall changes are needed.
