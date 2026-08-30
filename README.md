# Music SSP FX — Music Source Separation FX

**伴奏分離｜人聲分離｜樂器分離｜伴奏提取｜去人聲｜卡拉 OK 製作｜吉他分離｜
去混響｜去噪｜AI 音源分離｜vocal removal｜instrumental extraction｜stem separation**

把一首歌拆成人聲、伴奏、鼓、貝斯、吉他、鋼琴等音軌 —— 做卡拉 OK 伴奏、抽人聲
取樣、單獨練某個樂器、或把錄音裡的殘響與噪音去掉。

GPU 加速，**不需要安裝 Python、CUDA toolkit 或任何其他東西**，下載執行即可。

## 下載

前往 **[最新版本](https://github.com/bawboo/source-separation-gpu-fx/releases/latest)**：

| 我想要 | 下載 | 大小 |
|---|---|---|
| **一般使用（推薦）** | `Music_SSP_FX_Setup_x64.exe` | 5 MB |
| 免安裝，沒有 NVIDIA 顯示卡 | `Music_SSP_FX_Portable_win64_cpu-*.zip` | 268 MB |
| 免安裝，有 NVIDIA 顯示卡 | `Music_SSP_FX_Portable_win64_cuda-*.part1of3.zip` 等三個檔 | 合計 2.6 GB |

安裝程式只有 5 MB，它會偵測你的顯示卡再下載對應的版本，所以實際下載量等於上表的
免安裝版大小。安裝時可以自己選：**自動偵測（預設）／CPU／GPU (CUDA)**。

免安裝的 GPU 版因為超過 GitHub 單檔上限而分成三個壓縮檔，**三個都要解壓縮到
同一個資料夾**才完整。嫌麻煩就用安裝程式。

> 執行檔沒有經過程式碼簽章，Windows SmartScreen 首次執行會跳警告。
> 點「其他資訊」→「仍要執行」即可。

## 系統需求

- Windows 10 22H2 以上（64 位元）
- 有 NVIDIA 顯示卡的話，只需要顯示卡驅動（不必裝 CUDA toolkit）；沒有就用 CPU 版
- 硬碟空間：安裝版約 700 MB（CPU）或 4.5 GB（GPU）

macOS 目前沒有預先建置好的發行包，需要自行從原始碼建置，見下方「開發者」。

## 能做什麼

| 需求 | 用哪個模式 |
|---|---|
| 做卡拉 OK 伴奏（去人聲） | 人聲分離 → 匯出伴奏，或卡拉 OK 模式 |
| 抽出乾淨人聲（取樣、翻唱參考） | 人聲分離 → 匯出人聲 |
| 只留吉他／單獨練樂器 | 吉他分離，或 6 軌分離 |
| 完整拆成鼓／貝斯／其他／人聲 | 4 軌分離 |
| 再拆出吉他與鋼琴 | 6 軌分離 |
| 錄音有殘響、噪音、觀眾聲 | 去混響／去噪／群聲分離 |

內建兩個模型家族：

- **HTDemucs** — 4 軌（鼓／貝斯／其他／人聲）與 6 軌（再加吉他／鋼琴），另有
  微調版 `htdemucs_ft`（較慢但品質較好）
- **MelBand RoFormer** — 99 個模型，分成伴奏（37）、人聲（24）、通用（12）、
  去混響（8）、去噪（6）、卡拉 OK（5）、人聲＋伴奏（3）、氣音（2）、群聲（1）、
  吉他（1）共 10 類。其中 57 個經過逐一端到端驗證，其餘標示為 experimental

## 怎麼用

1. **匯入** 一個或多個音訊／影片檔
2. **選模式** —— 先決定「要分什麼」，每個模式都帶好預設模型，也可以換成同類的其他模型
3. **分離** —— 逐一處理，先完成的可以先試聽
4. **調整** 各軌音量，**匯出** 成 WAV（或把新音軌回填回原本的 MP4）

其他好用的地方：

- **多檔批次**：一次匯入多個檔案，每個檔案一列（勾選框＋檔名＋狀態）。點某列就
  切到該檔的調整介面，**每個檔案各自記住自己的設定**；匯出時只處理你勾選的檔案，
  選一個資料夾就全部輸出
- **按需下載**：選到的 RoFormer 模型會自動下載並驗證 SHA-256，只保留最近用的 3 個
- **中英雙語**：預設繁體中文，可即時切換，選擇會被記住
- **記住上次**：下次開啟自動還原上次用的模式與模型

匯出一律是 44.1 kHz／立體聲／32-bit float WAV，長度與來源一致。

## 檔案放在哪

| 內容 | 位置 |
|---|---|
| 下載的模型、設定、暫存 | `%LOCALAPPDATA%\Music SSP FX\` |
| 安裝位置（安裝版） | `%LOCALAPPDATA%\Programs\Music SSP FX\` |

移除時把這兩個資料夾刪掉即可（安裝版可從「應用程式與功能」解除安裝）。

## 常見問題

**要先裝 Python 或 CUDA 嗎？** 不用。發行包內建完整執行環境，GPU 版只需要你原本
就有的 NVIDIA 顯示卡驅動。

**沒有 NVIDIA 顯示卡可以用嗎？** 可以，選 CPU 版，只是比較慢。

**模型為什麼不含在安裝包裡？** 模型權重由各自的作者發布、授權條款也各不相同，
本專案一律不轉散布。第一次用到某個模型時才會從原始出處下載，並比對固定的
SHA-256 確認檔案正確。

**有 VST3 外掛版嗎？** 目前發行的是獨立應用程式。VST3 可以從原始碼建置。

## 授權

本專案原始碼採 **AGPL-3.0-or-later**（見 `LICENSE.md` 與 `COPYING`）。

發行包另外含有這些第三方元件：

- **FFmpeg**（LGPLv3 build）、**libsndfile**、**libsoxr** —— 皆為 LGPL，動態載入，
  可自行替換成你自己編譯的版本
- **NVIDIA CUDA／cuDNN**（僅 GPU 版）—— 取自官方 PyTorch wheel、未經修改，適用
  NVIDIA 自己的授權條款
- JUCE、Demucs、PyTorch 及其相依套件

完整清單、各元件的確切版本與條款連結見 **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**，
發行包內的 `Licenses/` 資料夾也附有同樣內容。

模型權重不隨任何發行物散布，各模型的使用條款依其作者規定。

---

# 開發者

以下只有從原始碼建置時才需要。

## 建置

### Windows

```bat
tools\build_windows_installed.cmd
```

產物：`build\windows-installed\HTDemucsGpuFX_artefacts\Release\Standalone\Music SSP FX.exe`

需要 Visual Studio 2022 Build Tools（C++ x64）。若遇到 `C1060 編譯器堆積空間不足`，
確認使用 64 位元工具鏈（`/p:PreferredToolArchitecture=x64`）—— `SpscRing` 具現化
會讓 32 位元的 `cl` 爆掉。

### macOS（Intel ＋ Apple Silicon 通用二進位）

```bash
tools/build_macos.sh
```

產物：`build/macos/HTDemucsGpuFX_artefacts/Release/Music SSP FX.app`
（`lipo -info` 可確認同時含 `x86_64` 與 `arm64`）。需要 Xcode Command Line Tools。

## 打包

發行包用的 frozen runtime 同時含 HTDemucs 與 RoFormer 兩個後端，共用同一份 PyTorch
（不是兩份 —— 分開凍結會讓下載多出約 2.5 GB）。

```powershell
tools\build_standalone_runtime.ps1 -Python <python.exe> -Flavor cuda|cpu
tools\package_windows_runtime.ps1 -Flavor cuda|cpu -Version <ver>
tools\package_windows_installer_payload.ps1 -Version <ver> -LicenseCollectorPython <python.exe>
tools\build_windows_web_installer.ps1 -ReleaseBaseUrl <tag url> -Version <ver>
tools\package_portable.ps1 -Flavor cuda|cpu -Version <ver>
```

授權通知由 `tools/collect_runtime_licenses.py` 從凍結該 runtime 的直譯器動態收集，
所以版本不會與實際出貨的套件脫節。

## 從原始碼跑 worker

```bash
conda create -n htfx-roformer python=3.11 -y
conda activate htfx-roformer
pip install torch            # macOS 用預設 wheel；Windows CUDA 見下行
# Windows CUDA: pip install torch --index-url https://download.pytorch.org/whl/cu126
pip install -r requirements-htfx-roformer.txt
```

從原始碼執行時，**工作目錄必須是專案根目錄**，程式才找得到
`assets/models/roformer-manifest.json` 與 `worker/roformer_worker.py`；否則模式清單
會退化成只剩 HTDemucs 4/6 軌。

可用環境變數覆寫路徑：

| 變數 | 用途 |
|---|---|
| `HTFX_WORKER_EXECUTABLE` | frozen worker（同時服務 HTDemucs 與 RoFormer） |
| `HTFX_ROFORMER_PYTHON` | 沒有 frozen worker 時的 python 執行檔 |
| `HTFX_ROFORMER_WORKER` | `roformer_worker.py` 路徑 |
| `HTFX_ROFORMER_MANIFEST` | 模型清單 JSON |
| `HTFX_ROFORMER_MODELS_DIR` | 模型快取目錄 |
| `HTFX_ROFORMER_OUTPUT_DIR` | RoFormer 中間輸出目錄 |
| `HTFX_FFMPEG` | ffmpeg 執行檔（預設用發行包內建的） |
| `HTFX_UI_LANGUAGE_FILE` / `HTFX_UI_STARTUP_FILE` | 介面語言／啟動選擇的設定檔 |

## 測試

```bat
.loop\checks\full.cmd                                   :: 四個 smoke tests
build\...\htdemucs_goal_check.exe "<某首歌.wav>"          :: 端到端驗收
```

`htdemucs_goal_check` 會對一首真實歌曲跑完 HTDemucs 4/6 軌與兩個 RoFormer 類別的
「匯入→分離→匯出人聲→匯出伴奏」，每種模式各印一行結果。

**發行前務必用 frozen runtime 再跑一次**（設 `HTFX_WORKER_EXECUTABLE` 指向它）：
凍結後才會出現的問題，開發環境測不出來。

跑測試時不要同時開著 App —— 會搶同一份模型快取，症狀是假的 access violation。

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
