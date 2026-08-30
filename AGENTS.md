# AGENTS.md — Music SSP FX

**這是本專案唯一的 agent 規則來源。** `CLAUDE.md` 只是指向本檔的轉址；任何要
寫進「CLAUDE.md」的規則，一律寫在這裡。

## 專案概要

Music SSP FX（Music Source Separation FX）是以 JUCE 製作的音源分離應用
（Windows standalone／VST3，macOS 支援建置）。分離推論在獨立的 Python/PyTorch
worker 中執行，不在 audio callback 內跑模型。

- **HTDemucs**：4 軌（鼓／貝斯／其他／人聲）與 6 軌（再加吉他／鋼琴），走
  shared-memory IPC 與 frozen worker 溝通
- **MelBand RoFormer**：99 個模型、10 個類別，直接啟動 Python worker（不走 IPC），
  按需下載並驗證 SHA-256，滾動快取上限 3 個

GitHub：`bawboo/source-separation-gpu-fx`（private）

## 重要目錄

| 路徑 | 內容 |
|---|---|
| `plugin/` | JUCE 前端與音訊處理（PluginProcessor、Localization、SpscRing） |
| `cpp/` | HTDemucs frozen worker 的 IPC client（Windows 與 POSIX 兩份實作） |
| `worker/` | Python worker（HTDemucs IPC、RoFormer 推論與快取） |
| `tests/` | smoke tests 與端到端驗收工具（`goal_check`） |
| `tools/` | 建置、封裝、驗證腳本 |
| `third_party/JUCE` | vendored JUCE 8.0.13，**已套用** `patches/juce-8.0.13-htfx.patch` |
| `assets/models/*.th` | **私人**預訓練權重（僅本機使用） |
| `dist/windows-web/*.zip` | **私人**CPU/CUDA runtime |
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

- **私人資產禁令**：`assets/models/*.th` 與 `dist/windows-web/*.zip` 禁止加入
  Git history、上傳 GitHub 或任何公開空間。Release 資產也不得包含它們。
- 未經使用者明確指示，不得 push、設定 git remote、建立公開 repo 或發佈 Release。
- 不覆蓋 `third_party/JUCE`、`third_party/demucs`、模型與 runtime；JUCE patch 狀態
  由 `tools/apply_dependency_patches.ps1` 冪等維護。
- 不刪除專案根目錄以外的任何檔案；專案內僅 `verify/roformer-cache/` 的權重檔可刪。
- 公開發佈前須遵守 `THIRD_PARTY_NOTICES.md`（權重再散布一項已由「一律不散布」
  結案；FFmpeg 的 GPL 對應原始碼仍是未決 gate）。

## 建置與驗證

- **Windows**：`tools\build_windows_installed.cmd`；若遇 `C1060 編譯器堆積空間不足`，
  加 `/p:PreferredToolArchitecture=x64`（本機 32 位元 cl 會在 `SpscRing` 具現化時爆掉）。
- **macOS**：`tools/build_macos.sh`（universal binary：`arm64;x86_64`）。
- 執行 CMake target 前，先從 `CMakeLists.txt` 或產生的 `.vcxproj` 確認完整名稱，不可猜測；
  若回報 target 不存在，修正後必須重跑原驗證指令。
- **Smoke tests**：`.loop\checks\full.cmd`（四個 smoke 全過）。
- **端到端驗收**：`htdemucs_goal_check.exe "<某首歌.wav>"` — 對真實歌曲跑完
  HTDemucs 4/6 軌與兩個 RoFormer 類別的「匯入→分離→匯出人聲→匯出伴奏」。
- **跑測試時不可同時開著 App**（會搶同一份模型快取，症狀是假的 Access violation）。
- 修改媒體匯入或預覽播放路徑後，必須以 48/96 kHz 來源及播放裝置驗證時長一致，
  並確認 44.1 kHz 模型與匯出格式不受影響。

## Git 操作

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
FFmpeg `C:\ffmpeg-master\bin\ffmpeg.exe`、RoFormer 環境 `htfx-roformer`（Python 3.11）。
