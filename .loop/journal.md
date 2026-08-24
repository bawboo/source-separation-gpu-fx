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

## Iteration 9 — 2026-08-23T21:18:42+08:00
**Inherited baseline:** HEAD=`1f40dae`（iteration 8 checkpoint），但工作樹在 driver log 之外另有未提交的 `plugin/PluginProcessor.cpp`／`plugin/PluginProcessor.h`／`tests/ui_configuration_smoke.cpp` 變更——內容完整實作 A7（依上游 worker 輸出檔名 `<input>_<output_id>.wav` 推導類別正確的 2-stem 命名，取代固定 htdemucs 來源順序假設，套用到 UI 按鈕標籤與 Export 匯出檔名）。視為前一個 agent 於協定第 10–12 步（journal／state／checkpoint）之前 crash-mid-write 的 handoff breakage；依 protocol 第 4 步，在新增任何變更前先重跑 `cmd //c .loop\checks\cheap.cmd` 建立 baseline。
**Hypothesis:** 若獨立重驗並 checkpoint 這個 inherited change-set（不額外添加新行為，以免把已存在的 green handoff 偽裝成本輪自己的 TDD 變更），A7 應可由本輪列印的 cheap-tier 證據翻為 pass，因為新增的 8 類別合成 fixture 推導斷言、真實分離後的 label 斷言（vocals/instrumental 而非殘留 htdemucs drums/bass）與真實 Export 檔名斷言都會在本輪重驗輸出中證實。
**Change-set:** recovery-only。`PluginProcessor.h` 新增 `getStemLabel(int)`／`static deriveRoformerStemLabel(juce::File, juce::String)` 宣告與 `SeparationResult::stemLabels`；`PluginProcessor.cpp` 的 `separationLoop` 為每個輸出檔呼叫 `deriveRoformerStemLabel()` 並記錄 `stemLabels`，`getStemLabel()` 供 UI 按鈕文字使用（無 label 時 fallback 回 `sourceName()`），`stemExportLoop` 匯出檔名改用推導出的 label 而非固定 `sourceName()`，stem 按鈕的 `setButtonText` 改呼叫 `processor_.getStemLabel(source)`；`tests/ui_configuration_smoke.cpp` 新增 8 個合成類別 fixture（vocals/instrumental/dry/reverb/noise/other/guitar/aspiration）逐一驗證 `deriveRoformerStemLabel` 推導正確、驗證真實分離後 `getStemLabel(0/1)` 為 vocals/instrumental 且非 htdemucs 殘留命名、驗證真實 `beginStemExport` 產生 2 個以類別命名的 WAV 檔，並在 smoke PASS 行追加 `roformer_stem_labels=`／`roformer_stem_label_categories=`／`roformer_export_naming=` 欄位。
**Verification:** `cmd //c .loop\checks\cheap.cmd`（正式重驗 exit 0）
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.004s
OK
Ran 2 tests in 0.088s
OK
Ran 8 tests in 0.110s
OK
CHEAP_EXIT=0
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 37 changed path(s) within policy
SCOPE_EXIT=0
```
**Criteria:** C1: fail（iteration 9 非 full cadence，A8/A9/A10/M-ENUM 仍未完成） · C2: fail（仍有未完成 backlog） · C3: pass
**Metric:** 8 個 backlog item passing（best so far: 8；improved: true）
**Decision:** continue
**Lesson:** 本 Bash 工具（Git Bash/MSYS）下，`cmd //c` 之後若把 `.loop\checks\cheap.cmd` 這類含反斜線的路徑寫成不加引號或雙反斜線跳脫，MSYS 仍會把每個 `\<letter>` 當成跳脫序列吃掉反斜線本身（例如 `.loop\checks\cheap.cmd` 或 `.loop\checks\cheap.cmd` 都會被吃成 `.loopcheckscheap.cmd`，導致 cmd 找不到檔案、但因為是 `%~dp0` 之類的相對路徑錯誤，實際會回報找不到指令且 exit code 非 0，不像先前 lesson 提到的「單獨 `/c` 誤轉路徑」那樣偽裝成功）；必須用「單引號」包住整個路徑（例如 `cmd //c '.loop\checks\cheap.cmd'`）才能讓反斜線原樣傳給 cmd.exe。已寫入 LESSONS.md。另外，本次也確認 PowerShell 工具在此 headless/自動化情境下對 `cmd /c ...`／`& script.cmd` 這類外部命令一律回報「contains multiple operations ... requires approval」而無法執行（可能是因為 LOOP_PLAN §9 AGENT_CMD_JSON 的 `--allowedTools` 只列了 `Bash`、未列 `PowerShell`）；本迴圈的所有建置/驗證指令一律用 Bash 工具＋單引號路徑呼叫 `cmd //c`，不要嘗試 PowerShell 工具跑 `.loop/checks/*.cmd`。
**Note:** A7 已由 recovery 重驗（含 8 類別 fixture 與真實分離/Export 佐證）證明並翻為 pass；下一輪依 priority 轉向 A8（新增 `htdemucs_roformer_smoke` CMake target 並納入 full tier）。

## Iteration 10 — 2026-08-23

