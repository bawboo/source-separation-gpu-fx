# Music SSP FX — Music Source Separation FX

**伴奏分離｜人聲分離｜樂器分離｜伴奏提取｜去人聲｜卡拉 OK 製作｜吉他分離｜
去混響｜去噪｜AI 音源分離｜vocal removal｜instrumental extraction｜stem separation**

以 JUCE 製作的 GPU 加速音源分離應用（Windows standalone／VST3，支援 macOS 建置）。
把一首歌拆成人聲、伴奏、鼓、貝斯、吉他、鋼琴等音軌——做卡拉 OK 伴奏、抽人聲取樣、
單獨練某個樂器、或把錄音裡的殘響與噪音去掉。分離推論在獨立的 Python/PyTorch
worker 中執行，不在 audio callback 內跑模型。

## 能做什麼

| 需求 | 用哪個模式 |
|---|---|
| 做卡拉 OK 伴奏（去人聲） | 人聲分離 → 匯出伴奏，或卡拉 OK 模式 |
| 抽出乾淨人聲（取樣、翻唱參考） | 人聲分離 → 匯出人聲 |
| 只留吉他／單獨練樂器 | 吉他分離，或 6 軌分離 |
| 完整拆成鼓／貝斯／其他／人聲 | 4 軌分離（HTDemucs） |
| 再拆出吉他與鋼琴 | 6 軌分離（HTDemucs 6-stem） |
| 錄音有殘響、噪音、觀眾聲 | 去混響／去噪／群聲分離 |

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
| Python | **不需要**（發行包內建 runtime；僅從原始碼開發時需要） | 同左 |
| FFmpeg | `C:\ffmpeg-master\bin\ffmpeg.exe` 或內建 sidecar | `brew install ffmpeg` 或內建 sidecar |

## 建置

### Windows

```bat
tools\build_windows_installed.cmd
```

產物：`build\windows-installed\HTDemucsGpuFX_artefacts\Release\Standalone\Music SSP FX.exe`

> 本機注意：`SpscRing` 具現化較大，若遇到 `C1060 編譯器堆積空間不足`，請確認
> 使用 64 位元工具鏈（`/p:PreferredToolArchitecture=x64`）。

### macOS（Intel ＋ Apple Silicon 通用二進位）

```bash
tools/build_macos.sh
```

產物：`build/macos/HTDemucsGpuFX_artefacts/Release/Music SSP FX.app`
（`lipo -info` 可確認同時包含 `x86_64` 與 `arm64`）

### 推論環境（只有從原始碼開發時才需要）

發行包內建的 frozen runtime 已同時包含 HTDemucs 與 RoFormer 兩個後端（共用同一份
PyTorch，不是兩份），使用者不需要安裝 Python。以下只給要從原始碼跑 worker 的開發者：


```bash
conda create -n htfx-roformer python=3.11 -y
conda activate htfx-roformer
pip install torch            # macOS 用預設 wheel；Windows CUDA 版見下行
# Windows CUDA: pip install torch --index-url https://download.pytorch.org/whl/cu126
pip install -r requirements-htfx-roformer.txt
```

## 執行

發行包解壓即可執行，不需要安裝 Python 或 CUDA toolkit（CUDA 版只需要 NVIDIA 顯示
卡驅動）。模型權重不隨包散布，第一次用到某個模型時才自動下載並驗證 SHA-256。

從原始碼執行時，啟動的**工作目錄必須是專案根目錄**，程式才找得到 `assets/models/roformer-manifest.json`
與 `worker/roformer_worker.py`；否則模式清單會退化成只有 HTDemucs 4/6 軌。發行的
可攜包已附啟動器處理好這件事。

可用環境變數覆寫路徑：

| 變數 | 用途 |
|---|---|
| `HTFX_WORKER_EXECUTABLE` | frozen worker（同時服務 HTDemucs 與 RoFormer） |
| `HTFX_ROFORMER_PYTHON` | 開發用：沒有 frozen worker 時的 python 執行檔 |
| `HTFX_ROFORMER_WORKER` | 開發用：`roformer_worker.py` 路徑 |
| `HTFX_ROFORMER_MANIFEST` | 模型清單 JSON |
| `HTFX_ROFORMER_MODELS_DIR` | 模型快取目錄 |
| `HTFX_ROFORMER_OUTPUT_DIR` | RoFormer 中間輸出目錄 |
| `HTFX_UI_LANGUAGE_FILE` / `HTFX_UI_STARTUP_FILE` | 介面語言／啟動選擇的設定檔 |

使用者設定存放於 `%LOCALAPPDATA%\Music SSP FX\`（macOS：
`~/Library/Application Support/Music SSP FX/`）。

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
worker/          Python worker（worker_main 分派器、HTDemucs IPC、RoFormer 推論與快取）
tests/           smoke tests 與端到端驗收工具
tools/           建置、封裝、驗證腳本
assets/models/   模型 metadata 與 RoFormer 清單（權重不入版控）
third_party/     vendored JUCE 8.0.13（已套用 patches/）與 Demucs
```

## 授權與限制

第三方授權見 `THIRD_PARTY_NOTICES.md`。預訓練模型權重與打包好的 runtime 屬私人
資產，不隨版控散布；公開發佈前須先處理該文件所列的模型權重與 FFmpeg 未決事項。
