param(
    [string]$ToolRoot = "E:\AMDDesignTools\2025.2"
)

$vitisRun = Join-Path $ToolRoot "Vitis\bin\vitis-run.bat"
if (-not (Test-Path -LiteralPath $vitisRun)) {
    throw "vitis-run.bat not found: $vitisRun"
}

& $vitisRun --mode hls --tcl "run_hls.tcl"
exit $LASTEXITCODE
