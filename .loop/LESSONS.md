# LESSONS — append-only signs; every iteration must obey all of these

- SIGN (wiring): cmake 不在 PATH——建置一律走 `.loop\checks\cheap.cmd`／`full.cmd`（內含 VsDevCmd），或先 call VsDevCmd.bat。CMake target 名稱先查 CMakeLists.txt。
- SIGN (wiring): conda 完整路徑 `C:\Users\<user>\anaconda3\Scripts\conda.exe`；anaconda base python=3.13.5 torch 2.8.0+cu126（cuda 可用）。套件只裝進 `htfx-roformer` env。
- SIGN (wiring): ffmpeg=`C:\ffmpeg-master\bin\ffmpeg.exe`（media_io_smoke 硬編碼此路徑）；測試 fixtures 在 `C:\CodexProjects\SourceSeparation_GPU_FX\verify\fixtures\`（test_48k_2s.wav）。
- SIGN (wiring): RoFormer 模型快取固定 `C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\`，同時最多 3 個權重，驗證完即刪最舊的；絕不下載到 C: 其他位置（磁碟只剩約 55 GB）。
- SIGN (wiring): 所有 `.loop/` JSON 讀寫必須 `encoding="utf-8"`；state/record 寫入一律 tmp+rename 原子操作。
- SIGN (wiring): smoke tests 的正式 sidecar 靠 junction `build\windows-installed\Release\Resources\sidecar`→`verify\payload-cuda\Resources\sidecar`；若 junction 不存在會導致 record_mode_smoke 失敗——用 `New-Item -ItemType Junction` 重建，不要改 test 程式。
- SIGN (wiring): 上游 repo https://github.com/openmirlab/melband-roformer-infer（MIT）；registry 在其 `src/mel_band_roformer/data/melband_models.json`；預設下載快取為 `~/.cache/melband-roformer-infer/`——需將其導向本專案快取目錄（環境變數 `MELBAND_ROFORMER_MODELS_PATH` 或 `--models_dir`）。