**Inherited baseline:** 工作樹在 iter-9 checkpoint（b812e00）之上又有未提交變更：`CMakeLists.txt`（新增 `htdemucs_roformer_smoke` target）與新檔 `tests/roformer_smoke.cpp`（對應 A8 的完整實作），視為前一個 agent 於協定第 10-12 步前 crash-mid-write 的 handoff breakage。本輪迭代號為 10（5 的倍數），依協定本就必須跑 full tier，因此直接以 full tier 重驗這個繼承的 change-set 作為基線。
**Hypothesis:** 若先以 full tier 重驗這個繼承的 A8 change-set（不當作本輪自己的效果），再視結果修正真正的根因（而非放寬斷言），A8 應可轉為 pass，因為底層匯出管線（`stemExportLoop` 一律以 `kSampleRate=44100` 寫出，與 A7/A9 既有測試已間接證明的行為一致，也是 `CLAUDE.md` 記載的既定不變量「44.1 kHz 模型與匯出不受影響」），只是新測試本身的斷言寫錯（誤把匯出取樣率等同來源 48 kHz）。
**Files touched:** `tests/roformer_smoke.cpp`（修正 sample-rate 斷言：`48'000.0` → `HTDemucsGpuFXAudioProcessor::kSampleRate`，並同步修正尾端輸出的 `roformer_sample_rate` 欄位）；`CMakeLists.txt`（繼承，未再修改，僅重驗）；`.loop/backlog.json`、`.loop/iterations/0010.json`、`.loop/journal.md`、`.loop/LESSONS.md`、`.loop/state.json`（loop 控制面）。
**Verification（第一次，繼承現狀原樣重驗）:** `cmd //c '.loop\checks\full.cmd'`（exit 1）
```text
=== ui_configuration_smoke ===
...roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
=== media_io_smoke ===
audio_import=true quick_vocals=true quick_accompany=true raw_stem_unchanged=true mix_controls=true video_import=true mp4_replace_audio=true mp4_bytes=31942 PASS
=== record_mode_smoke (auto/GPU) ===
backend=auto recorded_seconds=1.00426 preview_seconds=1.00426 progress=1 inference_ms=206.445 ... PASS=true
=== roformer_smoke ===
roformer_smoke fatal: RoFormer smoke: exported stem sample rate drifted from the source
```
根因排查：以 `ffprobe` 直接檢視本輪產生的匯出檔 `verify/output/roformer-smoke/export-*/test_48k_2s_vocals.wav`，實際為 `pcm_f32le, 44100 Hz, 2 channels, duration=2.000000s`——時長正確保留，但取樣率固定在插件內部處理率 `kSampleRate=44100`，而非來源 fixture 的 48000 Hz。這與既有 `htdemucs_record_mode_smoke`／`htdemucs_media_io_smoke` 及 `CLAUDE.md` 記載的既定不變量（44.1 kHz 模型與匯出一律不受來源取樣率影響）完全一致，證明是新測試本身的斷言假設錯誤，不是匯出管線的迴歸。修正 `tests/roformer_smoke.cpp` 把預期改為 `HTDemucsGpuFXAudioProcessor::kSampleRate`（44100）。
**Verification（修正後重跑）:** `cmd //c '.loop\checks\full.cmd'`（exit 0）
```text
=== ui_configuration_smoke ===
...roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
=== media_io_smoke ===
audio_import=true quick_vocals=true quick_accompany=true raw_stem_unchanged=true mix_controls=true video_import=true mp4_replace_audio=true mp4_bytes=31942 PASS
=== record_mode_smoke (auto/GPU) ===
backend=auto recorded_seconds=1.00426 preview_seconds=1.00426 progress=1 inference_ms=194.593 ... PASS=true
=== roformer_smoke ===
roformer_catalog=99 roformer_audited=57 roformer_stems=2 roformer_labels=vocals/instrumental roformer_export_naming=true roformer_sample_rate=44100 roformer_channels=2 roformer_bit_depth=32float roformer_finite=true PASS
```
**Backlog checker:** `python -c "import json,sys; sys.exit(any(not i['passes'] for i in json.load(open('.loop/backlog.json',encoding='utf-8'))))"`（exit 1 — A10/M-ENUM 仍未完成）
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 40 changed path(s) within policy
SCOPE_EXIT=0
```
**Criteria:** C1: pass（full tier 全綠，含新的 roformer smoke）· C2: fail（A10、M-ENUM 仍未完成）· C3: pass
**Metric:** 10 個 backlog item passing（best so far: 10；improved: true，A8＋A9 本輪轉 pass）
**Decision:** continue
**Lesson:** 這個專案的匯出管線（HTDemucs 與 RoFormer 共用）一律以插件內部固定處理率 `HTDemucsGpuFXAudioProcessor::kSampleRate`（44100 Hz）寫出匯出檔，與來源媒體的取樣率（例如本次 fixture 的 48 kHz）無關——這是 `CLAUDE.md` 記載的既定不變量（「44.1 kHz 模型與匯出不受影響」），未來任何新測試斷言匯出檔取樣率時，必須比對 `kSampleRate`，不可假設等於來源取樣率；只有「時長」需要與來源一致，取樣率不用。已寫入 LESSONS.md。
**Note:** A8／A9 本輪由 recovery＋根因修正＋full tier 重驗證明並翻為 pass；C1 本輪首次轉 pass。下一輪依 priority 轉向 A10（README.md 新增 RoFormer 章節）與 M-ENUM（依 manifest 產生 M001..M099 per-model backlog 項目）。

## iter 11 — 2026-08-23T22:35:00+08:00

**Hypothesis:** A10（README.md 新增 RoFormer 章節）是目前 backlog 中優先序最高的未通過項目（priority 10，早於 M-ENUM 的 11）。新增一個涵蓋分類/稽核狀態/按需下載與滾動快取/輸出命名與取樣率不變量的 README 章節，且章節內引用的數字（99/57/42、10 個分類、快取上限 3）皆對照 `assets/models/roformer-manifest.json` 與 `worker/roformer_cache.py` 的實際內容，A10 應可翻為 pass——這是純文件性質的完成度標準，cheap tier 綠燈（既有功能未退步）即足以佐證。

**Files touched:** `README.md`（新增「## MelBand RoFormer 模型（進階面板）」章節，置於「## 已知限制」之前）；`.loop/backlog.json`、`.loop/iterations/0011.json`、`.loop/journal.md`、`.loop/state.json`（loop 控制面）。

**內容依據核對：**
- 分類與統計：`python -c "..."` 讀取 `assets/models/roformer-manifest.json` 確認 99 個模型、10 個類別（vocals=24, instrumental=37, instvoc=3, karaoke=5, dereverb=8, denoise=6, crowd=1, aspiration=2, guitar=1, general=12）、57 個 audited/42 個 experimental，與 README 新章節文字一致。
- 快取上限：`worker/roformer_cache.py` `DEFAULT_MAX_CACHED = 3`，與 README「最多同時保留 3 個」一致。
- 取樣率不變量：呼應 iter 10 LESSONS 記載的既定事實（匯出固定 `kSampleRate`=44100 Hz，與來源取樣率無關；只有時長需一致）。

**Verification:** `cmd //c '.loop\checks\cheap.cmd'`（exit 0）
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.004s
OK
Ran 2 tests in 0.082s
OK
Ran 8 tests in 0.191s
OK
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 42 changed path(s) within policy
SCOPE_EXIT=0
```
**Backlog checker:** 未跑本輪未執行 full tier，故 C1/C2 仍記為 fail（本迭代非 5 的倍數，依協定僅需 cheap tier；per-item 統計以 Python 直接讀取 `.loop/backlog.json` 得 11/12 passing，僅 M-ENUM 剩下）。
**Criteria:** C1: fail（本輪未跑 full tier，不宣稱 pass）· C2: fail（M-ENUM 仍未完成）· C3: pass
**Metric:** 11 個 backlog item passing（best so far: 11；improved: true，A10 本輪轉 pass）
**Decision:** continue
**Lesson:** 無新規則（純文件性任務，未觸發新的踩坑）。
**Note:** 下一輪目標轉向 backlog 中僅存的未過項目 M-ENUM（依 `assets/models/roformer-manifest.json` 產生 M001..M099 per-model backlog 項目：57 個 audited 逐一「下載+SHA-256→分離 fixtures→輸出規格驗證」，42 個 experimental 逐一「收錄+標註+嘗試一次並記錄成敗」）——這是全 backlog 唯一剩下的未過項目，規模大，預期需跨多個 iteration 才能完成全部 99 項；下一輪應先產生 M001..M099 的 backlog 條目本身（若尚未存在），再開始逐批處理。

## iter 12 — 2026-08-23T23:10:00+08:00

**Hypothesis:** M-ENUM 是 backlog 中唯一剩下的未過項目，但其對應的 M001..M099 per-model 子項目尚未產生。若先依 `assets/models/roformer-manifest.json` 產生 M001..M099（append-only、idempotent），M-ENUM 本身（「產生」這個動作）應可翻為 pass；接著用一個新的可重用批次驗證腳本（download+SHA-256 驗證→分離 fixture→輸出規格驗證；audited 失敗才算 fail，experimental 失敗算 attempted_failed 且仍視為完成）處理第一批模型，應能讓一部分 M001..M099 子項目本身也翻為 pass——因為底層 `worker/roformer_cache.py` 與 `worker/roformer_worker.py` 的生產路徑已由 A3/A6/A8 證明正確，只是尚未跑過完整 99 個模型的清單。

**Files touched:** 新增 `tools/generate_roformer_model_backlog_items.py`（讀取 manifest，依模型順序 append M001..M099 到 `.loop/backlog.json`，以 `model_id` 比對避免重複、絕不覆寫既有項目）；新增 `tools/roformer_batch_verify.py`（可重用批次驗證腳本：audited 模型走 `ensure_cached`→`separate_file`→輸出契約驗證，失敗＝真缺陷；experimental 模型同樣嘗試，失敗記為 `attempted_failed`，不影響腳本 exit code）；`.loop/backlog.json`（append 99 個 M-items＋本輪翻 pass 40 項）；`.loop/iterations/0012.json`、`.loop/journal.md`、`.loop/state.json`（loop 控制面）；`.loop/driver.log`（外部 driver 程序自行 append 的執行紀錄，非本輪變更效果，隨 commit 一併納入）。

**產生 M-items：** `python tools/generate_roformer_model_backlog_items.py`（exit 0）
```text
appended 99 M-items (total backlog size now 111)
first appended: ['M001', 'M002', 'M003']
last appended: ['M097', 'M098', 'M099']
```

**第一批（3 個已稽核模型）第一次執行（發現 bug）：** `python tools/roformer_batch_verify.py --models melband-roformer-kim-vocals roformer-model-melband-roformer-guitar-by-becruily roformer-model-melband-roformer-deux-by-becruily --cache-dir verify/roformer-cache --fixture verify/fixtures/test_48k_2s.wav --output-root verify/output/roformer-batch --device auto --max-cached 3`（exit 1）
```text
kim-vocals: outcome=pass
guitar-by-becruily: outcome=pass
deux-by-becruily: 下載 435,006,815 bytes 成功、sha256 驗證通過，但 separate_file() 呼叫時拋出
  UnicodeEncodeError: 'cp950' codec can't encode character '\U0001f504' in position 2: illegal multibyte sequence
