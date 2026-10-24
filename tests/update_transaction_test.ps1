param([string]$CrashRoot = '', [switch]$CommittedCrash)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../src/port/update/update_transaction.ps1')
Add-Type -AssemblyName System.IO.Compression

if ($CrashRoot) {
    $archive = Join-Path $CrashRoot '.hotd2-update/download.zip'
    $payload = Expand-VerifiedUpdate $archive (Get-HotdHash $archive) (Join-Path $CrashRoot '.hotd2-update/stage') x64 'v2026.10.24-a'
    if ($CommittedCrash) {
        function Remove-HotdTransaction { [Environment]::Exit(78) }
        Install-Update $CrashRoot $payload
    } else {
        Install-Update $CrashRoot $payload -FaultInjector { param($index, $name) if ($index -eq 1) { [Environment]::Exit(77) } }
    }
    throw 'Crash fixture did not stop at the expected point.'
}

function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { $null = & $Action } catch { $threw = $true }
    Assert $threw $Message
}

function New-TestPe([string]$Architecture, [string]$Label, [bool]$Dll = $false) {
    $bytes = New-Object byte[] 1024
    $stream = [IO.MemoryStream]::new($bytes, $true)
    $writer = [IO.BinaryWriter]::new($stream)
    $writer.Write([uint16]0x5a4d)
    $stream.Position = 60; $writer.Write([uint32]128)
    $stream.Position = 128; $writer.Write([uint32]0x4550)
    $machine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x14c }
    $magic = if ($Architecture -eq 'x64') { 0x20b } else { 0x10b }
    $writer.Write([uint16]$machine); $writer.Write([uint16]1)
    $stream.Position = 148; $writer.Write([uint16]240)
    $flags = if ($Dll) { 0x2002 } else { 0x22 }
    $writer.Write([uint16]$flags); $writer.Write([uint16]$magic)
    $stream.Position = 600; $writer.Write([Text.Encoding]::ASCII.GetBytes($Label + [char]0))
    $writer.Dispose(); $stream.Dispose()
    return ,$bytes
}

