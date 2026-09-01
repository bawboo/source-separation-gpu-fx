# Changelog

本專案的版本紀錄。格式依循 [Keep a Changelog](https://keepachangelog.com/zh-TW/1.1.0/)，
版本號依循 [語意化版本](https://semver.org/lang/zh-TW/)。

## [0.0.4] - 2026-09-01

### 修正

- **RoFormer 分離期間完全沒有任何進度回報**：Python worker 從頭到尾只在最後
  印一次結果，C++ 端雖然有讀取 worker 的輸出，卻只把它當診斷文字累積起來。
  `setSeparationMessage()` 在啟動時設定一次之後就再也不更新，進度條固定是
  「不確定」動畫——於是整段流程（啟動、下載模型、載入、推論）都只顯示
  「正在載入 <模型>」，和當機無法區分。實測一首 5 分鐘的歌：啟動與 CUDA
  初始化 25 秒、載入模型 3 秒、推論 57 秒，**前 28 秒完全沒有任何回饋**，
  而模型未快取時還要再加上數百 MB 的下載
- worker 現在回報 prepare／download／load／infer／verify 五個階段，下載階段
  帶實際百分比；C++ 端解析這些標記，同時解析上游本來就每秒輸出、但過去被丟棄的
  「Estimated time remaining」，據此顯示剩餘時間並驅動進度條
- **worker 的輸出被緩衝，讓進度「憋著跳」**：把上述訊息接起來之後，剩餘時間
  整段只更新 1 次——訊息確實有送出，但全部卡在管線緩衝裡直到程序結束才抵達，
  使用者看到的仍然是長時間不動、然後突然跳到 81%。改成 line buffering 也無效，
  因為上游是 `sys.stdout.write(f"...
")`，結尾是歸位字元而非換行，永遠等不到
  觸發 flush 的 `
`。改為完全不緩衝後，同一首歌的估計值從 1 次變成 62 次、
  平均間隔 0.88 秒，進度條才真正逐秒推進
- **主程式讀取 worker 輸出的粒度，讓進度再次卡住**：worker 端修好之後，
  凍結版的進度**仍然**只更新 2 次（同一份 worker 直接用 Python 讀是 62 次）。
  `juce::ChildProcess::readProcessOutput` 會忙碌等待**直到填滿呼叫端給的整個
  緩衝區**才返回；原本給的是 4 KB，而每則進度約 40 bytes，等於要累積約 100 則
  ——比整段分離產生的還多——所以全部積到程序結束才一次抵達。改用 64 bytes 的
  緩衝後，同一首歌的進度更新從 2 次變成 **93 次**，剩餘時間逐秒遞減。
  順帶把斷行改為以原始位元組切割：一次讀取可能停在多位元組字元中間，先解碼
  再切行會弄壞該字元
- **每次 RoFormer 分離洩漏約 300 MB**：每次執行都建立
  `cpp-route-<uuid>/`，裡面放一份完整的輸入 WAV 與所有分離結果，而且從來不刪。
  開發樹已累積 8.7 GB。stems 讀進記憶體後這些檔案就沒有用途，現在改由 scope
  guard 確保刪除（成功、失敗、取消都會清），並在啟動時掃除舊版留下的殘留
- **輸入所在資料夾裡的其他 WAV 會被一起分離**：worker 把
  `input_path.parent` 整個交給上游的 `infer()`，它會處理該資料夾下所有音檔。
  旁邊多一個檔案就讓分離時間加倍，而且處理第二個檔案時會踩到上游
  `_print_estimate` 的 `AttributeError: 'chunk_size'` 而讓整個 worker 崩潰。
  現在把輸入隔離到只含它自己的暫存資料夾（優先用硬連結，跨磁碟才複製）
- 「正在啟動 Demucs worker」這則訊息對兩種後端都會顯示，選 RoFormer 模式時
  看到 Demucs 字樣會誤導，改為中性的「正在準備分離…」

### 測試

- `goal_check` 在等待期間會印出狀態文字的每次變化與百分比——先前它只印最終
  結果，因此「整段沉默」這種缺陷它根本測不出來

## [0.0.3] - 2026-08-30

### 變更

- **缺少的 HTDemucs 模型改為按需下載**：在全新安裝上選「6 軌分離」會被擋下來，
  訊息要你自己去「進階選項」下載模型；但選任何 RoFormer 模式卻是自動下載直接跑。
  兩個模型家族的行為不一致，而 6 軌分離是 README 首頁就宣傳的功能，新使用者
  第一次點就撞牆。下載器本來就存在也能用，只是沒有接到分離流程上——現在缺模型
  會自動開始下載（一樣驗證固定的 SHA-256），下載完成後自動接著分離。若使用者
  在下載期間換了模型，排隊中的分離會被放棄而不是突然開始；下載失敗則讓分離進入
  錯誤狀態，不會無限等待

### 修正

- **`goal_check` 從未 pump 過 message queue**：它有 `ScopedJuceInitialiser_GUI`
  但不跑 dispatch loop，所以任何依賴 `MessageManager::callAsync` 的行為在它裡面
  完全測不到——本次改動的第一版正好踩中，看起來會動但實際上永遠不會續跑。
  續跑改為直接在下載執行緒執行（`beginSeparation()` 不碰任何 UI），如此在沒有
  dispatch loop 的 host 或工具中行為一致；`goal_check` 也改為會等待進行中的下載，
  而不是把 `beginSeparation()` 回傳 false 直接判定失敗

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

公開發行前把剩下的授權 gate 全部走完：

- **FFmpeg 從 GPLv3 換成 LGPLv3 build**：原本用的是含 x264/x265 的 full build，
  那會讓每次發行 binary 都必須附上完整對應原始碼。但本專案只拿 FFmpeg 把輸入
  解碼成 PCM（`-vn -ac 2 -ar 44100 -c:a pcm_f32le`），匯出是 JUCE 自己寫 WAV，
  沒有用到任何 GPL-only 元件。改用 BtbN 的
  `ffmpeg-master-latest-win64-lgpl-shared`（有 `--enable-version3`，沒有
  `--enable-gpl`／`--enable-nonfree`），義務降為 LGPL，發行包也再小 19 MiB
- **NVIDIA redistributables 明確化**：GPU 版內嵌 2.46 GiB 的 cuBLAS／cuDNN／
  cuFFT 等 22 個 NVIDIA 函式庫（取自官方 PyTorch wheel，未經修改）。這些適用
  NVIDIA 自己的 CUDA EULA 與 cuDNN SLA，而非本專案的 AGPL 或 PyTorch 的 BSD ——
  torch 的 LICENSE 只涵蓋 NVIDIA 貢獻給 PyTorch 的原始碼。新增
  `NVIDIA-CUDA-NOTICE.txt` 列出確切檔案並指向兩份條款
- **LGPL 元件的義務**：FFmpeg、libsndfile（經 soundfile）、libsoxr（經 soxr）
  三者皆為 LGPL 且都是動態載入，使用者可自行替換，符合 LGPL 要求；notices
  記錄其上游來源
- **notices 重新稽核**：原稽核日期早於 RoFormer 整合，PyTorch／NumPy／PyYAML／
  tqdm／einops／julius／PyInstaller／CPython 版本全部過時，且缺少 librosa 拉進
  的十餘個相依。改由 `tools/collect_runtime_licenses.py` 從凍結該 runtime 的
  直譯器動態收集，版本不可能再與實際出貨的 wheel 脫節
- **安裝程式補上** `AppPublisherURL`／`AppSupportURL`／`AppUpdatesURL`，同時作為
  binary 的 AGPL 對應原始碼位置
- **移除建置機器的個人資訊**：使用者名稱與電腦名出現在 4 個受追蹤檔案與 12 個
  commit 的歷史中，已於 HEAD 與整個 git 歷史清除；兩個測試檔改讀 `HTFX_PYTHON`
  而非寫死只存在於單一機器的直譯器路徑

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
