# macOS 建置說明（Apple Silicon ＋ Intel）

## 現況

程式碼本身是跨平台的：

| 元件 | macOS 狀態 |
|---|---|
| JUCE 前端／音訊處理（`plugin/`） | ✅ 無平台相依程式碼 |
| CMake | ✅ 已預設通用二進位（`arm64;x86_64`）、部署目標 macOS 12.0 |
| sidecar 路徑解析 | ✅ 已包含 `<執行檔>/../Resources/sidecar`，正好是 `.app/Contents/Resources/sidecar` |
| 資料目錄 | ✅ 自動落在 `~/Library/Application Support/Music SSP FX` |
| HTDemucs frozen worker IPC | ⚠️ 走 `cpp/GpuWorkerClientPosix.cpp`；**尚未在 macOS 上編譯或執行過** |
| RoFormer | ✅ 直接啟動 worker，不走 IPC |
| frozen runtime | 由本文的腳本產生（每個架構一份） |

> **`.app` 必須在 Mac 上建置。** Windows 無法交叉編譯 macOS 二進位。

## Apple Silicon 與 Intel 是兩份 runtime

App 本身是一個通用二進位，但 **runtime 不可能通用**：PyInstaller 打包的是特定架構的
wheel，而且——

> **PyTorch 從 2.3.0 起不再發布 macOS x86_64 的 wheel，最後一版是 2.2.2。**

demucs 4.1.0 自己的 metadata 就宣告了這個限制：

```
torch<2.3,>=2.1 ; sys_platform == "darwin" and platform_machine == "x86_64"
numpy<2         ; sys_platform == "darwin" and platform_machine == "x86_64"
```

因此：

| | torch | 加速 | 備註 |
|---|---|---|---|
| Apple Silicon（arm64） | 與 Windows 同版 | MPS | 正常速度 |
| Intel（x86_64） | 鎖在 2.1–2.2＋numpy<2 | 無，只有 CPU | RoFormer 在 CPU 上 30 秒片段要 4–14 分鐘 |

**代價**：支援 Intel 等於 `worker/` 的程式碼必須同時相容 2024 年的 torch 2.2 與現行版本，
用新 API 就會被 Intel 版綁住。

一台 Apple Silicon Mac 就能產出兩份——x86_64 那份透過 Rosetta 2 建置與測試，不需要 Intel 機器。

## 最快的做法：一個指令

```bash
git submodule update --init --recursive
tools/macos_build_everything.sh
```

它會依序檢查工具、建立兩個 Python 環境、建置 LGPL FFmpeg、凍結並封裝兩份 runtime、
最後建出 `.app` 並簽章。缺什麼會直接告訴你要執行哪一行安裝指令；中斷後重跑會跳過
已完成的步驟。

只想先做 Apple Silicon（比較快、不需要 Rosetta）：

```bash
tools/macos_build_everything.sh --arm64-only
```

完成後 App 在
`build/macos/HTDemucsGpuFX_artefacts/Release/Music SSP FX.app`，雙擊即可執行。

下面是同樣流程的逐步版本，只有在上面那支腳本中途失敗、需要單獨重跑某一步時才會用到。

## 逐步（故障排除用）

### 1. 工具

```bash
xcode-select --install
brew install cmake sevenzip
softwareupdate --install-rosetta   # 只有要做 Intel 版才需要
```

### 2. 建立兩個 Python 環境

Apple Silicon（原生）：

```bash
conda create -n htfx-macos-arm64 python=3.11 -y
conda activate htfx-macos-arm64
pip install torch numpy demucs einops soundfile librosa ml_collections beartype
```

Intel（Rosetta）——關鍵是用 x86_64 的 Python：

```bash
CONDA_SUBDIR=osx-64 conda create -n htfx-macos-x86 python=3.11 -y
conda activate htfx-macos-x86
conda config --env --set subdir osx-64
pip install 'torch<2.3' 'numpy<2' demucs einops soundfile librosa ml_collections beartype
```

兩個環境都要能 import `mel_band_roformer`（RoFormer 推論套件）。

### 3. 凍結 runtime（每個架構各一次）

