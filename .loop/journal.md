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

## iter 3 — 2026-08-25T20:57:51+08:00

**Hypothesis：** 承接 iter 1/2（L1 語言基礎架構、L2a 主控制面板控件批次），L2（全部 UI 文字雙語化）再拆一個自包子里程碑：StemExportDialog（`plugin/PluginProcessor.cpp` 內 `ExportDialogContent` 類別）整體接上 `htfx::tr()`。此類別是每次點擊「Export」開啟對話框時才重新建構（非常駐元件），預期不需要像 L2a 三個切換按鈕那樣額外寫 `applyLocalizedStrings()` 重算 helper；且 `tests/ui_configuration_smoke.cpp` 從未對此類別的任何英文字面值做過斷言，預期零既有測試需要同步修改，可在低風險下完成一個完整 UI 區塊。

**Files touched：** `plugin/Localization.cpp`（新增 14 個字串鍵：`dialog.exportChooseTitle`／`dialog.exportSelectedStems`／`dialog.exportAllStems`／`dialog.exportMix`／`dialog.close`／`dialog.audioOnlyWav`／`dialog.videoWithMixedAudio`／`dialog.noteVideoExport`／`dialog.noteStemExport`／`alert.noStemsSelectedTitle`／`alert.noStemsSelectedMessage`／`filechooser.stemFolderTitle`／`filechooser.exportVideoWithMix`／`filechooser.exportMix`）、`plugin/PluginProcessor.cpp`（`ExportDialogContent` 建構式內 `title_`/`selectedButton_`/`allButton_`/`mixButton_`/`closeButton_`/`audioOnlyButton_`/`videoButton_`/`note_` 的字面值改 `htfx::tr(...)`；`chooseStemFolder()` 的 `AlertWindow::showMessageBoxAsync` 警告標題/訊息與 `juce::FileChooser` 資料夾選擇標題改 `htfx::tr(...)`；`chooseMixFile()` 的 `juce::FileChooser` 儲存標題（依 video/audio 分支）改 `htfx::tr(...)`）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.002s OK / Ran 2 tests in 0.051s OK / Ran 8 tests in 0.098s OK
```
PASS 行沒有新增獨立欄位（StemExportDialog 本身在 `ui_configuration_smoke.cpp` 中無任何既有斷言路徑覆蓋），假設驗證屬實：全部既有斷言在建置成功後維持全綠，代表本批次翻譯字串接線正確、未破壞任何邏輯；本批次的「雙語化本身」正確性靠原始碼審查（14 個鍵全數在 `ExportDialogContent` 建構式與兩個 `chooser_` 建立點被引用，無遺漏字面值）與 build 成功（模板/型別皆正確）佐證。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（iteration 3 非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail，C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** 新增 discovered 項目 L2b（StemExportDialog 雙語化子里程碑）passes=true，evidence 見 backlog.json。L2 本身仍為 false——segment/model/compute 選單描述文字、狀態列（`status_`/`metrics_`/`roformerStatus_`/`cpuWarning_`）、其餘 `AlertWindow` 錯誤訊息、`getStateDisplayName()`/`getResolvedDeviceName()` 等尚未接線，留給後續輪次。L1、L2a 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 3（較上一輪 2 增加 1：L2b 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** 承接 SIGN loop2 iter-1 的批次拆分策略——優先挑選「自包、無既有測試斷言覆蓋」的 UI 區塊（例如本輪的模態對話框類別，每次開啟才重新建構、不需要額外的即時刷新 helper）作為 L2 子里程碑，能在同一輪內完成「新增字串鍵＋接線＋驗證＋commit」而不需要同時處理 L2a 那類「切換態按鈕需要額外 helper 重算文字」或「舊斷言用英文字面值查找控件」的複雜度；下一輪可以往狀態列／錯誤訊息／`getStateDisplayName()` 這類分散在多處呼叫點、且已被至少一個既有斷言引用的字串移動，屆時要重新套用 iter 2 學到的「同步檢查 `tests/ui_configuration_smoke.cpp` 既有斷言」步驟。

---

## iter 4 — 2026-08-25T21:15:00+08:00

**Hypothesis：** 承接 iter 1–3（L1 語言基礎架構、L2a 主控制面板、L2b StemExportDialog），L2（全部 UI 文字雙語化）再拆一個自包子里程碑：`plugin/PluginProcessor.cpp` 匿名命名空間內三個媒體匯入/匯出自由函式（`runFfmpeg()`／`readAudioFileAtProjectRate()`／`writeFloatWav()`）的錯誤與診斷字面值接上 `htfx::tr()`。因為 `htfx::tr()` 是行程全域單例的自由函式（非類別成員方法），預期在匿名命名空間的自由函式內同樣可直接呼叫；且 `tests/ui_configuration_smoke.cpp` 與 `media_io_smoke.cpp` 都只斷言檔案是否成功產生等行為結果、從未比對這些錯誤訊息的確切文字內容，預期零既有斷言需要同步修改，可在低風險下完成，cheap tier 保持綠燈。

**Files touched：** `plugin/Localization.cpp`（新增 16 個 `error.*` 字串鍵：`error.ffmpegNotStarted`／`error.mediaOperationCancelled`／`error.ffmpegFailedPrefix`＋`error.ffmpegFailedSuffix`（結束碼數字嵌入句中，拆成 prefix/suffix 兩鍵）／`error.unsupportedAudioFile`／`error.mediaDurationInvalid`／`error.audioStreamDecodeFailed`／`error.audioStreamNoSampleRate`／`error.resampledAudioTooLarge`／`error.noAudioToExport`／`error.couldNotCreateOutputFolder`／`error.couldNotOpenOutputFile`／`error.couldNotCreateWavWriter`／`error.wavWriteFailed`／`error.couldNotReplaceExistingFile`／`error.couldNotCommitOutputFile`）、`plugin/PluginProcessor.cpp`（`runFfmpeg()` 的啟動失敗／操作取消／失敗含結束碼三處，`readAudioFileAtProjectRate()` 的不支援檔案／時長無效／解碼失敗／取樣率無效／重取樣過大五處，`writeFloatWav()` 的無音訊可匯出／建立資料夾失敗／開啟檔案失敗／建立 WAV 寫入器失敗／寫入失敗／取代既有檔失敗／提交輸出檔失敗七處，共 16 處字面值全數改 `htfx::tr(...)`；含動態值的訊息用 `htfx::tr(prefix/前綴含尾隨冒號) + 動態值` 或 `htfx::tr(prefix) + 動態值 + htfx::tr(suffix)` 組回完整句子）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s OK / Ran 2 tests in 0.044s OK / Ran 8 tests in 0.118s OK
```
PASS 行沒有新增獨立欄位（這批錯誤/診斷訊息在 `ui_configuration_smoke.cpp`／`media_io_smoke.cpp` 中皆無任何既有斷言路徑覆蓋），假設驗證屬實：全部既有斷言在建置成功後維持全綠，代表本批次翻譯字串接線正確、未破壞任何邏輯；本批次的「雙語化本身」正確性靠原始碼審查（16 個鍵全數在三個函式內被引用，`grep -n 'diagnostics = "\|error = "' plugin/PluginProcessor.cpp` 確認該區塊零殘留英文字面值）與 build 成功佐證。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（iteration 4 非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail，C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** 新增 discovered 項目 L2c（媒體匯入/匯出錯誤訊息雙語化子里程碑）passes=true，evidence 見 backlog.json。L2 本身仍為 false——狀態列訊息（`setSeparationMessage()` 約 20 處呼叫點，散布在錄音/HTDemucs/RoFormer 分離流程）、`getStateDisplayName()`/`getResolvedDeviceName()` 等尚未接線，留給後續輪次。L1、L2a、L2b 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 4（較上一輪 3 增加 1：L2c 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** `Localization::tr()` 目前沒有樣板/佔位符替換機制，遇到句子中間需要嵌入動態值（例如本輪的 FFmpeg 結束碼、檔名）時，可行做法是把該句拆成 prefix/suffix 兩個獨立 key（結束碼案例：`error.ffmpegFailedPrefix`／`error.ffmpegFailedSuffix`，呼叫端 `htfx::tr(prefix) + 動態值 + htfx::tr(suffix)`），或當動態值只出現在句尾時單一 key 即可（value 本身含尾隨冒號/空白，呼叫端直接 `htfx::tr(key) + 動態值`）。下一個子里程碑（`setSeparationMessage()` 狀態列訊息，例如 `"Loading RoFormer " + modelName + " on " + device`）會大量遇到同樣情境，可直接套用此模式；同時也確認了 `htfx::tr()` 是行程全域單例的自由函式，匿名命名空間內非類別成員的自由函式（本輪的 `runFfmpeg()`/`readAudioFileAtProjectRate()`/`writeFloatWav()`）同樣可直接呼叫，不需要額外傳遞或建構。

