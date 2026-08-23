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
