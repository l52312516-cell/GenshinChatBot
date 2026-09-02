# GenshinChatBot

GenshinChatBot is a Windows desktop chat assistant for Genshin Impact. Version 1.0 rebuilds the previous Python/Qt architecture as a pure C17 / Win32 application without Python, Qt, or PyInstaller. It recognizes the chat area with local PP-OCRv6, generates Paimon-style replies through any OpenAI-compatible model, and includes dependency detection, runtime repair, capture/OCR tests, and an in-game speech overlay.

## Language

- [简体中文](README.zh-CN.md)
- [English](README.en-US.md)

## Screenshots

![Overview](screenshots/overview.png)

![Control center](screenshots/control-center.png)

![OCR engine](screenshots/ocr.png)

![AI configuration](screenshots/ai.png)

![Bot configuration](screenshots/bot.png)

![Dependencies and environment](screenshots/dependencies.png)

![Plugins](screenshots/plugins.png)

## Major Upgrades Over The Python Version

- Native architecture: the main program is now C17 / Win32, using native windows and controls to reduce Python runtime, Qt library, and PyInstaller extraction overhead.
- Lightweight distribution: the release core is only `ChatGIBotNative.exe` and `config.example.json`; OCR runtimes and models are installed or repaired on demand from the Dependencies page.
- Local recognition upgrade: replaces the optional PaddleOCR / PP-OCRv5 setup from the Python version with PP-OCRv6 Medium by default, including model tier selection and dictionary validation.
- Modern capture paths: keeps BitBlt and PrintWindow and adds Desktop Duplication, with automatic fallback to BitBlt where unsupported.
- Optional GPU acceleration: adds a DirectML execution path with dynamically loaded ONNX Runtime; GPU initialization or inference failures automatically fall back to CPU.
- Dependency self-repair: adds a Dependencies page that detects ONNX Runtime, DirectML, PP-OCRv6, model hashes, dictionaries, and Windows capture capabilities, with one-click install/repair.
- Dark native settings UI: adds Control Center, OCR, AI, Bot, Dependencies, Plugins, and About pages with status text, live logs, capture tests, and OCR results.
- Safer configuration: API keys are recommended through environment variables such as `CHATGIBOT_AI_KEY`; the build process removes local configuration, logs, OCR screenshots, and test results.
- Stable sending policy: unified speech intervals, message deduplication, blacklist handling, foreground-window confirmation, and the log overlay; recognition defaults to every 1.5 seconds and game messages are separated by at least 600 ms.
- Automated coverage: adds build probes and tests for configuration compatibility, message deduplication, speech intervals, plugin commands, AI/vision requests, PP-OCRv6 loading, real-capture recognition, and UI flow.

## Requirements

- Windows 10/11 x64
- A capturable Genshin Impact window
- Administrator privileges (built into the application manifest for window capture, foreground switching, and input sending)

## Build

```powershell
.\build.ps1
.\build.ps1 -Debug
```

The build script prefers the workspace Zig C compiler and downloads it into the project `.cache` when missing. It does not install Python or Qt system-wide.

## Run

1. Start `dist\ChatGIBotNative.exe`.
2. On the Dependencies page, click “Install / Repair Runtimes” and “Validate & Repair Models”, then click “Re-detect”.
3. On the OCR page, choose capture mode, CPU/DirectML, and model tier, then run “Test Capture” or “Test OCR”.
4. On the AI page, enter an OpenAI-compatible Base URL and model; provide the API key with `CHATGIBOT_AI_KEY` where possible.
5. On the Bot page, save the game window title and sending limits, then start the bot.

The bot recognizes the chat area every 1.5 seconds by default and separates game messages by at least 600 ms. While running, it shows a non-interactive log overlay in the lower-right corner of the game; the overlay hides automatically when you switch away from Genshin Impact or stop the bot.

## Offline Deployment

The first release only needs `ChatGIBotNative.exe` and `config.example.json`. Runtimes and models can be downloaded from the Dependencies page, or carried manually for offline use:

```text
runtime\onnxruntime.dll
runtime\DirectML.dll
models\detection model
models\recognition model
models\dictionary
```

## Tests

```powershell
.\build_tests.ps1
```

## Security Notice

Do not commit or distribute a real `config.json`, logs, or archives containing API keys. Revoke and rotate leaked keys immediately in your provider console.
