# Imports the game exe into the Ghidra project (ghidra/hotd2), runs auto-analysis,
# and exports a function index + decompiled C to ghidra/export/.
#
# Usage: powershell -File tools/ghidra_analyze.ps1 -Exe game\<exe> [-Reimport]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$GhidraHome = $env:GHIDRA_HOME,
    [string]$Project = "hotd2",
    [switch]$Reimport
)

$ErrorActionPreference = "Stop"
if (-not $GhidraHome) { throw "Set GHIDRA_HOME or pass -GhidraHome with your Ghidra installation directory." }
$root = Split-Path $PSScriptRoot -Parent
$projDir = Join-Path $root "ghidra"
$exportDir = Join-Path $projDir "export-$Project"
$scriptDir = Join-Path $PSScriptRoot "ghidra"
$headless = Join-Path $GhidraHome "support\analyzeHeadless.bat"
$exePath = (Resolve-Path $Exe).Path
$exeName = Split-Path $exePath -Leaf

New-Item -ItemType Directory -Force $exportDir | Out-Null

$common = @("-scriptPath", $scriptDir, "-postScript", "ExportFunctions.java", $exportDir, "-log", (Join-Path $projDir "analyze-$Project.log"))

if ($Reimport -or -not (Test-Path (Join-Path $projDir "$Project.gpr"))) {
    & $headless $projDir $Project -import $exePath -overwrite -processor "x86:LE:32:default" -cspec "windows" @common
} else {
    & $headless $projDir $Project -process $exeName -noanalysis @common
}
if ($LASTEXITCODE -ne 0) { throw "analyzeHeadless failed ($LASTEXITCODE). See ghidra/analyze.log" }

$csv = Import-Csv (Join-Path $exportDir "functions.csv")
"Functions: $($csv.Count)  decompiled: $(($csv | Where-Object decompiled -eq 'true').Count)  thunks: $(($csv | Where-Object is_thunk -eq 'true').Count)"