```
**根因排查：** `worker/roformer_worker.py` 的 CLI `main()` 早在呼叫 `separate_file()` 前就呼叫 `configure_utf8_stream(sys.stdout)`／`configure_utf8_stream(sys.stderr)`（iter 3 已修過同類 emoji-in-cp950-console 問題），但本輪新寫的 `tools/roformer_batch_verify.py` 是直接 import `separate_file()` 當函式庫呼叫、繞過了那個 CLI 專屬的初始化，所以同一個舊 bug 在新的呼叫路徑上重現。修正：`roformer_batch_verify.py` 的 `main()` 一開始就呼叫同一組 `configure_utf8_stream`（從 `roformer_worker` import），而不是複製/放寬邏輯。
**修正後重跑：** 同一指令（exit 0）
```text
{
  "model": "melband-roformer-kim-vocals", ... "sample_rate": 48000, "frames": 96000, "channels": 2, "subtype": "FLOAT", "finite": true, "num_outputs": 2, "outcome": "pass"
  "model": "roformer-model-melband-roformer-guitar-by-becruily", ... "outcome": "pass"
  "model": "roformer-model-melband-roformer-deux-by-becruily", "cache_verified_sha256": "10255c02...eda3aa", "checkpoint_size": 435006815, ... "outcome": "pass"
}
```
**第二批（36 個缺 sha256 的 experimental 模型）：** `python tools/roformer_batch_verify.py --models <36 ids> --cache-dir verify/roformer-cache --fixture verify/fixtures/test_48k_2s.wav --output-root verify/output/roformer-batch --device auto --max-cached 3`（exit 0——experimental 失敗不影響 exit code）
```text
36/36 outcome=attempted_failed，每一個的 error 均為：
  ValueError: manifest entry has no recorded sha256 (checkpoint integrity cannot be verified)