---

## iter 5 — 2026-08-25T22:05:00+08:00

**Hypothesis：** 承接 iter 1–4（L1 語言基礎架構、L2a 主控制面板、L2b StemExportDialog、L2c 媒體匯入/匯出錯誤訊息），L2（全部 UI 文字雙語化）再拆一個子里程碑：`plugin/PluginProcessor.cpp` 內全部 26 個 `setSeparationMessage()` 呼叫點（`beginRecording()`/`endRecording()`/`beginSeparation()`/`cancelSeparation()`/`separationLoop()` 的錄音、模型未安裝、Demucs/RoFormer worker 啟動與進度、分離取消、分離完成預覽等狀態訊息）以及 `importMediaLoop()` 內與之成對出現的 4 個 `setMediaMessage()` 呼叫點（匯入中/取消/完成/失敗）接上 `htfx::tr()`。因為既有測試（`ui_configuration_smoke`/`media_io_smoke`/`record_mode_smoke`/`roformer_smoke`）皆未對這些狀態訊息的確切文字內容做斷言（`record_mode_smoke` 雖印出 `status` 欄位但只做行為判斷、不比對其值），預期零既有斷言需要同步修改；含動態值的句子沿用 iter 4 確立的 prefix/suffix 拆分模式，其中一句需要同時嵌入模型名稱與裝置名稱兩個動態值，新增 prefix/middle/suffix 三段式鍵處理。因本輪迭代編號（5）是 5 的倍數，依協定（step 6）本輪同時執行 full tier 與 backlog checker，而非僅 cheap tier。

