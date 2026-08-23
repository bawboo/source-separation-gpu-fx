# Loop journal — melband-roformer-integration

Plan approved: 2026-08-23T10:30+08:00（使用者回覆「直接LOOP」；10 個推斷項全數確認）
Origin: main @ 24bfb70f2c4de22ba92996cdcee08ac8ecbff9e6 · Loop branch: loop/melband-roformer
Engine: B（外部驅動器 .loop/run_loop.sh；claude -p headless；usage-limit 睡 20 分自動重試）
Criteria: C1 full.cmd（建置＋3 既有 smoke＋roformer smoke）· C2 backlog 全綠 · C3 scope 無違規
Budget: max_iterations=60 · stall_threshold=6 · iteration_timeout=40m · wall=none

---

## Iteration 1 — 2026-08-23
**Inherited baseline:** 工作樹僅有 driver 產生的 `.loop/driver.log`、`.loop/lastrun.log` 與 `.loop/nohup.out` 變動；變更前先執行 cheap tier，exit 0。建置與 `htdemucs_ui_configuration_smoke` PASS；尾端另印出非致命訊息：`'cheap' is not recognized as an internal or external command, operable program or batch file.`
**Hypothesis:** 若建立獨立 `htfx-roformer` conda env 並安裝 upstream package，A1 應會改善，因為該環境將提供 `melband-roformer-infer` CLI。
**Files touched:** `C:/Users/<user>/anaconda3/envs/htfx-roformer/`（允許的獨立 conda env）；`.loop/backlog.json`、`.loop/iterations/0001.json`、`.loop/journal.md`、`.loop/state.json`（loop 控制面）。
**Verification:** `cmd /c .loop\checks\cheap.cmd`（exit 0）
```text
.NET Framework 的 MSBuild 版本 17.14.23+b0019275e
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true PASS
'cheap' is not recognized as an internal or external command,
operable program or batch file.
```
**A1 check:** `C:\Users\<user>\anaconda3\Scripts\conda.exe run -n htfx-roformer melband-roformer-infer --help`（exit 0）
```text
usage: melband-roformer-infer [-h] [--model_type MODEL_TYPE]
                              [--config_path CONFIG_PATH]
                              [--model_path MODEL_PATH] [--model MODEL]
                              [--models_dir MODELS_DIR] --input_folder INPUT_FOLDER
Mel-Band Roformer inference runner
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 17 changed path(s) within policy
```
**Criteria:** C1: fail（本輪非 full cadence，且整體整合未完成） · C2: fail（其餘 backlog 未完成） · C3: pass
**Metric:** 1 個 backlog item passing（best so far: 1；improved: true）
**Decision:** continue
**Note:** A1 已由隔離環境內的 CLI 實際退出碼 0 證明；下一輪依 priority 轉向 A2。

---

