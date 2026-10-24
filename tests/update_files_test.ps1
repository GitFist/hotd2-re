$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../src/port/update/update_transaction.ps1')
Add-Type -AssemblyName System.IO.Compression

function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { $null = & $Action } catch { $threw = $true }
    Assert $threw $Message
}
function Get-BytesHash([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function New-TestPe([string]$Architecture, [string]$Label) {
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
    $writer.Write([uint16]0x22); $writer.Write([uint16]$magic)
    $stream.Position = 600; $writer.Write([Text.Encoding]::ASCII.GetBytes($Label + [char]0))
    $writer.Dispose(); $stream.Dispose()
    return ,$bytes
}
function Compress-Bytes([byte[]]$Bytes) {
    $stream = [IO.MemoryStream]::new()
    $gzip = [IO.Compression.GZipStream]::new($stream, [IO.Compression.CompressionMode]::Compress, $true)
    try { $gzip.Write($Bytes, 0, $Bytes.Length) } finally { $gzip.Dispose() }
    try { return ,$stream.ToArray() } finally { $stream.Dispose() }
}
function Write-FixtureIndex($Fixture) {
    $Fixture.Index.manifestBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($Fixture.Manifest | ConvertTo-Json -Depth 8)))
    [IO.File]::WriteAllText($Fixture.IndexPath, ($Fixture.Index | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}
function New-Fixture([string]$Name, [string]$Architecture = 'x64', [switch]$Unchanged, [switch]$DuplicateContent) {
    $directory = Join-Path $script:Scratch $Name
    $root = Join-Path $directory 'install'
    $assets = Join-Path $directory 'assets'
    $null = [IO.Directory]::CreateDirectory((Join-Path $root '.hotd2-update'))
    $null = [IO.Directory]::CreateDirectory($assets)
    $label = if ($Architecture -eq 'x64') { '2026 10 24 A' } else { '2026 10 24 NS32 A' }
    $files = [ordered]@{
        'hotd2.exe' = New-TestPe $Architecture $label
        'scenes/st1_play.scene' = [Text.Encoding]::ASCII.GetBytes('new scene')
        'BUILD.txt' = [Text.Encoding]::ASCII.GetBytes('new build')
        'README.txt' = [Text.Encoding]::ASCII.GetBytes('new readme')
        'game/PUT_YOUR_GAME_FILES_HERE.txt' = [Text.Encoding]::ASCII.GetBytes('placeholder')
    }
    if ($Unchanged) { $files['README.txt'] = New-Object byte[] 4MB }
    if ($DuplicateContent) { $files['CREDITS.txt'] = $files['README.txt'] }
    $records = @(); $indexed = @()
    foreach ($name in $files.Keys) {
        $hash = Get-BytesHash $files[$name]
        $compressed = Compress-Bytes $files[$name]
        [IO.File]::WriteAllBytes((Join-Path $assets "hotd2re-file-$hash.gz"), $compressed)
        $records += [pscustomobject]@{ path = $name; bytes = $files[$name].Length; sha256 = $hash }
        $indexed += [pscustomobject]@{ path = $name; bytes = $files[$name].Length; sha256 = $hash; packedBytes = $compressed.Length; packedSha256 = Get-BytesHash $compressed }
        $installed = Join-Path $root $name
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($installed))
        if ($Unchanged) { [IO.File]::WriteAllBytes($installed, $files[$name]) }
        elseif ($name -ne 'BUILD.txt') { [IO.File]::WriteAllText($installed, "old:$name") }
    }
    $protected = @('hotd2re_settings.ini', 'hotd2re_save.dat', 'hotd2re_records.dat', 'hotd2re_ticket.bin', 'game/Hod2.exe', 'visualpacks/mine/custom.bin', 'unknown.dll')
    foreach ($name in $protected) {
        $path = Join-Path $root $name
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, "player:$name")
    }
    $manifest = [pscustomobject]@{ format = 1; packageKind = 'public'; originalGameDataIncluded = $false; executable = [pscustomobject]@{ architecture = $Architecture; buildLabel = $label; sha256 = Get-BytesHash $files['hotd2.exe'] }; files = $records }
    $checksums = [Text.Encoding]::UTF8.GetBytes("exact metadata bytes`r`n")
    $fixture = [pscustomobject]@{
        Root = $root; Assets = $assets; Stage = Join-Path $root '.hotd2-update/stage'; Architecture = $Architecture
        IndexPath = Join-Path $directory 'index.json'; Manifest = $manifest; Files = $files; Protected = $protected
        Calls = [Collections.Generic.List[string]]::new()
        Index = [pscustomobject]@{ format = 1; architecture = $Architecture; buildLabel = $label; manifestBase64 = ''; checksumsBase64 = [Convert]::ToBase64String($checksums); files = $indexed }
    }
    Write-FixtureIndex $fixture
    return $fixture
}
function Expand-Fixture($Fixture, [string]$Architecture = '', [string]$Tag = 'v2026.10.24-a') {
    if (!$Architecture) { $Architecture = $Fixture.Architecture }
    $callback = {
        param($asset, $destination, $maximumBytes, $description)
        Assert ($asset -cmatch '^hotd2re-file-[0-9a-f]{64}\.gz$') 'Unsafe callback asset.'
        Assert ($destination -ceq (Join-Path $Fixture.Stage "blobs/$asset")) 'Unexpected callback destination.'
        Assert ($description -match '^Downloading changes [1-9][0-9]* of [1-9][0-9]* \(.+ MB total\)$') 'Missing bounded progress message.'
        $source = Join-Path $Fixture.Assets $asset
        Assert (([IO.FileInfo]::new($source)).Length -le $maximumBytes) 'Callback limit is smaller than its asset.'
        $Fixture.Calls.Add($asset)
        Copy-HotdDurable $source $destination
        'Downloader output must not escape'
    }
    return Expand-VerifiedFileUpdate $Fixture.Root $Fixture.IndexPath (Get-HotdHash $Fixture.IndexPath) $Fixture.Stage $Architecture $Tag $callback
}
function Assert-Protected($Fixture) {
    foreach ($name in $Fixture.Protected) {
        Assert ([IO.File]::ReadAllText((Join-Path $Fixture.Root $name)) -ceq "player:$name") "Player file changed: $name"
    }
}
function Assert-PreflightReject([string]$Name, [scriptblock]$Mutation) {
    $fixture = New-Fixture $Name
    & $Mutation $fixture
    Write-FixtureIndex $fixture
    Assert-Throws { Expand-Fixture $fixture } "Accepted invalid index: $Name"
    Assert ($fixture.Calls.Count -eq 0) "Downloaded before rejecting invalid index: $Name"
    Assert (![IO.Directory]::Exists($fixture.Stage)) "Created stage before rejecting invalid index: $Name"
    Assert-Protected $fixture
}