**Files touched：** `plugin/Localization.cpp`（新增 34 個 `status.*` 字串鍵，含 8 組 prefix/suffix 拆分鍵與 1 組 prefix/middle/suffix 三段式鍵 `status.loadingModelDeviceMiddle`）、`plugin/PluginProcessor.cpp`（`beginRecording()`/`endRecording()`/`beginSeparation()`/`cancelSeparation()`/`separationLoop()`（HTDemucs 與 RoFormer 兩條路徑）/`importMediaLoop()` 內全部字面值改 `htfx::tr(...)`；`worker.lastError()`／`std::exception::what()`／已在 L2c 本地化的 `error`/`ffmpegError` 等變數傳遞路徑維持原樣，未新增字串鍵；`CPU`/`GPU`/`CUDA GPU`/`cuda:N`/`auto` 等裝置識別字與 GPU 名稱維持英文原樣，比照模型 ID 的既有慣例視為技術性專有名詞不翻譯）、`.loop/backlog.json`（新增 discovered 項目 L2d passes=true 與 L2e passes=false）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s OK / Ran 2 tests in 0.078s OK / Ran 8 tests in 0.095s OK
```

**Verification — full tier（本輪迭代編號 5 為 5 的倍數，依協定執行）：** `cmd //c '.loop\checks\full.cmd'` → exit 0：
```text
=== ui_configuration_smoke ===
...(欄位與上方 cheap tier 輸出相同) PASS
=== media_io_smoke ===
audio_import=true quick_vocals=true quick_accompany=true raw_stem_unchanged=true mix_controls=true video_import=true mp4_replace_audio=true mp4_bytes=31942 PASS
=== record_mode_smoke (auto/GPU) ===
backend=auto recorded_seconds=1.00426 preview_seconds=1.00426 progress=1 inference_ms=185.304 full_mix_energy=360.007 muted_stem_energy=2.3891 bypass_original_energy=356.806 mix_controls=true status=準備預覽 · htdemucs · GPU PASS=true
=== roformer_smoke ===
roformer_catalog=99 roformer_audited=57 roformer_stems=2 roformer_labels=vocals/instrumental roformer_export_naming=true roformer_sample_rate=44100 roformer_channels=2 roformer_bit_depth=32float roformer_finite=true PASS
```
四個 smoke 全數 PASS；`record_mode_smoke` 印出的 `status=準備預覽 · htdemucs · GPU` 是本輪新翻譯的 `status.readyToPreviewPrefix`（"準備預覽 · "）在一次真實（fake-worker 停用、`--cpu` 未指定，走 auto/GPU 路徑）分離流程中實際產生的訊息，直接證明本輪翻譯接線在端到端流程中確實生效，而非僅通過編譯。