## Iteration 2 — 2026-08-23
**Inherited baseline:** 工作樹只有外部 driver 持續更新的 `.loop/driver.log` 與 `.loop/lastrun.log`；上一輪 cheap tier exit 0，無 crash-mid-write 跡象。
**Hypothesis:** 若以精確的 upstream `f3781bc766b3` revision 產生並驗證 99-model manifest，A2 應會改善，因為該 revision 對應核准的 57 audited / 42 experimental catalog。
**Files touched:** `assets/models/roformer-manifest.json`、`tools/generate_roformer_manifest.py`、`tools/validate_roformer_manifest.py`、`tests/test_roformer_manifest.py`、`.loop/checks/cheap_extra.cmd`，以及 loop 記錄檔。
**TDD RED:** `python tests\test_roformer_manifest.py -v`（exit 1）因 `tools/validate_roformer_manifest.py` 尚不存在而失敗；確認測試確實鎖住缺少的功能。
**TDD GREEN / A2 focused check:** generator 先揭露 checksum-only 分類只有 54 audited；追查 upstream audit 後確認三份 legacy config 位於 Politrees pinned mirror，加入精確來源後：
```text
generated assets\models\roformer-manifest.json: 99 models (57 audited, 42 experimental)
roformer manifest: 99 models, 57 audited, 42 experimental PASS
test_pinned_catalog_has_expected_audit_split_and_download_metadata ... ok
Ran 1 test in 0.052s
OK
```
**Verification:** `cmd /c .loop\checks\cheap.cmd`（exit 0）
```text
.NET Framework 的 MSBuild 版本 17.14.23+b0019275e
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
'cheap' is not recognized as an internal or external command,
operable program or batch file.
'vswhere.exe' is not recognized as an internal or external command,
operable program or batch file.
.
----------------------------------------------------------------------
Ran 1 test in 0.003s

OK
```
上述兩個 `not recognized` 訊息沿用上一輪的非致命環境輸出；命令實際 exit 0。
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 23 changed path(s) within policy
```
**Criteria:** C1: fail（本輪非 full cadence，整體整合尚未完成） · C2: fail（其餘 backlog 未完成） · C3: pass
**Metric:** 2 個 backlog item passing（best so far: 2；improved: true）
**Decision:** continue
**Note:** A2 已由 cheap tier 內的本地 validator 證明；manifest 固定 upstream revision 並保存 99/57/42 統計，下一輪依 priority 轉向 A3。

---

## Iteration 3 — 2026-08-23T12:32:09.5570084+08:00
**Inherited baseline:** 工作樹只有外部 driver 持續更新的 `.loop/driver.log` 與 `.loop/lastrun.log`；上一輪 record 完整且 cheap tier exit 0，無 crash-mid-write 跡象。Windows 使用者 `<host>\<user>` 與 repository owner 相符；branch=`loop/melband-roformer`，remote 與 tag 均為空，符合本 loop 不設 remote／無目標 tag。
**Hypothesis:** 若新增以 upstream session API 為核心、具嚴格 WAV 輸出驗證的 headless RoFormer worker，A3 應會改善，因為它能對 fixture 執行真實模型分離並以獨立檢查確認輸出契約。
**Change-set:** 新增 `worker/roformer_worker.py`（單檔 CLI、upstream session delegation、輸出契約驗證、Windows UTF-8 console）；新增 `tests/test_roformer_worker.py`；將該測試加入 `.loop/checks/cheap_extra.cmd`。模型與輸出僅寫入核准的 `verify/roformer-cache/` 與 `verify/output/roformer-iter3/`。
**TDD RED:** `python tests\test_roformer_worker.py -v`（exit 1）
```text
FileNotFoundError: ...\\worker\\roformer_worker.py
FAILED (errors=1)
```
真實執行首次穩定重現 upstream download progress emoji 在 CP950 console 的 `UnicodeEncodeError`；新增 encoding regression test 後先以缺少 `configure_utf8_stream` 得到 exit 1，再實作最小修正，2 tests PASS。
**Focused real-model verification:** `C:\Users\<user>\anaconda3\envs\htfx-roformer\python.exe worker\roformer_worker.py ... --device auto`（exit 0）
```text
Successfully downloaded: ...\roformer-cache\melband-roformer-kim-vocals\MelBandRoformer.ckpt
File size: 913,106,900 bytes
SHA256 verified: 87201f4d31afb5bc79993230fc49446918425574db48c01c405e44f365c7559e
CUDA is not available. Falling back to CPU. This will be slow.
Total tracks found: 1
Processing track 1/1: test_48k_2s.wav
Elapsed time: 15.48 sec
"sample_rate": 48000,
"frames": 96000,
"channels": 2,
"subtype": "FLOAT",
"finite": true
```
輸出 `vocals` 與 `instrumental` 兩 stem 均符合上述契約。
**Verification:** `cmd /c .loop\checks\cheap.cmd`（exit 0）
```text
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general ... models=4 compute=Auto/CUDA/CPU/MPS ... PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.004s
OK
Ran 2 tests in 0.084s
OK
CHEAP_EXIT=0
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 26 changed path(s) within policy
SCOPE_EXIT=0
```
**Criteria:** C1: fail（iteration 3 非 full cadence） · C2: fail（其餘 backlog 未完成） · C3: pass
**Metric:** 3 個 backlog item passing（best so far: 3；improved: true）
**Decision:** continue
**Note:** A3 已由真實、SHA-256 驗證模型的 headless 分離證明；`htfx-roformer` 目前為 CPU-only torch，已另 append D1，下一輪應先補 CUDA-capable runtime 並以 `--device cuda:0` 重驗。

---

## Iteration 4 — 2026-08-23T12:45:53.9633102+08:00
**Inherited baseline:** 工作樹只有外部 driver 持續更新的 `.loop/driver.log` 與 `.loop/lastrun.log`；上一輪 record 完整且 cheap tier exit 0，無 crash-mid-write 跡象。Windows 使用者 `<host>\<user>` 與 repository owner 相符；branch=`loop/melband-roformer`，remote 與 tag 均為空，符合本 loop 不設 remote／無目標 tag。
**Hypothesis:** 若只把 `htfx-roformer` 環境中的 CPU-only PyTorch 換成已由本機 base env 證實可用的 CUDA 12.6 wheel，D1 應會改善，因為 worker 將能在 RTX 4050 上以 `cuda:0` 完成真實模型分離。
**Root cause evidence:** 修正前 `htfx-roformer` 為 `torch 2.13.0+cpu`、`torch.version.cuda=None`、`cuda_available=False`；同機 base env 為 `torch 2.8.0+cu126` 且 RTX 4050 可用，因此故障位於隔離環境安裝了 CPU wheel，不是 driver 或硬體。
**Change-set:** 僅在核准的 `htfx-roformer` conda env 內以官方 cu126 index 將 torch 換成 `2.8.0+cu126`；測試輸出新增於 `verify/output/roformer-iter4/`。未修改產品原始碼。
**Install:** `C:\Users\<user>\anaconda3\envs\htfx-roformer\python.exe -m pip install --force-reinstall torch==2.8.0 --index-url https://download.pytorch.org/whl/cu126`（exit 0）
```text
Downloading torch-2.8.0+cu126-cp311-cp311-win_amd64.whl (2915.5 MB)
Successfully uninstalled torch-2.13.0
Successfully installed ... torch-2.8.0+cu126 ...
```
**Focused CUDA verification:** torch visibility check＋`worker/roformer_worker.py ... --device cuda:0`（exit 0）
```text
torch=2.8.0+cu126
torch_cuda=12.6
cuda_available=True
device_count=1
device=NVIDIA GeForce RTX 4050 Laptop GPU
Processing track 1/1: test_48k_2s.wav
Elapsed time: 2.37 sec
"sample_rate": 48000,
"frames": 96000,
"channels": 2,
"subtype": "FLOAT",
"finite": true
```
`vocals` 與 `instrumental` 兩個輸出均符合上述規格。
**Verification:** `cmd /c .loop\checks\cheap.cmd`（exit 0）
```text
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general ... models=4 compute=Auto/CUDA/CPU/MPS ... PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s
OK
Ran 2 tests in 0.085s
OK
```
`cheap`／`vswhere.exe` 的 `not recognized` 訊息仍為既有非致命輸出；命令實際 exit 0。
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 27 changed path(s) within policy
```
**Criteria:** C1: fail（iteration 4 非 full cadence） · C2: fail（其餘 backlog 未完成） · C3: pass
**Metric:** 4 個 backlog item passing（best so far: 4；improved: true）
**Decision:** continue
**Note:** D1 已由 CUDA 可見性與真實模型 `cuda:0` 分離共同證明；下一輪依 priority 轉向 A4。

---

## Iteration 5 — 2026-08-23T13:06:41.1788895+08:00
**Inherited baseline:** 工作樹只有外部 driver 持續更新的 `.loop/driver.log` 與 `.loop/lastrun.log`；上一輪 record 完整且 cheap tier exit 0，無 crash-mid-write 跡象。Windows repository 解析路徑正確；branch=`loop/melband-roformer`，remote 與 tag 均為空，符合本 loop 不設 remote／無目標 tag。
**Hypothesis:** 若 processor 能從固定 manifest 載入 99 模型、拒絕未知 ID、保存有效 RoFormer 選擇並讓 runtime configuration 採用該選擇，A4 的「模型清單／選擇」子路徑會先變成可驗證基礎，因為後續 worker 路由可依同一個穩定 ID 決策。
**Change-set:** `plugin/PluginProcessor.h/.cpp` 新增 RoFormer model DTO、manifest 定位/解析、thread-safe catalog getter 與 validated selection，選擇後 runtime configuration 使用穩定 model ID 與 2-stem layout；`tests/ui_configuration_smoke.cpp` 新增 99/57 統計、未知 ID 拒絕、有效 ID 保存 assertions。
**TDD RED:** `cmd /c .loop\checks\cheap.cmd`（exit 1）
```text
error C2039: 'getRoformerModels': 並非 'HTDemucsGpuFXAudioProcessor' 的成員
error C2039: 'selectRoformerModel': 並非 'HTDemucsGpuFXAudioProcessor' 的成員
error C2039: 'getSelectedRoformerModel': 並非 'HTDemucsGpuFXAudioProcessor' 的成員
```
**Cheap GREEN:** `cmd /c .loop\checks\cheap.cmd`（exit 0）
```text
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general ... models=4 ... PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s
OK
Ran 2 tests in 0.056s
OK
```
**Full verification:** `cmd /c .loop\checks\full.cmd`（exit 0）
```text
=== ui_configuration_smoke ===
default_panel=general ... PASS
=== media_io_smoke ===
audio_import=true quick_vocals=true quick_accompany=true raw_stem_unchanged=true mix_controls=true video_import=true mp4_replace_audio=true mp4_bytes=31942 PASS
=== record_mode_smoke (auto/GPU) ===
backend=auto recorded_seconds=1.00426 preview_seconds=1.00426 progress=1 inference_ms=194.875 ... mix_controls=true ... PASS=true
[full] htdemucs_roformer_smoke not built yet - A8 still open
```
`python -c "import json,sys; sys.exit(any(not i['passes'] for i in json.load(open('.loop/backlog.json',encoding='utf-8'))))"`（exit 1；仍有未完成 backlog）
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 31 changed path(s) within policy
```
**Criteria:** C1: fail（full.cmd exit 0，但必要的 roformer smoke 尚不存在） · C2: fail（backlog checker exit 1） · C3: pass
**Metric:** 4 個 backlog item passing（best so far: 4；improved: false）
**Decision:** continue
**Note:** catalog／selection 子路徑已通過 TDD 與完整回歸，但 A4 尚缺真實 RoFormer worker 啟動/結果載入路由，不得翻為 pass；下一輪維持 A4 priority 完成該路由。

