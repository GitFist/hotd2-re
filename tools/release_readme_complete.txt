THE HOUSE OF THE DEAD 2 - NIGHT SHIFT NATIVE PORT

START PLAYING
1. Extract the entire ZIP into a normal, writable folder. Do not run inside the ZIP.
2. Open the hotd2re folder and double-click hotd2.exe.

Build {{BUILD_LABEL}} (Windows {{ARCHITECTURE}}).
This complete package includes the required game data. No separate installation or disc is needed.
Keep the game and scenes folders next to hotd2.exe. Launch the top-level hotd2.exe.
Requires Windows 10/11 with Direct3D 11 support. Optional graphics features have additional
GPU/backend requirements. Settings and saves are kept in this folder. No personal saves
or device profiles are included. Sound is enabled on normal hotd2.exe launches, including
launches with a custom game-folder path or command-line options.

NIGHT SHIFT MENUS
Home uses the game's moving standby preview. Play contains Arcade, Original, Boss Mode
(time attack), Horde and Randomizer. Practice contains Training and Scene Select.
Home also offers Leaderboard, Online, Extras, Options and Exit. Host Online, Browse Lobby and
Peer to Peer are directly accessible beside the waiting-room preview.
Options > Game Options contains game rules, sound controls and Wii Remote / Light Gun setup.
The graphics menu's Display > Menu Style selects Night Shift or Classic for the next launch.
Classic changes menu appearance while retaining the same supported graphics settings.
Menus stay sharp at display resolution independently of gameplay render scale. Faded edges
and scroll indicators show when more list items are available above or below.
Extras includes all 38 named music tracks, effects/dialogue previews, and a model viewer.
Master volume has a gradual scale with quieter low levels. Attraction volume controls the
background menu loop separately from gameplay music, gunfire and effects.

ONLINE TWO-PLAYER
Both players should use this same package.
1. Use Home > Host Online to host, or Online to open all network choices.
   With Classic menus, choose Network from mode select.
2. The host chooses difficulty, Unlimited Continues and Private Room, then Start.
3. The other player selects BROWSE LOBBY and picks the host's room.
4. Public rooms need no code. Private rooms appear in the list but require the host's 5-digit code.
5. Both players enter the shared lobby. The creator (player 1) chooses ARCADE, ORIGINAL,
   SCENE SELECTION, BOSS MODE, HORDE MODE, or RANDOMIZER. Chapter and boss selections are controlled by the host.
   The host can change difficulty in the shared lobby; both players use that setting.
6. Pause > Return to Lobby brings BOTH players back to that room to choose another mode.
   The room and player slots stay connected. Leave Room (or Esc in the lobby) ends the room.

PEER TO PEER provides unlisted rooms: choose Host to get a 5-digit code, or Join to enter the
host's code. Only the joining player enters a code. This option also needs the lobby service.
Unlimited Continues OFF gives three continues. Backspace or right click goes back/closes a room.
The lobby address is already configured. Both players' versions on the Network screen must match.
Backing out cancels a room you were creating. Stale joins end with a connection error.

All three room options try a direct connection first and automatically use the online
server relay when needed. You do not need to set up router port forwarding. If the active
connection stops responding, the game tries the other path; a brief pause may occur.
Input delay is chosen from the connection's measured round trip before the game starts.
The lobby service is needed to find a player, and the relay must be available when it is
carrying gameplay or providing a backup path. Networks that block outgoing UDP can still
prevent online play. If Windows asks about network access, allow this app on your network.
If a player disconnects, the other player keeps playing in their original player slot.
After reopening the SAME game folder, choose Network > REJOIN GAME. No new code is needed.
The returning player catches up to the current match and keeps their original P1/P2 slot.
Temporary network interruptions recover automatically when possible. A brief initial stall
detects the lost connection. Rejoining uses the server relay and needs the other client to
remain in the room; it cannot restore a room after both clients close or someone leaves it.
Rejoin works across returns to the lobby and subsequent games in the same room.
Longer sessions take longer to catch up (up to four hours of history).
If both clients keep running during a network outage, P1's timeline is used on recovery.
Keep hotd2re_rejoin.bin in your game folder; it is your private rejoin ticket, not a room code.
Do not copy that file to your friend. Pausing pauses the shared game.
Switching to another window does not pause it automatically.

LEADERBOARD
Home > Leaderboard contains Arcade, Original, Horde and individual Boss Mode tables.
Source: Local shows your own records. Source: Online queries the shared service.
Shared records are unverified, client-reported results, not anti-cheat rankings.
After an eligible run's results and local initials screens, the online name prompt opens
automatically. Enter a name of 1-12 characters, then Publish or Skip. Failed submissions can
be retried in the prompt; Publish New Run remains a fallback. Type or use the on-screen
keyboard. Letters, numbers, spaces, periods, underscores and hyphens are supported.
Local initials stay three characters.
Opening the page never publishes your save automatically. The public service stores shared
records persistently; older clients display abbreviated names. Local records work offline.

