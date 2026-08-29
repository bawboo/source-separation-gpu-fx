# HTDemucs GPU FX

以 JUCE 製作的音源分離應用（Windows standalone／VST3，macOS 支援建置中）。
分離推論在獨立的 Python/PyTorch worker 中執行，不在 audio callback 內跑模型。

支援兩個模型家族：

- **HTDemucs** — 4 軌（鼓／貝斯／其他／人聲）與 6 軌（再加吉他／鋼琴）
- **MelBand RoFormer** — 99 個模型，涵蓋人聲、伴奏、卡拉 OK、吉他、去混響、
  去噪、群聲、氣音等 10 個類別（57 個經逐一端到端驗證，其餘標註 experimental）

## 功能

- **模式優先介面**：先選「要分什麼」（12 種分離模式），每個模式帶預設模型並可
  換同類替代；選定後拉桿才啟用，且拉桿名稱隨模式改變（人聲／伴奏、吉他／殘餘…）
- **多檔批次**：一次匯入多個檔案，每個檔案一列（勾選框＋檔名＋狀態）；點某列即
  切換到該檔的調音介面，**每個檔案獨立記住自己的拉桿設定**；分離時逐一推論，先
  完成的可先預覽；匯出時只處理勾選的檔案，選一個資料夾即全部輸出
- **按需下載**：選用的 RoFormer 模型自動下載並驗證 SHA-256，滾動快取上限 3 個
- **雙語介面**：預設繁體中文，可即時切換英文，選擇會被記住
- **啟動預設**：預選「人聲分離」模式，上次使用的模式與模型於下次啟動還原
- 匯入音訊或影片、預覽、逐軌音量、匯出 WAV 或回填 MP4 音軌

匯出一律為 44.1 kHz／立體聲／32-bit float WAV，長度與來源一致。

## 系統需求

| | Windows | macOS |
|---|---|---|
| 系統 | Windows 10 22H2 以上（x64） | macOS 12 以上（Intel／Apple Silicon） |
| 建置工具 | Visual Studio 2022 Build Tools（C++ x64） | Xcode Command Line Tools |
| GPU | NVIDIA CUDA（選配，無則用 CPU） | Apple Metal (MPS)（選配） |
| Python | 僅 RoFormer 需要（見下） | 同左 |
| FFmpeg | `C:\ffmpeg-master\bin\ffmpeg.exe` 或內建 sidecar | `brew install ffmpeg` 或內建 sidecar |

## 建置

### Windows

```bat
tools\build_windows_installed.cmd
```

產物：`build\windows-installed\HTDemucsGpuFX_artefacts\Release\Standalone\HTDemucs GPU FX.exe`

> 本機注意：`SpscRing` 具現化較大，若遇到 `C1060 編譯器堆積空間不足`，請確認
> 使用 64 位元工具鏈（`/p:PreferredToolArchitecture=x64`）。

### macOS（Intel ＋ Apple Silicon 通用二進位）

```bash
tools/build_macos.sh
```

產物：`build/macos/HTDemucsGpuFX_artefacts/Release/HTDemucs GPU FX.app`
（`lipo -info` 可確認同時包含 `x86_64` 與 `arm64`）

### RoFormer 推論環境（兩個平台皆需）

```bash
conda create -n htfx-roformer python=3.11 -y
conda activate htfx-roformer
pip install torch            # macOS 用預設 wheel；Windows CUDA 版見下行
# Windows CUDA: pip install torch --index-url https://download.pytorch.org/whl/cu126
pip install -r requirements-htfx-roformer.txt
```

## 執行

啟動時**工作目錄必須是專案根目錄**，程式才找得到 `assets/models/roformer-manifest.json`
與 `worker/roformer_worker.py`；否則模式清單會退化成只有 HTDemucs 4/6 軌。發行的
可攜包已附啟動器處理好這件事。

可用環境變數覆寫路徑：

| 變數 | 用途 |
|---|---|
| `HTFX_ROFORMER_PYTHON` | RoFormer 推論用的 python 執行檔 |
| `HTFX_ROFORMER_WORKER` | `roformer_worker.py` 路徑 |
| `HTFX_ROFORMER_MANIFEST` | 模型清單 JSON |
| `HTFX_ROFORMER_MODELS_DIR` | 模型快取目錄 |
| `HTFX_ROFORMER_OUTPUT_DIR` | RoFormer 中間輸出目錄 |
| `HTFX_UI_LANGUAGE_FILE` / `HTFX_UI_STARTUP_FILE` | 介面語言／啟動選擇的設定檔 |

使用者設定存放於 `%LOCALAPPDATA%\HTDemucs GPU FX\`（macOS：
`~/Library/Application Support/HTDemucs GPU FX/`）。

## 測試

```bat
.loop\checks\full.cmd                                   :: 四個 smoke tests
build\...\htdemucs_goal_check.exe "<某首歌.wav>"          :: 四種分離模式端到端驗收
```

`htdemucs_goal_check` 會對一首真實歌曲跑完 HTDemucs 4/6 軌與兩個 RoFormer 類別的
「匯入→分離→匯出人聲→匯出伴奏」，每種模式各印一行結果。

## 專案結構

```
plugin/          JUCE 前端與音訊處理（PluginProcessor、Localization、SpscRing）
cpp/             HTDemucs frozen worker 的 shared-memory IPC client
worker/          Python worker（HTDemucs IPC、RoFormer 推論與快取管理）
tests/           smoke tests 與端到端驗收工具
tools/           建置、封裝、驗證腳本
assets/models/   模型 metadata 與 RoFormer 清單（權重不入版控）
third_party/     vendored JUCE 8.0.13（已套用 patches/）與 Demucs
```

## 授權與限制

第三方授權見 `THIRD_PARTY_NOTICES.md`。預訓練模型權重與打包好的 runtime 屬私人
資產，不隨版控散布；公開發佈前須先處理該文件所列的模型權重與 FFmpeg 未決事項。
