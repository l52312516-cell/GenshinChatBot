$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$zig = Join-Path (Split-Path $root -Parent) ".toolchains\zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$test_dir = Join-Path $root ".cache\tests"
New-Item -ItemType Directory -Force -Path $test_dir | Out-Null
$test_exe = Join-Path $test_dir "ChatGIBotNativeTests.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS tests\test_core.c src\config.c src\util.c src\ai.c src\capture.c third_party\cjson\cJSON.c -Isrc -Ithird_party\cjson -o $test_exe -static -luser32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -lshlwapi -lwinhttp -lbcrypt -lole32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $test_exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$bot_exe = Join-Path $test_dir "ChatGIBotNativeBotTests.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -DCHATGIBOT_TESTING `
    tests\bot_probe.c src\bot.c src\plugin_manager.c src\config.c src\util.c third_party\cjson\cJSON.c `
    -Isrc -Ithird_party\cjson -o $bot_exe -static -luser32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -lshlwapi -lwinhttp -lbcrypt -lole32 -lshell32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $bot_exe
exit $LASTEXITCODE
