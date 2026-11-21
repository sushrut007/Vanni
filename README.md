# Vaani

Minimal **external-dictation-only** Windows tray app (Qt 6, CMake, C++17).

## Features

- System tray; settings close to tray
- Push-to-talk (default **F8**), 1–3 key chords; double-tap hands-free
- Whisper + Silero VAD — **downloaded on first run** to `%LOCALAPPDATA%\Vaani\models\` (default **ggml-base-q8_0.bin**)
- Paste into focused app; capture panel + clipboard when paste is blocked
- Dictation HUD (bottom centre) with voice-level animation

## Build

**Prerequisites:** Windows 10/11, VS 2022, CMake 3.16+, Qt 6.5+ (Widgets, Network), speech libs under `src/third_party/include/`.

```powershell
cd "C:\GitLab Repository\Vaani"
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release
```

Output: `build\Release\Vaani.exe` plus copied speech DLLs. Run **windeployqt** on the Release folder if Qt plugins are missing on another PC.

## First run

1. Tray icon appears; complete setup wizard or **Settings → Speech model → Download**.
2. Hold hotkey, speak, release — text pastes into the focused field.
3. Tray **Exit** quits the app.

## Branding

Replace `resources/vaani_app_icon.png` and `resources/icons/vaani-logo.png`, then update `resources/vaani.ico` (multi-size ICO tool) and rebuild.

## Out of scope

In-editor UI, licensing, voice commands, vocabulary training, bundled models in git.

## License

This project is licensed under the [MIT License](LICENSE).