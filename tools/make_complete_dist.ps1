# Build a self-contained local multiplayer package from the extracted game data.
# Game material stays under ignored build output and is never added to source control.
# Run: powershell -File tools/make_complete_dist.ps1 [-Out build\dist-complete]
param([string]$Out = "build\dist-complete", [string]$Exe = "build\src\port\Release\hotd2.exe", [string]$SourceRoot = "", [string]$Python = "python")

$ErrorActionPreference = "Stop"
$repoDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceDir = if ($SourceRoot) { (Resolve-Path -LiteralPath $SourceRoot).Path } else { $repoDir }
$outputDir = if ([IO.Path]::IsPathRooted($Out)) { [IO.Path]::GetFullPath($Out) } else {
    [IO.Path]::GetFullPath((Join-Path $repoDir $Out))
}
$packageDir = Join-Path $outputDir "hotd2re"
$zipPath = Join-Path $outputDir "hotd2re-multiplayer-complete.zip"
if ((Test-Path -LiteralPath $packageDir) -or (Test-Path -LiteralPath $zipPath)) {
    throw "Output already exists. Choose a new -Out directory to preserve the existing package."
}
$exePath = if ([IO.Path]::IsPathRooted($Exe)) { [IO.Path]::GetFullPath($Exe) } else { Join-Path $repoDir $Exe }
$manifestPath = Join-Path $repoDir "original\game-manifest.json"
foreach ($required in @($exePath, $manifestPath, (Join-Path $repoDir "scenes\st1_play.scene"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file missing: $required" }
}
$playSource = Get-Content -LiteralPath (Join-Path $sourceDir "src\port\tools\play.cpp") -Raw
$buildMatch = [regex]::Match($playSource, 'kBuildName\s*=\s*"([^"]+)"')
if (-not $buildMatch.Success) { throw "Could not read the client build label" }
$buildLabel = $buildMatch.Groups[1].Value
# Refuse to label an older executable with the version from newer source.
$exeText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exePath))
if (-not $exeText.Contains($buildLabel)) { throw "Executable build label does not match source. Rebuild hotd2 first." }

$privacyChecker = Join-Path $PSScriptRoot "check_release_privacy.py"
& $Python $privacyChecker $exePath
if ($LASTEXITCODE -ne 0) { throw "Executable failed the release privacy check." }

# The extraction manifest selects the actual game content, excluding captures, logs,
# screenshots, local saves, and other development files subsequently written to game\.
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
# Four .bin files are the original's personal save block, not model/texture assets.
# SaveData::LoadOriginal initializes fresh defaults when they are absent.
$excluded = @("Config.exe", "pol\bg_adv19.bin", "tex\scr_tod_itm_itamidome2.bin", "pol\komono_0.bin", "pol\tv2.bin")
$gameEntries = @($manifest | Where-Object { $_.file -notin $excluded })
foreach ($entry in $gameEntries) {
    if ($entry.file -notmatch '^(Hod2\.(exe|ini)|(cam|coli|evt|mot|pol|sound|tex)\\[^:]+)$' -or
        $entry.file -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "Unexpected game manifest path: $($entry.file)"
    }
    $sourcePath = Join-Path (Join-Path $repoDir "game") $entry.file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Missing game file: $($entry.file)" }
}

New-Item -ItemType Directory -Path (Join-Path $packageDir "scenes") -Force | Out-Null
Copy-Item -LiteralPath $exePath -Destination (Join-Path $packageDir "hotd2.exe")
Copy-Item -LiteralPath (Join-Path $repoDir "scenes\st1_play.scene") -Destination (Join-Path $packageDir "scenes\st1_play.scene")
foreach ($entry in $gameEntries) {
    $destinationPath = Join-Path (Join-Path $packageDir "game") $entry.file
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destinationPath)) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path (Join-Path $repoDir "game") $entry.file) -Destination $destinationPath
    if ((Get-Item -LiteralPath $destinationPath).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Game file does not match the extraction manifest: $($entry.file)"
    }
}

# Portable defaults, independent of the developer's remaster folder or personal saves.
@'
music=7
guns=7
sfx=7
input=mouse
fullscreen=0
scale=1
aspect=4:3
crt=0
render=1
lobby=64.23.186.88:27020
'@ | Set-Content -LiteralPath (Join-Path $packageDir "hotd2re_settings.ini") -Encoding ascii

@'
THE HOUSE OF THE DEAD 2 - NATIVE PORT WITH ONLINE TWO-PLAYER

START PLAYING
1. Extract the entire ZIP into a normal, writable folder. Do not run inside the ZIP.
2. Open the hotd2re folder and double-click hotd2.exe.

All required game data is included. No separate game installation or disc is needed.
Keep the game and scenes folders next to hotd2.exe. Launch the top-level hotd2.exe.
Requires Windows 10/11 with Direct3D 11 support. Graphics use the original game assets.
Settings and saves are created in this folder. No personal saves are included.

