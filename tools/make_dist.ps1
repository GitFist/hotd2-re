# Public native-port package: no original game data, settings, saves or asset packs.
# Existing outputs are refused. Defaults to x64; -Architecture x86 keeps legacy downloads available.
[CmdletBinding()]
param(
    [string]$Out = "build/dist",
    [ValidateSet("x86", "x64")][string]$Architecture = "x64",
    [string]$Exe = "",
    [string]$SourceRoot = "",
    [string]$Python = "python"
)
$ErrorActionPreference = "Stop"
$arguments = @("--kind", "public", "--architecture", $Architecture, "--out", $Out)
if ($Exe) { $arguments += @("--exe", $Exe) }
if ($SourceRoot) { $arguments += @("--source-root", $SourceRoot) }
& $Python (Join-Path $PSScriptRoot "package_release.py") @arguments
if ($LASTEXITCODE -ne 0) { throw "Public packaging failed; existing outputs were not removed." }
