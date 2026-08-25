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
