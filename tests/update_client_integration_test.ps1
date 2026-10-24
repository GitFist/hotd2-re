param(
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [ValidateSet('files', 'full', 'bad-index', 'bad-file', 'missing-file')][string]$DownloadMode = 'files'
)
# Real file/ZIP downloads -> verification -> transactional replacement -> native restart.
# Only the network and progress window are substituted; no release is published.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../src/port/update'))
. (Join-Path $source 'update_runner.ps1')
. (Join-Path $source 'update_transaction.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('hotd2-client-update-test-' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $scratch 'installed game'
$payload = Join-Path $scratch 'fixture/hotd2re'
$session = Join-Path $root ('.hotd2-update/session-' + [guid]::NewGuid())
$null = [IO.Directory]::CreateDirectory((Join-Path $payload 'scenes'))
$null = [IO.Directory]::CreateDirectory($session)
try {
    $fixtureExe = Join-Path $payload 'hotd2.exe'
    $compiler = New-Object CodeDom.Compiler.CompilerParameters
    $compiler.GenerateExecutable = $true
    $compiler.GenerateInMemory = $false
    $compiler.OutputAssembly = $fixtureExe
    $compiler.CompilerOptions = '/target:winexe /platform:' + $Architecture
    $fixtureSource = @'
using System;
using System.IO;
public static class UpdateClientFixture {
    public static void Main(string[] args) {
        File.WriteAllLines(args[0], new string[] { Environment.CurrentDirectory, args[1], args[2] });
    }
}
'@
    # Compile without loading the fixture into the test process (which would
    # lock its file and hide real installer behavior).
    $provider = New-Object Microsoft.CSharp.CSharpCodeProvider
    try {
        $compiled = $provider.CompileAssemblyFromSource($compiler, $fixtureSource)
        if ($compiled.Errors.HasErrors) { throw ($compiled.Errors | Out-String) }
    } finally { $provider.Dispose() }
    Copy-Item -LiteralPath $fixtureExe -Destination (Join-Path $root 'hotd2.exe')
    $label = if ($Architecture -eq 'x86') { '2026 10 24 NS32 C' } else { '2026 10 24 C' }
    # PE overlay makes the fixture carry the same ASCII version marker as hotd2.exe.
    $marker = [Text.Encoding]::ASCII.GetBytes($label + [char]0)
    $stream = [IO.File]::Open($fixtureExe, [IO.FileMode]::Append, [IO.FileAccess]::Write)
    try { $stream.Write($marker, 0, $marker.Length) } finally { $stream.Dispose() }
    [IO.File]::WriteAllText((Join-Path $payload 'scenes/st1_play.scene'), 'new scene')
    $shared = 'vendor-licenses/nvidia/fixture.txt'
    $null = [IO.Directory]::CreateDirectory((Join-Path $payload 'vendor-licenses/nvidia'))
    [IO.File]::WriteAllText((Join-Path $payload $shared), ('unchanged fixture data ' * 32768))
    foreach ($relative in @('scenes/st1_play.scene', $shared)) {
        $destination = Join-Path $root $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::Copy((Join-Path $payload $relative), $destination)
    }
    [IO.File]::WriteAllText((Join-Path $payload 'SHA256SUMS.txt'), 'fixture checksums; archive SHA256 verifies this metadata')
    $files = @('hotd2.exe', 'scenes/st1_play.scene', $shared) | ForEach-Object {
        $path = Join-Path $payload $_
        [pscustomobject]@{ path = $_; bytes = ([IO.FileInfo]::new($path)).Length; sha256 = Get-HotdHash $path }
    }
    $manifest = [pscustomobject]@{
        format = 1; packageKind = 'public'; originalGameDataIncluded = $false; files = @($files)
        executable = @{ architecture = $Architecture; buildLabel = $label; sha256 = Get-HotdHash $fixtureExe }
    }
    [IO.File]::WriteAllText((Join-Path $payload 'PACKAGE-MANIFEST.json'), ($manifest | ConvertTo-Json -Depth 5))
    $script:fixtureArchive = Join-Path $scratch 'fixture.zip'
    $zip = [IO.Compression.ZipFile]::Open($script:fixtureArchive, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in [IO.Directory]::EnumerateFiles($payload, '*', [IO.SearchOption]::AllDirectories)) {
            $relative = $file.Substring($payload.Length + 1).Replace('\', '/')
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file, ('hotd2re/' + $relative))
        }
    } finally { $zip.Dispose() }
    $script:fixtureHash = Get-HotdHash $script:fixtureArchive
    $script:blobSources = @{}
    $packedFiles = @()
    foreach ($entry in $files) {
        $asset = 'hotd2re-file-' + $entry.sha256 + '.gz'
        $blob = Join-Path $scratch $asset
        $outputStream = [IO.File]::Create($blob)
        try {
            $gzip = [IO.Compression.GZipStream]::new($outputStream, [IO.Compression.CompressionMode]::Compress, $true)
            try {
                $inputStream = [IO.File]::OpenRead((Join-Path $payload $entry.path))
                try { $inputStream.CopyTo($gzip) } finally { $inputStream.Dispose() }
            } finally { $gzip.Dispose() }
        } finally { $outputStream.Dispose() }
        $script:blobSources[$asset] = $blob
        $packedFiles += [pscustomobject]@{
            path = $entry.path; bytes = $entry.bytes; sha256 = $entry.sha256
            packedBytes = ([IO.FileInfo]::new($blob)).Length; packedSha256 = Get-HotdHash $blob
        }
    }
    $script:indexSource = Join-Path $scratch 'files.json'
    $index = [pscustomobject]@{
        format = 1; architecture = $Architecture; buildLabel = $label
        manifestBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $payload 'PACKAGE-MANIFEST.json')))
        checksumsBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $payload 'SHA256SUMS.txt')))
        files = $packedFiles
    }
    [IO.File]::WriteAllText($script:indexSource, ($index | ConvertTo-Json -Depth 8))
    $script:indexHash = Get-HotdHash $script:indexSource
    $script:requests = [Collections.Generic.List[string]]::new()
    foreach ($path in @('hotd2re_save.bin', 'hotd2re_settings.ini', 'hotd2re_lightguns.ini', 'packs/custom.bin', 'game/data.bin')) {
        $destination = Join-Path $root $path
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::WriteAllText($destination, ('keep ' + $path))
    }
    Copy-Item -LiteralPath (Join-Path $source 'update_transaction.ps1') -Destination $session
    $output = Join-Path $scratch 'restarted.txt'
    $unicode = [string][char]0x6e38 + [char]0x620f
    $request = [pscustomobject]@{
        root = $root; executable = Join-Path $root 'hotd2.exe'; workingDirectory = $root
        arguments = @($output, 'quoted "argument" with spaces', $unicode)
        architecture = $Architecture; tag = 'v2026.10.24-c'; recover = $false
        parentPid = 2147483647; parentStartTime = '1'
    }
    $requestPath = Join-Path $session 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json))
    function Initialize-UpdateWindow {}
    function Show-UpdateMessage([string]$Message) {
        $cause = ($Error | Select-Object -First 4 | ForEach-Object { $_.ToString() + "`n" + $_.ScriptStackTrace }) -join "`n"
        throw "Unexpected updater error: $Message`n$cause"
    }
    function Save-UpdateDownload($Uri, $Destination, $MaximumBytes, $Description) {
        Assert-UpdateDownloadUri $Uri
        $asset = [IO.Path]::GetFileName($Uri.AbsolutePath)
        $script:requests.Add($Uri.AbsoluteUri)
        if ($asset.Contains('.files.json') -and $DownloadMode -eq 'full') { throw 'Fixture release predates selective updates.' }
        if ($asset.EndsWith('.files.json.sha256')) {
            $hash = if ($DownloadMode -eq 'bad-index') { '0' * 64 } else { $script:indexHash }
            [IO.File]::WriteAllText($Destination, $hash + '  hotd2re-win-' + $Architecture + '.files.json')
        } elseif ($asset.EndsWith('.files.json')) {
            [IO.File]::Copy($script:indexSource, $Destination)
        } elseif ($asset.EndsWith('.gz')) {
            if ($DownloadMode -eq 'missing-file') { throw 'Fixture file asset is unavailable.' }
            if ($DownloadMode -eq 'bad-file') { [IO.File]::WriteAllText($Destination, 'corrupt compressed bytes') }
            else { [IO.File]::Copy($script:blobSources[$asset], $Destination) }
        } elseif ($asset.EndsWith('.zip.sha256')) {
            [IO.File]::WriteAllText($Destination, $script:fixtureHash + '  hotd2re-win-' + $Architecture + '.zip')
        } elseif ($asset.EndsWith('.zip')) { [IO.File]::Copy($script:fixtureArchive, $Destination) }
        else { throw ('Unexpected download: ' + $asset) }
        Assert ((Get-Item -LiteralPath $Destination).Length -le $MaximumBytes) 'Download exceeded its advertised bound.'
    }
    $result = Invoke-UpdateRunner $requestPath $session
    Assert ($result -eq 0) 'Real updater pipeline failed.'
    $deadline = [datetime]::UtcNow.AddSeconds(10)
    while (![IO.File]::Exists($output)) {
        if ([datetime]::UtcNow -gt $deadline) { throw 'Updated game did not restart.' }
        Start-Sleep -Milliseconds 30
    }
    $lines = [IO.File]::ReadAllLines($output)
    Assert ($lines[0] -ceq $root -and $lines[1] -ceq $request.arguments[1] -and $lines[2] -ceq $unicode) 'Restart lost arguments or working directory.'
    Assert ((Get-HotdHash (Join-Path $root 'hotd2.exe')) -ceq $manifest.executable.sha256) 'Updated executable mismatch.'
    $publicRelease = 'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.24-c/'
    $companionRelease = 'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/'
    $indexAsset = 'hotd2re-win-' + $Architecture + '.files.json'
    $expectedRequests = @($companionRelease + $indexAsset + '.sha256')
    if ($DownloadMode -ne 'full') { $expectedRequests += $companionRelease + $indexAsset }
    if ($DownloadMode -in @('files', 'bad-file', 'missing-file')) {
        $expectedRequests += $companionRelease + 'hotd2re-file-' + $manifest.executable.sha256 + '.gz'
    }
    if ($DownloadMode -ne 'files') {
        $expectedRequests += $publicRelease + 'hotd2re-win-' + $Architecture + '.zip.sha256'
        $expectedRequests += $publicRelease + 'hotd2re-win-' + $Architecture + '.zip'
    }
    Assert (($script:requests -join "`n") -ceq ($expectedRequests -join "`n")) 'Update assets did not use the exact matching companion/public release paths.'
    $blobs = @($script:requests | Where-Object { $_.EndsWith('.gz') })
    if ($DownloadMode -eq 'files') {
        Assert ($blobs.Count -eq 1 -and $blobs[0] -ceq ($companionRelease + 'hotd2re-file-' + $manifest.executable.sha256 + '.gz')) 'Unchanged files were downloaded.'
        Assert (-not ($script:requests | Where-Object { $_.EndsWith('.zip') })) 'Selective update unexpectedly downloaded the full package.'
    } else {
        Assert (@($script:requests | Where-Object { $_.EndsWith('.zip') }).Count -eq 1) 'Unavailable/invalid selective update did not use the full package.'
    }
    Assert ((Get-HotdHash (Join-Path $root $shared)) -ceq (Get-HotdHash (Join-Path $payload $shared))) 'Reused file changed.'
    foreach ($path in @('hotd2re_save.bin', 'hotd2re_settings.ini', 'hotd2re_lightguns.ini', 'packs/custom.bin', 'game/data.bin')) {
        Assert ([IO.File]::ReadAllText((Join-Path $root $path)) -ceq ('keep ' + $path)) "Player file changed: $path"
    }
    Assert (!(Test-UpdatePending $root)) 'Committed transaction was left pending.'
    Assert (![IO.Directory]::Exists($session)) 'Completed session was not cleaned.'
    Assert (![IO.Directory]::Exists((Join-Path $root '.hotd2-update/stage'))) 'Completed stage was not cleaned.'
    Write-Output "PASS: $Architecture $DownloadMode update, selective reuse/fallback, real install/restart, saved data and launch options preserved."
} finally {
    # Only this test's newly allocated directory; allow the just-exited fixture's
    # Windows image mapping to be released before removing its executable.
    $resolved = [IO.Path]::GetFullPath($scratch)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved) -match '^hotd2-client-update-test-[0-9a-f]{32}$') {
        for ($attempt = 0; $attempt -lt 10; ++$attempt) {
            try { Remove-Item -LiteralPath $resolved -Recurse -Force; break }
            catch { if ($attempt -eq 9) { throw }; Start-Sleep -Milliseconds 100 }
        }
    }
}
