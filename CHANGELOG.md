# Changelog

本專案的版本紀錄。格式依循 [Keep a Changelog](https://keepachangelog.com/zh-TW/1.1.0/)，
版本號依循 [語意化版本](https://semver.org/lang/zh-TW/)。

## [0.0.2] - 2026-08-30

更名為 **Music SSP FX**，並把 RoFormer 併進既有的 frozen runtime，讓使用者完全
不必安裝 Python。

### 變更

- **產品更名**：`HTDemucs GPU FX` → `Music SSP FX`（README 標題為
  Music Source Separation FX）。設定與 sidecar 目錄改用新名稱，舊目錄仍會被讀取
  並在首次啟動時搬移，既有使用者的語言與啟動選擇不會遺失
- **一份 runtime、兩個後端**：HTDemucs 與 MelBand RoFormer 共用同一份 PyTorch，
  凍結成單一 PyInstaller bundle（`worker/worker_main.py` 依第一個參數分派）。
  另外凍結一份 RoFormer runtime 會讓 torch 與 CUDA 函式庫重複約 2.5 GB
- **RoFormer 不再需要 Python**：偵測到 frozen runtime 時走它的 `roformer`
  子命令；找不到時才退回開發用的 python + script 路徑
- **安裝程式的 runtime 選擇頁**：改為三個選項——自動偵測（預設）、CPU、GPU
  (CUDA)。該頁一律顯示（過去偵測不到 CUDA 時整頁被跳過），並顯示 DXGI／CUDA
  Driver API 的偵測結果；在沒有可用 CUDA 的機器上手動選 GPU 會先確認
- 授權通知改為從凍結該 runtime 的 Python 環境動態收集
  （`tools/collect_runtime_licenses.py`），取代寫死且已過時的版本清單

### 發行體積

| 包 | 0.1.0（不含 RoFormer） | 0.0.2（含 RoFormer） |
|---|---|---|
| CPU | 297.6 MiB | **283.5 MiB** |
| CUDA（合計） | 2.31 GiB（2 個檔） | 2.59 GiB（3 個檔） |

CPU 版含了 RoFormer 反而更小（新的 CPU PyTorch wheel 較精簡）；CUDA 版
增加約 295 MiB，主要是 librosa 的相依（llvmlite 70 MB、scipy 37 MB）。
未壓縮的 RoFormer 淨增量為 110 MB。

### 修正

- CUDA runtime 的分包寫死了 `torch_cuda.dll`／`cudnn_cnn_infer64_8.dll`／
  `cublasLt64_12.dll` 三個檔名，換成新的 PyTorch/CUDA 版本後檔名不同會直接失敗；
  改為依實際檔案大小動態分包（每包上限 1.7 GB，低於 GitHub 的 2 GB 單檔限制），
  安裝程式的 `[Files]` 也改為由建置腳本產生，不再限定兩個檔案
- PyInstaller 工具目錄改為依直譯器版本區分，CPU（3.11）與 CUDA（3.13）runtime
  不再互相污染
- **HTDemucs 4 軌在凍結後完全無法使用**：該 checkpoint 是以 NumPy 1.x pickle 的，
  反序列化時會引用 `numpy.core.multiarray`，而 NumPy 2 只把該路徑保留為相容 shim，
  PyInstaller 的靜態分析看不到它，`torch.load` 因此丟出 ModuleNotFoundError。
  6 軌 checkpoint 格式較新所以不受影響，只有 4 軌會壞。已加
  `--collect-submodules numpy.core`。此問題只有在實際跑 frozen runtime 時才會
  出現，走開發用 Python 環境的測試一律看不到

- **安裝版的所有 RoFormer 模式都會失效**：模型快取與工作目錄的預設值是開發樹的
  相對路徑（`cwd/../verify/...`），安裝或免安裝執行時該處不存在、在 Program Files
  下也不可寫，`beginSeparation()` 因此直接判定路徑不可用。改為解析到
  `%LOCALAPPDATA%\Music SSP FX\` 之下，並在首次使用時建立
- **安裝包遺漏 `roformer-manifest.json`**：沒有它，模式清單退化成只剩 HTDemucs
  4／6 軌，99 個 RoFormer 模型全部不出現
- **frozen runtime 內含建置機器的路徑**：pip 的 `direct_url.json` 會記錄本地或
  VCS 安裝來源。建置腳本現在會刪除它，並掃描整個 runtime，發現任何機器路徑就
  讓建置失敗

### 合規

- `THIRD_PARTY_NOTICES.md` 的模型權重再散布一項結案：Demucs 與 99 個 RoFormer
  checkpoint 一律不隨任何發行物散布，全部於首次使用時從各自上游下載並驗
  SHA-256，因此不涉及再散布授權

## [0.0.1] - 2026-08-30

第一個標記版本。在既有的 HTDemucs 原型上加入 MelBand RoFormer 模型家族、
模式優先的操作介面、多檔批次流程與中英雙語介面。

### 新增

- **MelBand RoFormer 整合**：99 個模型（人聲／伴奏／卡拉 OK／吉他／去混響／
  去噪／群聲／氣音等 10 類），其中 57 個經逐一端到端驗證，其餘標註 experimental
- **模式優先介面**：12 種分離模式，各自帶預設模型並可換同類替代；選定模式後
  拉桿才啟用，且拉桿名稱隨模式改變
- **多檔批次流程**：多選匯入、每檔一列（勾選框／檔名／狀態）、點列切換到該檔的
  調音介面、每檔獨立記住拉桿設定、逐一推論（先完成先可預覽）、只匯出勾選的檔案
  並一次輸出到指定資料夾
- **按需下載**：選用的 RoFormer 模型自動下載並驗證 SHA-256，滾動快取上限 3 個
- **中英雙語介面**：預設繁體中文，可即時切換英文並記住選擇
- **啟動預設**：預選「人聲分離」模式，上次的模式與模型於下次啟動還原
- **macOS 建置支援**：universal binary（Intel ＋ Apple Silicon）設定與
  `tools/build_macos.sh`，說明見 `docs/MACOS_BUILD.md`
- **端到端驗收工具** `tests/goal_check.cpp`：對一首真實歌曲跑完四種分離模式的
  「匯入→分離→匯出人聲→匯出伴奏」

### 修正

- 快速匯出會強制把模型切回 `htdemucs`，與模式優先介面衝突，導致在任何 RoFormer
  模式下按匯出都等不到結果
- `isRoformerModelName()` 只比對 `melband-roformer-` 前綴，使 99 個模型中的 74 個
  （`roformer-model-*`）被誤判為 HTDemucs 模型而拒絕分離
- `SpscRing` 在編譯期值初始化約 75 MB 儲存區，造成編譯器堆積耗盡（C1060），
  並在每次啟動時白白歸零這些頁面
- 快速匯出僅支援 HTDemucs 4/6 軌結果，RoFormer 的 2 軌結果無法匯出
- RoFormer 模型未安裝時被前置檢查擋下，未讓 worker 走按需下載
- CPU 模式警告訊息未納入中文化
- 各分離模式切換時，音軌拉桿名稱停留在前一個模式（例如仍顯示鼓／貝斯）

### 效能

- 5 分鐘歌曲的分離時間：HTDemucs 4 軌 43→16 秒、RoFormer 人聲 83→63 秒
