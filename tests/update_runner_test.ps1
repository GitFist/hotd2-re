# Run with Windows PowerShell 5.1. All network operations are replaced with local fixtures.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$runnerSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../src/port/update/update_runner.ps1'))
. $runnerSource
$script:checks = 0
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:checks++
}
function Reject([scriptblock]$Action, [string]$Name) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    Check $rejected $Name
}
function Wait-File([string]$Path) {
    $deadline = [datetime]::UtcNow.AddSeconds(10)
    while (-not [IO.File]::Exists($Path)) {
        if ([datetime]::UtcNow -gt $deadline) { throw 'Child game did not start.' }
        Start-Sleep -Milliseconds 30
    }
    # The child owns a short-lived write handle until WriteAllLines returns.
    while ($true) {
        try { return [IO.File]::ReadAllLines($Path) } catch {
            if ([datetime]::UtcNow -gt $deadline) { throw }
            Start-Sleep -Milliseconds 30
        }
    }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('hotd2-update-runner-test-' + [guid]::NewGuid())
$null = New-Item -ItemType Directory -Path $temporary
$child = $null
$helperProcess = $null
$ownedProcess = $null
try {
    Check ((ConvertTo-UpdateArgument '') -ceq '""') 'empty argument'
    Check ((ConvertTo-UpdateArgument 'a"b') -ceq '"a\"b"') 'embedded quote'
    Check ((ConvertTo-UpdateArgument 'C:\two words\') -ceq '"C:\two words\\"') 'trailing backslash'
    foreach ($address in @(
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.23-a/hotd2re-win-x64.zip',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.24-c/hotd2re-win-x86.zip.sha256',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-win-x86.files.json.sha256',
        ('https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-file-' + ('a' * 64) + '.gz'),
        'https://release-assets.githubusercontent.com/github-production-release-asset/asset?sig=example',
        'https://objects.githubusercontent.com/path',
        'https://github-releases.githubusercontent.com/path?sig=example')) {
        Assert-UpdateDownloadUri ([Uri]$address)
        Check $true 'trusted release address'
    }
    foreach ($address in @(
        'http://github.com/GitFist/hotd2-re/releases/download/v2026.10.23-a/hotd2re-win-x64.zip',
        'https://github.com.evil.test/file', 'https://evil.test/file',
        'https://github.com/Other/hotd2-re/releases/download/v2026.10.23-a/hotd2re-win-x64.zip',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.23-a/hotd2re-win-x64-complete.zip',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-file-short.gz',
        ('https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-file-' + ('a' * 64) + '.exe'),
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-win-x64.files.json?redirect=other',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.24-c/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.24-c/hotd2re-win-x86.files.json.sha256',
        ('https://github.com/GitFist/hotd2-re/releases/download/v2026.10.24-c/hotd2re-file-' + ('a' * 64) + '.gz'),
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-win-x64.zip',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-c/hotd2re-win-x86.zip.sha256',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-updates-v2026.10.24-c/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-2026.10.24-c/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v1.0/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/UPDATES-v2026.10.24-c/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.24-C/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.02.30-c/hotd2re-win-x64.files.json',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.02.30-c/hotd2re-win-x64.zip',
        'https://github.com@evil.test/file', 'https://release-assets.githubusercontent.com:444/file',
        'https://release-assets.githubusercontent.com/file#fragment')) {
        Reject { Assert-UpdateDownloadUri ([Uri]$address) } 'untrusted redirect rejected'
    }
    $checksum = ('a' * 64)
    Check ((Get-UpdateChecksum ($checksum + "  hotd2re-win-x64.zip`n") 'hotd2re-win-x64.zip') -ceq $checksum) 'published checksum format'
    Reject { Get-UpdateChecksum ($checksum + '  other.zip') 'hotd2re-win-x64.zip' } 'wrong checksum asset'
    Reject { Get-UpdateChecksum ($checksum + "`n" + $checksum) 'hotd2re-win-x64.zip' } 'extra checksum records'
    Check ((Get-UpdateMutexName $temporary) -ceq (Get-UpdateMutexName $temporary.ToUpperInvariant())) 'same installation has case-insensitive mutex'

    # A real Windows child proves quoting, Unicode, working directory, and suspended launch.
    $probe = Join-Path $temporary 'argument probe.exe'
    Add-Type -OutputAssembly $probe -OutputType WindowsApplication -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class UpdateArgumentProbe {
    public static void Main(string[] args) {
        System.Threading.Thread.Sleep(150);
        string[] lines = new string[args.Length];
        lines[0] = Convert.ToBase64String(Encoding.UTF8.GetBytes(Environment.CurrentDirectory));
        for (int i = 1; i < args.Length; ++i) lines[i] = Convert.ToBase64String(Encoding.UTF8.GetBytes(args[i]));
        File.WriteAllLines(args[0], lines);
    }
}
'@
    Initialize-UpdateLaunch
    $output = Join-Path $temporary 'launch output.txt'
    $unicode = ([string][char]0x65E5) + [char]0x672C + [char]0x8A9E
    $values = @('', 'two words', 'embedded"quote', 'C:\trailing\', '\\"mixed', $unicode, "line`nbreak")
    $launchRequest = [pscustomobject]@{ executable = $probe; arguments = @($output) + $values; workingDirectory = $temporary }
    $child = [Hotd2Update.Launch]::Suspended($probe, (Get-UpdateCommandLine $launchRequest), $temporary)
    Check (-not [IO.File]::Exists($output)) 'suspended game does not run before commit'
    $child.Resume()
    $child.Dispose(); $child = $null
    $lines = @(Wait-File $output)
    $decoded = @($lines | ForEach-Object { [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_)) })
    Check ($decoded.Count -eq ($values.Count + 1)) 'all launch arguments retained'
    Check ($decoded[0] -ceq $temporary) 'working directory retained'
    for ($index = 0; $index -lt $values.Count; $index++) { Check ($decoded[$index + 1] -ceq $values[$index]) "argument $index round trips" }
    Reject { [Hotd2Update.Launch]::Suspended((Join-Path $temporary 'missing.exe'), 'missing.exe', $temporary) } 'launch failure reports before commit'

    # Killing the helper must not leave its suspended child locking the executable.
    $helperScript = Join-Path $temporary 'owner.ps1'
    [IO.File]::WriteAllText($helperScript, @'
param($Runner, $Probe, $Output, $Ready)
$ErrorActionPreference = 'Stop'
. $Runner
Initialize-UpdateLaunch
$request = [pscustomobject]@{ executable = $Probe; arguments = @($Output, 'must not run'); workingDirectory = [IO.Path]::GetDirectoryName($Probe) }
$child = [Hotd2Update.Launch]::Suspended($Probe, (Get-UpdateCommandLine $request), $request.workingDirectory)
[IO.File]::WriteAllText($Ready, [string]$child.ProcessId)
while ($true) { Start-Sleep -Seconds 1 }
'@)
    $ownerReady = Join-Path $temporary 'owner-ready.txt'
    $orphanOutput = Join-Path $temporary 'must-not-exist.txt'
    $helperStart = New-Object Diagnostics.ProcessStartInfo
    $helperStart.FileName = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    $helperStart.Arguments = (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $helperScript, $runnerSource, $probe, $orphanOutput, $ownerReady) | ForEach-Object { ConvertTo-UpdateArgument $_ }) -join ' '
    $helperStart.UseShellExecute = $false
    $helperStart.CreateNoWindow = $true
    $helperProcess = [Diagnostics.Process]::Start($helperStart)
    $ownedPid = [int](@(Wait-File $ownerReady)[0])
    $ownedProcess = [Diagnostics.Process]::GetProcessById($ownedPid)
    Check (-not $ownedProcess.HasExited -and -not [IO.File]::Exists($orphanOutput)) 'helper owns a live suspended game'
    $helperProcess.Kill()
    Check ($helperProcess.WaitForExit(10000)) 'helper killed during pending launch'
    Check ($ownedProcess.WaitForExit(10000)) 'helper death terminates its suspended game'
    Check (-not [IO.File]::Exists($orphanOutput)) 'orphan prevention never executes uncommitted game'
    $ownedProcess.Dispose(); $ownedProcess = $null
    $helperProcess.Dispose(); $helperProcess = $null

    $root = Join-Path $temporary 'game folder'
    $session = Join-Path $root ('.hotd2-update/session-' + [guid]::NewGuid())
    $null = New-Item -ItemType Directory -Path $session -Force
    Copy-Item -LiteralPath $probe -Destination (Join-Path $root 'hotd2.exe')
    $requestPath = Join-Path $session 'request.json'
    $gameOutput = Join-Path $root 'restarted.txt'
    $request = [pscustomobject]@{
        root = $root; tag = 'v2026.10.23-a'; architecture = 'x64'; parentPid = [int]::MaxValue
        parentStartTime = '1'; executable = (Join-Path $root 'hotd2.exe')
        arguments = @($gameOutput, 'kept launch option'); workingDirectory = $root; recover = $false
    }
    function Save-Request { [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Compress)) }
    Save-Request
    $read = Read-UpdateRequest $requestPath $session
    Check ($read.executable -ceq $request.executable) 'valid request accepted'
    $request.recover = $true; $request.tag = ''; Save-Request
    $read = Read-UpdateRequest $requestPath $session
    Check $read.recover 'recovery needs no network release tag'
    $request.recover = $false; $request.tag = 'v2026.02.30-a'; Save-Request
    Reject { Read-UpdateRequest $requestPath $session } 'invalid date rejected'
    $result = Invoke-UpdateRunner $requestPath $session
    Check ($result -eq 1 -and -not [IO.File]::Exists((Join-Path $session 'ready'))) 'malformed request never tells parent to quit'
    Check ([IO.File]::Exists((Join-Path $session 'error.txt'))) 'startup error available to parent'
    foreach ($tag in @('updates-v2026.10.24-c', 'v1.0', 'v2026.10.24-C')) {
        $request.tag = $tag; Save-Request
        Reject { Read-UpdateRequest $requestPath $session } 'request requires the official public date tag'
    }
    $request.tag = 'v2026.10.23-a'; $request.executable = $probe; Save-Request
    Reject { Read-UpdateRequest $requestPath $session } 'executable outside installation rejected'
    $request.executable = Join-Path $root 'hotd2.exe'; Save-Request

    # Exercise runner orchestration using a tiny fake transaction and local download fixtures.
    # The real native suspended child must survive the callback closure and resume after commit.
    $coreFixture = @'
function Test-UpdatePending { param($Root) return (Test-Path -LiteralPath (Join-Path $Root 'pending')) }
function Restore-Update {
    param($Root)
    if (Test-Path -LiteralPath (Join-Path $Root 'fail-recovery')) { throw 'Simulated recovery failure.' }
    if (Test-Path -LiteralPath (Join-Path $Root 'original.bin')) {
        [IO.File]::Copy((Join-Path $Root 'original.bin'), (Join-Path $Root 'hotd2.exe'), $true)
    }
    Remove-Item -LiteralPath (Join-Path $Root 'pending') -Force
    [IO.File]::WriteAllText((Join-Path $Root 'restored'), 'restored')
}
function Clear-UpdateStage { param($Root) }
function Expand-VerifiedUpdate { param($Archive, $ExpectedSha256, $Stage, $Architecture, $Tag) return $Stage }
function Install-Update {
    param($Root, $Payload, $BeforeCommit)
    & $BeforeCommit
    if (Test-Path -LiteralPath (Join-Path $Root 'restarted.txt')) { throw 'Game ran before commit.' }
    if (Test-Path -LiteralPath (Join-Path $Root 'fail-install')) {
        [IO.File]::WriteAllText((Join-Path $Root 'pending'), 'pending')
        throw 'Simulated commit failure after child creation.'
    }
    [IO.File]::WriteAllText((Join-Path $Root 'committed'), 'committed')
}
'@
    [IO.File]::WriteAllText((Join-Path $session 'update_transaction.ps1'), $coreFixture)
    function Initialize-UpdateWindow {}
    $script:messages = @()
    $script:failDownload = $false
    $script:downloads = 0
    $script:downloadUris = [Collections.Generic.List[string]]::new()
    function Show-UpdateMessage([string]$Message) { $script:messages += $Message }
    function Save-UpdateDownload {
        param($Uri, $Destination, $MaximumBytes, $Description)
        Assert-UpdateDownloadUri $Uri
        $script:downloadUris.Add($Uri.AbsoluteUri)
        $script:downloads++
        if ($script:failDownload) { throw 'Simulated download failure.' }
        if ($Uri.AbsolutePath.EndsWith('.sha256')) {
            [IO.File]::WriteAllText($Destination, (('a' * 64) + '  hotd2re-win-x64.zip'))
        } else { [IO.File]::WriteAllText($Destination, 'offline test download') }
    }
    $result = Invoke-UpdateRunner $requestPath $session
    Check ($result -eq 0) 'runner completes offline update'
    $expectedUris = @(
        'https://github.com/GitFist/hotd2-re/releases/download/updates-v2026.10.23-a/hotd2re-win-x64.files.json.sha256',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.23-a/hotd2re-win-x64.zip.sha256',
        'https://github.com/GitFist/hotd2-re/releases/download/v2026.10.23-a/hotd2re-win-x64.zip'
    )
    Check (($script:downloadUris -join "`n") -ceq ($expectedUris -join "`n")) 'companion index failure falls back to the matching public ZIP release'
    $lines = @(Wait-File $gameOutput)
    Check ([IO.File]::Exists((Join-Path $root 'committed'))) 'commit completes before game resumes'
    Check ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($lines[1])) -ceq 'kept launch option') 'restart callback keeps arguments'
    Check (-not [IO.Directory]::Exists($session)) 'completed session cleans its own downloads and scripts'
    Check ($script:messages.Count -eq 0) 'successful update shows no failure message'

    foreach ($scenario in @('download-failure', 'commit-failure', 'recovery', 'recovery-failure')) {
        $root = Join-Path $temporary $scenario
        $session = Join-Path $root ('.hotd2-update/session-' + [guid]::NewGuid())
        $null = New-Item -ItemType Directory -Path $session -Force
        Copy-Item -LiteralPath $probe -Destination (Join-Path $root 'hotd2.exe')
        $requestPath = Join-Path $session 'request.json'
        $gameOutput = Join-Path $root 'restarted.txt'
        $request.root = $root
        $request.executable = Join-Path $root 'hotd2.exe'
        $request.arguments = @($gameOutput, $scenario)
        $request.workingDirectory = $root
        $request.recover = $scenario.StartsWith('recovery')
        $request.tag = if ($request.recover) { '' } else { 'v2026.10.23-a' }
        $script:failDownload = $scenario -ne 'commit-failure'
        $script:downloads = 0
        $script:messages = @()
        if ($scenario -eq 'commit-failure') {
            [IO.File]::WriteAllText((Join-Path $root 'fail-install'), 'fail')
            Copy-Item -LiteralPath $probe -Destination (Join-Path $root 'original.bin')
        }
        if ($request.recover) { [IO.File]::WriteAllText((Join-Path $root 'pending'), 'pending') }
        if ($scenario -eq 'recovery-failure') { [IO.File]::WriteAllText((Join-Path $root 'fail-recovery'), 'fail') }
        [IO.File]::WriteAllText((Join-Path $session 'update_transaction.ps1'), $coreFixture)
        Save-Request
        $result = Invoke-UpdateRunner $requestPath $session
        if ($scenario -eq 'recovery-failure') {
            Check ($result -eq 1 -and -not [IO.File]::Exists($gameOutput)) 'failed recovery does not launch an incomplete game'
            Check ([IO.File]::Exists((Join-Path $root 'pending'))) 'failed recovery retains pending backup'
            Check ($script:messages.Count -eq 1 -and $script:messages[0].Contains('backup has been kept')) 'failed recovery explains retry'
            Check (-not [IO.Directory]::Exists($session)) 'failed recovery removes session files without touching backup'
        } else {
            $lines = @(Wait-File $gameOutput)
            Check ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($lines[1])) -ceq $scenario) "$scenario reopens the game"
            Check ($result -eq $(if ($scenario -eq 'recovery') { 0 } else { 1 })) "$scenario reports correct outcome"
            if ($scenario -eq 'commit-failure') {
                Check ([IO.File]::Exists((Join-Path $root 'restored'))) 'commit failure terminates suspended child so executable can be restored'
            }
        }
        if ($request.recover) { Check ($script:downloads -eq 0) "$scenario performs no network request" }
    }
    Write-Output "update_runner_test: $script:checks checks passed"
} finally {
    if ($null -ne $child) { $child.Terminate(); $child.Dispose() }
    if ($null -ne $helperProcess) { if (-not $helperProcess.HasExited) { $helperProcess.Kill() }; $helperProcess.Dispose() }
    if ($null -ne $ownedProcess) { if (-not $ownedProcess.HasExited) { $ownedProcess.Kill() }; $ownedProcess.Dispose() }
    $resolved = [IO.Path]::GetFullPath($temporary)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
