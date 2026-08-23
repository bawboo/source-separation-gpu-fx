# HTDemucs GPU FX — Codex 移交說明

這是供另一台 Windows 電腦上的 Codex 接手開發與驗證的私人移交包，建立日期為
2026-08-22。專案根目錄就是本檔所在資料夾。

## 先讀這些檔案

1. `AGENTS.md`
2. 本檔 `CODEX_HANDOFF.md`
3. `README.md`
4. `docs/RELEASING_WINDOWS.md`
5. `THIRD_PARTY_NOTICES.md`

## 移交包包含的內容

- 目前工作樹的完整原始碼、測試、建置與封裝腳本。
- `third_party/JUCE` 與 `third_party/demucs` 的實際原始碼工作樹。
- 已套用 HTFX 修改的 JUCE 原始碼；不需要 Git metadata 才能編譯。
- 圖示、模型 metadata，以及私人本機驗證所需的 `.th` 權重。
- CPU runtime ZIP、CUDA core ZIP、CUDA libraries ZIP 與配套 JSON/manifest。
- 授權文件、歷史實作報告與 Windows Release 文件。

為避免攜帶原電腦的帳號、電子郵件、remote 與絕對路徑，本包刻意排除了所有
`.git` 目錄。也排除了可重建或重複的 `Release/`、舊 `dist/`、`results/`、
`.tmp/`、cache 與大部分 `build/` 中間產物。唯一保留的 build output 是以最新
standalone 重新產生的 `build/windows-web/payload`，供 Web Installer 驗證與重建。

## 私人檔案與公開限制

- `assets/models/*.th` 是私人移機用途的預訓練權重，不可因為它們在本包內就
  直接提交到 Git、上傳 GitHub 或放入公開 Release。
- `dist/windows-web/*.zip` 是大型已建好的 CPU/CUDA runtime，也不應加入 Git
  history；若要發佈，只能依 Release 流程作為 GitHub Release assets。
- 不要提交憑證、code-signing key、密碼或 GitHub token。
- 公開前仍須遵守 `THIRD_PARTY_NOTICES.md`，尤其是模型權重與 FFmpeg 的待處理事項。

## 移交時的程式狀態

- 一般面板提供 Import audio/video、Export Vocals only 與
  Export Accompany only；進階功能位於進階面板。
- 已修正不同 sample rate 匯入後的變速／變調：預覽以 host playback sample
  rate 做分數游標與線性插值，模型與輸出仍維持 44.1 kHz。
- 已修正一般面板分離看似卡在 1%：模型載入階段改為 indeterminate 動畫、顯示
  loading 階段文字，worker ready 後才切到可測量的 block progress。
- sidecar 尋找已加入正式安裝路徑 fallback，開發版 standalone 可使用已安裝的
  `%LOCALAPPDATA%\Programs\HTDemucs GPU FX\Resources\sidecar`。
- 原電腦最後的正式 CUDA worker、media I/O 與 UI smoke tests 均通過。
- 48 kHz、2 秒測試檔的 GUI `Export Vocals only` 已成功，輸出為 44.1 kHz、
  2.000 秒、stereo、32-bit float WAV；證明修正後不再變速變調，也不再停在 1%。
- 正式 installer 尚未因上述最新修正重新建置；這是後續工作，不要誤認舊安裝版
  已包含修正。
- `build/windows-web/payload/HTDemucs GPU FX.exe` 是最新 standalone，SHA-256 為
  `A824786E136EC3B14AD5FC0030BC06BC399AF1D2B51989EF65152D87FB6939DA`。

## 新電腦需求

- Windows 10 22H2 或更新版本（x64）
- Visual Studio 2022 / Build Tools，含 Desktop development with C++
- CMake 3.22+
- Git（新電腦如要建立新的本機 history 才需要；不要直接設定 remote 或 push）
- Python/PyTorch CPU 與 CUDA 環境（只有重建 worker/runtime 時需要）
- Inno Setup 6.7+（只有重建 Windows installer 時需要）
- NVIDIA driver 與可用 CUDA GPU（執行 CUDA 路徑時需要）

## 建議接手順序

1. 先執行 `TRANSFER_BINARY_SHA256.txt` 的校驗，不要先改程式。
2. 確認 `third_party/JUCE/CMakeLists.txt` 與
   `third_party/demucs/demucs/__init__.py` 存在。
3. 讀取 `CMakeLists.txt`，確認完整 target 名稱後再建置，不要猜 target。
4. 先執行 `tools/build_windows_installed.cmd` 建置 standalone 與測試。
5. 依風險比例重跑：`htdemucs_record_mode_smoke`、
   `htdemucs_media_io_smoke`、`htdemucs_ui_configuration_smoke`。
6. 在未設定 HTFX 開發 override 的情況，用正式 CUDA worker 匯出短音檔，確認
   loading 動畫、階段文字、block progress、輸出檔與 sample rate/duration。
7. 驗證完成後才規劃重建正式 Web Installer，再用 Windows Sandbox/VM 測安裝、
   啟動、CPU/CUDA 選擇、模型下載與解除安裝。

若缺少工具或環境，請列出缺少的確切元件與偵測證據；不要重新下載並覆蓋本包的
JUCE、Demucs、模型或 runtime，也不要自行發佈到外部服務。