```
這 36 個模型在 manifest 產生時就缺 sha256/size（上游中繼資料當時無法取得），因此連下載都不會發生，快速失敗；依 LOOP_PLAN backlog 語意「嘗試一次並記錄結果（成敗皆可）」，這仍是完成的 M-item。

**Backlog 更新：** 直接以 Python（tmp+rename 原子寫入）把 M-ENUM＋3 個 audited 通過項＋36 個 experimental 已嘗試項共 40 個 item 的 `passes` 翻為 `true` 並寫入 `evidence`；其餘 59 個 M-item（21 個尚未處理的 audited、6 個有 sha256 尚未處理的 experimental）保持 `passes:false` 不變，留給後續 iteration。

**Verification:** `cmd //c '.loop\checks\cheap.cmd'`（exit 0）
```text
default_panel=general ... roformer_browser=99 categories=10 search=true experimental=true download_status=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s
OK
Ran 2 tests in 0.071s
OK
Ran 8 tests in 0.117s
OK
```
（cmd 標準輸出開頭出現兩行 mojibake「'cheap' 不是內部或外部命令」「'vswhere.exe' 不是內部或外部命令」——這是 VsDevCmd.bat 既有的內部探測噪音，與本輪變更無關，exit code 仍為 0，不影響任何實際檢查。）
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 45 changed path(s) within policy
SCOPE_EXIT=0
```
**Backlog checker（本輪非 5 的倍數，non-full-tier，僅供參考）：** `python -c "...sys.exit(any(not i['passes'] for i in json.load(...)))"`（exit 1）——`passing 51 / 111`
**Criteria:** C1: fail（本輪未跑 full tier，不宣稱 pass）· C2: fail（仍有 60 個 M-item 未過）· C3: pass
**Metric:** 51 個 backlog item passing（best so far: 51；improved: true，較上輪 11 大幅躍進，主因是 M-ENUM 產生 99 個新項目後、其中 40 個當輪立即以真實驗證/嘗試證據翻為 pass）
**Decision:** continue
**Lesson:** 任何新寫的腳本只要直接 import 並呼叫 `worker/roformer_worker.py` 的 `separate_file()`（而不是透過它的 CLI `main()`），都必須自己呼叫 `configure_utf8_stream(sys.stdout)`／`configure_utf8_stream(sys.stderr)`，否則會在 Windows cp950 主控台上重現 iter 3 已修過的 `UnicodeEncodeError`（upstream 進度輸出含 emoji，例如 U+1F504 🔄）；這個初始化不是 CLI 專屬的裝飾，是每個呼叫路徑都需要的前置條件。已寫入 LESSONS.md。
**Note:** 下一輪應繼續處理剩下的 59 個 M-item：21 個尚未跑的 audited 模型（依 checkpoint size 由小到大排序以控制單輪時間）＋6 個有 sha256 但尚未嘗試的 experimental 模型；`tools/roformer_batch_verify.py` 與 `tools/generate_roformer_model_backlog_items.py`（已具 idempotent 保護）皆可直接重用，不需要重寫。

## Operator note — 2026-08-23T22:45+08:00（使用者要求暫停）
- 使用者指示「暫停，但不要遺失工作階段」。STOP 檔已投放；同時 iteration 13 因 agent 提前結束（背景下載未等完、未寫 record/commit）觸發外部 scope 檢查：移交樹內出現不該存在的相對路徑 `verify/roformer-cache/`（290MB 部分下載 .ckpt.part）。driver 依設計鎖 blocked 停機。
- 孤兒下載程序已自行結束；殘留 `verify/` 目錄留待續跑時清理（norule 未刪）。
- 狀態由 blocked 改為 paused/user_pause_required（使用者主動暫停）。進度無損：iter 12 完成、backlog 51/111 綠（A1–A10、D1、M-ENUM＋39 M 項）、checkpoints 至 `b812e00`＋iter-12 commit。
- 續跑程序：status 改回 running/null → `bash .loop/run_loop.sh`。續跑前 STEER 已備妥（見 .loop/STEER.md）。

## iter 13 — 2026-08-24T20:31:02+08:00

**STEER.md（續跑後一次性指令，本輪引用後刪除）：**
> （續跑後第一個 iteration 的一次性指令）
> 1. 先清理移交樹內誤建的 `verify/` 目錄（相對路徑 bug 的殘留，含部分下載 .ckpt.part——屬快取例外可刪），確認 `git status` 乾淨後再開始本輪 change-set。
> 2. 之後所有模型快取操作一律使用絕對路徑 C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\。
> 3. audited 模型批次縮小為每輪同步完成 2–4 個（下載→SHA-256→分離→驗證→記錄，全部在本輪內完成，不留背景工作）。

**Hypothesis:** 依 STEER 指令執行，M-ENUM 剩下的 60 個 M-item 應能穩定推進而不再重演 iter-13 incident（相對路徑污染／提前結束留下背景工作）；本輪目標 4 個 audited（M038/M039/M040/M041，sucial 系列體積最小的 dereverb checkpoint）＋6 個 experimental（M074/M085/M086/M087/M088/M091，syh99999／kimberley-jensen 系列）共 10 個 M-item 翻為 pass。

**Files touched:** 刪除移交樹內誤建的相對 `verify/`（僅含一個 290MB 部分下載 `.ckpt.part`，快取例外可刪）；`.loop/backlog.json`（10 個 M-item 翻 pass＋evidence）；`.loop/iterations/0013.json`、`.loop/journal.md`、`.loop/LESSONS.md`、`.loop/state.json`、`.loop/STEER.md`（刪除，一次性指令已消費）、`.loop/driver.log`、`.loop/lastrun.log`（外部 driver 自行 append，隨 commit 一併納入）。未修改任何 `plugin/`／`cpp/`／`worker/`／`tests/` 原始碼。

**Step 1 — 清理：** `rm -rf verify/`（移交樹內的相對路徑殘留）後 `git status --porcelain=v1 -uall` 僅剩 `.loop/driver.log`／`.loop/lastrun.log`／`.loop/state.json` 三個既有追蹤檔的修改（operator pause/resume 遺留），確認乾淨後才開始本輪 change-set。

**Step 2 — 批次驗證（新踩坑：`conda run` 不能用）：** 第一次嘗試 `conda.exe run -n htfx-roformer python tools/roformer_batch_verify.py ...`（exit 1）——不是 `separate_file()` 的 emoji/cp950 老問題（`configure_utf8_stream` 已在腳本 `main()` 開頭呼叫），而是 `conda run` 包裝器本身：它把子行程的 stdout 完整捕捉後，用**自己的** `sys.stdout`（綁在 cp950 主控台、未被子行程的 utf8 重新設定影響）`print()` 出來，一樣的 `UnicodeEncodeError: 'cp950' codec can't encode character '�'`。改用 `htfx-roformer` env 的 python.exe 絕對路徑直接呼叫（`C:\Users\<user>\anaconda3\envs\htfx-roformer\python.exe tools/roformer_batch_verify.py ...`，略過 `conda run` 這層轉印）即可正常運作。已寫入 LESSONS.md。

