$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$zig = Join-Path (Split-Path $root -Parent) ".toolchains\zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$testDir = Join-Path $root ".cache\tests"
$configProbe = Join-Path $testDir "legacy_config_probe.exe"
$aiProbe = Join-Path $testDir "legacy_ai_probe.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS `
    tests\legacy_config_probe.c src\config.c src\util.c third_party\cjson\cJSON.c `
    -Isrc -Ithird_party\cjson -o $configProbe -municode -static -lshlwapi -lbcrypt
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS `
    tests\legacy_ai_probe.c src\ai.c src\capture.c src\config.c src\util.c third_party\cjson\cJSON.c `
    -Isrc -Ithird_party\cjson -o $aiProbe -municode -static -luser32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -lshlwapi -lwinhttp -lbcrypt -lole32
exit $LASTEXITCODE