**Backlog checker：** `python -c "import json,sys; sys.exit(any(not i['passes'] for i in json.load(open('.loop/backlog.json',encoding='utf-8'))))"` → exit 1（預期：L2 本身、L2e、L3–L7 仍為 false，AND 規則下尚未全數轉綠）。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Criteria：** C1 pass（full tier 本輪執行且 exit 0）／C2 fail（backlog checker exit 1，L2/L2e/L3–L7 仍有 false 項）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** 新增 discovered 項目 L2d（分離/錄音/RoFormer 狀態列訊息＋媒體匯入 setMediaMessage 雙語化子里程碑）passes=true，evidence 見 backlog.json。同時新增 discovered 項目 L2e（匯出流程 `setMediaMessage()` 訊息雙語化，StemExportDialog 驗證/匯出所選/快速匯出/匯出混音/匯出取消等 14 處呼叫點）passes=false，留給下一輪——本輪掃描 `setSeparationMessage`/`setMediaMessage` 全部呼叫點時發現的、但不屬於本輪「一個子里程碑」範圍的工作，依協定 append 為新項目而非併入本次變更集。L2 本身仍為 false（等 L2e 與尚未盤點到的其他英文字面值全部接線後才會轉綠）。L1、L2a、L2b、L2c 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 5（較上一輪 4 增加 1：L2d 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** 當一句話需要同時嵌入兩個動態值時（本輪 `"Loading " + modelName + " on " + device + " · first load can take a while"`），iter 4 的 prefix/suffix 兩段式 key 不夠用——可自然延伸為 prefix/middle/suffix 三段式 key（`status.loadingModelPrefix` + modelName + `status.loadingModelDeviceMiddle` + device + `status.loadingModelDeviceSuffix`），呼叫端仍是單純字串相加，`Localization::tr()` 本身不需要任何改動。另外，掃描 `setSeparationMessage()`/`setMediaMessage()` 全部呼叫點時会一次看到同一類但語意上分屬不同子系統的字面值（本例：`setSeparationMessage()` 26 處 vs. 匯出流程專屬的 `setMediaMessage()` 14 處）——正確作法是把後者記錄成新的 discovered backlog 項目（本輪為 L2e）而非順手一起做掉，即使兩者程式碼手法完全相同；這能維持「一輪一個 hypothesis」的可歸因性（協定 step 4／Hard rules 最後一條）。

---

## iter 6 — 2026-08-25T22:35:00+08:00

**Hypothesis：** 承接 iter 1–5（L1 語言基礎架構、L2a 主控制面板、L2b StemExportDialog、L2c 媒體匯入/匯出錯誤訊息、L2d 分離/錄音/RoFormer 狀態列訊息），L2（全部 UI 文字雙語化）再拆下一個自包子里程碑：iter 5 discovered 的 L2e——匯出流程（`beginMediaImport()`/`cancelMediaOperation()`/`beginStemExport()`/`stemExportLoop()`/`beginQuickExport()`/`quickExportLoop()`/`beginMixExport()`/`mixExportLoop()`）內全部 15 個純字面值（不含執行期動態值串接，含 2 組僅二選一分支的三元運算式）`setMediaMessage()` 呼叫點接上 `htfx::tr()`。因為這些呼叫點皆為靜態或二選一分支字串（沿用 L2a 已驗證過的三元運算式模式，例如 `advancedButton_` 的展開/收合文字），且 `tests/ui_configuration_smoke.cpp`／`media_io_smoke.cpp` 皆未對這些訊息的確切文字內容做斷言，預期零既有斷言需要同步修改；含動態值串接的 `setMediaMessage()` 呼叫點（`error` 變數、`exception.what()`、檔名/路徑/數量內嵌值）刻意排除在本輪範圍外、記錄為新 discovered 項目 L2f，維持一輪一個 hypothesis 的可歸因性。本輪迭代編號（6）非 5 的倍數，依協定僅需 cheap tier。

