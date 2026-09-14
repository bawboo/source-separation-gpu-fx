# macOS 移植交接說明

這份文件是給**在 Mac 上接手這項工作的 Claude Code session** 看的。Windows 那邊已經
把所有能在 Windows 上完成的部分做完了；剩下的每一件事都必須在 Mac 上執行。

先讀 `AGENTS.md` 與 `.loop/LESSONS.md`，那是本專案的規則與踩過的坑。本文只補充
macOS 特有的部分。

---

## 你的任務

把這個 App 在 macOS 上建置成可執行的 `.app`，Apple Silicon 與 Intel 兩種架構都要，
並實際驗證分離功能可用。

**完成的定義**（四項全要成立，缺一不可）：

1. `lipo -info` 顯示 `.app` 的主執行檔同時含 `x86_64` 與 `arm64`
2. App 能啟動，匯入一個音檔並用 **HTDemucs 4 軌**完成分離與匯出
3. 同一台機器上用 **RoFormer**（任一分類）完成分離與匯出
4. Apple Silicon 上確認實際跑在 **MPS** 而非 CPU（狀態列會顯示裝置）

Intel 版可以在同一台 Apple Silicon Mac 上用 Rosetta 2 建置與測試，不需要 Intel 硬體。

---

## 目前狀態

Windows 上已完成、且**不需要你重做**的部分：

| 項目 | 狀態 |
|---|---|
| `plugin/` 前端與音訊處理 | 無平台相依程式碼 |
| CMake | 已預設通用二進位（`arm64;x86_64`）、部署目標 12.0 |
| sidecar 路徑解析 | 已含 `<執行檔>/../Resources/sidecar`，正好是 `.app/Contents/Resources/sidecar`，**不需要改 C++** |
| 資料目錄 | 已自動落在 `~/Library/Application Support/Music SSP FX` |
| 建置／凍結／封裝腳本 | `tools/` 下五支 `.sh`，見下節 |

**從未在 macOS 上驗證過的部分**（你會在這裡遇到問題，這是預期的）：

- `cpp/GpuWorkerClientPosix.cpp` —— POSIX 共享記憶體 IPC，**從來沒有被編譯或執行過**。
  HTDemucs 走這條路；RoFormer 不走（它直接啟動 worker 程序）。
- MPS 的運算正確性 —— 部分 torch 運算在 MPS 上會 fallback，實務上常需要
  `PYTORCH_ENABLE_MPS_FALLBACK=1`。這是要驗的，不是已知可用的。
- smoke 測試 —— 用了 `_wputenv_s` 等 Windows API，`.loop/checks/full.cmd` 是批次檔。
  兩者都還沒有 POSIX 版，所以 Mac 上**目前沒有自動化測試可跑**。

---

## 怎麼做

```bash
tools/macos_build_everything.sh              # 兩種架構
tools/macos_build_everything.sh --arm64-only # 只做 Apple Silicon（先跑通建議用這個）
```

這支腳本依序做六件事，中斷後重跑會跳過已完成的部分：

1. 檢查工具（Xcode CLT、cmake、7-Zip、conda、Rosetta）
2. 建立 Python 環境（`htfx-macos-arm64` 與 `htfx-macos-x86`）
3. `tools/build_ffmpeg_lgpl_macos.sh` —— 從原始碼建 LGPL 靜態 FFmpeg
4. `tools/build_standalone_runtime_macos.sh` —— PyInstaller 凍結 worker
5. `tools/package_macos_runtime.sh` —— 壓成 `.7z` ＋ manifest
6. `tools/build_macos.sh --bundle-runtime` —— 建 `.app`、放進 runtime、ad-hoc 簽章

失敗時**先找根因再修**，不要繞過檢查、不要放寬門檻——那些檢查每一個都是為了擋住一個
具體的錯誤而存在的（見下節）。

---

## 關鍵設計決定與理由

不理解這些的話很容易「修」掉重要的東西。

### Apple Silicon 與 Intel 是兩份 runtime，不是一份通用的

`.app` 本身是通用二進位，但 runtime 不可能通用：PyInstaller 打包的是特定架構的 wheel。
更關鍵的是——

> **PyTorch 從 2.3.0 起不再發布 macOS x86_64 的 wheel，最後一版是 2.2.2。**

