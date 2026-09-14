# AGENTS.md — Music SSP FX

**這是本專案唯一的 agent 規則來源。** `CLAUDE.md` 只是指向本檔的轉址；任何要
寫進「CLAUDE.md」的規則，一律寫在這裡。

## 專案概要

Music SSP FX（Music Source Separation FX）是以 JUCE 製作的音源分離應用
（Windows standalone／VST3，macOS 支援建置）。分離推論在獨立的 Python/PyTorch
worker 中執行，不在 audio callback 內跑模型。

- **HTDemucs**：4 軌（鼓／貝斯／其他／人聲）與 6 軌（再加吉他／鋼琴），走
  shared-memory IPC 與 frozen worker 溝通
- **MelBand RoFormer**：manifest 收錄 99 個模型，但 App **只提供**
  `assets/models/roformer-catalog.json` 列出的代表性子集（每類一個 audited 代表，
  效果不同才多留），CPU runtime 再依條目的 `cpu` 旗標過濾。直接啟動 Python
  worker（不走 IPC），按需下載並驗證 SHA-256（checkpoint 與 config 都驗），
  滾動快取上限 3 個。**新增模型先進 manifest，再決定是否進目錄。**

GitHub：`bawboo/source-separation-gpu-fx`（**public**）。Release 資產必須能被未登入的
使用者直接下載——安裝程式就是靠這點抓 runtime，不帶任何認證。

## 重要目錄

| 路徑 | 內容 |
|---|---|
| `plugin/` | JUCE 前端與音訊處理（PluginProcessor、Localization、SpscRing） |
| `cpp/` | HTDemucs frozen worker 的 IPC client（Windows 與 POSIX 兩份實作） |
| `worker/` | Python worker（HTDemucs IPC、RoFormer 推論與快取） |
| `tests/` | smoke tests 與端到端驗收工具（`goal_check`） |
| `tools/` | 建置、封裝、驗證腳本 |
| `third_party/JUCE` | vendored JUCE 8.0.13，**已套用** `patches/juce-8.0.13-htfx.patch` |
| `assets/models/*.th` | 預訓練權重，**不散布**（僅本機使用） |
| `dist/windows-web/`、`dist/portable/` | 發行資產（runtime 封存檔、安裝檔、免安裝包），不進 Git 但會上傳 Release |
| `.loop/` | 自主開發迴圈的計畫、逐輪紀錄與教訓（`LESSONS.md` 必讀） |

## 發行體積的硬性原則

使用者對本專案的核心要求是：**下載小、安裝快、開啟即用**。任何提高下載量或
增加安裝步驟的設計都必須先有明確理由。

- **一份 runtime、兩個後端。** HTDemucs 與 MelBand RoFormer 使用同一份 PyTorch，
  因此凍結成單一 PyInstaller bundle（`worker/worker_main.py` 為進入點，依第一個
  參數分派）。**絕對不要為 RoFormer 另外凍結一份 runtime** —— 那會讓 torch 與
  CUDA 函式庫重複約 2.5 GB。新增第三個後端時沿用同一個 bundle 與分派器。
- **權重不進發行包。** 所有模型首次使用時才下載並驗 SHA-256，發行物只含程式碼
  與 runtime。
- 發行包尺寸有變動時，於 `CHANGELOG.md` 記錄前後大小。

## 不可任意變更的部分

- **權重不散布**：`assets/models/*.th` 禁止加入 Git history、上傳 GitHub 或任何公開
  空間，Release 資產也不得包含（見 `THIRD_PARTY_NOTICES.md`，此項已由「一律不散布」
  結案）。模型一律由 App 首次使用時下載並驗 SHA-256。
- **建置產物不進 Git**：`dist/`、`build/` 由 `.gitignore` 擋住，不得繞過。其中
  `dist/windows-web/*.zip` 與 `dist/portable/*.zip` 是**發行資產**，發 Release 時就是要
  上傳它們（web 安裝程式從 release 下載 runtime，這是設計的一部分）；它們不進 Git 是
  因為體積，不是因為保密。
- 未經使用者明確指示，不得 push、設定 git remote、建立公開 repo 或發佈 Release。
- 不覆蓋 `third_party/JUCE`、`third_party/demucs`、模型與 runtime；JUCE patch 狀態
  由 `tools/apply_dependency_patches.ps1` 冪等維護。
- 不刪除專案根目錄以外的任何檔案；專案內僅 `verify/roformer-cache/` 的權重檔可刪。
- 公開發佈前須遵守 `THIRD_PARTY_NOTICES.md`（權重再散布一項已由「一律不散布」
  結案；FFmpeg 已於 2026-08-30 換成 LGPL build 而結案）。**不得換回 GPL build** —— 那會讓每次發行都必須附上完整對應原始碼。發行包一律使用 `build/ffmpeg-lgpl/`。

## 建置與驗證