---

## Iteration 6 — 2026-08-23T13:30:23.4959931+08:00
**Inherited baseline:** 工作樹只有外部 driver 持續更新的 `.loop/driver.log` 與 `.loop/lastrun.log`；上一輪 record 完整且 cheap/full tier exit 0，無 crash-mid-write 跡象。Windows 使用者 `<host>\<user>` 與 repository owner 相符；branch=`loop/melband-roformer`，remote 與目標 tag 均為空，符合核准計畫。
**Hypothesis:** 若 processor 對已選 RoFormer ID 寫入 44.1 kHz float WAV、啟動既有 Python worker，並將兩個已驗證輸出載回 preview，A4 應會通過，因為 C++ standalone 路徑將實際完成模型啟動與結果載入。
**Change-set:** `plugin/PluginProcessor.cpp` 新增 RoFormer Python／worker／cache／output 路徑解析、已快取模型辨識、可取消的 child-process 啟動、兩 stem WAV 載入與 preview result 發佈；`tests/ui_configuration_smoke.cpp` 以真實 Kim vocals 模型和 48 kHz/2 秒 fixture 覆蓋 C++ route、兩 stem、model identity 與時長。未提前處理 A6 下載管理或 A7 輸出命名。
**TDD RED:** 首次 `cmd /c .loop\checks\cheap.cmd`（exit 1）因測試變數命名衝突，修正後重跑（exit 1）得到預期失敗：
```text
ui_configuration_smoke fatal: RoFormer C++ route did not start
```
**TDD GREEN / Verification:** `cmd /c .loop\checks\cheap.cmd`（實作後 exit 0；正式重驗 exit 0）
```text
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general ... fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.002s
OK
Ran 2 tests in 0.076s
OK
```
既有 `cheap`／`vswhere.exe` 的 `not recognized` 訊息仍為非致命環境輸出；命令實際 exit 0。
**Focused WAV contract:** 第一次 `python -c` 因 PowerShell 引號轉義得到 `SyntaxError`（exit 1），改由 stdin script 重跑（exit 0）：
```text
stem_count=2
input_instrumental.wav: rate=44100 frames=88200 channels=2 subtype=FLOAT finite=True
input_vocals.wav: rate=44100 frames=88200 channels=2 subtype=FLOAT finite=True
FOCUSED_EXIT=0
```
真實 C++ route 的輸入源為 48 kHz/96000 frames/2 秒 fixture；processor 既有匯入路徑轉為模型契約的 44.1 kHz/88200 frames，preview 仍為 2.0 秒，未改動 44.1 kHz 模型或匯出格式。
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 32 changed path(s) within policy
```
**Criteria:** C1: fail（iteration 6 非 full cadence，A8 等仍未完成） · C2: fail（仍有未完成 backlog） · C3: pass
**Metric:** 5 個 backlog item passing（best so far: 5；improved: true）
**Decision:** continue
**Note:** A4 已由真實 C++→RoFormer worker→兩 stem preview 路徑證明並翻為 pass；下一輪依 priority 轉向 A5 model browser UI。

---

## Iteration 7 — 2026-08-23T14:37:25.4633701+08:00
**Inherited baseline:** HEAD=`8df0079`（iteration 6 checkpoint），但工作樹除 driver log 外另有未提交的 `plugin/PluginProcessor.cpp` 與 `tests/ui_configuration_smoke.cpp` A5 變更；視為前一個 agent crash-mid-write／handoff breakage。依 protocol 在任何新變更前重跑 `cmd /c .loop\checks\cheap.cmd`（exit 0），確認 inherited A5 實作已為 green；此 baseline 不歸因於本 iteration 新寫的程式碼。
**Hypothesis:** 若獨立重驗並 checkpoint inherited 的一般面板 RoFormer model browser change-set，A5 應可由本輪列印的 UI smoke 證據翻為 pass，因為分類、搜尋、99 模型、experimental 標註與下載狀態都由真實 editor controls 驗證。
**Change-set:** recovery-only：保留並審核 inherited `plugin/PluginProcessor.cpp` model browser UI（category/search/model/status controls、filtering、selection、download/experimental status）與 `tests/ui_configuration_smoke.cpp` assertions；本 iteration 未另加產品行為，以免把已存在的 green handoff 偽裝成自己的 TDD 變更。
**Verification:** `cmd /c .loop\checks\cheap.cmd`（正式重驗 exit 0）
```text
HTDemucsGpuFX_Standalone.vcxproj -> ...\Standalone\HTDemucs GPU FX.exe
htdemucs_ui_configuration_smoke.vcxproj -> ...\htdemucs_ui_configuration_smoke.exe
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.006s
OK
Ran 2 tests in 0.063s
OK
CHEAP_EXIT=0
```
既有 `cheap`／`vswhere.exe` 的 `not recognized` 訊息仍為非致命環境輸出；命令實際 exit 0。
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 33 changed path(s) within policy
SCOPE_EXIT=0
```
**Criteria:** C1: fail（iteration 7 非 full cadence，A8 等仍未完成） · C2: fail（仍有未完成 backlog） · C3: pass
**Metric:** 6 個 backlog item passing（best so far: 6；improved: true）
**Decision:** continue
**Note:** A5 已由本輪獨立 cheap-tier editor smoke 證明並翻為 pass；下一輪依 priority 轉向 A6 下載管理。