demucs 4.1.0 自己的 metadata 就宣告了這個限制：

```
torch<2.3,>=2.1 ; sys_platform == "darwin" and platform_machine == "x86_64"
numpy<2         ; sys_platform == "darwin" and platform_machine == "x86_64"
```

所以 Intel 版鎖在 torch 2.1–2.2 ＋ numpy<2，而且**只有 CPU、沒有任何加速**。
凍結腳本會強制檢查這件事（arm64 必須 MPS 可用、x86_64 必須 torch<2.3），
因為這兩點錯了都會安靜地產出一個不能用的 runtime。**不要把這個檢查拿掉。**

### FFmpeg 必須是 LGPL 且靜態

- `brew install ffmpeg` 是 **GPL** build，用了會讓發行包背上 GPL 的對應原始碼義務。
  `THIRD_PARTY_NOTICES.md` 已經把這件事結案為「一律使用 LGPL」。**不可以為了省事改用
  Homebrew 的 ffmpeg。**
- Homebrew 的二進位還連結 `/opt/homebrew/lib` 的 dylib，換一台 Mac 就跑不起來。

`tools/build_ffmpeg_lgpl_macos.sh` 從原始碼建（FFmpeg 不加 `--enable-gpl` 就是 LGPL），
用 `--disable-autodetect` 確保不會連進建置機器上的任何東西，然後**證明**兩件事：
banner 裡沒有 `--enable-gpl`、`otool -L` 只有系統函式庫。

### 簽章

- **Ad-hoc 簽章是強制的**：Apple Silicon 不執行沒有簽章的 Mach-O。免費，PyInstaller
  與建置腳本都會自動做。
- Developer ID 簽章（US$99/年）與公證是**選配**，目前刻意不做。使用者要自己在
  系統設定 → 隱私權與安全性 → Open Anyway 放行一次。

### 一份 runtime、兩個後端

HTDemucs 與 RoFormer 共用同一個 PyInstaller bundle（`worker/worker_main.py` 依第一個
參數分派）。**絕對不要為 RoFormer 另外凍結一份 runtime**——那會讓 torch 重複約 2.5 GB。
這條規則在 `AGENTS.md` 裡。

---

## 預期會遇到的問題與排查方向

### 1. `cpp/GpuWorkerClientPosix.cpp` 編不過或跑不起來

這是最可能出事的地方。它實作 HTDemucs 的共享記憶體 IPC，對照組是
`cpp/GpuWorkerClientWindows.cpp`（已驗證可用）與 `worker/gpu_ipc_worker.py`（協定的另一端）。

macOS 的 POSIX 共享記憶體有 Linux 沒有的限制，最常見的兩個：

- `shm_open` 的名稱長度上限只有 31 個字元（含前導 `/`）
- `ftruncate` 在一個已經 map 過的 shm 物件上會失敗

如果 HTDemucs 分離卡住或立刻失敗，先看 worker 的 stderr，再確認共享記憶體的建立與
大小設定這兩步。

### 2. MPS 相關

分離結果全是靜音、NaN，或出現 `not implemented for MPS` —— 先用
`PYTORCH_ENABLE_MPS_FALLBACK=1` 確認是不是 fallback 問題。若是，要在 worker 裡處理，
而不是叫使用者自己設環境變數。

### 3. Gatekeeper

`.app` 移動到另一台 Mac 或從瀏覽器下載後打不開，屬於預期行為（未公證）。
用 `xattr -l <path>` 看 quarantine 旗標。**值得順手驗證的一件事**：App 自己下載的檔案
（模型）應該**不帶** quarantine 旗標——如果成立，未來改成「首次啟動下載 runtime」時，
使用者一輩子只需要放行 `.app` 一次。

---

## 不要做的事

- 不要改用 GPL 的 FFmpeg
- 不要為 RoFormer 另外凍結第二份 runtime
- 不要把凍結腳本裡的架構／版本檢查拿掉
- 不要把模型權重（`*.th`、`*.ckpt`）加進 Git
- **未經使用者明確指示，不得 push、發 Release**
- 不要為了讓測試過而放寬門檻

## 回報方式

用**繁體中文**，並包含：實際執行的指令、通過與失敗的項目、失敗的根本原因。
不確定的事情要說不確定，不要猜。
