$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$zig = Join-Path (Split-Path $root -Parent) ".toolchains\zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$output = Join-Path $root ".cache\tests\dml_api_probe.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE `
    tests\dml_api_probe.c -Ithird_party\onnxruntime -o $output -municode -static
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Build complete: $output"
