# Loop journal — ui-language-and-defaults

Plan approved: 2026-08-25T11:45+08:00（使用者三問確認後「沿用配置直接開跑」）
Origin: loop/melband-roformer @ 8223700526be4d3df5a92ed092c6b3e9f15f7664 · Loop branch: loop/ui-language
前輪（melband-roformer-integration）已收斂歸檔：.loop-archive-20260825-1141/（LESSONS.md 延續有效）
Budget: max_iterations=30 · stall=5 · iteration_timeout=90m（2026-08-25 修訂：CMakeLists 變更後全量重建每循環 15-20 分鐘，60m 兩度不足）

---

## iter 1 — 2026-08-25T20:29:43+08:00

**STEER（一次性指令，13:25 投放，本輪套用後刪除）：** 上一輪（被 60 分鐘 ceiling 終止）留下未 commit 的繼承工作：plugin/Localization.h/.cpp（新檔）＋ CMakeLists.txt、plugin/PluginProcessor.cpp、tests/ui_configuration_smoke.cpp 修改。指示：(1) 先跑 cheap tier 建立繼承基準；(2) 本輪目標縮小為讓繼承的 Localization 模組「編譯通過＋cheap tier 綠」即可，有 record 有 commit 就收；(3) L1 剩餘接線與 L2 全量字串留給後續輪次。

**Hypothesis：** 繼承的 Localization 模組（zh-TW/en 字串表單例 + settingsFile() 持久化 + languageButton_ 切換控件 + applyLocalizedStrings() 接上 18 個既有靜態字串）與已擴充的 ui_configuration_smoke 斷言（預設 zh-TW／toggle 即時切換／持久化／重開 editor 還原）之間彼此一致、可編譯，工作樹是被 hang guard 中斷、不是邏輯錯誤——驗證即可直接 commit，作為 L1 的第一個子里程碑而不需要新寫程式碼。