**Files touched：** `plugin/Localization.cpp`（新增 18 個 `status.*` 字串鍵：`status.cancellingMediaOperation`／`status.switchToRecordModeBeforeImport`／`status.selectedMediaFileNotFound`／`status.separateBeforeExportingStems`／`status.selectAtLeastOneStemToExport`／`status.exportingOriginalVolumeStems`／`status.stemExportCancelled`／`status.separateBeforeQuickExport`／`status.quickExportRequiresHtdemucs`／`status.chooseDifferentOutputNameProtected`（於 `beginQuickExport()` 與 `beginMixExport()` 兩處共用）／`status.exportingVocalsOriginalLevel`／`status.exportingAccompanyOriginalLevel`／`status.quickExportCancelled`／`status.separateBeforeExportingMix`／`status.videoExportRequiresImportedVideo`／`status.mixingReplacingVideoAudio`／`status.exportingCurrentInterfaceMix`／`status.mixExportCancelled`）、`plugin/PluginProcessor.cpp`（上述 8 個函式內共 15 處字面值 `setMediaMessage(...)` 呼叫改 `htfx::tr(...)`，其中 2 處三元運算式的兩個分支各自改指向不同 key）、`.loop/backlog.json`（L2e 由 `passes:false` 翻為 `true`＋補上 evidence；新增 discovered 項目 L2f，`passes:false`，記錄剩餘 9 個含動態值串接的 `setMediaMessage()` 呼叫點行號）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.003s OK / Ran 2 tests in 0.044s OK / Ran 8 tests in 0.075s OK
```
PASS 行沒有新增獨立欄位（這批訊息在 `ui_configuration_smoke.cpp`／`media_io_smoke.cpp` 中皆無既有斷言路徑覆蓋），全部既有斷言在建置成功後維持全綠，代表本批次翻譯字串接線正確、未破壞任何邏輯；本批次「雙語化本身」正確性靠原始碼審查佐證——`grep -n 'setMediaMessage("' plugin/PluginProcessor.cpp` 於本輪變更後回傳零筆，確認範圍內 15 個純字面值呼叫點已全數清空，僅剩已本地化的 `htfx::tr(...)` 呼叫、變數傳遞（`error`／`message`／`ffmpegError`，L2c/L2d 已本地化或屬外部例外訊息）、以及本輪刻意排除的 9 個含動態值串接呼叫點（記錄為 L2f）。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（iteration 6 非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail，C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** L2e 由 `passes:false` 翻為 `true`（見上）。新增 discovered 項目 L2f（匯出流程含動態值串接的 `setMediaMessage()` 訊息雙語化，9 處呼叫點），`passes:false`，留給下一輪。L2 本身仍為 false（等 L2f 與尚未盤點到的其他英文字面值全部接線後才會轉綠）。L1、L2a、L2b、L2c、L2d 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 6（較上一輪 5 增加 1：L2e 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** L2a 已驗證過的「三元運算式二選一分支、無動態值內嵌」模式（例如按鈕文字依開合狀態切換）可以直接套用到 `setMediaMessage()` 這類狀態訊息——只要兩個分支都是完整靜態字串（本輪的 `kind == QuickExportKind::vocals ? ... : ...`／`replaceVideoAudio ? ... : ...`），做法與單一字面值呼叫點完全一致（各自對應一個獨立 key），不需要 iter 4/5 那套 prefix/suffix 拆分機制；prefix/suffix（或 prefix/middle/suffix）機制只在句子中間真的需要嵌入執行期字串/數值時才需要。掃描同一類 setter 的全部呼叫點時，若同時看到「純字面值／二選一分支」與「含動態值串接」兩種複雜度截然不同的呼叫點混在一起（本輪：15 處純字面值 vs. 9 處含動態值），繼續維持 iter 5 已確立的作法——只做前者，後者記錄成新的 discovered backlog 項目（本輪為 L2f），不要因為函式相鄰、程式碼手法相近就一起做掉。

## iter 7 — 2026-08-25T21:37:10+08:00

**Hypothesis：** 承接 iter 6（L2e：匯出流程 15 個純字面值 `setMediaMessage()` 呼叫點），本輪處理當時刻意排除、記錄為 discovered 項目 L2f 的剩餘 9 個含動態值串接呼叫點——`stemExportLoop()` 匯出完成訊息（數量＋路徑兩個動態值）與例外訊息、`quickExportLoop()` 匯出完成訊息（Vocals/Accompany 二選一分支＋路徑）與例外訊息、`mixExportLoop()` 的 WAV 匯出完成訊息、MP4 混流失敗訊息（`error` 變數＋靜態 suffix）、無法取代既有 MP4 輸出檔訊息、MP4 匯出完成訊息、例外訊息。沿用 iter 4/5 確立的 prefix/middle/suffix 拆分模式（動態值在句尾用 prefix 鍵；動態值在句首、後接靜態文字用 suffix 鍵；句中同時有兩個動態值用 prefix/middle 兩段式）；二選一分支（Vocals/Accompany）沿用 iter 6 已驗證過的「兩個分支各自獨立 key」模式。`tests/ui_configuration_smoke.cpp`／`media_io_smoke.cpp` 皆未對這些訊息的確切文字內容做斷言，預期零既有斷言需要同步修改。本輪迭代編號（7）非 5 的倍數，依協定僅需 cheap tier。

**Files touched：** `plugin/Localization.cpp`（新增 11 個 `status.*` 字串鍵：`status.stemExportSuccessPrefix`／`status.stemExportSuccessMiddle`（兩段式，供匯出數量＋輸出資料夾路徑兩個動態值嵌入）、`status.stemExportFailedPrefix`、`status.quickExportedVocalsPrefix`／`status.quickExportedAccompanyPrefix`（二選一分支各自獨立鍵）、`status.quickExportFailedPrefix`、`status.mixExportedPrefix`、`status.mp4StreamCopyIncompatibleSuffix`（單一 suffix 鍵，直接接在既有 `error` 變數之後，無對應 prefix 鍵——因為原始字面值本身就是「變數在前、靜態文字在後」）、`status.couldNotReplaceMp4OutputPrefix`、`status.mixExportedMp4Prefix`、`status.mixExportFailedPrefix`）、`plugin/PluginProcessor.cpp`（`stemExportLoop()`/`quickExportLoop()`/`mixExportLoop()` 內全部 9 個含動態值串接的 `setMediaMessage()` 呼叫點改接 `htfx::tr(...)` 前後綴，`error`／`exception.what()`／`getFullPathName()`／`sourceIndices.size()` 等執行期值維持原樣拼接、不變）、`.loop/backlog.json`（L2f 由 `passes:false` 翻為 `true`＋補上 evidence）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.004s OK / Ran 2 tests in 0.057s OK / Ran 8 tests in 0.116s OK
```
PASS 行沒有新增獨立欄位（這批訊息在 `ui_configuration_smoke.cpp`／`media_io_smoke.cpp` 中皆無既有斷言路徑覆蓋），全部既有斷言在建置成功後維持全綠，代表本批次翻譯字串接線正確、未破壞任何邏輯；本批次「雙語化本身」正確性靠原始碼審查佐證——`grep -n 'setMediaMessage("' plugin/PluginProcessor.cpp` 於本輪變更後回傳零筆（exit 1，無匹配），確認 `setMediaMessage()` 全部呼叫點（含 L2e 已清空的純字面值呼叫點）已無任何內嵌英文字面值，僅剩 `htfx::tr(...)` 呼叫與變數傳遞。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（iteration 7 非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail，C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** L2f 由 `passes:false` 翻為 `true`（見上）。L2 本身仍為 false——`setMediaMessage()` 呼叫點雖已全數清空英文字面值，但 L2 涵蓋範圍更廣（控件標籤/選單/狀態列/錯誤訊息/按鈕全體），尚待下一輪盤點 `setSeparationMessage()`/`setMediaMessage()` 以外是否還有遺漏的英文字面值（例如 JUCE 內建對話框、tooltip、或其他尚未掃描過的 UI 元件）才能轉綠。L1、L2a、L2b、L2c、L2d、L2e 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 7（較上一輪 6 增加 1：L2f 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** 「動態值在句首、句尾接靜態文字」（例如本輪 `error + " (The source video codec may not be compatible...)"`）只需要一個 suffix 鍵、不需要對應的 prefix 鍵——因為變數本身已經在句首，呼叫端寫法就是 `error + htfx::tr(suffix)`，與 iter 4/5「靜態文字在句首、動態值在句尾」時只需要 prefix 鍵（`htfx::tr(prefix) + value`）是對稱的鏡像情況。掃描含動態值串接的呼叫點時，應先判斷「動態值在句子的哪個位置」（純句尾／純句首／句中兩側都有靜態文字／句中有兩個動態值），再決定要拆 prefix-only、suffix-only、還是 prefix/middle（/suffix）——不要預設每個含動態值的呼叫點都需要完整的三段式拆分。