function Get-BytesHash([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function New-TestInstallation([string]$Name, [string]$Architecture = 'x64', [string]$Extra = '') {
    $root = Join-Path $script:Scratch $Name
    $null = [IO.Directory]::CreateDirectory((Join-Path $root '.hotd2-update'))
    $null = [IO.Directory]::CreateDirectory((Join-Path $root 'scenes'))
    $null = [IO.Directory]::CreateDirectory((Join-Path $root 'game'))
    $null = [IO.Directory]::CreateDirectory((Join-Path $root 'visualpacks/mine'))
    [IO.File]::WriteAllText((Join-Path $root 'hotd2.exe'), 'old executable')
    [IO.File]::WriteAllText((Join-Path $root 'BUILD.txt'), 'old build')
    [IO.File]::WriteAllText((Join-Path $root 'scenes/st1_play.scene'), 'old scene')
    foreach ($relative in @('hotd2re_settings.ini', 'hotd2re_save.dat', 'hotd2re_records.dat', 'hotd2re_ticket.bin', 'game/Hod2.exe', 'game/PUT_YOUR_GAME_FILES_HERE.txt', 'visualpacks/mine/custom.bin', 'unknown.dll')) {
        [IO.File]::WriteAllText((Join-Path $root $relative), "player:$relative")
    }
    $label = if ($Architecture -eq 'x64') { '2026 10 24 A' } else { '2026 10 24 NS32 A' }
    $files = @{
        'hotd2.exe' = New-TestPe $Architecture $label
        'scenes/st1_play.scene' = [Text.Encoding]::ASCII.GetBytes('new scene')
        'BUILD.txt' = [Text.Encoding]::ASCII.GetBytes('new build')
        'README.txt' = [Text.Encoding]::ASCII.GetBytes('new readme')
        'game/PUT_YOUR_GAME_FILES_HERE.txt' = [Text.Encoding]::ASCII.GetBytes('placeholder')
    }
    if ($Extra -eq 'settings') { $files['hotd2re_settings.ini'] = [Text.Encoding]::ASCII.GetBytes('default settings') }
    $records = @()
    foreach ($name in ($files.Keys | Sort-Object)) {
        $records += [ordered]@{ path = $name; bytes = $files[$name].Length; sha256 = Get-BytesHash $files[$name] }
    }
    $manifest = [ordered]@{ format = 1; packageKind = 'public'; originalGameDataIncluded = $false; executable = @{ architecture = $Architecture; buildLabel = $label; sha256 = Get-BytesHash $files['hotd2.exe'] }; files = $records }
    $files['PACKAGE-MANIFEST.json'] = [Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Depth 8))
    $files['SHA256SUMS.txt'] = [Text.Encoding]::ASCII.GetBytes('checksums fixture')
    $archive = Join-Path $root '.hotd2-update/download.zip'
    $zipStream = [IO.File]::Create($archive)
    $zip = [IO.Compression.ZipArchive]::new($zipStream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        foreach ($name in ($files.Keys | Sort-Object)) {
            $entry = $zip.CreateEntry('hotd2re/' + $name)
            $stream = $entry.Open()
            try { $stream.Write($files[$name], 0, $files[$name].Length) } finally { $stream.Dispose() }
        }
        if ($Extra -in @('traversal', 'duplicate', 'link', 'reserved', 'ads')) {
            $name = switch ($Extra) { traversal { 'hotd2re/../outside.txt' } duplicate { 'hotd2re/build.TXT' } link { 'hotd2re/LICENSE-SDL.txt' } reserved { 'hotd2re/dxc-licenses/NUL.txt' } ads { 'hotd2re/BUILD.txt:evil' } }
            $entry = $zip.CreateEntry($name)
            if ($Extra -eq 'link') { $entry.ExternalAttributes = [int](-1577058304) } # UNIX symlink 0xa2000000
        }
    } finally { $zip.Dispose(); $zipStream.Dispose() }
    return $root
}

function Expand-TestPackage([string]$Root, [string]$Architecture = 'x64', [string]$Tag = 'v2026.10.24-a') {
    $archive = Join-Path $Root '.hotd2-update/download.zip'
    return Expand-VerifiedUpdate $archive (Get-HotdHash $archive) (Join-Path $Root '.hotd2-update/stage') $Architecture $Tag
}

function Assert-PlayerFiles([string]$Root) {
    foreach ($relative in @('hotd2re_settings.ini', 'hotd2re_save.dat', 'hotd2re_records.dat', 'hotd2re_ticket.bin', 'game/Hod2.exe', 'game/PUT_YOUR_GAME_FILES_HERE.txt', 'visualpacks/mine/custom.bin', 'unknown.dll')) {
        Assert ([IO.File]::ReadAllText((Join-Path $Root $relative)) -ceq "player:$relative") "Player file changed: $relative"
    }
}

function Assert-OldInstallation([string]$Root) {
    Assert ([IO.File]::ReadAllText((Join-Path $Root 'hotd2.exe')) -ceq 'old executable') 'Old executable was not restored.'
    Assert ([IO.File]::ReadAllText((Join-Path $Root 'BUILD.txt')) -ceq 'old build') 'Old metadata was not restored.'
    Assert ([IO.File]::ReadAllText((Join-Path $Root 'scenes/st1_play.scene')) -ceq 'old scene') 'Old scene was not restored.'
    Assert (![IO.File]::Exists((Join-Path $Root 'README.txt'))) 'Rollback left a newly installed file.'
    Assert-PlayerFiles $Root
}

function Invoke-Crash([string]$Root, [switch]$Committed) {
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'), '-CrashRoot', ('"' + $Root + '"'))
    if ($Committed) { $arguments += '-CommittedCrash' }
    $process = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    $expected = if ($Committed) { 78 } else { 77 }
    Assert ($process.ExitCode -eq $expected) "Crash child failed unexpectedly: $($process.ExitCode)"
    Assert (Test-UpdatePending $Root) 'Interrupted update lost its recovery journal.'
}

$script:Scratch = Join-Path ([IO.Path]::GetTempPath()) ('hotd2-update-test-' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($script:Scratch)
try {
    $root = New-TestInstallation success
    $payload = Expand-TestPackage $root
    $order = [Collections.Generic.List[string]]::new()
    Install-Update $root $payload -FaultInjector { param($index, $name) $order.Add($name) }
    Assert ($order[$order.Count - 1] -ceq 'hotd2.exe') 'Executable was not replaced last.'
    Assert ([IO.File]::ReadAllText((Join-Path $root 'scenes/st1_play.scene')) -ceq 'new scene') 'New scene was not installed.'
    Assert ((Get-HotdHash (Join-Path $root 'hotd2.exe')) -ceq (Get-HotdHash (Join-Path $payload 'hotd2.exe'))) 'Executable was not installed.'
    Assert (!(Test-UpdatePending $root)) 'Successful update left a pending transaction.'
    Assert-PlayerFiles $root
    Restore-Update $root
    Clear-UpdateStage $root
    Assert (![IO.Directory]::Exists((Join-Path $root '.hotd2-update/stage'))) 'Staging files were not cleared.'

    $root = New-TestInstallation obsolete
    [IO.File]::WriteAllText((Join-Path $root 'dxcompiler.dll'), 'old runtime')
    [IO.File]::WriteAllText((Join-Path $root 'PACKAGE-MANIFEST.json'), (@{ format = 1; files = @(@{ path = 'dxcompiler.dll'; sha256 = Get-HotdHash (Join-Path $root 'dxcompiler.dll') }, @{ path = 'hotd2re_settings.ini'; sha256 = '0' * 64 }) } | ConvertTo-Json -Depth 8))
    $payload = Expand-TestPackage $root
    Install-Update $root $payload
    Assert (![IO.File]::Exists((Join-Path $root 'dxcompiler.dll'))) 'Obsolete managed runtime was left installed.'
    Assert-PlayerFiles $root

    # Complete installations inventory thousands of player-owned game assets.
    # Their old inventory must not inherit the small public update payload limit.
    $root = New-TestInstallation completeinventory
    [IO.File]::WriteAllText((Join-Path $root 'dxcompiler.dll'), 'old runtime')
    $completeRecords = @(
        @{ path = 'dxcompiler.dll'; sha256 = Get-HotdHash (Join-Path $root 'dxcompiler.dll') },
        @{ path = 'game/Hod2.exe'; sha256 = '0' * 64 },
        @{ path = 'hotd2re_settings.ini'; sha256 = '0' * 64 }
    )
    $completeRecords += @(0..599 | ForEach-Object { @{ path = "game/asset-$_.bin"; sha256 = '0' * 64 } })
    [IO.File]::WriteAllText((Join-Path $root 'PACKAGE-MANIFEST.json'), (@{
        format = 1; packageKind = 'complete'; originalGameDataIncluded = $true; files = $completeRecords
    } | ConvertTo-Json -Depth 8))
    $payload = Expand-TestPackage $root
    Install-Update $root $payload
    Assert ((Get-HotdHash (Join-Path $root 'hotd2.exe')) -ceq (Get-HotdHash (Join-Path $payload 'hotd2.exe'))) 'Complete installation was not updated.'
    Assert (![IO.File]::Exists((Join-Path $root 'dxcompiler.dll'))) 'Complete inventory retained an obsolete managed runtime.'
    Assert (!(Test-UpdatePending $root)) 'Complete installation left a pending transaction.'
    Assert-PlayerFiles $root

    $root = New-TestInstallation oversizedmanagedinventory
    $managedRecords = @(0..512 | ForEach-Object { @{ path = "vendor-licenses/nvidia/old-$_.txt"; sha256 = '0' * 64 } })
    [IO.File]::WriteAllText((Join-Path $root 'PACKAGE-MANIFEST.json'), (@{
        format = 1; packageKind = 'complete'; originalGameDataIncluded = $true; files = $managedRecords
    } | ConvertTo-Json -Depth 8))
    $payload = Expand-TestPackage $root
    Assert-Throws { Install-Update $root $payload } 'Complete inventory bypassed the managed-file limit.'
    Assert-OldInstallation $root

    $root = New-TestInstallation unsafeoldinventory
    $unsafeRecords = @($completeRecords) + @(@{ path = 'game/../outside.txt'; sha256 = '0' * 64 })
    [IO.File]::WriteAllText((Join-Path $root 'PACKAGE-MANIFEST.json'), (@{
        format = 1; packageKind = 'complete'; originalGameDataIncluded = $true; files = $unsafeRecords
    } | ConvertTo-Json -Depth 8))
    $payload = Expand-TestPackage $root
    Assert-Throws { Install-Update $root $payload } 'Unsafe old inventory path was accepted.'
    Assert-OldInstallation $root

    foreach ($kind in @('traversal', 'duplicate', 'link', 'reserved', 'ads', 'settings')) {
        $root = New-TestInstallation $kind x64 $kind
        Assert-Throws { Expand-TestPackage $root } "Unsafe $kind package was accepted."
        Assert-OldInstallation $root
    }
    $root = New-TestInstallation wrongarch x86
    Assert-Throws { Expand-TestPackage $root x64 } 'Wrong architecture was accepted.'
    $root = New-TestInstallation wrongtag
    Assert-Throws { Expand-TestPackage $root x64 'v2026.10.25-a' } 'Wrong release build was accepted.'
    $root = New-TestInstallation wronghash
    Assert-Throws { Expand-VerifiedUpdate (Join-Path $root '.hotd2-update/download.zip') ('0' * 64) (Join-Path $root '.hotd2-update/stage') x64 'v2026.10.24-a' } 'Wrong archive hash was accepted.'

    $root = New-TestInstallation corruptstage
    $payload = Expand-TestPackage $root
    [IO.File]::WriteAllText((Join-Path $payload 'scenes/st1_play.scene'), 'corrupt')
    Assert-Throws { Install-Update $root $payload } 'Changed staging payload was installed.'
    Assert-OldInstallation $root

    $root = New-TestInstallation halfway
    $payload = Expand-TestPackage $root
    Assert-Throws { Install-Update $root $payload -FaultInjector { param($index, $name) if ($index -eq 1) { throw 'simulated disk failure' } } } 'Injected failure did not abort.'
    Assert-OldInstallation $root
    Assert (!(Test-UpdatePending $root)) 'Completed rollback left a pending transaction.'

    $root = New-TestInstallation launchfailure
    $payload = Expand-TestPackage $root
    Assert-Throws { Install-Update $root $payload -BeforeCommit { throw 'simulated CreateProcess failure' } } 'Launch failure did not abort.'
    Assert-OldInstallation $root

    $root = New-TestInstallation locked
    $payload = Expand-TestPackage $root
    $locked = [IO.File]::Open((Join-Path $root 'scenes/st1_play.scene'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { Assert-Throws { Install-Update $root $payload } 'Locked program file was replaced.' }
    finally { $locked.Dispose() }
    Assert-OldInstallation $root
    Assert (!(Test-UpdatePending $root)) 'Locked-file rollback left a pending transaction.'

    $root = New-TestInstallation lockedrollback
    $payload = Expand-TestPackage $root
    $script:RollbackLock = $null
    try {
        Assert-Throws {
            Install-Update $root $payload -FaultInjector {
                param($index, $name)
                if ($index -eq 0) {
                    $script:RollbackLock = [IO.File]::Open((Join-Path $root $name), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
                    throw 'simulated failure while rollback target is locked'
                }
            }
        } 'Failed rollback was not reported.'
        Assert (Test-UpdatePending $root) 'Failed rollback discarded recovery state.'
        Assert-Throws { Clear-UpdateStage $root } 'Pending recovery allowed staging cleanup.'
    } finally { if ($script:RollbackLock) { $script:RollbackLock.Dispose() } }
    Restore-Update $root
    Assert-OldInstallation $root

    $root = New-TestInstallation interrupted
    Invoke-Crash $root
    $journalPath = Join-Path $root '.hotd2-update/transaction.json'
    $journalText = [IO.File]::ReadAllText($journalPath)
    $journal = $journalText | ConvertFrom-Json
    $journal.files[0].path = '../game/Hod2.exe'
    [IO.File]::WriteAllText($journalPath, ($journal | ConvertTo-Json -Depth 8))
    Assert-Throws { Restore-Update $root } 'Unsafe recovery path was accepted.'
    Assert (Test-UpdatePending $root) 'Invalid recovery erased its journal.'
    Assert-PlayerFiles $root
    [IO.File]::WriteAllText($journalPath, $journalText)
    Restore-Update $root
    Restore-Update $root
    Assert-OldInstallation $root
    Assert (!(Test-UpdatePending $root)) 'Recovery was not idempotent.'

    $root = New-TestInstallation committed
    Invoke-Crash $root -Committed
    Restore-Update $root
    Assert ([IO.File]::ReadAllText((Join-Path $root 'scenes/st1_play.scene')) -ceq 'new scene') 'Committed update was rolled back after restart.'
    Assert (!(Test-UpdatePending $root)) 'Committed transaction did not finalize.'
    Assert-PlayerFiles $root

    Write-Output 'PASS: update package validation, complete-install inventories, player file preservation, stale runtime removal, rollback, launch failure, locked files, interruption recovery, and committed recovery.'
} finally {
    $resolved = [IO.Path]::GetFullPath($script:Scratch)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^hotd2-update-test-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
