param([string]$DataDir)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$zig = Join-Path (Split-Path $root -Parent) ".toolchains\zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$test_dir = Join-Path $root ".cache\tests"
New-Item -ItemType Directory -Force -Path $test_dir | Out-Null
$test_exe = Join-Path $test_dir "ChatGIBotOcrSmoke.exe"
& $zig cc -target x86_64-windows-gnu -std=c17 -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS tests\ocr_smoke.c src\ocr.c src\vision.c src\capture.c src\config.c src\util.c third_party\cjson\cJSON.c -Isrc -Ithird_party\cjson -o $test_exe -municode -static -luser32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -lshlwapi -lwinhttp -lbcrypt -lole32 -lwindowscodecs -lm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $DataDir) {
    $DataDir = Get-ChildItem -LiteralPath (Join-Path $root ".cache") -Directory |
        Where-Object {
            (Test-Path (Join-Path $_.FullName "runtime\onnxruntime.dll")) -and
            (Test-Path (Join-Path $_.FullName "models\PP-OCRv6_medium_det.onnx")) -and
            (Test-Path (Join-Path $_.FullName "models\PP-OCRv6_medium_rec.onnx")) -and
            (Test-Path (Join-Path $_.FullName "models\ppocr_keys_v6.txt"))
        } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $DataDir -or -not (Test-Path -LiteralPath $DataDir)) {
    throw "No validated PP-OCRv6 test data directory was found. Pass -DataDir explicitly."
}
$env:CHATGIBOT_DATA_DIR = (Resolve-Path -LiteralPath $DataDir).Path
& $test_exe
$code = $LASTEXITCODE
Remove-Item Env:CHATGIBOT_DATA_DIR -ErrorAction SilentlyContinue
exit $code