- **Windows**：`tools\build_windows_installed.cmd`；若遇 `C1060 編譯器堆積空間不足`，
  加 `/p:PreferredToolArchitecture=x64`（本機 32 位元 cl 會在 `SpscRing` 具現化時爆掉）。
- **macOS**：`tools/macos_build_everything.sh --arm64-only`（工具檢查→Python 環境→LGPL FFmpeg→
  凍結 worker→封裝→建 `.app` 並 ad-hoc 簽章；中斷可重跑）。只建 `.app` 用
  `tools/build_macos.sh --bundle-runtime`。`.app` 產出在
  `build/macos/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app`——JUCE 每種格式
  各有子資料夾。`.app` 一律是 universal（`arm64;x86_64`），但裡面放的 runtime 只有本機架構的。
- **macOS 驗收**：`goal_check`／`full_feature_check`／`format_matrix_check`／`ui_snapshot`／
  `ui_configuration_smoke` 都能建。**把它們複製到 `.app/Contents/MacOS/` 再執行**，
  `bundledSidecarPath()` 才會解析到和 App 相同的 worker／ffmpeg／sidecar／模型路徑。
  完整流程是三步，**第三步不能省**：

  ```bash
  cp build/macos/<工具> "<App>/Contents/MacOS/"
  codesign --force --sign - --timestamp=none "<App>/Contents/MacOS/<工具>"   # 讓它跑得起來
  # ...跑驗收...
  rm -f "<App>/Contents/MacOS/<工具>" && codesign --force --sign - --timestamp=none "<App>"
  ```

  只簽那支工具會讓**整包**的簽章失效——`codesign --verify --strict` 報
  `nested code is modified or invalid`，而 `.app` 本身仍然跑得起來，所以不驗就看不出來。
  交付前務必 `codesign --verify --strict` 確認一次。
  注意 `build_macos.sh` 不會重建這些工具，改完 `plugin/` 要自己 `cmake --build ... --target <工具>`。
- **改了 `worker/` 就必須重新凍結**，光重建 `.app` 沒有用。
  `tools/build_macos.sh --bundle-runtime` 只是把**既有的**凍結產物複製進 bundle，
  Python 端的修改要先跑 `tools/build_standalone_runtime_macos.sh --python <env 的 python>`。
  這個失敗方式所有靜態檢查都會通過——`.app` 建置成功、`codesign --verify` 有效、
  `file -b` 架構正確——**只有實際跑一次分離才會錯**，而且錯在 worker 裡，
  看起來像是產品缺陷而不是用了舊的凍結包。判斷方法：比對
  `build/standalone-runtime-macos-<arch>-dist/htdemucs-worker/runtime-manifest.json`
  的時間與 `worker/` 底下檔案的時間。
- **macOS 交付**：壓縮用 `ditto -c -k --keepParent "<App>" <name>.zip`——`zip` 指令不保留
  簽章與延伸屬性，解開後的 `.app` 會是壞的。
- **一次只能有一個 `.app`**：arm64 與 Intel 版**同名同路徑**
  （`build/macos/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app`），
  `tools/build_macos.sh --bundle-runtime --runtime-arch x86_64` 會直接覆蓋掉前一個。
  做完一個要先搬走再做另一個。
  另有 `htdemucs_posix_ipc_check <worker> <models-dir> [model] [auto|mps|cpu]`，
  單獨驗 `cpp/GpuWorkerClientPosix.cpp` 的共享記憶體 IPC（Windows 的 `gpu_worker_smoke` 用
  `wmain`＋`<windows.h>`，在 macOS 建不起來）。
- 執行 CMake target 前，先從 `CMakeLists.txt` 或產生的 `.vcxproj` 確認完整名稱，不可猜測；
  若回報 target 不存在，修正後必須重跑原驗證指令。
- **Smoke tests**：`.loop\checks\full.cmd`（四個 smoke 全過）。跑之前設
  `HTFX_PYTHON=<htfx-roformer env 的 python.exe>`，並確認
  `build\windows-installed\Release\Resources\sidecar` 這個 junction 指向現行的
  sidecar（`verify\payload-current`），否則 RoFormer 相關的兩個 smoke 會失敗。
- **全功能驗收**：`htdemucs_full_feature_check.exe "<歌.wav>" --backend auto|cpu`
  —— 對一首真實歌曲跑完所有使用者功能（每種模式、三種匯出、取消、影片回填、
  批次），CPU 與 GPU 各跑一次。執行時間長，用脫離式啟動（Start-Process）並看 log，
  不要綁在工具的單次逾時上。
