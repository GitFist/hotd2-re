# Creates (if needed) and decompiles functions at the given addresses into ghidra/export-hotd2/functions/.
# Usage: powershell -File tools/ghidra_decompile_at.ps1 0x0049E420 0x0049E4A0 ...
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Addresses)

$root = Split-Path $PSScriptRoot -Parent
if (-not $env:GHIDRA_HOME) { throw "Set GHIDRA_HOME to your Ghidra installation directory." }
$headless = Join-Path $env:GHIDRA_HOME "support\analyzeHeadless.bat"
$out = Join-Path $root "ghidra\export-hotd2\functions"
& $headless (Join-Path $root "ghidra") hotd2 -process Hod2.exe -noanalysis `
    -scriptPath (Join-Path $PSScriptRoot "ghidra") -postScript DecompileAt.java $out @Addresses `
    -log (Join-Path $root "ghidra\decompile_at.log") | Select-String "created function|ERROR|Exception"
foreach ($a in $Addresses) {
    $f = Join-Path $out ("0x{0:X8}.c" -f [Convert]::ToUInt32(($a -replace '^0x', ''), 16))
    if (Test-Path $f) { "ok      $f" } else { "MISSING $f" }
}
