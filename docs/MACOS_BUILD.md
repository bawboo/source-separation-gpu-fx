# macOS 建置說明（Intel ＋ Apple Silicon 通用二進位）

## 現況

程式碼本身是跨平台的：

| 元件 | macOS 狀態 |
|---|---|
| JUCE 前端／音訊處理（`plugin/`） | ✅ 無平台相依程式碼 |
| RoFormer 99 個模型 | ✅ 直接可用（啟動 Python worker，不走 IPC）；裝置自動選 `mps` |
| HTDemucs frozen worker IPC | ✅ 走 `cpp/GpuWorkerClientPosix.cpp`（POSIX 共享記憶體） |
| Windows MME 音訊裝置 | 不編入（僅 Windows 需要，macOS 用 CoreAudio） |
| 打包好的 frozen runtime | ❌ 目前只有 Windows 版；macOS 需自建 Python 環境 |

> **重要**：`.app` 必須在 macOS 上建置。Windows 無法交叉編譯 macOS 二進位
> （需要 macOS SDK 與 Xcode 工具鏈），因此本專案的 Windows 開發機只能提供
> 建置設定與腳本，實際產物請在 Mac 上執行下列步驟取得。

## 步驟

### 1. 安裝工具

```bash
xcode-select --install          # Xcode Command Line Tools
brew install cmake ffmpeg       # CMake 3.22+ 與 FFmpeg
```

### 2. 取得原始碼

複製整個 repository（含 `third_party/JUCE` 與 `third_party/demucs`）。
權重與 runtime 等私人資產不在版控內，見 `TRANSFER_README.md`。

### 3. 建置

```bash
tools/build_macos.sh
```

腳本會：套用 JUCE patch → 以 `arm64;x86_64` 設定 CMake → 建置 standalone →
用 `lipo -info` 印出實際包含的架構。

產物：`build/macos/HTDemucsGpuFX_artefacts/Release/HTDemucs GPU FX.app`

確認是通用二進位：

```bash
lipo -info "build/macos/HTDemucsGpuFX_artefacts/Release/HTDemucs GPU FX.app/Contents/MacOS/HTDemucs GPU FX"
# 應輸出: Architectures in the fat file: ... are: x86_64 arm64
```

### 4. RoFormer 推論環境

```bash
conda create -n htfx-roformer python=3.11 -y
conda activate htfx-roformer
pip install torch torchaudio          # Apple Silicon 會自動啟用 MPS
pip install -r requirements-htfx-roformer.txt
```

啟動 App 時指向它：

```bash
export HTFX_ROFORMER_PYTHON="$HOME/miniconda3/envs/htfx-roformer/bin/python"
cd /path/to/repo   # 工作目錄必須是專案根目錄
"build/macos/HTDemucsGpuFX_artefacts/Release/HTDemucs GPU FX.app/Contents/MacOS/HTDemucs GPU FX"
```

## 已知差異與注意事項

- **運算裝置**：進階選項的 compute 選單在 macOS 顯示 `Auto (Apple MPS, otherwise CPU)`
  與 `Apple Metal (MPS)`；CUDA 選項在 macOS 無作用。
- **HTDemucs 權重**：需要 `assets/models/*.th`（私人資產），或改用 RoFormer 模型
  （會自動下載，不需要預先準備權重）。
- **首次執行的 Gatekeeper**：未簽章的 `.app` 首次開啟會被攔下，
  用「系統設定 → 隱私權與安全性 → 仍要開啟」放行，或
  `xattr -dr com.apple.quarantine "HTDemucs GPU FX.app"`。
- **簽章與公證**：要散布給其他 Mac 使用者需要 Apple Developer 帳號進行
  codesign 與 notarize；自用不需要。
- **尚未在 macOS 上實測**：本設定是依據既有的跨平台程式碼（POSIX IPC 已實作、
  MME 已隔離）撰寫的，但開發機為 Windows，`.app` 尚未實機驗證。首次在 Mac 上
  建置若遇到問題，最可能的地方是 `cpp/GpuWorkerClientPosix.cpp` 的 HTDemucs IPC
  路徑；RoFormer 路徑不經過它，受影響機率較低。
