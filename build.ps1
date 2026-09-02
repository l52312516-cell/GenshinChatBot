param(
    [switch]$Debug,
    [switch]$Console,
    [switch]$NoBuildToolInstall
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".cache\zig-local"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".cache\zig-global"
$toolchains = Join-Path (Split-Path $root -Parent) ".toolchains"
$zigNative = Join-Path $toolchains "zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$zigCli = Join-Path $toolchains "zigc\node_modules\@zigc\cli\bin\zig"
$usingZigCli = -not (Test-Path $zigNative) -and (Test-Path $zigCli)
$zig = if (Test-Path $zigNative) { $zigNative } else { Join-Path $root ".toolchains\zig\zig.exe" }
$download = Join-Path $root ".cache\zig.zip"

if (-not $usingZigCli -and -not (Test-Path $zig)) {
    if (Test-Path (Join-Path $root ".toolchains\zig-windows-x86_64-0.13.0\zig.exe")) {
        $zig = Join-Path $root ".toolchains\zig-windows-x86_64-0.13.0\zig.exe"
    }
}

if (-not (Test-Path $zig) -and -not $NoBuildToolInstall) {
    New-Item -ItemType Directory -Force -Path (Split-Path $download) | Out-Null
    curl.exe -L --retry 5 --retry-all-errors --connect-timeout 30 -o $download "https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip"
    Expand-Archive -LiteralPath $download -DestinationPath (Join-Path $root ".toolchains") -Force
    $zig = Join-Path $root ".toolchains\zig-windows-x86_64-0.13.0\zig.exe"
    Remove-Item $download
}

if (-not $usingZigCli -and -not (Test-Path $zig)) { throw "Zig C compiler not found." }
New-Item -ItemType Directory -Force -Path (Join-Path $root "dist") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root ".cache") | Out-Null

$dist = Join-Path $root "dist"
$releaseResidue = @(
    "models", "runtime", "ChatGIBotNative-test-run.exe", "clone.exe"
)
foreach ($name in $releaseResidue) {
    $path = Join-Path $dist $name
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Get-ChildItem -LiteralPath (Join-Path $root "dist") -File -Force -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Extension -in @('.pdb', '.lib', '.log', '.bmp') -or
        $_.Name -in @('config.json', 'ocr-game-result.txt')
    } |
    Remove-Item -Force -ErrorAction SilentlyContinue

$output = if ($Debug) {
    if ($Console) { "ChatGIBotNative-debug-console.exe" } else { "ChatGIBotNative-debug.exe" }
} else { "ChatGIBotNative.exe" }
$output_path = Join-Path $root (Join-Path "dist" $output)
$pdb_path = [System.IO.Path]::ChangeExtension($output_path, ".pdb")
Remove-Item -LiteralPath $output_path, $pdb_path -Force -ErrorAction SilentlyContinue
$resource_output = Join-Path $root ".cache\app.res"
$sources = @(
    "src\main.c", "src\ui.c", "src\util.c", "src\config.c",
    "src\dependencies.c", "src\capture.c", "src\ai.c", "src\ocr.c", "src\vision.c", "src\bot.c", "src\plugin_manager.c", "src\overlay.c",
    "third_party\cjson\cJSON.c"
)
$gdi_object = Join-Path $root ".cache\gdi_ui.o"
$gdi_arguments = @(
    "c++", "-target", "x86_64-windows-gnu", "-std=c++17", "-fno-exceptions", "-fno-rtti",
    "-DUNICODE", "-D_UNICODE", "-D_CRT_SECURE_NO_WARNINGS", "-Isrc",
    "-c", "src\gdi_ui.cpp", "-o", $gdi_object
)
if ($usingZigCli) {
    & node $zigCli @gdi_arguments
} else {
    & $zig @gdi_arguments
}
if ($LASTEXITCODE -ne 0) { throw "GDI+ UI compilation failed with exit code $LASTEXITCODE." }

$arguments = @(
    "cc", "-target", "x86_64-windows-gnu", "-std=c17", "-DUNICODE", "-D_UNICODE",
    "-D_CRT_SECURE_NO_WARNINGS", "-Isrc", "-Ithird_party\cjson"
)
if ($Debug) { $arguments += @("-g", "-O0") } else { $arguments += @("-O2") }
$arguments += $sources
$arguments += $resource_output
$arguments += $gdi_object
$arguments += @(
    "-o", (Join-Path "dist" $output),
    "-municode", "-lgdi32", "-lgdiplus", "-ldwmapi", "-luxtheme", "-lcomctl32", "-lshlwapi",
    "-lwinhttp", "-lbcrypt", "-ldxgi", "-ld3d11", "-lole32", "-lwindowscodecs", "-lcomdlg32", "-lshell32", "-static"
)
if (-not $Console) { $arguments += @("-Wl,--subsystem,windows") }

if ($usingZigCli) {
    & node $zigCli rc "/FO$resource_output" "resources.rc"
} else {
    & $zig rc "/FO$resource_output" "resources.rc"
}
if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed with exit code $LASTEXITCODE." }

if ($usingZigCli) {
    & node $zigCli @arguments
} else {
    & $zig @arguments
}
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
if (-not $Debug) { Remove-Item -LiteralPath $pdb_path -Force -ErrorAction SilentlyContinue }
Copy-Item -LiteralPath (Join-Path $root "config.example.json") `
    -Destination (Join-Path $root "dist\config.example.json") -Force
Get-ChildItem -LiteralPath (Join-Path $root "dist") -File -Force -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Extension -in @('.pdb', '.lib', '.log', '.bmp') -or
        $_.Name -in @('config.json', 'ocr-game-result.txt')
    } |
    Remove-Item -Force -ErrorAction SilentlyContinue
Write-Host "Build complete: dist\$output"