$script:Scratch = Join-Path ([IO.Path]::GetTempPath()) ('hotd2-update-files-test-' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($script:Scratch)
try {
    $fixture = New-Fixture unchanged -Unchanged
    $payload = Expand-Fixture $fixture
    Assert ($payload -is [string]) 'Downloader output contaminated payload return value.'
    Assert ($fixture.Calls.Count -eq 0) 'Unchanged files were downloaded.'
    Assert (([IO.FileInfo]::new((Join-Path $payload 'README.txt'))).Length -eq 4MB) 'Large unchanged file was not staged.'
    Assert ((Get-HotdHash (Join-Path $payload 'PACKAGE-MANIFEST.json')) -ceq (Get-BytesHash ([Convert]::FromBase64String($fixture.Index.manifestBase64)))) 'Manifest exact bytes changed.'
    Assert ((Get-HotdHash (Join-Path $payload 'SHA256SUMS.txt')) -ceq (Get-BytesHash ([Convert]::FromBase64String($fixture.Index.checksumsBase64)))) 'Checksum exact bytes changed.'

    foreach ($architecture in @('x64', 'x86')) {
        $fixture = New-Fixture "success-$architecture" $architecture
        # Same-size corruption and an untrusted old inventory must not enable reuse.
        [IO.File]::WriteAllText((Join-Path $fixture.Root 'README.txt'), 'bad readme')
        [IO.File]::WriteAllText((Join-Path $fixture.Root 'PACKAGE-MANIFEST.json'), ($fixture.Manifest | ConvertTo-Json -Depth 8))
        [IO.File]::WriteAllBytes((Join-Path $fixture.Root 'scenes/st1_play.scene'), $fixture.Files['scenes/st1_play.scene'])
        $placeholderHash = Get-HotdHash (Join-Path $fixture.Root 'game/PUT_YOUR_GAME_FILES_HERE.txt')
        $payload = Expand-Fixture $fixture
        Assert ($fixture.Calls.Count -eq 4) 'Missing, changed, or corrupt files were not downloaded selectively.'
        $order = [Collections.Generic.List[string]]::new()
        Install-Update $fixture.Root $payload -FaultInjector { param($index, $name) $order.Add($name) }
        Assert ($order[$order.Count - 1] -ceq 'hotd2.exe') 'Executable was not replaced last.'
        Assert ((Get-HotdHash (Join-Path $fixture.Root 'hotd2.exe')) -ceq $fixture.Manifest.executable.sha256) 'Verified executable was not installed.'
        Assert ((Get-HotdHash (Join-Path $fixture.Root 'game/PUT_YOUR_GAME_FILES_HERE.txt')) -ceq $placeholderHash) 'Installer replaced player-owned placeholder.'
        Assert (!(Test-UpdatePending $fixture.Root)) 'Successful install left a pending transaction.'
        Assert-Protected $fixture
    }

    $fixture = New-Fixture rollback
    $before = @{}
    foreach ($name in @('hotd2.exe', 'README.txt', 'scenes/st1_play.scene')) { $before[$name] = Get-HotdHash (Join-Path $fixture.Root $name) }
    $payload = Expand-Fixture $fixture
    Assert-Throws { Install-Update $fixture.Root $payload -FaultInjector { param($index, $name) if ($name -eq 'hotd2.exe') { throw 'Injected final replacement failure' } } } 'Fault did not roll back installation.'
    foreach ($name in $before.Keys) { Assert ((Get-HotdHash (Join-Path $fixture.Root $name)) -ceq $before[$name]) "Rollback did not restore $name" }
    Assert (![IO.File]::Exists((Join-Path $fixture.Root 'BUILD.txt'))) 'Rollback retained a newly installed file.'
    Assert (!(Test-UpdatePending $fixture.Root)) 'Rollback left a pending transaction.'
    Assert-Protected $fixture

    $fixture = New-Fixture duplicate-content -DuplicateContent
    $payload = Expand-Fixture $fixture
    Assert ($fixture.Calls.Count -eq 5) 'Identical content was downloaded more than once.'
    Assert ((Get-HotdHash (Join-Path $payload 'CREDITS.txt')) -ceq (Get-HotdHash (Join-Path $payload 'README.txt'))) 'Duplicate content was not staged.'
    $fixture = New-Fixture reused-duplicate -DuplicateContent
    [IO.File]::WriteAllBytes((Join-Path $fixture.Root 'CREDITS.txt'), $fixture.Files['CREDITS.txt'])
    $null = Expand-Fixture $fixture
    Assert ($fixture.Calls.Count -eq 4) 'An installed duplicate did not satisfy another path.'
    $fixture = New-Fixture conflicting-transport -DuplicateContent
    $fixture.Index.files[5].packedSha256 = '0' * 64
    Write-FixtureIndex $fixture
    Assert-Throws { Expand-Fixture $fixture } 'Accepted conflicting transport metadata for identical content.'
    Assert ($fixture.Calls.Count -eq 0) 'Downloaded conflicting content-addressed assets.'

    Assert-PreflightReject traversal { param($f) $f.Index.files[2].path = '../BUILD.txt' }
    Assert-PreflightReject userfiles { param($f) $f.Index.files[2].path = 'hotd2re_settings.ini' }
    Assert-PreflightReject duplicate { param($f) $f.Index.files[2].path = 'HOTD2.EXE' }
    Assert-PreflightReject collision { param($f) $f.Index.files[2].path = 'vendor-licenses/nvidia/a.txt'; $f.Index.files[3].path = 'vendor-licenses/nvidia/a.txt/b.txt' }
    Assert-PreflightReject metadata { param($f) $f.Index.files[2].path = 'PACKAGE-MANIFEST.json' }
    Assert-PreflightReject reserved { param($f) $f.Index.files[2].path = 'dxc-licenses/NUL.txt' }
    Assert-PreflightReject manifest-path { param($f) $f.Manifest.files[2].path = 'CREDITS.txt' }
    Assert-PreflightReject manifest-hash { param($f) $f.Manifest.files[2].sha256 = '0' * 64 }
    Assert-PreflightReject manifest-size { param($f) $f.Manifest.files[2].bytes++ }
    Assert-PreflightReject manifest-duplicate { param($f) $f.Manifest.files[2] = $f.Manifest.files[1] }
    Assert-PreflightReject label { param($f) $f.Index.buildLabel = '2026 10 24 B' }
    Assert-PreflightReject complete { param($f) $f.Manifest.packageKind = 'complete' }
    Assert-PreflightReject original-data { param($f) $f.Manifest.originalGameDataIncluded = $true }
    Assert-PreflightReject count { param($f) $f.Index.files = @($f.Index.files[0]) * 513 }
    Assert-PreflightReject large-file { param($f) $f.Index.files[2].bytes = 512MB + 1 }
    Assert-PreflightReject large-total { param($f) foreach ($i in 0..2) { $f.Index.files[$i].bytes = 512MB } }
    Assert-PreflightReject metadata-total {
        param($f)
        $remaining = ($f.Index.files[2..4] | Measure-Object -Property bytes -Sum).Sum
        $f.Index.files[0].bytes = 512MB
        $f.Index.files[1].bytes = [long](512MB - $remaining)
        $f.Manifest.files[0].bytes = $f.Index.files[0].bytes
        $f.Manifest.files[1].bytes = $f.Index.files[1].bytes
    }
    Assert-PreflightReject packed-total { param($f) $f.Index.files[2].packedBytes = 512MB }
    Assert-PreflightReject zero-packed { param($f) $f.Index.files[2].packedBytes = 0 }
    Assert-PreflightReject fractional-size { param($f) $f.Index.files[2].bytes = 1.5 }
    Assert-PreflightReject conflicting-content { param($f) $f.Index.files[2].sha256 = $f.Index.files[3].sha256 }
    Assert-PreflightReject base64 { param($f) $f.Index.checksumsBase64 = '!' }
    foreach ($case in @('architecture', 'tag')) {
        $fixture = New-Fixture $case
        $arch = if ($case -eq 'architecture') { 'x86' } else { 'x64' }
        $tag = if ($case -eq 'tag') { 'v2026.10.24-b' } else { 'v2026.10.24-a' }
        Assert-Throws { Expand-Fixture $fixture $arch $tag } "Accepted wrong $case."
        Assert ($fixture.Calls.Count -eq 0) "Downloaded before rejecting wrong $case."
    }
    $fixture = New-Fixture tampered-index
    $expected = Get-HotdHash $fixture.IndexPath
    [IO.File]::AppendAllText($fixture.IndexPath, ' ')
    Assert-Throws { Expand-VerifiedFileUpdate $fixture.Root $fixture.IndexPath $expected $fixture.Stage x64 'v2026.10.24-a' { throw 'Must not download' } } 'Accepted tampered index.'
    $fixture = New-Fixture oversized-index
    [IO.File]::WriteAllBytes($fixture.IndexPath, (New-Object byte[] (2MB + 1)))
    Assert-Throws { Expand-Fixture $fixture } 'Accepted oversized index.'
    $fixture = New-Fixture wrong-stage
    $fixture.Stage = Join-Path $script:Scratch 'outside/.hotd2-update/stage'
    Assert-Throws { Expand-Fixture $fixture } 'Accepted a stage outside the installation.'
    $fixture = New-Fixture existing-stage
    $null = [IO.Directory]::CreateDirectory($fixture.Stage)
    Assert-Throws { Expand-Fixture $fixture } 'Accepted an existing stage.'
    $fixture = New-Fixture linked-root
    $junction = Join-Path $script:Scratch 'linked-install'
    $null = New-Item -ItemType Junction -Path $junction -Target $fixture.Root
    try {
        $fixture.Root = $junction
        $fixture.Stage = Join-Path $junction '.hotd2-update/stage'
        Assert-Throws { Expand-Fixture $fixture } 'Accepted a linked installation root.'
        Assert ($fixture.Calls.Count -eq 0) 'Downloaded into a linked installation.'
    } finally {
        $junctionFull = [IO.Path]::GetFullPath($junction)
        Assert ($junctionFull -ceq ([IO.Path]::Combine([IO.Path]::GetFullPath($script:Scratch), 'linked-install'))) 'Refusing unexpected junction cleanup path.'
        # Delete just the fixture junction, never recursively traverse its target.
        [IO.Directory]::Delete($junctionFull)
    }

    foreach ($case in @('invalid-pe', 'wrong-pe-architecture', 'wrong-embedded-label')) {
        $fixture = New-Fixture $case
        $entry = $fixture.Index.files[0]
        $newExe = switch ($case) {
            invalid-pe { New-Object byte[] 1024 }
            wrong-pe-architecture { New-TestPe x86 '2026 10 24 A' }
            wrong-embedded-label { New-TestPe x64 '2026 10 24 B' }
        }
        $entry.sha256 = Get-BytesHash $newExe
        $compressed = Compress-Bytes $newExe
        $entry.packedBytes = $compressed.Length; $entry.packedSha256 = Get-BytesHash $compressed
        $fixture.Manifest.files[0].sha256 = $entry.sha256
        $fixture.Manifest.executable.sha256 = $entry.sha256
        [IO.File]::WriteAllBytes((Join-Path $fixture.Assets ('hotd2re-file-' + $entry.sha256 + '.gz')), $compressed)
        Write-FixtureIndex $fixture
        Assert-Throws { Expand-Fixture $fixture } "Accepted invalid final payload: $case"
        Assert ($fixture.Calls.Count -eq 5) "Final payload test failed before all files were staged: $case"
        Assert (!$script:HotdVerifiedPayloads.ContainsKey((Join-Path $fixture.Stage 'hotd2re'))) "Invalid final payload received a verification receipt: $case"
    }

    foreach ($case in @('packed-hash', 'truncated-trailer', 'truncated-deflate', 'bomb', 'unpacked-hash')) {
        $fixture = New-Fixture $case
        $entry = $fixture.Index.files[0]
        $assetPath = Join-Path $fixture.Assets ('hotd2re-file-' + $entry.sha256 + '.gz')
        $compressed = [IO.File]::ReadAllBytes($assetPath)
        if ($case -eq 'packed-hash') { $entry.packedSha256 = '0' * 64 }
        elseif ($case -eq 'truncated-trailer') { $compressed = [byte[]]$compressed[0..($compressed.Length - 2)] }
        elseif ($case -eq 'truncated-deflate') { $compressed = [byte[]]($compressed[0..11] + $compressed[($compressed.Length - 8)..($compressed.Length - 1)]) }
        elseif ($case -eq 'bomb') {
            $compressed = Compress-Bytes (New-Object byte[] 8192)
            [Array]::Copy([BitConverter]::GetBytes([uint32]$entry.bytes), 0, $compressed, $compressed.Length - 4, 4)
        } elseif ($case -eq 'unpacked-hash') { $compressed = Compress-Bytes (New-Object byte[] $entry.bytes) }
        if ($case -ne 'packed-hash') { $entry.packedBytes = $compressed.Length; $entry.packedSha256 = Get-BytesHash $compressed }
        [IO.File]::WriteAllBytes($assetPath, $compressed)
        Write-FixtureIndex $fixture
        Assert-Throws { Expand-Fixture $fixture } "Accepted invalid compressed file: $case"
        Assert ($fixture.Calls.Count -eq 1) "Compressed-file test failed before its download: $case"
        Assert (!$script:HotdVerifiedPayloads.ContainsKey((Join-Path $fixture.Stage 'hotd2re'))) "Rejected payload received a verification receipt: $case"
        Assert-Protected $fixture
    }
    Write-Output 'update_files_test: passed (reuse, deduplication, x86/x64 installation, rollback, metadata and compressed-file rejection)'
} finally {
    $scratchFull = [IO.Path]::GetFullPath($script:Scratch)
    $tempFull = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$scratchFull.StartsWith($tempFull, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($scratchFull) -notmatch '^hotd2-update-files-test-[0-9a-f]{32}$') { throw 'Refusing unexpected fixture cleanup path.' }
    Remove-Item -LiteralPath $scratchFull -Recurse -Force
}
