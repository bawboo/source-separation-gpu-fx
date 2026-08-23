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