**批次 1（2 個 audited，M041/M038）：** exit 0，`outcome=pass` × 2（sha256 皆驗證通過；separation sample_rate=48000 frames=96000 channels=2 subtype=FLOAT finite=true num_outputs=2）。
**批次 2（2 個 audited，M039/M040）：** exit 0，`outcome=pass` × 2（同上規格）。
**批次 3（6 個 experimental，M074/M085/M086/M087/M088/M091）：** 因指令輸出量大，工具自動轉入背景執行；本輪以 `TaskOutput(block=true)` 同步等待其完成（未提前結束 turn，遵守 LESSONS iter-13 教訓）。exit 0（experimental 失敗不影響腳本 exit code）；6/6 皆 `outcome=attempted_failed`——checkpoint 全數下載＋sha256 驗證通過，但 `separate_file()` 呼叫時皆因上游 config yaml（`config_vocals_mel_band_roformer_*_ft.yaml` / `vocals_mel_band_roformer.yaml`）在 `raw.githubusercontent.com/TRvlvr/application_data` 回 404（上游中繼資料本身缺失，非本專案程式碼缺陷）而 `RuntimeError`。依 LOOP_PLAN backlog 語意（「嘗試一次並記錄結果，成敗皆可」），這是完成的 M-item，記為 `attempted_failed`。

**Backlog 更新：** 以一次性腳本（tmp+rename 原子寫入，用畢即刪）把上述 10 個 M-item 的 `passes` 翻為 `true` 並寫入含指令、sha256、size、separation 規格或失敗原因的 `evidence`；51 → 61 個 backlog item passing。

