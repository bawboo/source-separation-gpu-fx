# LESSONS — append-only signs; every iteration must obey all of these

- SIGN (wiring): cmake 不在 PATH——建置一律走 `.loop\checks\cheap.cmd`／`full.cmd`（內含 VsDevCmd），或先 call VsDevCmd.bat。CMake target 名稱先查 CMakeLists.txt。
- SIGN (wiring): conda 完整路徑 `C:\Users\<user>\anaconda3\Scripts\conda.exe`；anaconda base python=3.13.5 torch 2.8.0+cu126（cuda 可用）。套件只裝進 `htfx-roformer` env。
- SIGN (wiring): ffmpeg=`C:\ffmpeg-master\bin\ffmpeg.exe`（media_io_smoke 硬編碼此路徑）；測試 fixtures 在 `C:\CodexProjects\SourceSeparation_GPU_FX\verify\fixtures\`（test_48k_2s.wav）。
- SIGN (wiring): RoFormer 模型快取固定 `C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\`，同時最多 3 個權重，驗證完即刪最舊的；絕不下載到 C: 其他位置（磁碟只剩約 55 GB）。
- SIGN (wiring): 所有 `.loop/` JSON 讀寫必須 `encoding="utf-8"`；state/record 寫入一律 tmp+rename 原子操作。
- SIGN (wiring): smoke tests 的正式 sidecar 靠 junction `build\windows-installed\Release\Resources\sidecar`→`verify\payload-cuda\Resources\sidecar`；若 junction 不存在會導致 record_mode_smoke 失敗——用 `New-Item -ItemType Junction` 重建，不要改 test 程式。
- SIGN (wiring): 上游 repo https://github.com/openmirlab/melband-roformer-infer（MIT）；registry 在其 `src/mel_band_roformer/data/melband_models.json`；預設下載快取為 `~/.cache/melband-roformer-infer/`——需將其導向本專案快取目錄（環境變數 `MELBAND_ROFORMER_MODELS_PATH` 或 `--models_dir`）。
- SIGN (user, 2026-08-23): 絕對不允許刪除 `C:\CodexProjects\SourceSeparation_GPU_FX\` 專案根目錄以外的任何檔案（包含 ~/.cache、temp 等）；專案內也僅限 `verify\roformer-cache\` 的權重檔可刪。
- SIGN (iter 5): A4 只有在真實 RoFormer worker 路由也完成並有證據後才能翻為 pass；catalog 載入與選擇本身只是可驗證基礎。
- SIGN (iter 8): 用 Bash 工具（Git Bash/MSYS）執行 `.loop\checks\*.cmd` 時，`cmd /c "..."` 的 `/c` 會被 MSYS 誤轉成路徑、整個指令被吞掉——只會跳出互動式 cmd banner、不執行任何內容，且 exit code 仍是 0（極易誤判為成功但其實什麼都沒跑）。一律改用 `cmd //c "..."`（雙斜線跳脫路徑轉換）才會真的執行 cheap.cmd／full.cmd。
- SIGN (iter 9): 承上，`cmd //c` 之後的路徑（例如 `.loop\checks\cheap.cmd`）若不加引號或只用雙反斜線跳脫，MSYS 仍會把每個 `\<letter>` 當跳脫序列吃掉反斜線，變成 `.loopcheckscheap.cmd`（cmd 找不到檔案，這次 exit code 會是非 0，不會偽裝成功）。必須用「單引號」包住整個路徑，例如 `cmd //c '.loop\checks\cheap.cmd'`，反斜線才會原樣傳給 cmd.exe。
- SIGN (iter 9): 本專案 headless 迴圈的 AGENT_CMD_JSON 只允許 `Bash` 工具（未列 PowerShell）；PowerShell 工具呼叫 `cmd /c ...`／`& *.cmd` 會被拒絕並回報 "contains multiple operations ... requires approval"。一律用 Bash 工具＋單引號路徑呼叫 `cmd //c`，不要嘗試用 PowerShell 工具跑 `.loop/checks/*.cmd`。
- SIGN (iter 10): 匯出管線（HTDemucs 與 RoFormer 共用 `stemExportLoop`）一律以插件內部固定處理率 `HTDemucsGpuFXAudioProcessor::kSampleRate`（44100 Hz）寫出匯出檔，與來源媒體的原始取樣率（例如 48/96 kHz fixture）無關——這是 `CLAUDE.md` 記載的既定不變量。任何新測試斷言匯出檔取樣率時必須比對 `kSampleRate`，不可假設等於來源取樣率；只有「時長」需要與來源一致。
- SIGN (iter 12): 任何新腳本若直接 import 並呼叫 `worker/roformer_worker.py` 的 `separate_file()`（繞過它的 CLI `main()`），都必須自己先呼叫 `configure_utf8_stream(sys.stdout)` 與 `configure_utf8_stream(sys.stderr)`（從 `roformer_worker` import 同一組函式），否則會在 Windows cp950 主控台上重現 iter 3 已修過的 `UnicodeEncodeError`（upstream 進度輸出含 emoji，例如 U+1F504 🔄）。這個初始化不是 CLI 專屬裝飾，是每個呼叫 `separate_file()` 的路徑都要做的前置條件。
- SIGN (iter 13): 快取一律用絕對路徑 `C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\`——絕不在移交樹內建立相對 `verify/`（會觸發 scope 違規鎖機）。移交樹內殘留的 `verify/` 目錄應在下次 iteration 開頭清掉（此為快取例外，可刪）。
- SIGN (iter 13): 一個 iteration 絕不可在背景工作未完成時結束 turn——同步等待或縮小批次（例如每輪 2–3 個 audited 模型）；提前結束＝沒有 record、沒有 commit、留下孤兒程序。
