# HTDemucs GPU FX

HTDemucs GPU FX 是以 JUCE 製作的 Windows standalone / VST3 原型。完整
HTDemucs 模型在獨立的 Python/PyTorch worker 中執行，前端透過 shared-memory
IPC 傳送 stereo audio，避免在 audio callback 直接執行模型推論。

目前公開發佈的主要形式是 Windows Web Installer：安裝器偵測 CUDA GPU，讓有
可用 CUDA 的電腦選擇 CUDA 或純 CPU runtime；沒有可用 CUDA 時自動安裝 CPU
runtime。預設 `htdemucs` 權重會在安裝時由 Meta 官方伺服器下載並驗證 SHA-256，
其他模型由應用程式按需下載。

## 功能

- 預設一般面板只需匯入音訊／影片，再按 `Export Vocals only` 或
  `Export Accompany only`；程式會以預設 `htdemucs` 模型自動分離並詢問輸出位置。
- 一般面板輸出為 32-bit float WAV，預設檔名是
  `<原檔名>_vocals.wav` 與 `<原檔名>_accompany.wav`；伴奏為
  drums、bass、other 的原始音量總和。
- 匯入音訊或影片並分離 drums、bass、other、vocals。
- 匯出個別 stem、全部 stems，或依介面比例混音後匯出。
- 影片輸入可只匯出音訊，或把混音後音訊替換回 MP4。
- Record mode、超高延遲 realtime preview、CPU/CUDA 選擇與 GPU index。
- 全螢幕、UI 縮放與可調整視窗大小。
- 可切換到進階面板使用錄音、即時模式、預覽、stem 比例、完整匯出與模型設定。

## Repository 與 Release 的分工

Repository 的 Git history 只放原始碼、建置腳本、模型 metadata、圖示、測試與
授權文件，不放 build output、runtime、安裝檔或 `.th` 權重。

GitHub Release 放可供安裝器下載的二進位資產：

- `runtime-win-x64-cpu-<version>.zip`
- `runtime-win-x64-cuda-core-<version>.zip`
- `runtime-win-x64-cuda-libraries-<version>.zip`
- `HTDemucs_GPU_FX_Setup_x64.exe`
- runtime JSON、`release-manifest.json` 與 `SHA256SUMS.txt`

CUDA runtime 分成兩個 ZIP，是因為 GitHub Release 的單一 asset 必須小於
2 GiB。安裝器會自動下載並解壓兩個分包，使用者不需手動處理。

完整發佈步驟見 [`docs/RELEASING_WINDOWS.md`](docs/RELEASING_WINDOWS.md)。

## Clone 與建置前置

```powershell
git clone --recurse-submodules https://github.com/OWNER/REPOSITORY.git
cd REPOSITORY
```

需要：

- Windows 10 22H2 或更新版本（x64）
- Visual Studio 2022 Build Tools（Desktop development with C++）
- CMake 3.22+
- Python / PyTorch 環境（CPU 與 CUDA runtime 分別建置）
- Inno Setup 6.7+
- FFmpeg distribution

JUCE 固定在 `third_party/JUCE`，Demucs source 固定在
`third_party/demucs`。若 clone 時未帶 submodule：

```powershell
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File .\tools\apply_dependency_patches.ps1
```

JUCE 以 submodule 固定官方版本；專案需要的 MME 與 portable settings 變更保存在
`patches/juce-8.0.13-htfx.patch`。Windows build script 會以可重複方式自動套用。

## Windows 建置順序

```powershell
.\tools\build_windows_installed.cmd

powershell -ExecutionPolicy Bypass -File .\tools\build_standalone_runtime.ps1 `
  -Flavor cpu -Python C:\path\to\cpu-python.exe
powershell -ExecutionPolicy Bypass -File .\tools\build_standalone_runtime.ps1 `
  -Flavor cuda -Python C:\path\to\cuda-python.exe

powershell -ExecutionPolicy Bypass -File .\tools\package_windows_runtime.ps1 `
  -Flavor cpu -Ffmpeg C:\path\to\ffmpeg.exe