**Step 3 — 驗證發現的真實回歸（本輪造成，本輪修正）：** 第一次 `cmd //c '.loop\checks\cheap.cmd'`（exit 1）：
```text
ui_configuration_smoke fatal: RoFormer C++ route did not start
```
根因排查：`tests/ui_configuration_smoke.cpp` 硬編碼選用 `melband-roformer-kim-vocals` 跑 RoFormer C++ 路由；`plugin/PluginProcessor.cpp` 的 `beginSeparation()` 在呼叫前會先檢查 `isModelInstalled(configuration.modelName)`——若模型不在 `verify/roformer-cache/` 內，直接回傳 `false`（不會自動下載，下載是另一個 UI 動作）。本輪批次驗證的 10 個模型陸續寫入同一個共用的滾動快取（`max_cached=3`，LRU by mtime），把 `melband-roformer-kim-vocals` 擠出快取，使 smoke test 的硬編碼前提被破壞——這是本輪 change-set 的真實副作用（不是繼承的壞掉狀態），必須修根因而非放寬斷言。修正：`python worker/roformer_cache.py --model melband-roformer-kim-vocals --cache-dir C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache` 重新下載+驗證並把它變回最近觸碰（cache_dir_entries 確認 kim-vocals 回到快取內）。重跑 `cmd //c '.loop\checks\cheap.cmd'`（exit 0）：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.002s
OK
Ran 2 tests in 0.076s
OK
Ran 8 tests in 0.253s
OK
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 48 changed path(s) within policy
SCOPE_EXIT=0
```
**Backlog checker（本輪非 5 的倍數，non-full-tier，僅供參考）：** exit 1 —— passing 61/111。
**Criteria:** C1: fail（本輪未跑 full tier，不宣稱 pass）· C2: fail（仍有 50 個 M-item 未過）· C3: pass
**Metric:** 61 個 backlog item passing（best so far: 61；improved: true，較上輪 51 增加 10）
**Decision:** continue
**Lesson:** (1) 呼叫 `worker/roformer_worker.py` 相關腳本一律用 `htfx-roformer` env 的 python.exe 絕對路徑（`C:\Users\<user>\anaconda3\envs\htfx-roformer\python.exe`）直接執行，不要包一層 `conda run`——`conda run` 的輸出轉印邏輯本身在 Windows cp950 主控台上會對含 emoji 的子行程輸出拋 `UnicodeEncodeError`，即使子行程自己已呼叫過 `configure_utf8_stream`，因為壞掉的是 `conda run` 自己的 print，不是子行程的 stream。(2) `ui_configuration_smoke` 的 RoFormer C++ 路由測試硬編碼依賴 `melband-roformer-kim-vocals` 必須常駐 `verify/roformer-cache/`（`beginSeparation()` 不會自動下載未安裝模型）；任何在同一個共用滾動快取（`max_cached=3`）內批次跑其他模型的 M-item 驗證，都可能把 kim-vocals 擠出快取而讓 cheap/full tier 的 UI smoke 失敗——批次跑完 M-item 後、執行 cheap/full tier 之前，必須確認（必要時重新觸碰）kim-vocals 仍在快取內。已寫入 LESSONS.md。
**Note:** 下一輪應繼續處理剩下的 50 個 audited M-item（依 checkpoint size 由小到大，下一批約 913MB 級距的 gabox/instv6 系列共 ~14 個同尺寸模型）；`tools/roformer_batch_verify.py` 與 `tools/generate_roformer_model_backlog_items.py` 皆可直接重用。每輪批次跑完後、進 cheap/full tier 前，務必確認 `melband-roformer-kim-vocals` 仍在 `verify/roformer-cache/` 內（不在則重新觸碰），避免重演本輪的 smoke test 回歸。

## Operator note — 2026-08-24T20:55+08:00（iter-14 事故處理與續跑）
- iteration 14 重蹈兩個舊病：(1) 背景任務＋Monitor 後結束 turn（headless -p 不會被喚醒）→ 無 record 無 commit；(2) 絕對路徑未引號經 bash 傳遞 → repo 內畸形目錄 `CodexProjectsSourceSeparation_GPU_FXverifyroformer-cache/`（229MB 部分下載）→ scope 鎖機（防護正確動作）。
- 處置：畸形目錄整個**搬移**（非刪除）至 `verify\quarantine\iter14-mangled-cache\`；無孤兒程序；兩條規則升級為 ITERATION_PROMPT 硬規則＋LESSONS SIGN。
- 狀態 blocked→running，driver 重啟續跑（下一輪仍記為 iteration 14）。

## Operator note — 2026-08-24T22:15+08:00（兩振超時處置）
- iteration 14 第 2、3 次嘗試均 40 分鐘超時（claude -p 無中途輸出，推測慢速行動網路下同步下載大模型未完）。driver 依設計兩振鎖 blocked/stalled_no_state_update。
- 修訂 §6 iteration_timeout 40→60 min（Amendments 已記）；STEER（單模型批次＋15 分下載止損）未被消費、下輪生效。狀態解鎖 running，driver 重啟。

## iter 14 — 2026-08-24T22:15:23+08:00

**STEER.md（21:35 投放，一次性指令，本輪引用後刪除）：**
> （一次性操作指令，2026-08-24 21:35 投放）
> 前兩次 iteration-14 嘗試分別因背景執行與 40 分鐘超時失敗。本輪硬性要求：
> 1. 批次縮到最小：本輪只同步驗證 **1 個** audited 模型（若其權重已在快取可加做第 2 個）。挑 manifest 中檔案較小的優先。
> 2. 時間預算管理：下載＋分離＋驗證＋record＋commit 必須在 40 分鐘內全部完成。若單一模型下載超過 15 分鐘仍未完，中止該模型、記為本輪觀察（journal），改驗快取內既有模型或改做小項目，確保本輪有 record 有 commit。
> 3. 其餘照 ITERATION_PROMPT 硬規則（禁背景、路徑單引號、絕對路徑快取）。

**Hypothesis:** 檢查 `verify/roformer-cache/` 發現前兩次失敗的 iteration-14 嘗試已把 M031（aspiration-by-sucial）與 M032（aspiration-less-aggressive-by-sucial）的 checkpoint 完整下載（位元組數與 manifest `size` 完全吻合：835983746／835982664），只有 M037（de-reverb-echo）是部分 `.ckpt.part`（825,229,312 / 835,997,896 bytes，未完整）。對已完整快取的 M031＋M032 執行 batch verify 完全不需要下載，應能在極短時間內完成並翻為 pass，不重蹈前兩輪的下載逾時；M037 因無斷點續傳（`ensure_cached` 一律整檔重下）會觸發全新完整下載，違背本輪風險最小化的 STEER 精神，本輪不嘗試。

**Files touched:** `.loop/backlog.json`（M031/M032 翻 pass＋evidence）；`.loop/iterations/0014.json`、`.loop/journal.md`、`.loop/state.json`、`.loop/STEER.md`（刪除，一次性指令已消費）、`.loop/driver.log`、`.loop/lastrun.log`（外部 driver 自行 append，隨 commit 一併納入）。未修改任何 `plugin/`／`cpp/`／`worker/`／`tests/` 原始碼。

**執行（零下載，快取已完整）：** `C:/Users/<user>/anaconda3/envs/htfx-roformer/python.exe tools/roformer_batch_verify.py --models roformer-model-melband-roformer-aspiration-by-sucial roformer-model-melband-roformer-aspiration-less-aggressive-by-sucial --cache-dir C:/CodexProjects/SourceSeparation_GPU_FX/verify/roformer-cache --fixture C:/CodexProjects/SourceSeparation_GPU_FX/verify/fixtures/test_48k_2s.wav --output-root C:/CodexProjects/SourceSeparation_GPU_FX/verify/output/roformer-batch --device auto --max-cached 3`（exit 0，總耗時數秒——checkpoint 已在本機、`ensure_cached` 直接比對 SHA-256 通過，未觸發任何網路下載）：
```text
{
  "model": "roformer-model-melband-roformer-aspiration-by-sucial",
  "cache_verified_sha256": "9e791258c866c6c8da66052693d8cc3b64f1f42c01e052dbdc570cd278380cc5",
  "checkpoint_size": 835983746,
  "separation": {"sample_rate": 48000, "frames": 96000, "channels": 2, "subtype": "FLOAT", "finite": true, "num_outputs": 2},
  "outcome": "pass"
}
{
  "model": "roformer-model-melband-roformer-aspiration-less-aggressive-by-sucial",
  "cache_verified_sha256": "83bfe991cec4fbadde9f30d1f79cd5293ad0b1f936256be327bba5cbb4883374",
  "checkpoint_size": 835982664,
  "separation": {"sample_rate": 48000, "frames": 96000, "channels": 2, "subtype": "FLOAT", "finite": true, "num_outputs": 2},
  "outcome": "pass"
}
```

**Backlog 更新：** 以一次性腳本（tmp+rename 原子寫入，用畢即刪）把 M031／M032 的 `passes` 翻為 `true` 並寫入含指令、sha256、size、separation 規格的 `evidence`；61 → 63 個 backlog item passing。

**已知的滾動快取副作用（沿用 iter-13 修法，非新踩坑）：** batch verify 把 M031/M032 標記為最近使用、`max_cached=3` 觸發 eviction，如預期把 `melband-roformer-kim-vocals` 擠出快取（`verify/roformer-cache/` 驗證只剩 3 個 sucial/aspiration 目錄）。依 LESSONS SIGN (iter 13) 執行 `worker/roformer_cache.py --model melband-roformer-kim-vocals --cache-dir C:/CodexProjects/SourceSeparation_GPU_FX/verify/roformer-cache` 重新觸碰（exit 0，`cache_dir_entries` 確認回到快取內，順帶把仍不完整的 `de-reverb-echo-by-sucial`（M037 部分下載殘留）擠出——該項未過，留給下一輪重新完整下載）。

**Verification:** `cmd //c '.loop\checks\cheap.cmd'`（exit 0）
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s
OK
Ran 2 tests in 0.056s
OK
Ran 8 tests in 0.169s
OK
```
**Scope:** `python .loop/check_scope.py`（exit 0）
```text
[scope] OK — 49 changed path(s) within policy
```
**Backlog checker（本輪非 5 的倍數，non-full-tier，僅供參考）：** exit 1 —— passing 63/111。
**Criteria:** C1: fail（本輪未跑 full tier，不宣稱 pass）· C2: fail（仍有 48 個 M-item 未過）· C3: pass
**Metric:** 63 個 backlog item passing（best so far: 63；improved: true，較上輪 61 增加 2）
**Decision:** continue
**Lesson:** 無新規則——本輪嚴格遵循既有 LESSONS（絕對路徑單引號、禁背景、直接呼叫 htfx-roformer python.exe、批次跑完後確認 kim-vocals 仍在快取）以及 STEER 的風險最小化指令（優先驗證已完整快取、零下載的模型），未觸發新的踩坑。唯一值得記錄的操作性觀察：`ensure_cached` 沒有斷點續傳，任何 `.ckpt.part` 殘留在下次呼叫時會被整檔覆蓋重下，不能省下已下載的部分位元組——這解釋了為何本輪優先選字節數與 manifest size 完全吻合的 M031/M032（真正「零下載」）而非位元組數僅差 ~1% 的 M037（仍會觸發全新完整下載）。
**Note:** 下一輪可考慮先處理 M037（de-reverb-echo-by-sucial，需要完整重新下載 ~836MB，非資源續傳，抓 15 分鐘下載止損）；其後依 checkpoint size 由小到大處理 ~913MB 級距的 gabox/instv6/voc-fv 系列（M004/M006–M014/M022/M023 等 12 個同尺寸模型）。`tools/roformer_batch_verify.py` 與快取續住 `melband-roformer-kim-vocals` 的固定流程持續適用；仍剩 48 個 audited M-item 待驗證，estimé需要多輪、每輪同步完成 1–4 個以符合 60 分鐘 iteration_timeout。

## iter 15 — 2026-08-24T23:20:00+08:00

**Hypothesis:** 本輪無 STEER，依 LOOP_PLAN 常態推進剩餘 48 個 audited M-item。先用 `curl --max-time 60` 對 HuggingFace 做 60 秒吞吐量取樣，量出約 2.67MB/s——遠快於 iter-14 兩次 40 分鐘逾時的情況，推斷當時是行動網路暫時壅塞而非系統性問題。據此依 checkpoint 尺寸由小到大挑選 2 個 audited 模型（M037 de-reverb-echo-by-sucial 836MB、M004 denoise-debleed-gabox 913MB）逐一同步驗證，預期零失敗；若 Bash 工具把長時間下載自動轉為背景執行，一律用 `TaskOutput(block=true)` 反覆阻塞等待到 completed，絕不因此結束 turn（避免重演 iter-13/iter-14 的孤兒程序事故）。

**Files touched:** `.loop/backlog.json`（M037／M004 翻 pass＋evidence）、`.loop/iterations/0015.json`、`.loop/journal.md`、`.loop/state.json`、`.loop/LESSONS.md`（新增 1 條 SIGN）、`.loop/driver.log`（外部 driver 自行 append）。未修改任何 `plugin/`／`cpp/`／`worker/`／`tests/` 原始碼。

**Step 1 — 吞吐量取樣：** `curl -sS -L -o /dev/null --max-time 60 -w '...' https://huggingface.co/Politrees/UVR_resources/.../dereverb-echo_mel_band_roformer_sdr_10.0169.ckpt`（exit 28，逾時屬預期，僅取樣用）：
```text
http_code=200 size_download=160526645 time_total=60.062091 speed_download_Bps=2672678
```
約 2.67MB/s，836MB 全檔預估 5–6 分鐘可下載完成——確認網路目前正常，決定本輪批次為 2 個 audited 模型。

