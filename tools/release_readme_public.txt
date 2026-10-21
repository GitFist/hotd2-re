The House of the Dead 2 - native port (hotd2-re)
=================================================

Build {{BUILD_LABEL}} (Windows {{ARCHITECTURE}}).
This folder contains the port and any accompanying redistributable runtime components. It needs your own copy of the game.

1. Install The House of the Dead 2 (PC, 2001) from your disc, or locate an existing installation
   (the folder with Hod2.exe and its data folders).
2. Copy EVERYTHING from that installation folder into the "game" folder here, so that you have:
     hotd2re\game\Hod2.exe
     hotd2re\game\tex\ ... pol\ ... evt\ ... Sound\ ... (all the game's folders)
3. Double-click hotd2.exe. The original CD is not needed by the port.

Controls (mouse / touchpad / light gun): left button = trigger, right button = reload, middle button or Enter = start.
Keyboard in menus: arrows, Enter (confirm), Esc / Backspace (back). Esc pauses/resumes during play and never quits.
The pause menu has volume and display settings. Settings are kept in hotd2re_settings.ini,
your rankings in hotd2re_save.bin (both created here).
Touchpad users: if clicks do nothing in the game, open hotd2re_settings.ini and set  input=mouse

NIGHT SHIFT
The Home page uses the game's live standby preview. Play contains Arcade, Original,
Boss Mode (time attack), Horde and Randomizer. Practice contains training and scene
selection. Boss Mode is a scored time attack, not a practice mode.
Options opens the display/graphics pages. System > Menu Style selects Night Shift
or Classic for the next launch. Existing campaign rules and saves are retained.

ONLINE TWO-PLAYER
Home has Host Online, Browse Lobby and Peer to Peer actions; Online also opens
the network page. Classic uses mode select > Network.
  Host Online / Create Room - choose Unlimited Continues (or 3 continues) and Private Room.
                 A private room shows a 5-digit code to give your friend. When someone joins,
                 both players enter the shared mode-selection lobby.
  Browse Lobby - view open rooms and choose one; a private room asks for its code.
  Peer to Peer - Host shows a 5-digit code; your friend picks Join and enters it. Unlisted.
The host (player 1) chooses Arcade, Original, Scene Selection, Boss Mode, Horde Mode, or Randomizer. Pause > Return to Lobby brings both
players back to the same room. Esc pauses/resumes and Enter confirms. Either player can resume a pause.
Chapter results stay open until a click, shot, or Enter after a three-second minimum; either player can continue.
Close the window or use Home > Exit to quit. Both players need the same version (shown on Network).
Connections try direct first and automatically use the server relay when needed; no port forwarding is required.
After a disconnect the other player keeps playing. Reopen the same folder and choose Network > Rejoin Game.
Keep your private hotd2re_rejoin.bin ticket in that folder; do not share it.

LEADERBOARD
Home > Leaderboard shows Arcade, Original, Horde and individual Boss Mode tables.
Source: Local reads your own records. Source: Online is the shared service view.
Shared records are unverified, self-reported scores rather than anti-cheat rankings.
Publishing is manual: finish an eligible new run, choose Publish New Run, select
initials and confirm. Opening the page never publishes your save automatically.
The public rankings service update is pending deployment; shared queries or publishing
can be unavailable until the matching service is installed. Local records work offline.

GRAPHICS AND DISPLAY
Options or Pause > Display provides Display, Image, Lighting, System, Packs, Color,
Resolution, CRT and Temporal pages. Original preserves the authored appearance;
Clean and Enhanced are optional starting points, with individual controls available.
Fullscreen includes borderless and exclusive choices. Resolution includes authored
scale, match-output and 150%/200% output-relative SSAA. Higher settings cost GPU time
and memory. Calibration cards help inspect black level, white, gamma and geometry.
Experimental widescreen can reveal original off-camera staging. Optional render-pose
interpolation smooths presentation above 60 FPS with up to one tick of visual delay;
gameplay remains at 60 Hz and aiming/HUD input remains immediate.

Direct3D 11 and SDL_GPU offer the broad optional raster-effects paths. Native Direct3D 12
on Windows x64 enables supported vendor temporal reconstruction/frame generation and
experimental directional DXR shadows. Options are checked against the active backend
and GPU; every backend does not support every effect. Renderer, vendor reconstruction,
frame generation, menu style and visual-pack choices require restart. Unavailable
SDK/GPU features report a fallback instead of pretending to run the selected effect.
Advanced effects, DXR, frame generation and HDR are optional and Off by default.
HDR uses linear scRGB and requires Windows HDR; original art retains its SDR range.
Normal/roughness maps and mesh refinement need an explicitly authored installed pack.
No original game assets, visual packs or vendor SDK source are included here.

RANDOMIZER
Choose Play > Randomizer or use the shared online lobby. Ordinary zombies and group
sizes vary while bosses, rescues and scripted encounters retain their roles. Each
chapter rolls weapons and compatible bonuses; pickups equip immediately. Recorded
dialogue and character appearances vary consistently with the run seed. Both PCs use
the same seed. Original-mode inventory, unlocks and high scores are preserved.

BUILD.txt identifies the actual executable architecture/build label. PACKAGE-MANIFEST.json
records payload hashes, runtime notices and any reviewed vendor-byte privacy exceptions.
SHA256SUMS.txt also hashes the manifest; the adjacent .zip.sha256 hashes the archive.