- **格式矩陣驗收**：`htdemucs_format_matrix_check.exe "<媒體資料夾>" --backend auto|cpu`
  —— 對資料夾內每個檔案跑匯入→分離→全部匯出（影片再回填 MP4），期望值由檔名決定
  （`bogus*`／`*_empty*`／`*_no_audio*` 必須被拒絕、`*_silent*` 可為靜音、
  `*_half_second*` 為 0.5 秒、`*_20min*` 為 1200 秒、其餘為 30 秒）。`--batch N` 決定批次
  段落匯入幾個檔案（預設 3）；`--roformer <模型 id>` 讓逐檔段落改走該 RoFormer 模型
  （預設 HTDemucs 4 軌）。測試媒體以 `tools/make_test_media.sh <資料夾> <完整版 ffmpeg.exe>`
  產生（30 秒合成訊號；子資料夾 `media2/` 是第二批邊界案例、`media3/` 是 20 分鐘壓力檔；
  產生器需要 libx264/libvpx/libmp3lame 等編碼器，用本機的 full build 而不是發行用的 LGPL
  build），改動匯入／匯出路徑後 CPU 與 GPU 各跑一次；要驗 CPU 版實際體感，另設
  `HTFX_WORKER_EXECUTABLE` 指向 `build/standalone-runtime-cpu-dist` 的 worker。
- **介面截圖**：`htdemucs_ui_snapshot.exe "<輸出資料夾>" [媒體檔...]` —— 不開 App 直接把
  編輯器繪成 PNG（一般／進階面板、中英文、匯入前後、分離中／完成、匯出後、拖曳中；給
  兩個以上媒體檔會再拍多檔批次狀態）。改版面、配色或字串後先看圖再跑
  `ui_configuration_smoke`；兩種語言都要看。
- **端到端驗收**：`htdemucs_goal_check.exe "<某首歌.wav>"` — 對真實歌曲跑完
  HTDemucs 4/6 軌與兩個 RoFormer 類別的「匯入→分離→匯出人聲→匯出伴奏」。
- **跑測試時不可同時開著 App**（會搶同一份模型快取，症狀是假的 Access violation）。
- **發行前必須用 frozen runtime 再跑一次端到端驗收**（設 `HTFX_WORKER_EXECUTABLE`
  指向 `build/standalone-runtime-dist/htdemucs-worker/htdemucs-worker.exe`）。凍結
  專屬的缺陷開發環境測不出來 —— 例如 HTDemucs 4 軌 checkpoint 需要 NumPy 2 的
  `numpy.core` 相容 shim，PyInstaller 靜態分析看不到，漏打包會讓 4 軌 100% 失敗。
- 修改媒體匯入或預覽播放路徑後，必須以 48/96 kHz 來源及播放裝置驗證時長一致，
  並確認 44.1 kHz 模型與匯出格式不受影響。

## Git 操作

- **commit 身分一律用 GitHub 的 noreply 信箱**，不要用真實信箱——這個 repo 是公開的，
  作者欄位任何人都看得到。在任何新的工作副本（包含 Mac）第一件事先設定：

  ```bash
  git config user.name "bawboo"
  git config user.email "42883691+bawboo@users.noreply.github.com"
  ```

  這是 repo 層級的設定，不會跟著全域設定走，所以每 clone 一份就要設一次。
  v0.0.8 以前的歷史仍帶著真實信箱，刻意不重寫——那會讓七個已發布的 tag 全部失效。

- 若 Git 回報 dubious ownership，或 `origin does not appear to be a git repository`
  但 remote 設定實際存在，先取得此 repository 的解析後絕對路徑，再只將該路徑加入
  全域 `safe.directory`；不得使用 `safe.directory '*'` 等萬用設定。
- 修正後重新執行原本失敗的 Git 指令，確認問題排除後再繼續 Release 流程。

## 工作流程

1. 接手先讀本檔與 `.loop/LESSONS.md`（累積的實戰教訓，每條都是踩過的坑）。
2. 動手前確認不觸犯「不可任意變更的部分」。
3. 建置／測試依上節執行；失敗先找根因，不繞過、不放寬標準。
4. 回報使用**繁體中文**，內容含：實際執行的命令、通過/失敗項目、失敗根因。

## 已知環境（開發機）

Windows 11（26200）、VS Build Tools 2022（**必須用 x64 工具鏈**）、
anaconda Python 3.13.5＋torch 2.8.0+cu126、RTX 4050 Laptop 6 GB、
發行用 FFmpeg 為 `build/ffmpeg-lgpl/`（LGPL build，打包腳本預設值；本機另有 `C:\ffmpeg-master` 的 GPL full build，**不可用於發行**）、RoFormer 環境 `htfx-roformer`（Python 3.11）。

macOS 開發機：MacBook Air M1（arm64）、macOS 26.2、Xcode 17 命令列工具、Homebrew 的
cmake 與 sevenzip（`7zz`）。conda 是 **miniforge3 4.11.0**，且 shell function 壞掉
（`CONDA_EXE` 未設）——一律用絕對路徑 `~/miniforge3/bin/conda`，或把 `~/miniforge3/bin`
放進 `PATH` 再跑腳本（`macos_build_everything.sh` 用 `command -v conda` 檢查）。
Python 環境 `htfx-macos-arm64`（3.11＋torch 2.14.0，MPS 可用）與 `htfx-macos-x86`。
**磁碟空間很吃緊**：arm64 單條路徑峰值約 6–7 GB，兩種架構一起做約需 13 GB。
