$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$zig = Join-Path (Split-Path $root -Parent) ".toolchains\zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$output = Join-Path $root ".cache\tests\legacy_vision_probe.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS `
    tests\legacy_vision_probe.c src\vision.c src\config.c src\util.c third_party\cjson\cJSON.c `
    -Isrc -Ithird_party\cjson -o $output -municode -static `
    -luser32 -lgdi32 -lshlwapi -lwinhttp -lbcrypt -lole32 -lwindowscodecs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Build complete: $output"