UPDATES AND EXISTING SAVES
Normal launches check the official GitHub release page once in the background. A newer
build is offered at the local title/main menu: Open download page or Later. Downloading
and installation are manual. Offline/failed checks stay silent. Pass --no-update-check
to disable this check. Scripted/headless/direct gameplay launches skip it.

When updating, close the game and extract the ENTIRE new ZIP into a separate writable
folder. Keep its new executable, all accompanying DLLs, licenses and scenes together.
Replacing only hotd2.exe can leave older, incompatible graphics runtimes installed.
To retain your progress and preferences, copy these files from your old hotd2re folder
into the new hotd2re folder before starting it, replacing the supplied default settings:
  hotd2re_settings.ini       display, audio, menu and input preferences
  hotd2re_save.bin           game progress, records, inventory and unlocks
  hotd2re_horde_records.txt  local Horde records, if present
  hotd2re_lightguns.ini      light-gun assignments, calibration and bindings, if present
  hotd2re_wii.ini            Wii Remote calibration and controls, if present
Copy your installed visual-pack folders too if your settings select them. Keep the old
folder as a backup until you have checked the new version. Both online players should
update together and create a new room. Never give your private rejoin ticket to another player.

HORDE MODE
Choose Play > Horde Mode, or let the host choose it in the shared online lobby.
Defend the barrels against timed enemy waves. Between rounds, use the 4 x 4 store to buy
weapons, upgrade each weapon's damage and ammunition, repair or reinforce barrels,
buy lives, or recruit CPU agents. Shoot an owned weapon's icon to equip it.
Select READY when finished shopping. Every tenth round includes the Zeal / Kuarl boss pair.
Use the same updated executable on both PCs.

RANDOMIZER
Choose Play > Randomizer or the shared online lobby. It plays the Original
campaign with randomized ordinary zombie types and group sizes (one to three, with a
crowd limit). Bosses, rescues, vehicles and special scripted enemies retain their encounters.
Each chapter rolls a different weapon and a compatible bonus item for each player.
Spoken dialogue is randomized with matching subtitles. Scene characters can look like
other civilians, agents or zombies, while retaining their original actions and rescue roles.
Costumes and dialogue choices stay consistent within a run and change with the run seed.
Randomized pickups equip immediately for the receiving player. A rescue reward goes to P1
when both players are active. The weapon and bonus are shown briefly after a roll or pickup.
Both PCs use the same run seed; reconnecting replays those same decisions. Randomizer
does not spend or save Original-mode inventory, unlocks or high scores.

CONTROLS AND DISPLAY
Prototype (local play): press F8 on the title or main menu. This hidden HOTD4 chapter-one
adaptation uses HOTD2 assets, simplified rooms/encounters, two routes and a four-armed boss.
Hold the trigger for its SMG, right-click to reload, and press G to throw a grenade.
Shoot a route choice or use Left/Right. ESC pauses; results unlock after three seconds.
Left mouse button: shoot. Right mouse button: reload.
Esc: pause / resume during gameplay, back in selection screens. It never closes the app.
Enter: confirm menus, start / continue an inactive player. Middle mouse button also pauses.
Menus: arrow keys, Enter, Esc / Backspace, or aim and click. Either player can resume a pause.
The game and sound continue when you switch to another window. Use manual Pause before
switching if you want the game stopped. Background keyboard/device input cannot fire or
operate menus. Gun, mouse and Wii Remote buttons held across a focus change must be
released before use again.
Original item select: shoot a stock item to equip it, an equipped item to put it back,
and your END button when ready. Both players choose their own items and confirm separately.
Shoot the inventory arrows to scroll. BACK returns to the main menu or shared lobby.
Boss, Training and Scene Select: shoot a side arrow/card to browse, then the centre card
or START to play. LEVEL arrows change unlocked difficulty; BACK returns to the menu/lobby.
Training instructions and Retry/Back, ranking-page arrows and BACK are clickable too.
Online menus have clickable rows, a room-code number pad, DELETE, BACK and cancel buttons.
At the end of a chapter, the results show each player's stage totals and the team summary.
After three seconds, click, shoot, or press Enter to continue. Either online player can advance both games.
Click, shoot, or press Enter to skip the ending credits. Qualifying scores still reach name entry;
online players return to their shared lobby. Either player can skip.
Native Wii Remotes: Options > Game Options > Wii Remotes, or Pause > Wii Remotes.
Classic menus retain Options > Wii Remotes.
Select Connect remotes and press red SYNC (or 1 + 2). A powered sensor bar and compatible
Bluetooth adapter are required; for DolphinBar, use Mode 4 and sync with the bar first.
Choose Player 1/2 setup to calibrate aim and change buttons. Defaults: B fires, A reloads,
+ confirms, HOME pauses. D-pad navigates. Aim at each calibration cross and press B.
Automatic reconnect keeps searching; Swap players also swaps their saved profiles.
Native Wii support has automated coverage; real-hardware pairing and aiming are not yet verified.

