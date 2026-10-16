# Lists code references to data addresses/ranges. Output: ghidra/datarefs.csv (also printed).
# Usage: powershell -File tools/ghidra_datarefs.ps1 0x007DBD50-0x007DBD5F 0x007DBE34 ...
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets)

$root = Split-Path $PSScriptRoot -Parent
if (-not $env:GHIDRA_HOME) { throw "Set GHIDRA_HOME to your Ghidra installation directory." }
$headless = Join-Path $env:GHIDRA_HOME "support\analyzeHeadless.bat"
$out = Join-Path $root "ghidra\datarefs.csv"
& $headless (Join-Path $root "ghidra") hotd2 -process Hod2.exe -noanalysis -readOnly `
    -scriptPath (Join-Path $PSScriptRoot "ghidra") -postScript DataRefs.java $out @Targets `
    -log (Join-Path $root "ghidra\datarefs.log") | Select-String "ERROR|Exception"
Import-Csv $out | Format-Table -AutoSize | Out-String -Width 220