**Step 2 — M037（de-reverb-echo-by-sucial，836MB，全新下載）：** `C:/Users/<user>/anaconda3/envs/htfx-roformer/python.exe tools/roformer_batch_verify.py --models roformer-model-melband-roformer-de-reverb-echo-by-sucial --cache-dir C:/CodexProjects/SourceSeparation_GPU_FX/verify/roformer-cache --fixture C:/CodexProjects/SourceSeparation_GPU_FX/verify/fixtures/test_48k_2s.wav --output-root C:/CodexProjects/SourceSeparation_GPU_FX/verify/output/roformer-batch --device auto --max-cached 3`。此次呼叫被 Bash 工具自動轉為背景執行（未主動要求）；依新教訓改用 `TaskOutput(bskhcyb65, block=true, timeout=600000)` 反覆阻塞等待（第一次逾時仍 running，第二次拿到 completed），全程未結束 turn。exit 0：
```json
{
  "model": "roformer-model-melband-roformer-de-reverb-echo-by-sucial",
  "cache_verified_sha256": "cd2b737a394cfb80cd48cc9fcbaf89f5f4062f6b93066c2911617a06d8b7860a",
  "checkpoint_size": 835997896,
  "separation": {"sample_rate": 48000, "frames": 96000, "channels": 2, "subtype": "FLOAT", "finite": true, "num_outputs": 2},
  "outcome": "pass"
}
```

