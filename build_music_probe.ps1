$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$zig = Join-Path (Split-Path $root -Parent) ".toolchains\zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$output = Join-Path $root ".cache\tests\ChatGIBotMusicProbe.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -DCHATGIBOT_TESTING -D_CRT_SECURE_NO_WARNINGS `
    tests\music_probe.c src\bot.c src\plugin_manager.c src\config.c src\util.c third_party\cjson\cJSON.c `
    -Isrc -Ithird_party\cjson -o $output -static -luser32 -lgdi32 -lshell32 -lshlwapi -lwinhttp -lbcrypt
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