ONLINE TWO-PLAYER
Both players should use this same package.
1. Start the game, enter the mode-select menu, and choose Network.
2. The host selects CREATE ROOM, chooses Unlimited Continues and Private Room, then Start.
3. The other player selects BROWSE LOBBY and picks the host's room.
4. Public rooms need no code. Private rooms appear in the list but require the host's 5-digit code.
5. Both players enter the shared lobby. The creator (player 1) chooses ARCADE, ORIGINAL,
   SCENE SELECTION, BOSS MODE, HORDE MODE, or RANDOMIZER. Chapter and boss selections are controlled by the host.
6. Pause > Return to Lobby brings BOTH players back to that room to choose another mode.
   The room and player slots stay connected. Leave Room (or Esc in the lobby) ends the room.

PEER TO PEER provides unlisted rooms: choose Host to get a 5-digit code, or Join to enter the
host's code. Only the joining player enters a code. This option also needs the lobby service.
Unlimited Continues OFF gives three continues. Backspace or right click goes back/closes a room.
The lobby address is already configured. Both players' versions on the Network screen must match.

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

HORDE MODE
Choose Horde Mode from the main menu, or let the host choose it in the shared online lobby.
Defend the barrels against timed enemy waves. Between rounds, use the 4 x 4 store to buy
weapons, upgrade each weapon's damage and ammunition, repair or reinforce barrels,
buy lives, or recruit CPU agents. Shoot an owned weapon's icon to equip it.
Select READY when finished shopping. Every tenth round includes the Zeal / Kuarl boss pair.
Use the same updated executable on both PCs.

RANDOMIZER
Choose Randomizer from the main menu or the shared online lobby. It plays the Original
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
Close the window or use Exit on the main menu to quit.
Pause > Display: fullscreen, aspect ratio, render resolution, and CRT scanlines.
Pause also offers separate music, gunshot, and sound-effect volumes.
Mouse/touchpad input is enabled by default. For raw light-gun input, close the game
and change input=mouse to input=raw in hotd2re_settings.ini.

BUILD.txt identifies this build. SHA256SUMS.txt lists the packaged file hashes.
'@ | Set-Content -LiteralPath (Join-Path $packageDir "README.txt") -Encoding ascii

$commit = (& git -C $sourceDir rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not read build source revision" }
$sourceChanges = @(& git -C $sourceDir status --porcelain --untracked-files=all -- src CMakeLists.txt)
if ($LASTEXITCODE -ne 0) { throw "Could not read build source status" }
$sourceStatus = if ($sourceChanges.Count) { "local working tree changes" } else { "clean committed source" }
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
@"
Native Windows x86 direct-first multiplayer package with relay fallback ($buildLabel)
Lobby endpoint: 64.23.186.88:27020 UDP
Source base revision: $commit ($sourceStatus)
hotd2.exe SHA256: $exeHash
Includes $($gameEntries.Count) game data files verified against the extraction manifest.
"@ | Set-Content -LiteralPath (Join-Path $packageDir "BUILD.txt") -Encoding ascii

# Retain the native port and SDL notices alongside a complete portable release.
$projectLicense = Join-Path $sourceDir "LICENSE"
if (Test-Path -LiteralPath $projectLicense) {
    Copy-Item -LiteralPath $projectLicense -Destination (Join-Path $packageDir "LICENSE-native-code.txt")
}
$sdlLicense = @((Join-Path $sourceDir "build\_deps\sdl3-src\LICENSE.txt"),
                (Join-Path $repoDir "build\_sdl3_src\LICENSE.txt"),
                (Join-Path $repoDir "build\_deps\sdl3-src\LICENSE.txt")) |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $sdlLicense) { throw "SDL license not found; retain its LICENSE.txt in the build dependency directory." }
Copy-Item -LiteralPath $sdlLicense -Destination (Join-Path $packageDir "LICENSE-SDL.txt")
@'
The native port code is licensed separately from the bundled original game data.
LICENSE-native-code.txt covers the project's original code when that file is present.
SDL retains the notice in LICENSE-SDL.txt. Original game assets and data retain their
respective rights and are not covered by the native port's MIT license.
'@ | Set-Content -LiteralPath (Join-Path $packageDir "CREDITS.txt") -Encoding ascii

$checksums = @(Get-ChildItem -LiteralPath $packageDir -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($packageDir.Length + 1).Replace('\', '/')
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
})
$checksums | Set-Content -LiteralPath (Join-Path $packageDir "SHA256SUMS.txt") -Encoding ascii
Write-Output "Verified and staged $($gameEntries.Count) game files. Compressing package..."
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageDir, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $true)
& $Python $privacyChecker $zipPath
if ($LASTEXITCODE -ne 0) { throw "Package failed the release privacy check. Do not publish it." }
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$zipHash  $([IO.Path]::GetFileName($zipPath))" | Set-Content -LiteralPath ($zipPath + ".sha256") -Encoding ascii
Get-Item -LiteralPath $zipPath | Select-Object FullName, Length, LastWriteTime