```bash
tools/build_standalone_runtime_macos.sh --python "$(conda run -n htfx-macos-arm64 which python)"
tools/build_standalone_runtime_macos.sh --python "$(conda run -n htfx-macos-x86 which python)"
```

腳本從直譯器本身判斷架構，並強制檢查該架構該有的條件（arm64 必須 MPS 可用；x86_64 必須
torch<2.3 且 numpy<2），因為這兩點錯了都會安靜地產出一個不能用的 runtime。

### 3.5 LGPL FFmpeg

```bash
tools/build_ffmpeg_lgpl_macos.sh --arch arm64
tools/build_ffmpeg_lgpl_macos.sh --arch x86_64   # 在 M 系列上交叉編譯
```

從原始碼建置（FFmpeg 不加 `--enable-gpl` 就是 LGPL），並驗證下載的 tarball 雜湊。
產物在 `build/ffmpeg-lgpl-macos-<arch>/bin/`。

### 4. 封裝 runtime

```bash
tools/package_macos_runtime.sh --arch arm64 --version 0.0.9 --ffmpeg build/ffmpeg-lgpl-macos-arm64/bin
```

**FFmpeg 必須是 LGPL 的靜態建置**，腳本會擋兩件事：

- `--enable-gpl` 的建置（`brew install ffmpeg` 預設就是 GPL，會讓發行包背上 GPL 的
  對應原始碼義務，見 `THIRD_PARTY_NOTICES.md`）
- 連結到 `/opt/homebrew/lib` 等非系統動態庫的建置（換一台 Mac 就跑不起來）

產物：`dist/macos/runtime-macos-<arch>-<version>.7z` 與同名 `.json`。

### 5. 建置 .app

```bash
tools/build_macos.sh --bundle-runtime
```

會建置通用二進位、把 sidecar 資源與**本機架構**的 runtime 放進
`Contents/Resources/sidecar/`，然後 ad-hoc 簽章。

驗證是通用二進位：

```bash
lipo -info "build/macos/HTDemucsGpuFX_artefacts/Release/Music SSP FX.app/Contents/MacOS/Music SSP FX"
```

## 簽章與散布

簽章其實是三件事，只有第一件是強制的：

| | 費用 | 必要性 |
|---|---|---|
| **Ad-hoc 簽章** | 免費 | **強制**：Apple Silicon 不執行沒有簽章的 Mach-O。PyInstaller 與上面的腳本都會自動做 |
| **Developer ID 簽章** | US$99/年 | 選配 |
| **公證（notarization）** | 需先有上一項 | 選配 |

沒有 Developer ID 與公證時，使用者仍可執行，但要自己放行：

1. 先雙擊一次讓它被擋下
2. 系統設定 → 隱私權與安全性 → 往下捲 → **Open Anyway** → 再確認一次 → 輸入管理員密碼

> macOS 15 起，以前「右鍵 → 打開」那條捷徑已被移除，只剩上面這條。

**一個對散布有利的性質**：quarantine 旗標是下載檔案的那個程式掛上的。App 自己下載的東西
（模型，以及未來若改成首次啟動下載 runtime）不會被標記，所以使用者一輩子只需要放行
`.app` 那一次，而不是對 runtime 裡幾百個 dylib 逐一處理。**這點請在實機確認**
（`xattr -l <檔案>`）。

## 尚未完成 / 需要實機驗證

- `cpp/GpuWorkerClientPosix.cpp` 從未在 macOS 編譯或執行過（POSIX 共享記憶體 IPC）
- MPS 的運算正確性；部分 torch 運算會 fallback，實務上常需要
  `PYTORCH_ENABLE_MPS_FALLBACK=1`
- smoke 測試使用 `_wputenv_s` 等 Windows API，`.loop/checks/full.cmd` 是批次檔，
  兩者都需要 POSIX 版
- 尚無 `.dmg` 封裝腳本
- 「小體積 App ＋ 首次啟動下載 runtime」尚未實作；目前 `--bundle-runtime` 是把 runtime
  直接放進 `.app`（簡單，但每個架構的下載量會是 GB 級）