**Files touched（繼承，本輪僅驗證＋commit，未新增修改）：** plugin/Localization.h（新檔）、plugin/Localization.cpp（新檔）、CMakeLists.txt（target_sources 加兩檔）、plugin/PluginProcessor.cpp（languageButton_＋applyLocalizedStrings()＋18 處靜態字串改查表＋佈局調整）、tests/ui_configuration_smoke.cpp（HTFX_UI_LANGUAGE_FILE 隔離＋4 項新斷言＋PASS 訊息追加欄位）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.002s OK / Ran 2 tests in 0.076s OK / Ran 8 tests in 0.108s OK
```
新增的四項語言斷言（zh-TW 預設／languageToggle 找得到且文字為 EN／toggle 後 getLanguage()==en 且 resetWorker_ 標籤即時變回 "Reset worker"／languageSettingsFile 內容為 "en"／重開 editor 後 resetWorker 仍是英文）全數在這次執行中通過，證據即上方 PASS 行的 `language_default=zh-TW language_toggle=true language_persist_reopen=true` 三個欄位。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 55 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail（未產出本輪證據），C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。
**Metric：** backlog_items_passing = 1（較上一輪 0 增加 1：L1 轉 true）；improved: true。
**Backlog：** L1 → passes=true，evidence 見 backlog.json（cheap tier 上述 PASS 行）。L2–L7 維持 false，留給後續輪次（每輪一個子里程碑，依 SIGN loop2 iter-1）。
**Decision：** continue（status 保持 running，stop_reason null）。
**Lesson：** 無新失敗——假設成立：中斷的繼承工作樹是被 hang guard 打斷而非邏輯錯誤，直接驗證＋commit 即可推進一個子里程碑，不需要重寫。STEER.md 已依協定在寫入本篇 journal 後刪除。

---

## iter 2 — 2026-08-25T20:48:16+08:00

**Hypothesis：** 承接 iter 1（L1 語言基礎架構），L2（全部 UI 文字雙語化）拆一個子里程碑：把主控制面板一批仍是英文字面值的控件（Advanced options/Advanced panel/Full screen/Scale UI 四個切換按鈕、Import/Export/Export Vocals only/Export Accompany only 按鈕、operatingMode 選單、separationModeBox 固定項目、RoFormer「All categories」／搜尋框預留文字、record/play-pause/download-model 按鈕）接上 htfx::tr()，應該能在不破壞 L1 既有斷言的前提下讓 cheap tier 保持綠燈，因為切換態按鈕（record/play-pause/download-model）本來就靠 timerCallback() 逐幀刷新，直接改成 tr() 呼叫即可；真正需要新邏輯的只有三個只在建構式／onClick 才設值的切換按鈕（panelSwitchButton_/advancedButton_/fullScreenButton_），為此新增三個 helper（updatePanelSwitchButtonText/updateAdvancedButtonText/updateFullScreenButtonText）並在 applyLocalizedStrings() 呼叫，讓語言切換當下也能重算正確的目前狀態文字。

**Files touched：** plugin/Localization.cpp（新增 24 個字串鍵：button.exportVocalsOnly/exportAccompanyOnly/import/export/scaleUi/record/stopRecording/play/pause/modelInstalled/modelDownloading/downloadModel/fullScreen/exitFullScreen/advancedOptionsExpand/advancedOptionsCollapse/advancedPanel/generalPanel，combo.separationModePlaceholder/separationMode4Stem/separationMode6Stem/modeRecord/modeRealtime/roformerAllCategories，placeholder.roformerSearch，label.noMediaSelected）、plugin/PluginProcessor.cpp（上述控件的建構式／timerCallback()／onClick 呼叫點改用 htfx::tr()；新增三個 helper 並接上 applyLocalizedStrings()）、tests/ui_configuration_smoke.cpp（既有 B1–B3/D2/D3 等既定行為斷言原本用英文字面值 findButton/findCombo/getItemText 尋找這批控件，因為預設語言已是 zh-TW 而全部失敗——改成呼叫 htfx::tr(...) 取同一把字串表，手法與 L1 既有 resetWorker_ 斷言一致，非放寬測試）。

**Verification — cheap tier（第一次，修字串前先接完控件，協定 step 6）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 1，建置成功但執行期輸出：
```text
ui_configuration_smoke fatal: required UI controls were not found
```
根因：既有斷言仍用英文字面值尋找剛被在地化的控件，預設語言 zh-TW 下找不到（編譯期無錯誤，執行期才現形）。

**Verification — cheap tier（修完測試斷言後，第二次）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s OK / Ran 2 tests in 0.081s OK / Ran 8 tests in 0.091s OK
```
PASS 行本身沒有新增獨立欄位（這批屬既有行為斷言的翻譯接線，不是新斷言），證據是這些既定行為斷言（B1–B3/D2/D3 等）在 zh-TW 預設語言下全數維持通過，代表翻譯字串接線正確且未破壞既有邏輯。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（iteration 2 非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail，C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** 新增 discovered 項目 L2a（本輪這批控件雙語化子里程碑）passes=true，evidence 見 backlog.json。L2 本身（全部 UI 文字雙語化）仍為 false——segment/model/compute 選單、狀態列／錯誤訊息、StemExportDialog、RoFormer 目錄動態分類名稱等尚未接線，留給後續輪次。L1 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 2（較上一輪 1 增加 1：L2a 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** 承接 L1 的既有教訓——任何把一個字面值英文控件改成 htfx::tr() 接線，都必須同步檢查 tests/ui_configuration_smoke.cpp 裡是否有既有斷言用同一段英文字面值透過 findButton/findCombo/getItemText 尋找或比對該控件；因為預設語言已是 zh-TW，這類舊斷言會在編譯成功後於執行期以「required UI controls were not found」或字串比對失敗的方式現形，而非編譯期錯誤——這不是新教訓（SIGN loop2 wiring 已預告），但本輪是第一次實際踩到，值得記錄具體徵狀（exit 1 且訊息是 fatal required UI controls were not found）供下一輪快速辨識同類問題。