---

## iter 8 — 2026-08-25T22:05:00+08:00

**Hypothesis：** 承接 iter 1–7（L1、L2a–L2f 全數 pass），本輪盤點 `setSeparationMessage()`/`setMediaMessage()` 以外、`HTDemucsGpuFXEditor` 類別層級尚未接線的英文字面值——用 grep 掃過 `setButtonText`/`setText`/`addItem`/`AlertWindow::show`/`setTitle` 等呼叫模式後，找到四群不同性質的缺口：(1) `chooseMediaFile()`/`chooseQuickExportFile()`/`showExportDialog()` 內 6 個 AlertWindow/FileChooser/DialogWindow 呼叫點（零既有斷言覆蓋，可直接做）；(2) `refreshRoformerBrowser()`/`updateRoformerStatus()` 的 RoFormer 瀏覽器狀態文字（`tests/ui_configuration_smoke.cpp` 有 4 處既有英文斷言直接比對，需同步改斷言）；(3) `segmentBox_`/`computeBox_` 選單項目（測試用文字內容尋找元件、且多處直接比對選項文字，需先補 `setName()` 才能安全改斷言）；(4) `separationModeBox_` 12 個模式項目與 stem slider 標籤（需要 RoFormer 類別→雙語顯示名稱翻譯表，且與 L4/L5 預設模式高度相關）。依協定「一輪一個 hypothesis」與 iter 5/6 已確立的複雜度分流原則，本輪只做 (1)（零既有斷言、風險最低、範圍明確自洽），(2)(3)(4) 記錄為新 discovered 項目 L2h/L2i/L2j 留給後續輪次，且在各自 evidence 中預先寫明「為什麼需要同步改測試」以降低下一輪的偵察成本。