Close the window or use Exit on the main menu to quit.
Pause also offers separate music, gunshot, and sound-effect volumes.
Mouse/touchpad input is enabled by default. Open Options > Game Options > Light Guns,
or Pause > Light Guns. Classic menus retain Options > Light Guns.
Choose LIGHT GUNS, then Assign Player 1 and pull that gun's trigger. Repeat for Player 2.
Player settings offer button capture, four-target aim calibration, and Test both guns.
ESC cancels setup or leaves the test. Assignments and settings save automatically.
Choose MOUSE to use a mouse or touchpad again. Input changes release native Wii Remote
control; it can be enabled again in Wii Remotes. Sinden users must keep their border enabled.
Online players should assign their local gun to Player 1. Light-gun hardware testing is pending.

GRAPHICS AND DISPLAY
Open a graphics shortcut in Options, or Pause > Graphics Options. The full menu has nine
categories: Display, Image, Lighting, Performance, Packs, Color, Resolution, CRT and Advanced.
Keyboard: Up/Down selects a row, Left/Right changes a setting, Enter selects/changes it,
and Esc/Backspace returns. Click a category tab, or use Left/Right on Back to change category.

Display: windowed, borderless or exclusive Fullscreen; monitor, window size, aspect ratio,
Fit/Stretch, integer scaling and menu theme. Night Shift's Menu Opacity changes background
strength while text remains readable; Classic labels this option Night Shift only.
Display > Reset All Graphics asks for confirmation, saves defaults and preserves your
pack, menu theme, game rules, audio and controls. Some renderer changes need a restart.

Image: image presets, MSAA/post AA/TAA, texture filtering, anisotropy, Sharp HUD,
sharpening and Gameplay Crosshairs. Setting Gameplay Crosshairs Off hides both players'
gameplay sights locally; menu, shop and calibration pointers remain visible. Aiming and
shooting are unchanged. The system '+' pointer is also hidden automatically during play
and returns for menus, pause, calibration and desktop use.
Image Preset > Original changes image options and downsampling only; it is not Reset All Graphics.

Lighting: pixel lighting, supported smoke blending/contact shading, raster/ray shadows,
bloom, volumetric fog, reflections and cutscene blur. These are optional effects.
Performance: VSync, frame limit, supported tearing/latency controls and statistics.
Packs: installed visual-pack choice, material detail and model smoothing. Material maps
and smoothing require a compatible authored pack. Pack changes apply on the next launch.
Color: brightness, gamma, contrast, saturation, color grading, Blood Color, HDR and test
patterns. Blood Color switches Green/Red immediately and saves locally; online players
choose independently.
HDR uses scRGB and needs Windows HDR enabled; the original art remains SDR.
Resolution: internal render scale, output matching/SSAA, dynamic resolution, downsampling
and FSR 1 spatial upscaling. Render Scale 2x/3x/4x renders more pixels at runtime and
needs no texture-pack download. A separate optional 2x texture pack replaces artwork.
CRT: scanlines, phosphor masks, curvature and glow. Curvature is disabled for raw light
guns and active frame generation so their input/display requirements remain intact.
Advanced: renderer, supported vendor upscaling, upscale quality, frame generation and
pose interpolation. Renderer/vendor changes require restart. Pose interpolation above
60 FPS adds up to one simulation tick of visual delay; game logic stays at 60 Hz.

Direct3D 11 and SDL GPU provide the broad optional raster-effects paths. Native Direct3D 12
on Windows x64 enables supported vendor reconstruction/frame generation and experimental
directional DXR shadows. Each backend/GPU supports different features; an unavailable
option explains its dependency or reports a fallback. Advanced effects, HDR, ray shadows
and frame generation are optional and Off by default.

While changing graphics during a paused game, select Hide UI or press H to see the actual
scene without resuming. H, Show UI, Back/Reload, Enter or Start restores the menu. The
fullscreen test patterns provide black, white, gamma and geometry references.

BUILD.txt and PACKAGE-MANIFEST.json identify the executable and source label.
SHA256SUMS.txt lists the payload and manifest hashes.
