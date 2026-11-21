# Agent notes — Vaani

- **Goal:** Minimal Windows tray app for external dictation only. See [README.md](README.md).
- **Stack:** Qt 6 Widgets, CMake 3.16+, C++17, Windows-first.
- **Identity:** `QApplication::setOrganizationName("com.vaani")`, `setApplicationName("Vaani")`.
- **Reference:** `c:\Gitlab-repo\dictation-pro-cpp\dictationprocpp` for speech, PTT, and injection behaviour — do not copy editor, licensing, or DSTextEditor dependencies.
- **Changes:** Prefer small diffs; no new features beyond the current prompt’s scope.
- **Build:** Configure with `CMAKE_PREFIX_PATH` pointing at Qt 6; requires Qt **Widgets** and **Network**. Speech models download at runtime to `%LOCALAPPDATA%\Vaani\models\` (not bundled in the repo).