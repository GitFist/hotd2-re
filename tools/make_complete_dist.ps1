# Complete LOCAL package from the user's own installation and extraction manifest.
# Example: -GameDir R:/game -GameManifest R:/original/game-manifest.json
# No input game files are changed. Existing output directories are always refused.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [string]$GameManifest = "",
    [string]$Out = "build/dist-complete-local",
    [ValidateSet("x86", "x64")][string]$Architecture = "x86",
    [string]$Exe = "",
    [string]$SourceRoot = "",
    [string]$Python = "python"
)
$ErrorActionPreference = "Stop"
$arguments = @("--kind", "complete", "--architecture", $Architecture, "--out", $Out, "--game-dir", $GameDir)
if ($GameManifest) { $arguments += @("--game-manifest", $GameManifest) }
if ($Exe) { $arguments += @("--exe", $Exe) }
if ($SourceRoot) { $arguments += @("--source-root", $SourceRoot) }
& $Python (Join-Path $PSScriptRoot "package_release.py") @arguments
if ($LASTEXITCODE -ne 0) { throw "Complete-local packaging failed; existing outputs were not removed." }
