# Packs the port for another PC: only our own files (hotd2.exe, the scene file it needs, a README). No game data, no
# save, no settings, no asset pack - the other player copies their own House of the Dead 2 installation into game\.
#   powershell -File tools/make_dist.ps1 [-Out build\dist]
param([string]$Out = "build\dist")

$exe = "build\src\port\Release\hotd2.exe"
if (-not (Test-Path $exe)) { Write-Error "build hotd2 first ($exe missing)"; exit 1 }
$root = Join-Path $Out "hotd2re"
if (Test-Path $root) { Remove-Item -Recurse -Force $root }
New-Item -ItemType Directory -Force (Join-Path $root "scenes") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $root "game") | Out-Null
Copy-Item $exe (Join-Path $root "hotd2.exe")
Copy-Item "scenes\st1_play.scene" (Join-Path $root "scenes\st1_play.scene")

@"
The House of the Dead 2 - native port (hotd2-re)
=================================================

This folder contains only the port itself. It needs your own copy of the game.

1. Install The House of the Dead 2 (PC, 2001) from your disc, or locate an existing installation
   (the folder with Hod2.exe and its data folders).
2. Copy EVERYTHING from that installation folder into the "game" folder here, so that you have:
     hotd2re\game\Hod2.exe
     hotd2re\game\tex\ ... pol\ ... evt\ ... Sound\ ... (all the game's folders)
3. Double-click hotd2.exe. The original CD is not needed by the port.

Controls (mouse / touchpad / light gun): left button = trigger, right button = reload, middle button or Enter = start.
Keyboard in menus: arrows, Enter (confirm), Esc / Backspace (back). Esc pauses/resumes during play and never quits.
The pause menu has volume and
display settings. Settings are kept in hotd2re_settings.ini, your rankings in hotd2re_save.bin (both created here).
Touchpad users: if clicks do nothing in the game, open hotd2re_settings.ini and set  input=mouse

Online two-player: mode select > Network.
  Create Room  - choose Unlimited Continues (or 3 continues) and Private Room; a private room shows a 5-digit code
                 to give your friend. When someone joins, both players enter the shared mode-selection lobby.
  Browse Lobby - the list of open rooms; pick one (a private room asks for its code).
  Peer to Peer - Host shows a 5-digit code; your friend picks Join and types it. Unlisted.
The host (player 1) chooses Arcade, Original, Scene Selection, Boss Mode, Horde Mode, or Randomizer. Pause > Return to Lobby brings both
players back to the same room. Esc pauses/resumes and Enter confirms. Either player can resume a pause.
Chapter results stay open until a click, shot, or Enter after a three-second minimum; either player can continue.
Close the window or use the main menu's Exit to quit. Both players need the same version (shown on Network).
Connections try direct first and automatically use the server relay when needed; no port forwarding is required.
After a disconnect the other player keeps playing. Reopen the same folder and choose Network > Rejoin Game.
Keep your private hotd2re_rejoin.bin ticket in that folder; do not share it.

Randomizer uses the Original campaign with varied ordinary zombies and group sizes.
Both players get a new weapon and bonus each chapter; randomized pickups equip immediately.
Dialogue uses random recorded lines with matching subtitles. Scene characters can look like
other civilians, agents or zombies while retaining their actions, rescues and story roles.
Bosses and special scripted encounters stay intact. Original inventory and records are preserved.
"@ | Set-Content -Encoding ascii (Join-Path $root "README.txt")

@"
Copy your own House of the Dead 2 installation into this folder: Hod2.exe and all of its data folders
(tex, pol, evt, Sound, ...). The port reads the game's files from here and never changes them.
"@ | Set-Content -Encoding ascii (Join-Path $root "game\PUT_YOUR_GAME_FILES_HERE.txt")

$zip = Join-Path $Out "hotd2re-win32.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $root -DestinationPath $zip
Get-ChildItem -Recurse $root | Where-Object { -not $_.PSIsContainer } | ForEach-Object { "{0,10}  {1}" -f $_.Length, $_.FullName.Substring((Resolve-Path $Out).Path.Length + 1) }
"zip: $zip ($([int]((Get-Item $zip).Length / 1KB)) KB)"
