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
The pause menu has volume and Graphics Options settings. Settings are kept in hotd2re_settings.ini,
your rankings in hotd2re_save.bin (both created here).
Touchpad users: if clicks do nothing in the game, open hotd2re_settings.ini and set  input=mouse

NIGHT SHIFT
The Home page uses the game's live standby preview. Play contains Arcade, Original,
Boss Mode (time attack), Horde and Randomizer. Practice contains training and scene
selection. Boss Mode is a scored time attack, not a practice mode.
Options opens graphics shortcuts and Game Options. Display > Menu Style selects Night Shift
or Classic for the next launch. Existing campaign rules and saves are retained.
Menus stay sharp at display resolution independently of gameplay render scale. Faded edges
and scroll indicators show when more list items are available above or below.
Extras includes all 38 named music tracks, effects/dialogue previews, and a model viewer.
Master volume has a gradual scale with quieter low levels. Attraction volume controls the
background menu loop separately from gameplay music, gunfire and effects.

UPDATING AN EXISTING INSTALLATION
Close the game and extract the ENTIRE new runtime ZIP into a fresh writable folder.
Keep its executable, all accompanying DLLs, notices and scenes together; replacing only
hotd2.exe can leave incompatible older runtimes installed. Copy your existing game folder
into the new hotd2re folder, along with hotd2re_settings.ini and hotd2re_save.bin. Also copy
hotd2re_horde_records.txt, hotd2re_lightguns.ini and hotd2re_wii.ini if present, plus any
installed visual-pack folders selected by your settings. Keep the old folder as a backup
until the new version works. Both online players should update together and create a new room.
Never give your private hotd2re_rejoin.bin ticket to another player.

Normal hotd2.exe launches enable sound, including custom game paths/options. Switching
windows keeps gameplay and sound running; use Esc to pause manually when needed.
Normal launches check the official GitHub release page once in the background. A newer
version offers Update and restart, Open download page, or Later at the local title/main
menu. Update and restart downloads and verifies the matching Windows version, closes
the game, replaces its program files, and reopens it. Saves, settings, controller
calibration, original game data and installed packs are kept. Failed installations
restore the previous program files; an interrupted update is recovered on next launch.
Keep the .hotd2-update folder if recovery needs another attempt. A writable local game
folder and Windows PowerShell are required. --no-update-check disables the check.

ONLINE TWO-PLAYER
Home has Host Online, Browse Lobby and Peer to Peer actions; Online also opens
the network page. Classic uses mode select > Network.
  Host Online / Create Room - choose difficulty, Unlimited Continues (or 3 continues) and Private Room.
                 A private room shows a 5-digit code to give your friend. When someone joins,
                 both players enter the shared mode-selection lobby.
  Browse Lobby - view open rooms and choose one; a private room asks for its code.
                 Listings show advertised difficulty; older hosts can show Unknown.
  Peer to Peer - Host shows a 5-digit code; your friend picks Join and enters it. Unlisted.
The host (player 1) chooses Arcade, Original, Scene Selection, Boss Mode, Horde Mode, or Randomizer. Pause > Return to Lobby brings both
players back to the same room. Esc pauses/resumes and Enter confirms. Either player can resume a pause.
The host can change difficulty in the shared lobby; both players use that setting.
Chapter results stay open until a click, shot, or Enter after a three-second minimum; either player can continue.
Close the window or use Home > Exit to quit. Both players need the same version (shown on Network).
Connections try direct first and automatically use the server relay when needed; no port forwarding is required.
After a disconnect the other player keeps playing. Reopen the same folder and choose Network > Rejoin Game.
Keep your private hotd2re_rejoin.bin ticket in that folder; do not share it.

LEADERBOARD
Home > Leaderboard shows Arcade, Original, Horde and individual Boss Mode tables.
Source: Local reads your own records. Source: Online is the shared service view.
Shared records are unverified, self-reported scores rather than anti-cheat rankings.
After an eligible run's results and local initials screens, the online name prompt opens
automatically. Enter a name of 1-12 characters, then Publish or Skip. Failed submissions can
be retried in the prompt; Publish New Run remains a fallback. Type or use the on-screen
keyboard. Letters, numbers, spaces, periods, underscores and hyphens are supported.
Local initials stay three characters.
Opening the page never publishes your save automatically. The public service stores shared
records persistently; older clients display abbreviated names. Local records work offline.

GRAPHICS AND DISPLAY
Options shortcuts or Pause > Graphics Options provide Display, Image, Lighting,
Performance, Packs, Color, Resolution, CRT and Advanced pages. Image Preset > Original,
Clean and Enhanced change image choices and downsampling, not every graphics setting.
Fullscreen includes borderless and exclusive choices. Resolution includes authored
scale, match-output and 150%/200% output-relative SSAA. Higher settings cost GPU time
and memory. Calibration cards help inspect black level, white, gamma and geometry.
Render Scale 2x/3x/4x draws more pixels at runtime without a texture-pack download;
the separate optional 2x texture pack replaces artwork.
Experimental widescreen can reveal original off-camera staging. Optional render-pose
interpolation smooths presentation above 60 FPS with up to one tick of visual delay;
gameplay remains at 60 Hz and aiming/HUD input remains immediate.

Display > Reset All Graphics asks for confirmation and saves graphics defaults. It keeps
the selected pack, menu theme, game rules, audio and controls. Some changes need restart.
Night Shift's Display > Menu Opacity changes background strength while text stays readable;
Classic labels it Night Shift only. During paused graphics changes, Hide UI or H reveals
the actual scene without resuming. H, Show UI, Back/Reload, Enter or Start restores the UI.
Image > Gameplay Crosshairs > Off hides both players' gameplay sights locally. Menu, shop
and calibration pointers remain visible; aiming and shooting are unchanged. The system
'+' pointer also hides automatically in gameplay and returns for UI/desktop use.
Color > Blood Color switches Green/Red immediately and saves locally; online players
choose independently.

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

DEVICE SETUP
Options > Game Options > Light Guns or Wii Remotes opens native setup; both are also on
Pause. Classic retains Options > Light Guns / Wii Remotes. Light Guns assigns each gun,
captures bindings, calibrates aim and tests both players. Online players configure their
local gun as Player 1. Keep any required light-gun border enabled.
Wii Remotes supports a powered sensor bar with compatible Bluetooth, or DolphinBar Mode 4.
Connect and sync, then use Player 1/2 setup for calibration and controls. B fires, A reloads,
+ confirms and HOME pauses by default. Calibration always uses four fresh B presses.
Physical light-gun/Wii pairing and aiming remain unverified; automated device tests pass.

RANDOMIZER
Choose Play > Randomizer or use the shared online lobby. Ordinary zombies and group
sizes vary while bosses, rescues and scripted encounters retain their roles. Each
chapter rolls weapons and compatible bonuses; pickups equip immediately. Recorded
dialogue and character appearances vary consistently with the run seed. Both PCs use
the same seed. Original-mode inventory, unlocks and high scores are preserved.

BUILD.txt identifies the actual executable architecture/build label. PACKAGE-MANIFEST.json
records payload hashes, runtime notices and any reviewed vendor-byte privacy exceptions.
SHA256SUMS.txt also hashes the manifest; the adjacent .zip.sha256 hashes the archive.