powershell -ExecutionPolicy Bypass -File .\tools\package_windows_runtime.ps1 `
  -Flavor cuda -Ffmpeg C:\path\to\ffmpeg.exe

powershell -ExecutionPolicy Bypass -File .\tools\package_windows_installer_payload.ps1
powershell -ExecutionPolicy Bypass -File .\tools\verify_windows_web_packages.ps1
```

Runtime ZIP 不是讓使用者自己解壓的 portable 包；它們是 Web Installer 的
下載來源。最終 Setup 必須以實際 GitHub Release 的固定 tag URL 編譯：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows_web_installer.ps1 `
  -ReleaseBaseUrl https://github.com/OWNER/REPOSITORY/releases/download/v0.1.0
```

## 模型與授權

Repository 與 runtime ZIP 都不包含預訓練 `.th` 權重。安裝器只保存官方 URL、
檔案大小與 SHA-256，並在使用者安裝時直接從官方來源取得預設模型。

本專案的 JUCE 開源路線採 AGPL-3.0-or-later；第三方元件與發佈注意事項見
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。模型權重與模型訓練資料的
權利不因本專案授權而改變。

目前 Windows runtime 使用 GPLv3-enabled FFmpeg build。公開二進位 Release 前，
仍需附上該 build 對應的 source/build 資訊，或改成不隨 runtime 散布 FFmpeg。

## MelBand RoFormer 模型（進階面板）

除了預設的 HTDemucs 之外，進階面板提供一個分類式 model browser，收錄
[openmirlab/melband-roformer-infer](https://github.com/openmirlab/melband-roformer-infer)
（MIT）的全部 99 個 MelBand RoFormer 分離模型。清單、URL、檔案大小與
SHA-256 記錄在 `assets/models/roformer-manifest.json`；推論由獨立的
Python/PyTorch worker（`worker/roformer_worker.py`）執行，透過與 HTDemucs
相同的 shared-memory IPC 與外掛溝通，audio callback 本身不做任何模型運算。

**分類與稽核狀態**

- 模型依用途分成 10 個類別：`vocals`、`instrumental`、`instvoc`、
  `karaoke`、`dereverb`、`denoise`、`crowd`、`aspiration`、`guitar`、
  `general`。Model browser 可依類別瀏覽，也可用文字搜尋模型名稱。
- 99 個模型中，57 個已由本專案端到端稽核（下載＋SHA-256 驗證→對測試音檔
  分離→輸出格式驗證：時長不變、stereo、32-bit float、樣本值有限）；其餘
  42 個尚未逐一稽核，UI 上會標註 **experimental**，代表尚未保證每個模型都
  能穩定產生可用輸出，使用前請自行核對結果。

**按需下載與快取**

- 模型權重不隨安裝檔或 Repository 一起發佈；使用者在 model browser 選擇
  尚未安裝的模型時才會觸發下載，並顯示下載狀態（下載中／已安裝／失敗）。
- 下載完成後以 manifest 記錄的 SHA-256 驗證雜湊值，驗證失敗會重新下載，
  持續失敗則中止並回報錯誤，不會使用損毀的權重進行推論。
- 本機快取採滾動上限：最多同時保留 3 個已下載模型，超過上限時依最後使用
  時間淘汰最舊的一個（`worker/roformer_cache.py`）。使用者不需手動清理，
  但重新選用先前被淘汰的模型會重新觸發下載。

**輸出命名**

- 每個模型依其類別產生對應命名的 stem（例如 vocals 類別輸出
  `<原檔名>_vocals.wav` 與 `<原檔名>_instrumental.wav`），檔名由該模型
  worker 實際輸出的 stem id 推導，不假設固定的 HTDemucs 輸出順序。
- 匯出檔案的取樣率固定為外掛內部處理率（44,100 Hz），與來源媒體的原始取
  樣率無關；時長則與來源保持一致。

## 已知限制

- Demucs 推論固定為 44,100 Hz stereo。
- 完整模型的 realtime 模式有顯著延遲，不等同一般低延遲效果器。
- GPU 版目前固定為 CUDA 12.1 / PyTorch build；公開發佈前應在乾淨 Windows
  Sandbox 或 VM 驗證安裝、解除安裝與模型下載。