**Files touched：** `plugin/Localization.cpp`（新增 10 個字串鍵：`filechooser.importMediaTitle`／`filechooser.exportVocalsTitle`／`filechooser.exportAccompanyTitle`／`alert.importMediaFirstTitle`／`alert.importMediaFirstMessage`／`alert.defaultModelMissingTitle`／`alert.defaultModelMissingMessage`／`alert.nothingToExportTitle`／`alert.nothingToExportMessage`／`dialog.exportStemsOrMixTitle`）、`plugin/PluginProcessor.cpp`（`HTDemucsGpuFXEditor::chooseMediaFile()`/`chooseQuickExportFile()`/`showExportDialog()` 內全部 6 個呼叫點改 `htfx::tr(...)`：FileChooser 建構子標題參數 ×3、`AlertWindow::showMessageBoxAsync` 的 title/message 各 ×2 組、`DialogWindow::LaunchOptions::dialogTitle` ×1；這些對話框皆為使用者觸發時動態建構的一次性物件，不需要 `applyLocalizedStrings()` 式的即時刷新 helper，`tr()` 在每次呼叫時即時取值即可反映當下語言）、`.loop/backlog.json`（新增 discovered 項目 L2g `passes:true`＋evidence，並新增 L2h／L2i／L2j 三個 `passes:false` 的 discovered 項目，各自記錄範圍與「為何需要同步改測試」）。

