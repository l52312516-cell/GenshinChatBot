param(
    [string]$Output = "ChatGIBotNative.exe",
    [switch]$Debug,
    [switch]$NoResource,
    [switch]$Console
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$toolchains = Join-Path (Split-Path $root -Parent) ".toolchains"
$zigNative = Join-Path $toolchains "zigc\node_modules\@zigc\win32-x64\bin\zig.exe"
$zig = $zigNative
if (-not (Test-Path $zig)) { throw "Zig C compiler not found at $zig" }

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$sources = @(
    "src\main.c", "src\ui.c", "src\util.c", "src\config.c",
    "src\dependencies.c", "src\capture.c", "src\ai.c", "src\ocr.c", "src\vision.c", "src\bot.c", "src\plugin_manager.c", "src\overlay.c",
    "third_party\cjson\cJSON.c"
)

# GDI+ is a C++ API; compile separately to an object (relative path literal) and link.
$gdiObj = ".cache\gdi_ui.o"
$gdiCompile = @("cc", "-target", "x86_64-windows-gnu", "-fno-exceptions", "-fno-rtti", "-c", "src\gdi_ui.cpp", "-o", $gdiObj)
if ($Debug) { $gdiCompile += @("-O0", "-g") } else { $gdiCompile += @("-O2") }
& $zig @gdiCompile
if ($LASTEXITCODE -ne 0) { throw "GDI+ helper compilation failed with exit code $LASTEXITCODE." }

$arguments = @(
    "cc", "-target", "x86_64-windows-gnu", "-std=c17", "-DUNICODE", "-D_UNICODE",
    "-D_CRT_SECURE_NO_WARNINGS", "-Isrc", "-Ithird_party\cjson"
)
if ($Debug) { $arguments += @("-g", "-O0") } else { $arguments += @("-O2") }
$arguments += $sources
$arguments += $gdiObj
if (-not $NoResource) {
    $resource_output = Join-Path $root ".cache\app.res"
    & $zig rc "/FO$resource_output" "resources.rc"
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed with exit code $LASTEXITCODE." }
    $arguments += $resource_output
}
$arguments += @(
    "-o", (Join-Path "dist" $Output),
    "-municode", "-lgdi32", "-lgdiplus", "-ldwmapi", "-luxtheme", "-lcomctl32", "-lshlwapi",
    "-lwinhttp", "-lbcrypt", "-ldxgi", "-ld3d11", "-lole32", "-lwindowscodecs", "-lcomdlg32", "-lshell32", "-static"
)
if (-not $Console) { $arguments += @("-Wl,--subsystem,windows") }

& $zig @arguments
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
Write-Host "Build complete: dist\$Output"
