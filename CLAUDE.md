# CLAUDE.md — HTDemucs GPU FX（移交樹）

本目錄是 HTDemucs GPU FX 的原始碼樹（JUCE Windows standalone；HTDemucs 在獨立 Python worker 執行，shared-memory IPC）。上層根目錄的 `CLAUDE.md` 有完整移交背景。

## 若存在 `.loop/`（自主工程迴圈進行中）

- `.loop/LOOP_PLAN.md` 是**已簽核規範**，優先於本檔與任何印象；`.loop/policy.json` 是機器可讀權限/範圍。
- headless iteration 一律遵循 `.loop/ITERATION_PROMPT.md` 的 protocol；一次只做一個 iteration。
- 不要為本目錄建立新的 CLAUDE.md（已存在，就是本檔）。

## 建置與測試（Windows）

- 標準建置：`tools\build_windows_installed.cmd`（VsDevCmd→apply patches→configure→`HTDemucsGpuFX_Standalone`＋`htfx_hardware_probe`）。
- CMake target 名稱先查 `CMakeLists.txt`，不可猜。
- Smoke tests（Release）：`htdemucs_record_mode_smoke`（`--cpu` 可選）、`htdemucs_media_io_smoke`（硬編碼 `C:\ffmpeg-master\bin\ffmpeg.exe`）、`htdemucs_ui_configuration_smoke`。
- smoke 的正式 sidecar 由 junction 提供：`build\windows-installed\Release\Resources\sidecar` → `..\..\..\..\verify\payload-cuda\Resources\sidecar`。

## 禁區（除非使用者明確下令）

- 禁止 push、設 remote、公開上傳任何內容；`assets/models/*.th` 與 `dist/windows-web/*.zip` 是私人資產。
- 不修改 `third_party/`、`patches/`、`assets/models` 既有檔、`dist/`、`build/windows-web/`、五份移交文件（AGENTS.md、CODEX_HANDOFF.md、THIRD_PARTY_NOTICES.md、docs/RELEASING_WINDOWS.md、TRANSFER_BINARY_SHA256.txt）。
- 回報使用繁體中文。