**Verification — cheap tier（協定 step 6，每輪必跑）：** `cmd //c '.loop\checks\cheap.cmd'` → exit 0：
```text
default_panel=general quick_exports=vocals/accompany default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true record_button=red media_buttons=true proportional_scale=true fullscreen_toggle=true roformer_cpp_route=true roformer_stems=2 roformer_seconds=2 roformer_browser=99 categories=10 search=true experimental=true download_status=true separation_mode_gate=true separation_modes=12 separation_mode_defaults=true separation_mode_stem_gating=true separation_mode_all_categories_verified=true stem_slider_relabels=true roformer_stem_labels=vocals/instrumental roformer_stem_label_categories=8 roformer_export_naming=true language_default=zh-TW language_toggle=true language_persist_reopen=true PASS
roformer manifest: 99 models, 57 audited, 42 experimental PASS
Ran 1 test in 0.009s OK / Ran 2 tests in 0.134s OK / Ran 8 tests in 0.328s OK
```
PASS 行沒有新增獨立欄位（這批對話框在 `ui_configuration_smoke.cpp`／`media_io_smoke.cpp` 中皆無既有斷言路徑覆蓋——事先以 grep 逐一確認 `"Import media first"`／`"Nothing to export"`／`"Export HTDemucs stems or mix"`／`"Import an audio or video file"`／`"Export vocals"`／`"Export accompaniment"`／`"Default model is missing"` 在 `tests/` 內零匹配），全部既有斷言在建置成功後維持全綠，代表本批次翻譯字串接線正確、未破壞任何邏輯；本批次「雙語化本身」正確性靠原始碼審查＋grep 佐證——`grep -n '"Import media first"\|"Nothing to export"\|"Export HTDemucs stems or mix"\|"Import an audio or video file"\|"Export vocals"\|"Export accompaniment"\|"Default model is missing"' plugin/PluginProcessor.cpp` 於本輪變更後回傳零筆。

**Scope self-check（協定 step 7）：** `python .loop/check_scope.py` → exit 0：`[scope] OK — 54 changed path(s) within policy`。

**Full tier / backlog checker：** 本輪未跑（iteration 8 非第 5 輪倍數，亦未宣告 converged）——criteria C1/C2 本輪標記 fail，C3 pass。

**Criteria：** C1 fail（full tier 未跑）／C2 fail（backlog checker 未跑）／C3 pass（scope 無違規）。AND 規則下未全數通過，非 converged。

**Backlog：** 新增 discovered 項目 L2g（Editor 層級 AlertWindow/FileChooser 對話框雙語化）`passes:true`，evidence 見 backlog.json。同時新增三個 discovered 項目留給後續輪次：L2h（RoFormer 瀏覽器狀態文字，需同步改 4 處既有英文斷言）、L2i（segmentBox_/computeBox_ 選單項目，需先補 setName() 再改既有斷言）、L2j（separationModeBox_ 12 模式項目與 stem slider 標籤，需要類別→雙語顯示名稱翻譯表，建議併入 L4/L5 規劃），三者皆 `passes:false`。L2 本身仍為 false——這四群缺口中僅 L2g 本輪清空，L2h/L2i/L2j 仍待後續輪次。L1、L2a–L2f 維持 true。L3–L7 維持 false。

**Metric：** backlog_items_passing = 8（較上一輪 7 增加 1：L2g 新增為 true）；improved: true。

**Decision：** continue（status 保持 running，stop_reason null）。

**Lesson：** 掃描「L2 全部 UI 文字雙語化」的剩餘缺口時，不能只靠 grep `setSeparationMessage`/`setMediaMessage` 這類已知函式名——用更廣的 pattern（`setButtonText`/`setText`/`addItem`/`AlertWindow::show`/`setTitle`）掃過整個檔案後才發現 `HTDemucsGpuFXEditor` 類別層級（非 `ExportDialogContent`）自己也有 3 個函式共 6 個 AlertWindow/FileChooser/DialogWindow 呼叫點從未被前幾輪掃到過。找到缺口後第一步永遠是「這批文字有沒有既有測試斷言直接比對」——本輪用 grep 精準區分出四群缺口中只有 1 群（L2g）零斷言覆蓋可直接做，另外 3 群（L2h 的 RoFormer 狀態文字、L2i 的 segmentBox_/computeBox_ 選單、L2j 的 separationModeBox_ 類別與 stem 標籤）都有既有英文斷言直接比對文字內容，且 L2i 還多一層「元件目前靠文字內容而非 setName() 被測試尋找」的額外前置工作——這些都不是能在同一輪跟 L2g 一起做掉的「掃描到就順手做」，必須各自成為獨立的 discovered 項目，避免打破「一輪一個 hypothesis」的可歸因性，也避免本輪因為要同時改動 production 程式碼與多處測試斷言而超出可審查的變更集大小。