---

## Iteration 8 — 2026-08-23T20:29:58+08:00
**Hypothesis:** 若新增 `worker/roformer_cache.py`（依 manifest 的 SHA-256 做按需下載驗證＋以目錄 mtime 做滾動快取上限 3 個的自動清理）並在 `worker/roformer_worker.py` 的 CLI `main()` 中串接為必經路徑，A6 應可翻為 pass，因為下載管理與既有真實分離流程共用同一條生產路徑，並有單元測試＋一次真實網路下載＋一次真實端到端分離佐證。
**Change-set:** 新增 `worker/roformer_cache.py`（`ensure_cached()`／`evict_oldest()`／`http_downloader()`／CLI）；`worker/roformer_worker.py` 的 `main()` 新增 `--manifest`／`--max-cached` 參數並在呼叫 `separate_file` 前呼叫 `_ensure_model_cached()`（manifest 未收錄的舊有預設模型會安靜略過，交由上游 session 自行下載，不受快取上限影響）；新增 `tests/test_roformer_cache.py`（8 個測試：缺檔下載、已驗證則跳過、快取檔案損毀時重下載、重下載仍失敗則刪檔並拋錯、未知 model id 拋 KeyError、滾動快取上限 3 個淘汰最舊、touch 保護最近使用項目不被淘汰、`evict_oldest()` 直接呼叫語意）；`.loop/checks/cheap_extra.cmd` 新增一行執行該測試檔（append-only，僅擴充既有擴充點）。
**Unit tests:** `python tests\test_roformer_cache.py -v`（exit 0）
```text
test_downloads_when_missing_and_verifies_sha256 ... ok
test_evict_oldest_direct_call_respects_keep_count ... ok
test_raises_and_deletes_when_redownload_still_mismatches ... ok
test_redownloads_when_cached_file_is_corrupt ... ok
test_rolling_cache_evicts_oldest_beyond_cap ... ok
test_skips_download_when_already_verified ... ok
test_touching_an_existing_model_protects_it_from_eviction ... ok
test_unknown_model_id_raises_key_error ... ok
Ran 8 tests in 0.299s
OK
```
**真實下載＋SHA-256 佐證：** `python worker\roformer_cache.py --model roformer-model-melband-roformer-guitar-by-becruily --cache-dir C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache --max-cached 3`（exit 0；真的從 huggingface.co 下載 45,142,183 bytes，sha256=`83472bbf...2a83c` 與 manifest 相符）；重跑同一指令 0.42 秒完成、無下載呼叫（快取已驗證，跳過）。
**真實端到端分離佐證（生產路徑）：** `htfx-roformer` env 下 `python worker\roformer_worker.py --input verify\fixtures\test_48k_2s.wav --output-dir verify\output\roformer-cache-integration --model roformer-model-melband-roformer-guitar-by-becruily --models-dir verify\roformer-cache --device cpu`（exit 0）——下載並 SHA-256 驗證了 config（upstream 自身邏輯，非 checkpoint 快取上限管轄範圍）、CPU 推論完成、輸出 `Guitar`／`Other` 兩個 stem，皆為 48000 Hz／96000 frames／stereo／FLOAT／finite（與輸入 2 秒 48 kHz fixture 時長一致）。快取此時為 `[melband-roformer-kim-vocals, roformer-model-melband-roformer-guitar-by-becruily]`（2 ≤ 3，未觸發真實淘汰；淘汰邏輯已由單元測試決定性證明，真實第 4 個模型下載觸發的淘汰將隨後續 iteration 的 M-ENUM 逐一下載自然發生，未強行於本輪下載第 3、4 個大型權重以免超出 iteration 時間／磁碟預算）。
**Verification:** `cmd //c .loop\checks\cheap.cmd`（exit 0；Bash 工具下需用 `cmd //c` 而非 `cmd /c`，見下方 lesson）
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s
OK
Ran 2 tests in 0.085s
OK
Ran 8 tests in 0.126s
OK
CHEAP_EXIT=0
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 36 changed path(s) within policy
```
**Criteria:** C1: fail（iteration 8 非 full cadence，A7/A8/A9/A10/M-ENUM 仍未完成） · C2: fail（仍有未完成 backlog） · C3: pass
**Metric:** 7 個 backlog item passing（best so far: 7；improved: true）
**Decision:** continue
**Lesson:** 在本 Bash 工具（Git Bash/MSYS）下呼叫 `cmd /c "..."` 時，MSYS 會把 `/c` 誤判為路徑並吃掉整個指令（實測只會開出一個互動式 cmd banner，不執行任何內容，且 exit code 仍是 0，非常容易被誤判為「成功」）；必須改用 `cmd //c "..."`（雙斜線跳脫 MSYS 路徑轉換）才會真的執行。已寫入 LESSONS.md。
**Note:** A6 已由 cache manager＋單元測試＋真實下載＋真實端到端分離證明並翻為 pass；下一輪依 priority 轉向 A7 輸出 UX（各類別 2-stem 輸出命名與 Export 流程）。

---