**Step 3 — M004（denoise-debleed-gabox，913MB，全新下載）：** 同一支腳本改帶 `--models melband-roformer-denoise-debleed-gabox`，這次在前景同步完成（未觸發背景轉移）。exit 0：
```json
{
  "model": "melband-roformer-denoise-debleed-gabox",
  "cache_verified_sha256": "91aa7a546ed2e93482e4629c982d35b0d258bb3de6eeab497fd91658cc86c7fd",
  "checkpoint_size": 913026650,
  "separation": {"sample_rate": 48000, "frames": 96000, "channels": 2, "subtype": "FLOAT", "finite": true, "num_outputs": 2},
  "outcome": "pass"
}
```

**快取狀態：** 兩次下載後 `verify/roformer-cache/` 內為 `melband-roformer-kim-vocals`＋`roformer-model-melband-roformer-de-reverb-echo-by-sucial`＋`melband-roformer-denoise-debleed-gabox`（3 項，`max_cached=3`）；`melband-roformer-kim-vocals` 的 mtime（22:15，iter-14 重新觸碰）比被擠出的兩個 sucial 舊項新，故本輪未被驅逐，無需重新觸碰。

**Backlog 更新：** 以一次性腳本（tmp+rename 原子寫入，用畢即刪）把 M037／M004 的 `passes` 翻為 `true` 並寫入含指令、sha256、size、separation 規格的 `evidence`；63 → 65 個 backlog item passing。

**本輪為 5 的倍數（iteration 15），依協定跑 full tier：**

`cmd //c '.loop\checks\cheap.cmd'`（exit 0）：
```text
default_panel=general ... roformer_browser=99 categories=10 search=true experimental=true download_status=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s
OK
Ran 2 tests in 0.068s
OK
Ran 8 tests in 0.101s
OK
```

`python .loop/check_scope.py`（exit 0）：
```text
[scope] OK — 49 changed path(s) within policy
```

`cmd //c '.loop\checks\full.cmd'`（exit 0，standalone＋4 個 smoke 全 PASS）：
```text
=== ui_configuration_smoke ===
... roformer_browser=99 categories=10 search=true experimental=true download_status=true ... PASS
=== media_io_smoke ===
audio_import=true quick_vocals=true quick_accompany=true raw_stem_unchanged=true mix_controls=true video_import=true mp4_replace_audio=true mp4_bytes=31942 PASS
=== record_mode_smoke (auto/GPU) ===
backend=auto recorded_seconds=1.00426 preview_seconds=1.00426 progress=1 inference_ms=226.121 full_mix_energy=360.007 muted_stem_energy=2.3891 bypass_original_energy=356.806 mix_controls=true status=Ready to preview · htdemucs · GPU PASS=true
=== roformer_smoke ===
roformer_catalog=99 roformer_audited=57 roformer_stems=2 roformer_labels=vocals/instrumental roformer_export_naming=true roformer_sample_rate=44100 roformer_channels=2 roformer_bit_depth=32float roformer_finite=true PASS
```

**Backlog checker（C2）：** `python -c "...sys.exit(any(not i['passes'] for i in json.load(...)))"`（exit 1）—— passing 65/111，仍有 46 個 M-item 未過，C2 未過。

**Criteria:** C1: pass（full tier 全綠，本輪已跑）· C2: fail（46 個 M-item 仍未過）· C3: pass
**Metric:** 65 個 backlog item passing（best so far: 65；improved: true，較上輪 63 增加 2）
**Decision:** continue
**Lesson:** (1) Bash 工具偶爾會在未要求的情況下把長時間指令自動轉為背景執行——這不違反「禁止背景」硬規則本身，正確處理是立刻用 `TaskOutput(block=true)` 反覆阻塞等待到 completed，絕不能因看到「running in background」字樣就結束 turn。(2) 本輪實測 HuggingFace 吞吐量恢復正常（≈2.67MB/s），先前 iter-14 兩次 40 分鐘逾時的根因較可能是當時網路環境暫時性壅塞，非批次大小或程式邏輯問題；建議往後每輪先用 60 秒 `curl --max-time 60` 取樣目前吞吐量，據此動態決定本輪批次大小。已寫入 LESSONS.md。
**Note:** 下一輪應繼續處理剩餘 46 個 audited M-item（均為 ~913MB 級距的 gabox/instv6/voc-fv/kimmel/karaoke 等系列，可依 `tools/roformer_batch_verify.py` 既有流程逐批 2–3 個同步驗證）；下下一輪（iteration 20）將再次是 5 的倍數，記得再跑一次 full tier。批次跑完後、進 cheap/full tier 前，務必確認 `melband-roformer-kim-vocals` 仍在 `verify/roformer-cache/` 內（不在則用 `worker/roformer_cache.py --model melband-roformer-kim-vocals` 重新觸碰）。
