# LESSONS — append-only signs; every iteration must obey all of these

- SIGN (wiring): cmake 不在 PATH——建置一律走 `.loop\checks\cheap.cmd`／`full.cmd`（內含 VsDevCmd），或先 call VsDevCmd.bat。CMake target 名稱先查 CMakeLists.txt。
- SIGN (wiring): conda 完整路徑 `conda.exe` 的完整路徑；anaconda base python=3.13.5 torch 2.8.0+cu126（cuda 可用）。套件只裝進 `htfx-roformer` env。
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
- SIGN (iter 13): 呼叫 `worker/roformer_worker.py` 相關腳本（含 `tools/roformer_batch_verify.py`）一律直接用 `htfx-roformer` env 的 python.exe 絕對路徑（該 env 的 `python.exe` 絕對路徑）執行，不要包一層 `conda run -n htfx-roformer python ...`——即使子行程自己已呼叫 `configure_utf8_stream` 把自己的 stdout/stderr 轉成 utf-8，`conda run` 包裝器仍會用**它自己**綁在 Windows cp950 主控台的 `sys.stdout` 去 `print()` 捕捉到的子行程輸出，一樣對 emoji 拋 `UnicodeEncodeError`；這是 `conda run` 轉印層本身的問題，子行程端無法修。
- SIGN (iter 13): `tests/ui_configuration_smoke.cpp` 的 RoFormer C++ 路由測試硬編碼選用 `melband-roformer-kim-vocals`，且 `plugin/PluginProcessor.cpp::beginSeparation()` 只檢查 `isModelInstalled()`、不會自動下載未安裝模型——若模型不在 `verify/roformer-cache/` 就直接回傳 false（"Model ... is not installed"）。快取是共用的滾動快取（`max_cached=3`，LRU by mtime），任何一輪批次驗證其他 M-item 模型都可能把 `melband-roformer-kim-vocals` 擠出快取。任何批次處理 M-item 後、執行 cheap/full tier 前，必須確認 `melband-roformer-kim-vocals` 仍在快取內（不在則用 `python worker/roformer_cache.py --model melband-roformer-kim-vocals --cache-dir <絕對路徑>` 重新觸碰，使其成為最近使用而不被下一輪擠出）。
- SIGN (iter 14): headless `-p` 模式**沒有**背景喚醒機制——Bash run_in_background、Monitor、`&`、「等背景工作完成」在 iteration 內一律絕對禁止；結束訊息＝一切孤兒化。所有下載/驗證/建置必須在本輪內同步完成，批次太大就縮小（每輪 2–4 個 audited）。
- SIGN (iter 14): 經 Bash 工具傳遞 Windows 絕對路徑必須用單引號包住或改用正斜線——未加引號的反斜線會被 MSYS 吃掉，在 repo 內產生畸形目錄（例：`CodexProjectsSourceSeparation_GPU_FXverifyroformer-cache/`），觸發 scope 鎖機。iter-14 的畸形目錄已由 operator 搬移隔離至 `C:\CodexProjects\SourceSeparation_GPU_FX\verify\quarantine\iter14-mangled-cache\`（未刪除）。
- SIGN (iter 15): Bash 工具有時會在單一指令執行較久時，即使已明確帶入 `timeout` 參數、也沒有設定 `run_in_background`，仍自動把該指令轉為背景執行（回傳「Command running in background with ID ...」）。這不算違反「禁止背景執行」的硬規則本身——真正違反規則的是看到這行字就結束 turn。正確處理：立刻用 `TaskOutput(task_id, block=true, timeout=600000)` 同步阻塞等待；若回傳 `retrieval_status=timeout` 且 `status=running`，代表指令仍在跑，直接再呼叫一次 `TaskOutput` 繼續阻塞，不得換成不阻塞檢查或結束 turn，直到拿到 `status=completed`（或明確失敗）為止。本輪用此模式同步等到 M037（836MB checkpoint）下載＋分離完成，全程未離開 turn。另外，本輪實測 HuggingFace 下載吞吐量約 2.6–2.7 MB/s（60 秒取樣 curl 直接量測），836–913MB 的稽核模型下載本身約 5–6 分鐘即可完成——先前 iter-14 兩次 40 分鐘逾時的根因很可能是當時網路環境（行動網路熱點）暫時性壅塞，而非本專案下載邏輯或批次大小設計的問題；仍建議每輪同步驗證前先用一次 60 秒 `curl --max-time 60` 量測目前吞吐量，藉此決定本輪批次大小（吞吐量正常時 2–3 個稽核模型／輪皆可行）。
- SIGN (iter 20): `tests/ui_configuration_smoke.cpp` 裡「`separationMode->setSelectedItemIndex(N, sendNotificationSync); juce::Thread::sleep(X); juce::Timer::callPendingTimersSynchronously();`」這個自 iter-18 就在用的組合本身是有 race 的，不是穩定寫法——`callPendingTimersSynchronously()` 只會呼叫「countdown 已經被 JUCE 真正的背景 `TimerThread` 倒數到 ≤0」的 timer；那個倒數是由另一條真背景執行緒按自己的排程週期（最多每 100ms 醒一次）去扣減的，跟本執行緒 `Thread::sleep()` 睡了多久完全無關，所以偶爾會「明明睡夠了，倒數卻還沒被那條背景執行緒扣完」而整個 miss 掉，導致只靠 `onChange` 同步路徑更新的狀態（例如本檔的 stem slider）看起來是對的，但只靠 `timerCallback()` 才會更新的狀態（例如 `modelBox_`/`roformerCategoryBox_`/`roformerSearch_`/`roformerModelBox_` 的 `setEnabled()`）維持在呼叫 `setSelectedItemIndex` 之前的舊值——本地重跑同一支已建置好的 exe 5 次量到 2 次失敗、8 次量到 3 次失敗，且失敗時的失敗訊息與失敗行完全確定（不是隨機斷言，是同一斷言必定因為同一組控制項而失敗），純粹是背景執行緒排程的時間點問題。修法：把共用的 `waitUntil(predicate, timeout)` helper 改成「每次輪詢都呼叫一次 `juce::Timer::callPendingTimersSynchronously()`、再檢查 predicate」，並把檔案內所有一次性的 `sleep+callPendingTimersSynchronously` 呼叫點都換成 `require(waitUntil(...), ...)`；换成這個寫法後本機重跑同一支 exe 8/8 全過。之後任何在這個測試檔（或未來新增的 smoke test）裡需要等待 JUCE `Timer`-驅動的 UI 狀態變化時，一律用這個輪詢式 `waitUntil`，不要再用單次 `sleep()+callPendingTimersSynchronously()`。
- SIGN (iter 31): 部分 audited M-item 的 checkpoint 在 upstream `mel_band_roformer` 套件 registry 中對應的 config yaml URL 是死的（`DEFAULT_CONFIG_BASE_URL` 404），且該套件自己的 `data/overrides.json` 修補點不在本 repo scope（位於 conda env `site-packages`，policy.json `allow_paths` 未涵蓋，屬不可回溯的環境修改，絕不可編輯）——已知至少涵蓋 `roformer-model-mel-roformer-crowd-aufr33-viperx`（本輪修好）與 `roformer-model-mel-roformer-denoise-aufr33`／`-aggr`（下一輪待處理，同家族、同樣缺 config override）。正確排除法：(1) 用已驗證過的 checkpoint SHA-256，在 HuggingFace 上搜尋是否有其他鏡像 repo 同時上傳了該 checkpoint 與其 config；(2) 比對鏡像 repo 該 checkpoint 檔案的 LFS oid 是否與本地已驗證的 SHA-256 完全一致（一致才可信為正牌 config，不可用架構相近但無法驗證的其他 config 替代——會有 shape mismatch 或靜默錯誤風險）；(3) 下載該 config，放進（repo 外、允許建立/刪除的）`verify/roformer-cache/<model_slug>/<套件預期的 config 檔名>`——`mel_band_roformer` 套件的 `download.py::_download_config()` 邏輯是「目標路徑檔案已存在就跳過下載」，不需要碰任何 repo 內檔案或 third-party 套件安裝目錄。若找不到 SHA-256 完全比對的鏡像 repo，該 M-item 應保持 `passes:false` 並記錄為 `blocked` 交接，不可用不可驗證的 config 硬湊。
- SIGN (iter 22): 元件可見度的權責若分散在兩個函式（例如本檔的 `updateAdvancedVisibility()` 對一整組進階列元件無條件 `setVisible(advancedPanel_ && advancedVisible_)`，vs. `updateSixSourceControls()` 再依「目前分離模式種類」做更細緻的可見度收斂），只在後者加上新的模式相依條件是不夠的——任何會呼叫前者、但接下來不會呼叫後者的路徑，都會產生單一畫格（直到下一個 timer tick 校正）的過期可見度。本例是 `advancedButton_.onClick`（展開/收合「Advanced options」的按鈕）只呼叫了 `updateAdvancedVisibility()`；修法是在其後補呼叫一次 `updateSixSourceControls()`。之後任何在 `updateAdvancedVisibility()` 那組元件清單上疊加「依模式收斂可見度」邏輯時，都要檢查所有呼叫 `updateAdvancedVisibility()` 的地點是否也接著呼叫了負責細化的函式。
- SIGN (iter 39): 本輪透過 `Get-Process`（依 StartTime 排序找 `claude`/`node`/`MSBuild`）與 `.loop/driver.log` 時間戳記交叉比對，發現正式的 `.loop/run_loop.sh` 外部驅動器（LOOP_PLAN §9 Engine B）在本次互動式 session 開始前就已在背景持續運行、且幾乎同時各自獨立啟動了「iteration 39」——這是本 loop 第一次真的遇到「同一個 iteration 編號被兩個互不知情的 process 同時推進」，過去 38 輪皆為單一 driver 依序 launch。徵兆：讀到的 `state.json.iteration` 與最新 commit 的 iter 編號一致（看起來「乾淨」），但 `.loop/driver.log` 尾端有一行 `[driver] iteration <N> launching <timestamp>` 之後沒有對應的完成/commit 行、且 `Get-Process` 能找到啟動時間吻合、仍存活的 `claude`/`node`/build 相關 process。任何 iteration 開始動作前，除了讀 `state.json.status` 之外，也應該做這個並行 process 檢查；若發現已有其他 process 在跑同一 repo，應優先確認彼此的最終 `status`（尤其 `converged`），不要各自獨立推進、假設自己是唯一的 driver instance。發現時的正確處理：不殺對方 process、不覆蓋其工作，盡快把已完成且有完整證據的變更寫入並 commit 以縮小競態視窗，並在 journal 與最終回報中明確提醒使用者去檢查/手動停止背景 session（尤其是已宣告 converged 之後，背景 session 不應再繼續消耗資源）。
- SIGN (loop2 wiring): 本輪會把預設語言改為 zh-TW——ui smoke 既有的英文字串斷言（"4-stem separation"、"Record mode" 等）必須改為依語言查表取值（行為變更的必要測試更新），並新增 en 模式斷言；這不是放寬測試。
- SIGN (loop2 wiring): relabel 修復（stemSliderLabel 斷言）已由 operator 完成並 commit（2cb63b5＋後續）——不要重做，只需在 L7 確認回歸保持綠。
- SIGN (loop2 iter-1): L1 全量一次做會超過 60 分鐘 ceiling——雙語化工作每輪只做一個子里程碑（例：本輪只讓 Localization 模組編譯通過＋接一組控件），cheap tier 綠了就 record+commit，下一輪再擴。
- SIGN (loop2 iter-1): `.loop-archive-*` 目錄已加入 .gitignore（operator 歸檔，非 agent 工作範圍）。
- SIGN (loop2 iter-4): `htfx::Localization::tr()` 沒有樣板/佔位符替換機制。句子中間需要嵌入動態值（結束碼、檔名、模型名稱等）時，拆成 prefix/suffix 兩個獨立 key，呼叫端 `htfx::tr(prefix) + 動態值 + htfx::tr(suffix)`；動態值只在句尾則單一 key（value 含尾隨冒號/空白）＋呼叫端 `htfx::tr(key) + 動態值` 即可，不需要 suffix key。`setSeparationMessage()` 狀態列訊息（下個 L2 子里程碑，約 20 處呼叫點）會大量遇到同類情境，直接套用此模式。另外，`htfx::tr()` 是行程全域單例的自由函式，匿名命名空間內的非類別成員自由函式（不只類別方法）也能直接呼叫，不需要額外傳遞或建構。
- SIGN (loop2 iter-5): 一句話同時嵌入兩個動態值時，把 iter-4 的 prefix/suffix 兩段式 key 自然延伸成 prefix/middle/suffix 三段式 key 即可（例：`status.loadingModelPrefix` + modelName + `status.loadingModelDeviceMiddle` + device + `status.loadingModelDeviceSuffix`），呼叫端仍是單純字串相加，`tr()` 本身不需要改動。`setSeparationMessage()`/`setMediaMessage()` 的實際呼叫點數量遠多於粗估（26＋14，不是「約 20」）；`CPU`/`GPU`/`CUDA GPU`/`cuda:N`/`auto`／GPU 名稱等裝置識別字視為技術性專有名詞、比照模型 ID 慣例不翻譯。掃描一類 setter（如 `setMediaMessage()`）全部呼叫點時，若同時看到明顯屬於不同子系統的字面值群（本輪：`setSeparationMessage()` 分離狀態 vs. 匯出流程專屬的 `setMediaMessage()`），只做本輪鎖定的子系統，另一群記錄成新的 discovered backlog 項目留給下一輪，不要因為程式碼手法相同就一起做掉——維持一輪一個 hypothesis 的可歸因性。
- SIGN (loop2 iter-5): 本輪迭代編號剛好是 5 的倍數，依協定同一輪需同時跑 cheap tier 與 full tier（外加 backlog checker）；full tier 在未觸及 CMakeLists 的情況下（僅 .cpp 修改）走增量建置，四個 smoke 全部重新建置＋執行仍可在合理時間內於 Bash 工具同步跑完，不需要因為「這是第 5 輪」就額外放大這輪的程式碼變更範圍或縮小驗證動作。
- SIGN (loop2 iter-6): 掃描同一類 setter（`setSeparationMessage()`／`setMediaMessage()` 等）的全部呼叫點時，若同時看到「純字面值或二選一分支的三元運算式（無執行期動態值內嵌）」與「含動態值串接（error 變數／`exception.what()`／檔名路徑數量等）」兩種複雜度截然不同的呼叫點，只做前者、後者記錄成新的 discovered backlog 項目，不要因為函式相鄰或程式碼手法相近就一起做掉——這是 loop2 iter-5 splitting 原則的延伸適用。二選一分支的三元運算式（兩個分支皆為完整靜態字串）可直接比照 L2a 已驗證過的按鈕文字模式：兩個分支各自對應一個獨立 key，呼叫端維持三元運算式、只是每個分支換成 `htfx::tr(key)`；不需要用到 iter 4/5 的 prefix/suffix（或 prefix/middle/suffix）拆分機制，那套機制只在句子中間真的需要嵌入執行期字串/數值時才需要。
- SIGN (loop2 iter-7): 設計含動態值串接呼叫點的字串鍵拆分時，先判斷「動態值在句子的哪個位置」再決定鍵的組合，不要預設每個含動態值的呼叫點都需要完整三段式拆分：純句尾動態值（靜態文字+value）只需要一個 prefix 鍵（`htfx::tr(prefix) + value`）；純句首動態值、後接靜態文字（value+靜態文字，例如本輪 `error + " (The source video codec...)"`）只需要一個 suffix 鍵、不需要對應 prefix 鍵（`value + htfx::tr(suffix)`）——這是 prefix-only 情況的鏡像；句中兩側皆有靜態文字且只有一個動態值仍是單一 prefix 或 suffix 視動態值位置而定；只有句中同時內嵌兩個（或以上）動態值時才需要 iter 4/5 的 prefix/middle/suffix 三段式（或更多段）拆分。
- SIGN (loop2 iter-8): 掃描 L2（全部 UI 文字雙語化）剩餘缺口時，只用已知函式名（`setSeparationMessage`/`setMediaMessage`）已經掃不到新的缺口——`HTDemucsGpuFXEditor` 類別層級自己的 `chooseMediaFile()`/`chooseQuickExportFile()`/`showExportDialog()` 三個函式（非 `ExportDialogContent`）有 6 個 AlertWindow/FileChooser/DialogWindow 呼叫點被前幾輪全部漏掃。之後找新的 discovered L2 子項目前，先用更廣的 pattern（`setButtonText\(|setText\(|addItem\(|setTooltip\(|AlertWindow::show|setTitle\(|dialogTitle`）掃過整份 `PluginProcessor.cpp`，且對每一批候選文字都要先 grep `tests/` 確認有沒有既有斷言直接比對其內容：零斷言覆蓋的批次可以像本輪 L2g 一樣單純接線、不動測試；有斷言覆蓋的批次（已知還剩 L2h：RoFormer 瀏覽器狀態文字、L2i：segmentBox_/computeBox_ 選單、L2j：separationModeBox_ 類別與 stem 標籤）必須把「同步改測試斷言」納入該批次範圍內一起做，不能只改 production 程式碼；其中 L2i 的 `segmentBox_`/`computeBox_` 測試目前是靠選項文字內容（而非 `setName()`）尋找元件，翻譯前必須先補 `setName()`，否則語言切換後測試會直接找不到元件。
- SIGN (2026-08-29, operator): 本機建置必須用 64 位元編譯器工具鏈——`cmake --build ... -- /p:PreferredToolArchitecture=x64`。預設的 32 位元 cl.exe 在重編 `SpscRing<RecordFrame,524288>` 那個 TU 時會 C1060「編譯器堆積空間不足」（即使系統有 22GB 空閒）；平時增量建置不重編該 TU 所以碰不到，一改 PluginProcessor.h 就會踩到。
- SIGN (2026-08-29, operator): 跑 smoke 測試時絕對不能同時開著 App（含 verify\play 的可攜版）——兩者會搶同一份 roformer-cache 與輸出目錄，症狀是 `ui_configuration_smoke fatal: Access violation - no RTTI data!`，且與程式碼改動無關（極易誤判為自己改壞）。
- SIGN (2026-08-29, operator): RoFormer worker 的冷啟動成本約 67 秒（python + torch import + 913MB checkpoint 載入 + CUDA init），實際推論僅約 2.5 秒。大量檔案複製（例如打包 4GB 可攜包）會洗掉 OS 檔案快取，使冷啟動成本浮現。ui_configuration_smoke 的 RoFormer 等待上限已從 60 秒放寬至 240 秒。
- SIGN (2026-08-30, operator): `isRoformerModelName()` 必須同時認 `melband-roformer-` 與 `roformer-model-` 兩種 id 前綴——manifest 的 99 個模型裡有 74 個是後者，只比對前者會讓四分之三的模型在 C++ 端被當成 HTDemucs 模型、以「模型尚未安裝」拒絕分離。
- SIGN (2026-08-30, operator): 快速匯出（一般面板的「僅匯出人聲／伴奏」）不可強制切換模型——舊碼會 `modelBox_.setSelectedItemIndex(0)` 把模型改回 htdemucs 並只認 `previewUsesModel("htdemucs")`，與模式優先 UI 直接衝突，使用者在任何 RoFormer 模式按匯出都會永遠等不到結果。
- SIGN (2026-08-30, operator): `SpscRing` 的 `std::array<Item, 2^19> storage_{}` 值初始化會讓編譯器 C1060（堆積不足）；改成建構式 memset。**不可直接移除歸零**——那會讓 ring 帶著未定值，ui_configuration_smoke 會 segfault。
- SIGN (2026-08-30, operator): 驗收工具 `tests/goal_check.cpp`（target `htdemucs_goal_check`）用真實歌曲跑 HTDemucs 4/6-stem 與兩個 RoFormer 類別的完整流程；命令列取路徑必須用 `CommandLineToArgvW(GetCommandLineW())`，argv 的 ANSI 視圖會把中文路徑變亂碼。

- SIGN (2026-09-04, full_feature_check): RoFormer 上游套件會用**它自己登錄檔**裡的 config 網址去抓 config yaml，其中 guitar／denoise-aufr33／crowd 三個已是 404；開發機能跑只因舊快取裡早有 config。快取器（`worker/roformer_cache.py`）必須連 config 一起用 manifest 的 `config_url` 下載並驗 `config_sha256`，上游看到檔案存在才不會去抓死網址。**任何「開發機能跑、全新安裝不能跑」的模型問題，先清空 `%LOCALAPPDATA%\Music SSP FX\RoformerModels` 重現。**
- SIGN (2026-09-04): `juce::ChildProcess::readProcessOutput(buf, n)` 會忙碌等待**直到填滿 n 個位元組**才返回，不是「有多少讀多少」。子行程逐行輸出進度（每行約 40 bytes）時，給 4 KB 緩衝等於要累積約 100 行才回來——整段推論都不到那麼多行，於是全部積到程序結束才一次抵達。要即時解析子行程輸出，緩衝給 64 bytes 左右，且以原始位元組切行後再解 UTF-8（讀取可能停在多位元組字元中間）。
- SIGN (2026-09-04): 驗收工具的逾時要用「多久沒有任何狀態變化」判定，不能用總時長——RoFormer 的模型下載發生在 worker 內、算在同一次分離裡，網速慢時 800 MB 就吃掉 15 分鐘，總時限會把正常推進中的分離誤判為失敗。
- SIGN (2026-09-04): `roformer_smoke` 與 `ui_configuration_smoke` 需要環境變數 `HTFX_PYTHON` 指向 `htfx-roformer` env 的 python.exe（個資清除時把硬編碼路徑換成了它），沒設會以 exit 2「htfx-roformer Python is missing」立即失敗。smoke 用的 sidecar junction `build\windows-installed\Release\Resources\sidecar` 現在指向 `verify\payload-current\Resources\sidecar`（worker/、src/、demucs_repo/、models/ 含兩份 manifest 與 catalog，Runtime/ 以 junction 跟隨 `build\standalone-runtime-dist` 與 `build\ffmpeg-lgpl`）；舊的 `verify\payload-cuda` 是 7 月的 runtime，不認得 `roformer` 子命令。
- SIGN (2026-09-04): 跑 `full_feature_check` 或任何用到 frozen runtime 的測試時，**不可同時重建同一份 runtime**（PyInstaller 會先清空再寫入 `build/standalone-runtime-*-dist`），測試會抓到半成品的 exe，症狀是 worker 印出 htdemucs 的 usage 後以結束碼 2 退出。
- SIGN (2026-09-05): `ui_configuration_smoke` 間歇性 `0xC0000005`（fault address `FFFFFFF0` 之類的懸空值）不是產品 bug：測試開頭 `collectComponents()` 蒐集一次指標，中間的模式／面板切換與批次清單會讓編輯器銷毀重建子元件，之後用舊向量做 `getName()`／`dynamic_cast` 就讀到已釋放記憶體，能不能碰巧讀到決定了它間歇。**每個檢查區塊前重新蒐集元件樹。** 抓這類崩潰的方法：`__try/__except` 包住 `main`，filter 內用 `StackWalk64`+`SymFromAddr` 走 exception CONTEXT，smoke 與 `HTDemucsGpuFX` 都加 `/Zi` 並連結 `/DEBUG`，先看 stack 是否落在測試自己的查找函式再決定要不要追產品。
- SIGN (2026-09-06, format_matrix_check): 「影片回填 MP4」用 `-c:v copy` 時，MP4 容器裝不下 VP8（WebM）、WMV2、部分舊編碼，ffmpeg 以 -22 失敗；MPEG-1 與 MPEG-4/H.264 可以。stream copy 失敗要自動退到重新編碼——隨附的 LGPL FFmpeg 沒有 libx264，但有 `libopenh264`（BSD）與內建 `mpeg4`，兩者都可用於發行。**任何只用一首 MP4 驗證過的影片功能，都要再用 WebM／WMV 各跑一次。**
- SIGN (2026-09-06): 批次匯出的輸出檔名只取「主檔名＋_vocals/_accompany」，`song.mp3` 與 `song.flac` 同批會互相覆蓋，最後只剩一個檔。批次內的輸出名要去重（加來源副檔名、再加計數）。同類問題的通用檢查：**測試資料要刻意包含同名不同格式的檔案**。
- SIGN (2026-09-06): 30 秒合成訊號（`tools/make_test_media.sh`）就足以驗證匯入／匯出／容器相容性，GPU 上整套 28 個檔案約 7 分鐘；真實歌曲只留給分離品質與逾時行為的驗收。
- SIGN (2026-09-06, ui_snapshot): `juce::ComboBox::changeItemText()` 只改選單項目，不更新框內顯示文字；而 `getSelectedItemIndex()` 會拿顯示文字跟項目文字比對，不一致就回 -1。所以切換語言後所有「有沒有選模式」的判斷全部變成未選，推桿與模型選單全被停用。重貼文字後要以 id 重新選取（先 `setSelectedId(0)` 再設回原 id）。**任何用 changeItemText 的地方都要這樣做。**
- SIGN (2026-09-06): `juce::String(const char*)` 只接受 ASCII，非 ASCII 位元組會逐位元組轉成碼點（「—」變成「â」加兩個控制字元）。字串表的英文字面值也要用 `u8` 前綴（或 `String::fromUTF8`）。開發機介面預設中文，所以英文亂碼一直沒人看到——**兩種語言都要截圖檢查**。
- SIGN (2026-09-06): 檢視 UI 不必開 App：`htdemucs_ui_snapshot.exe <輸出資料夾> [媒體檔]` 用 `createComponentSnapshot` 把一般／進階面板、中英文、匯入前後與分離完成的狀態都輸出成 PNG，直接看圖。改版面或配色後先跑它，再跑 `ui_configuration_smoke`。
- SIGN (2026-09-06): 編輯器的裝飾層（步驟列、檔案卡、狀態圓點）畫在承載所有控制項的 `scaledContent_` 的 `paint()` 裡，位於子元件之下；任何要蓋在按鈕上面的提示（拖曳中的「放開即匯入」）必須用 `paintOverChildren()`，否則會被按鈕遮住——截圖工具第一次就抓到了。
- SIGN (2026-09-06, format_matrix_check media2): JUCE 的 `File::existsAsFile()` 直接呼叫 `GetFileAttributes`，完整路徑超過 260 字元就回 false（除非程式宣告 longPathAware 且系統開了 LongPathsEnabled，後者使用者機器上通常沒開）。匯入被拒時要明講「路徑太長」，不能只說「找不到檔案」。測試資料要放一個 180 字元檔名的檔案在深層資料夾。
- SIGN (2026-09-06, 工具面): 在 Claude Code 的 Bash 工具裡用 heredoc 餵 Python 改檔，內容中的反斜線序列會被剝一層（`\n` 變成真正的換行、`\\` 變成單一反斜線），`<<` 也可能讓 heredoc 解析失敗；C++ 字串裡的 `\n` 與 Windows 路徑就這樣壞掉過三次。**含反斜線的補丁一律用 Write 工具寫成 .py 檔再執行，或直接用 Edit 工具。**
- SIGN (2026-09-06): 用合成訊號跑 `full_feature_check` 時，「export vocals」會因為訊號裡沒有人聲而被靜音門檻（1e-4，給真實歌曲用）判失敗；判讀時只看每個模式的 `separate` 行。全新快取（清空 RoformerModels）下 10 個模式全部下載＋分離成功，30 秒片段的 GPU 牆鐘時間：guitar 23 s、dereverb 72 s、其餘 110–140 s，aspiration 458 s（含 797 MB 下載）。
- SIGN (2026-09-06): 30 秒片段在 CPU（`--backend cpu`）的牆鐘時間：HTDemucs 6 軌 43 s、guitar RoFormer 249 s、kim-vocals RoFormer 605 s——換算整首 5 分鐘歌約 100 分鐘，證實目錄把 vocals 類標成 GPU 專用是對的。
- SIGN (2026-09-06): 同一個 30 秒片段的 HTDemucs 4 軌，在 CUDA runtime 選 CPU 後端約 43 s，在 CPU 凍結 runtime（`standalone-runtime-cpu-dist`）卻要 85–100 s。CPU 版使用者拿到的是後者，`--backend cpu` 的數字不能直接代表 CPU 版體感；要量 CPU 版就設 `HTFX_WORKER_EXECUTABLE` 指向 CPU dist。
- SIGN (2026-09-06, ui_snapshot EN): 動態狀態訊息（「已匯出伴奏：路徑」「已匯入 N 個檔案」）是在事件發生當下用 `htfx::tr()` 組好的字串，之後切換語言不會重譯；剪輯列的「待分離／完成」也曾如此，已改成存字串表的 key、在 `getClipInfo()` 讀取時才翻譯。其餘 setMediaMessage 呼叫點很多，仍是「以設定當下的語言顯示」——要徹底解決得改成存 key＋參數。
- SIGN (2026-09-06): 把 `HTFX_WORKER_EXECUTABLE` 指向 `standalone-runtime-cpu-dist` 的 worker，主程式會讀到旁邊的 `runtime-manifest.json`（flavor=cpu），目錄過濾隨之生效——`full_feature_check --modes quick` 在這個設定下只跑 6 軌與 guitar，vocals 自動消失。這是驗證「CPU 版使用者實際看到的模式清單」的正確方法，不必另外打包安裝。
- SIGN (2026-09-06, code review): 多檔匯入在啟動時把 `separationState_` 設成 loading，只有「第一個成功的檔案」會把它推進；全部失敗時狀態就永遠停在 loading，UI 全部停用、取消無效。**任何在迴圈前先設「進行中」狀態的流程，都要有「一個都沒成功」的出口。** 這條是審查找到的，矩陣測試原本只測「夾一個壞檔」沒測「全是壞檔」。
- SIGN (2026-09-06, code review): 10 Hz timer 裡不要做會阻塞的系統呼叫——`File::exists()` 對斷線的網路磁碟會卡住訊息執行緒；判斷「有沒有匯出」用路徑是否為空，真的要驗證存在時在使用者按下時才查。同樣道理，捷徑鍵要看 `isShowing()` 不能只看 `isEnabled()`（簡易面板的預覽按鈕隱藏但仍 enabled，空白鍵會暗中播放）。
- SIGN (2026-09-10): 0.0.5 安裝程式的「安裝後自我測試」只看結束碼，失敗時 stderr 被 Inno 的 Exec 丟掉，使用者只看到「self-test failed」。**任何在安裝程式裡跑的子程序都要把輸出導到記錄檔**（`cmd /C "... > log 2>&1"`），worker 端失敗也要寫報告；而且 GPU 自測失敗不該讓整個安裝失敗——CUDA runtime 也能跑 CPU，退回去測一次 CPU 就好。
- SIGN (2026-09-10): 測試安裝程式時 `/DIR` 不要指到 scratchpad 那種很深的路徑——runtime 解壓後的 torch 路徑會超過 260 字元，Inno 報 MoveFile code 3「找不到路徑」；而且 `/VERYSILENT /SUPPRESSMSGBOXES` **不會**壓掉檔案錯誤的「Select action」對話框，它會直接跳在使用者桌面上。用 `build\itest` 之類的短路徑。
- SIGN (2026-09-10, 0.0.5 安裝失敗根因): 安裝程式用 Inno 的 `extractarchive` 把 runtime zip 直接解到 `{app}`，不會清掉先前另一種 runtime 留下的檔案。在裝過 CUDA 版的目錄上改裝 CPU 版，`torch\lib\` 裡會同時有 CPU 版的 `torch_cpu.dll` 和舊的 `c10_cuda.dll`／`torch_cuda.dll`／`cublasLt64_12.dll`（合計 4.5 GB），torch 啟動時載入 `c10_cuda.dll` 就以 WinError 127 失敗，App 與自我測試都起不來。**換版本前必須先 DelTree `Resources\sidecar\Runtime`**；判斷污染的方法：`runtime-manifest.json` 說 cpu 但 `torch\lib` 有 `c10_cuda.dll`。
- SIGN (2026-09-12): RoFormer 的兩個輸出檔以 `findChildFiles` 讀入，順序是字母序，所以 `_instrumental` 會排在 `_vocals` 前面——**第 0 軌不一定是該分類的目標軌**。任何「依分類名稱決定第 0/1 軌是什麼」的 UI 都會對調；要用 `result->stemLabels`（由檔名推導）當唯一真相，而且只在結果來自目前選定的模型時採用，否則會沿用上一個模式的殘留結果。使用者回報「聲音都集中在人聲軌道上、好像沒分離」就是這個 bug。
- SIGN (2026-09-12): `juce::ProgressBar::setTextToDisplay()` 會把 `displayPercentage` 永久設成 false，傳空字串也一樣。要恢復百分比得呼叫 `setPercentageDisplay(true)`。另外不確定狀態的跑馬紋會蓋掉文字，要自己畫一層遮罩再寫字。
- SIGN (2026-09-12): RoFormer 在 CPU 上的耗時由序列長度決定，與 checkpoint 大小幾乎無關（43 MB 的吉他與 797 MB 的氣音落在同一個時間帶）。**不要用模型大小推估 CPU 速度**，要實測；而且量測時不能同時跑建置或 GPU 工作，否則同一模型可以差一倍。

## SIGN: 面板專屬的提示要由知道面板的那一層產生

processor 寫的狀態訊息裡塞了「請按下「分離」」，但普通面板沒有分離鍵。誰知道面板，
誰就負責寫下一步該按什麼；processor 只描述狀態。UI smoke 現在會匯入一個檔案，分別在
兩個面板上驗證提示指向該面板真的有的按鈕。

## SIGN: 會停用整個面板的旗標，必須有無條件的釋放路徑

`pendingQuickExport_` 只在 previewReady / error / cancelled 被清掉，其餘結局（分離沒
真的開始、使用者又匯入了別的檔案）會讓它永遠留著，而它停用普通面板僅有的兩個匯出鍵與
切換面板鍵。匯入鍵不受它限制，所以症狀看起來完全是「匯入完成後匯出鍵按不下去」。
凡是會讓控制項變灰的狀態，都要問一句：它有沒有可能永遠不被清掉。

## SIGN: 改了 .iss 就要真的編一次

564c92e 改了安裝腳本但沒跑 ISCC，那份狀態其實做不出任何安裝檔（Pascal Script 依順序
解析識別字，函式必須定義在呼叫之前）。安裝腳本沒有編譯期以外的測試，唯一的驗證就是編
它。

## SIGN: 入門面板不能有「沒有出口的灰按鈕」

普通面板的兩個匯出鍵原本要求 htdemucs 檢查點已安裝才啟用，但那個面板沒有任何模型控制
項，使用者看到的就是兩顆永遠按不下去、也不說原因的按鈕。正確做法是讓按鍵負責「把事情
做完」——缺什麼就去抓什麼（RoFormer 模式一直都是這樣），做不到再報真正的錯誤。
凡是用「某個資源已就緒」當作啟用條件的控制項，都要問：使用者在這個畫面上有辦法讓它就
緒嗎？沒有的話，這個條件就不該存在。

## SIGN: 一道永遠不會通過的檢查，等於沒有檢查

`check_public_repo.ps1` 的身分掃描會命中「它自己用來偵測使用者路徑的那個正規表示式」，
連帶命中每一支執行同一道檢查的打包腳本，所以它從來沒有通過過——它要保護的東西實際上
從未被檢查。`verify_windows_web_packages.ps1` 寫死「CUDA 恰好 2 個封存檔」也一樣，
runtime 一長大就永遠失敗。
把關腳本要在它真正該通過的情境下跑過一次，確認它會 PASS；只看到它會 fail 不代表它有用。

## SIGN: 修掉一個模式的閘門時，要找出同一個閘門的所有副本

0.0.7 把「預設檢查點已安裝」從普通面板的兩個匯出鍵拿掉，卻漏了進階面板的「分離」鍵，
於是同一個死路換個面板又出現一次。而且那顆鍵檢查的是 HTDemucs 模型下拉選單的內容——
RoFormer 模式下那個模型不會被使用，等於為了一個不會被開啟的檔案停用按鈕。
改掉一個錯誤的啟用條件時，先 grep 這個述詞的所有使用處，一次全部處理。

## SIGN: 慢的原因要量，不要猜——而且要量端到端

「GPU 版下載太久」的直覺答案是「檔案太大，壓縮它」。實測發現真正的原因是 GitHub Release
從這條線路只有 0.46 MB/s，而 HF／Cloudflare 有 8～10 MB/s（換一個知名 repo 的 release
也一樣慢，所以不是本專案的問題）——搬家的效益是壓縮的 20 倍。
更重要的是：只量下載會得到錯誤的結論。第一版 7z 用 solid，下載省了 2 分鐘卻讓安裝多花
14 分鐘，端到端比原本更糟。一定要量使用者真正等待的那段時間。

## SIGN: Inno 的 extractarchive 一定要用 non-solid 封存檔

Inno 逐檔取出封存檔內容，solid 的 7z 每取一檔都要重新解壓整個區塊：實測每檔 0.62 秒，
1500 個檔就是 933 秒。Inno 自己會在日誌寫「Archive is solid; extraction performance
may degrade」——要看安裝日誌，它已經把答案告訴你了。`-ms=off` 體積只多 9%，單檔取出從
0.498 秒降到 0.052 秒。

## SIGN: [Code] 裡的 MsgBox 會卡住無人值守安裝

`/SUPPRESSMSGBOXES` 只對 Setup 自己的訊息框有效，[Code] 裡呼叫的 `MsgBox` 照樣跳出並等
人按。實測一個「確認下載 1.7 GB 嗎」的對話框把靜默安裝卡了 460 秒。一律用
`SuppressibleMsgBox` 並想清楚預設答案：會造成損失的預設「否」（刪資料、放行壞掉的安裝），
純粹確認的預設「是」。

## macOS 移植（2026-09-14，Apple Silicon M1 / macOS 26.2）

- SIGN (macos): `tools/*.sh` 在 Git index 裡是 `100644`（Windows 那邊 commit 時沒帶 exec bit），
  而 `macos_build_everything.sh` 是用**直接路徑**呼叫其他腳本的，所以第一次執行就 Permission denied。
  `.gitattributes` 已經防了 CRLF 卻沒防這個。新增 `.sh` 時一併 `git update-index --chmod=+x`。
- SIGN (macos): PyInstaller 的 torch hook **不收 `torch/bin`**，而 `torch/__init__.py` 在**非 Windows**
  平台 import 時會呼叫 `_manager_path()` 檢查 `torch/bin/torch_shm_manager`，不存在就 raise
  （Windows 走 early-return，所以 Windows 腳本可以整個 `rm -rf torch/bin`）。macOS 必須用
  `--add-binary "<torch>/bin/torch_shm_manager:torch/bin"` **主動加進去**。把 Windows 的刪除行照抄
  過來是 no-op，不是根因。
- SIGN (macos): 頂層 `project(... LANGUAGES CXX)` 在 macOS 會讓 CMake **configure 成功、generate 失敗**，
  錯誤是 `Missing variable is: CMAKE_C_COMPILE_OBJECT`。JUCE 模組在 macOS 帶進 C 來源，而
  `third_party/JUCE` 自己的 `project(JUCE ... LANGUAGES C CXX)` 只在**它自己的目錄範圍**啟用 C。
  頂層要宣告 `LANGUAGES C CXX`。Windows 全是 `.cpp` 所以碰不到。
- SIGN (macos): `std::atomic<std::shared_ptr<T>>`（C++20 P0718 特化）**MSVC STL 有、Apple libc++ 沒有**，
  於是退回主樣板、以 `is_trivially_copyable` 靜態斷言失敗，並讓整個類別定義失效、連帶噴出 9 個
  看起來毫不相干的錯（「無法從 X* 初始化 juce::AudioProcessor*」之類）。**只看第一個錯誤**。
  修法是 `plugin/AtomicSharedPtr.h`：用 `juce::SpinLock` 包一層、保留 `load(order)`/`store(v, order)`
  介面，呼叫端一行都不用改。不可以用 `std::mutex`——`processBlock` → `processRecordMode` 會讀它。
- SIGN (macos): macOS 是 **LP64**（`size_t` = `unsigned long`），Windows x64 是 **LLP64**
  （`size_t` = `unsigned long long`）。所以 `std::min(size_t, uint64_t)` 在 Windows 推導得出來
  （同一型別）、在 macOS 推導不出來。混用 `size_t` 與 `uint64_t` 的 min/max 一律顯式轉型。
- SIGN (macos): `juce::File f; f = {};` 在 clang 下對三個 `operator=`（`const String&`／`const File&`／
  `File&&`）不明確，MSVC 放行。寫 `f = juce::File{}`。
- SIGN (macos): `torch.device("mps")` 的 index 是 `None`，但**放在 mps 上的張量回報的是 `mps:0`**，
  而 `torch.device` 比對含 index，所以兩者永遠不相等——`engine.py` 的合約檢查會噴
  `hop is on mps:0, expected mps`。CUDA 不會，因為 auto 路徑本來就回傳 `cuda:0`；CPU 沒有 index。
  `resolve_device()` 要把 MPS 正規化成 `torch.device("mps", 0)`。
- SIGN (macos): 凍結腳本原本只檢查 `--help` 與 `roformer --help`，那**只驗證參數解析**——
  `--exclude-module cloudpickle` 讓 RoFormer 整條 import 鏈（librosa → joblib → loky →
  `from cloudpickle import dumps, loads`）在凍結後才斷掉，而兩個 help 檢查照樣通過。
  已在 `worker_main.py` 加 `--import-check`（實際 import 兩個後端的相依樹），凍結腳本必跑。
  找「哪些排除的模組其實會被載入」的方法：在 env 裡 import 一次整條鏈，再比對 `sys.modules`。
- SIGN (macos): POSIX 的 `multiprocessing.resource_tracker` 會用
  `sys.executable -B -S -I -c "from multiprocessing.resource_tracker import main;main(fd)"`
  重新啟動自己；凍結後 `sys.executable` 就是 worker，那些參數會撞上我們的 argparse，
  tracker 死掉、semaphore 洩漏。**`multiprocessing.freeze_support()` 救不了**——它第一行就是
  `if sys.platform == 'win32'`，非 Windows 直接 return；而 Windows 根本沒有 resource_tracker，
  所以這個問題只在 macOS/Linux 出現。`worker_main.py` 要自己認出 `-c <含 multiprocessing 的程式碼>`
  並 exec 它。
- SIGN (macos): JUCE 把每種格式放進各自的子資料夾，standalone 的 `.app` 在
  `Release/**Standalone**/<name>.app`，不是 `Release/<name>.app`。
- SIGN (macos): `macos_build_everything.sh` 原本用「執行檔存在」判斷凍結是否完成，但**失敗的凍結也會
  留下執行檔**，於是重跑會跳過重凍、拿壞掉的 runtime 去封裝。改用 `runtime-manifest.json`——
  它是凍結腳本**所有檢查通過後**才寫的，才是誠實的完成標記。
- SIGN (macos): `goal_check`／`full_feature_check`／`format_matrix_check`／`ui_snapshot` 四支驗收工具
  對 Windows 的相依**只有**「`<windows.h>`＋`CommandLineToArgvW` 取 UTF-16 argv」那一段，而且四支
  都已經有 UTF-8 fallback。用 `#ifdef _WIN32` 包起來就能在 macOS 建置執行，不需要改邏輯。
  另外 `full_feature_check` 的 `bundledFfmpeg()` 寫死 `ffmpeg.exe`／`ffprobe.exe` 與
  `build/ffmpeg-lgpl/`，macOS 要改成無副檔名並優先找 `Resources/sidecar/Runtime/ffmpeg/bin/ffmpeg`。
- SIGN (macos): 把驗收工具**複製到 `.app/Contents/MacOS/` 再執行**，`bundledSidecarPath()` 就會解析到
  和 App 完全一樣的 worker／ffmpeg／sidecar／模型路徑，等於順便驗證了 bundle 的路徑解析，
  比設一堆 `HTFX_*` 環境變數繞過去有價值。記得 `codesign --force --sign -` 一次。
- SIGN (macos): 用 **MP4 當來源歌曲**跑 `full_feature_check` 時，所有 frame 數斷言會差固定的樣本數
  （本例 882 @44.1k＝960 @48k＝20 ms）。**這不是管線缺陷**：App 用 ffmpeg 解碼，匯出長度與 ffmpeg
  的解碼結果逐 sample 吻合；而測試的期望值是 `inspect()` 用 `juce::AudioFormatManager` 量的，
  macOS 上那會註冊 `CoreAudioFormat`（所以才讀得了 MP4），兩個解碼器對 AAC 編碼器 padding 的處理不同。
  換成 WAV 來源就是 17/17 全過。**不要為此放寬門檻**；要改就改成用同一支 ffmpeg 量來源。
- SIGN (macos): MPS 與 CPU 的分離結果在本機實測**一致到 6 位有效數字**（4 軌與 6 軌皆是）。
  懷疑 MPS 算錯時，先用 `--backend cpu` 跑同一個輸入對照，再去追 MPS。
- SIGN (2026-09-14, 非平台相依): 快速匯出的伴奏加總迴圈寫死 `for (int source = 0; source < 4; ...)`，
  6 軌模型的 guitar(4) 與 piano(5) 永遠不會被加進去。吉他主導的歌實測匯出只有 −48 dBFS，
  而同一首歌的 4 軌伴奏是 −17 dBFS。改成 `source < result->sourceCount`（4 軌結果的
  `sourceCount` 就是 4，行為不變）。這個 bug 與 macOS 無關，Windows 同樣存在。
- SIGN (macos): `configuredModelsDirectory()` 在「已安裝的 Models 目錄沒有 manifest」時會退回
  **bundle 內**的 models 目錄，而 macOS 沒有安裝程式去種那個 manifest（Windows 靠安裝程式寫進
  `%LOCALAPPDATA%`），於是第一次分離就把 139 MB 的 checkpoint 寫進**已簽章的 `.app` 裡**——
  破壞簽章、把不可散布的權重塞進可散布的產物、而且 App 一旦放到 `/Applications` 就會寫入失敗。
  修法是第一次解析時從 bundle 把**manifest（只有 json/yaml）**複製到使用者的資料目錄再回傳它。
  驗證方式：清空重建 bundle 後跑一次分離，`find <app> -name '*.th'` 必須是空的。
- SIGN (macos): `juce::File::userApplicationDataDirectory` 在 macOS 是 **`~/Library`**，
  所以 `installedDataDirectory()` 實際是 `~/Library/Music SSP FX`，
  **不是** `docs/MACOS_HANDOFF.md` 寫的 `~/Library/Application Support/Music SSP FX`。
  目前可寫、功能正常，但不符 Apple 慣例；要改成 Application Support 的話記得既有使用者的
  `RoformerModels` 快取需要搬移，否則會整批重新下載。

## macOS 效能量測（2026-09-14，M1 Air 8 GB）

- SIGN (macos): 上游 `mel_band_roformer/inference.py::_resolve_device()` 對 `auto` **只檢查 CUDA**，
  找不到就 `print("CUDA is not available. Falling back to CPU. This will be slow.")` 回傳 cpu——
  **它從頭到尾不認得 MPS**。而 `PluginProcessor.cpp` 的 RoFormer device 字串在 `backend == 0`
  （auto，**預設值**）時傳的就是 `"auto"`，所以 **Apple Silicon 上每一次 RoFormer 分離都跑在 CPU**，
  使用者得自己去進階面板改成「Apple Metal (MPS)」才會用到 GPU。偵測方法：worker 的 stderr 會直接
  印出那行 `Falling back to CPU`。修在 `worker/roformer_worker.py`（送進上游之前自己解析 auto），
  不要修在 C++——凍結 runtime 與原始碼執行才會走同一條路。加速依模型而定，不是齊頭的：
  整首 246.7 秒檔案上 guitar 461→198 秒（約 2.3×），但 karaoke-gabox 只快 7%。
  正確性已驗：MPS 與 CPU 輸出相關係數 0.9999999999、逐樣本最大差 2.8e-05（float32 捨入）。
- SIGN (macos): **效能比較一律用真實長度的檔案，短片段會系統性低估加速比。** 同一個 guitar 模型：
  30 秒片段量出 CPU 56 s vs MPS 36 s ＝ 1.56×；整首 246.72 秒量出 CPU ~461 s vs MPS 198 s ＝ 2.3×。
  差距全部來自模型載入與暖機這類**固定成本**——片段越短，固定成本在總時間裡的佔比越大，
  把兩邊的比值往 1 壓。30 秒片段拿來驗「功能會不會壞」很好用（LESSONS 2026-09-06 已記），
  但拿來量「快多少」會得到偏保守的錯誤結論。
- SIGN (2026-09-14): **不可以用「同家族、同檔案大小」去推論一個沒量過的模型要跑多久。**
  karaoke-gabox 與 melband-roformer-kim-vocals 都是 913 MB、同為 MelBand RoFormer，
  實測 karaoke **10.2× realtime**、kim-vocals **2.3–2.9× realtime**，差約四倍。
  2026-09-12 那條「RoFormer 耗時由序列長度決定、與 checkpoint 大小幾乎無關」說的是
  「**大小不能用來預測速度**」，不是「同大小的模型速度相近」——本輪就是把它誤讀成後者，
  推出「karaoke 約 10 分鐘」而實際是約 45 分鐘，差點讓 UI 的預估值寫錯四倍。
  沒量過的模型就去量，不要外推。
- SIGN (macos, 工具面): 上游在推論開始時印的
  `Estimated total processing time for this track: <n> seconds` **是可信的**，不必等跑完。
  驗證方法：取樣兩次 `Estimated time remaining`，比對「估計值下降量 ÷ 牆鐘經過秒數」，
  比值接近 1.0 就代表估計準確（本輪量到 1.08×）。這讓「要跑 45 分鐘的模型」可以在 90 秒內
  得到可信的總時長，不必真的等完。
- SIGN (macos): 這台 M1 Air 無風扇，**持續負載會降頻，所有效能數字都帶著「機器當時多熱」這個隱藏變數**。
  同一支腳本、同一首歌、同樣 `--device auto` 量 guitar：冷機 **189 s**、`.app` 內冷機 198 s、
  goal_check 剛跑完 84 秒後的熱機 **243 s**（慢 25%）；而 goal_check 內部（接在一段 434 秒的
  vocals 之後）worker 自己估的推論時間是 269 s。量測前記錄熱狀態，要比較裝置差異就必須在
  可比的熱狀態下量。也因此 **「跑一次量到的時間」不等於「使用者一直用會看到的時間」**——
  UI 的預估值取整往上留餘裕不只是保守，是在吸收這件事。
- SIGN (macos): 承上，`goal_check` 每個模式印的秒數是「匯入 + worker 冷啟動 + 推論 + 兩次快速匯出
  （各寫 87 MB）」，**不是純推論**，固定成本在這首 246.7 秒的歌上約 175 秒。拿它跟直接叫用 worker
  的數字相比會得到「修正沒生效」的錯誤結論（實際是 444 s vs 189 s，差距由固定成本＋降頻構成）。
  要判斷 worker 本身多快，讀 log 裡上游倒數的起始值，不要用 goal_check 的總秒數。
  **這條與「短片段會低估加速比」是一對：兩者都是「量到的不是你以為在量的東西」，
  一個被固定成本污染，一個被機器狀態污染。**
- SIGN (macos): **Rosetta 2 的翻譯快取綁在「那一份檔案」上，搬動或複製之後要重新付一次。**
  同一個 x86 worker：在 `build/` 內第一次跑 `ready in 58523 ms`、第二次 5279 ms、第三次 4637 ms；
  把 `.app` 複製到 `/Applications` 之後，第一次又變成 **71766 ms**。所以量 Intel 版效能時，
  「同一份檔案跑第二次」與「複製一份跑第一次」是兩個不同的數字，不能互相取代；
  而使用者實際會遇到的是後者（他安裝完之後的第一次）。文件要寫的也是後者。
- SIGN (macos, 文件驗證): 給終端使用者的說明要**照著走一遍**，而且規則是「只做文件寫的事，
  遇到沒寫的就停下來記錄」——用已知的東西補上去，正是讀者做不到的那一步。這樣走一次抓到的
  東西與功能測試完全不同類：本輪抓到 (a) `sysctl -n hw.optional.avx2_0` 在 Apple Silicon 上
  **結束碼 0、stdout 空、stderr 空**，於是「四行分別是…」的位置對應會**無聲錯位**，
  讀者會回報「AVX2 = 26.2、macOS = arm64」而毫無理由懷疑——**要求讀者按行序解讀輸出的指令，
  少一行就會污染其餘每一行，每行自帶標籤才安全**；(b) 編號清單與下方敘述句互相矛盾
  （清單對、敘述錯），作者自己讀不出來，因為會自動用對的那一半去理解錯的那一半。
- SIGN (macos, 文件驗證): 本機建置的 `.app` **沒有 quarantine 旗標**，所以雙擊直接開、
  Gatekeeper 那一步在開發機上**完全不會發生**——照著走會得到「這步通過」但其實沒被測到。
  要驗它必須手動模擬從網路收到：
  `xattr -w com.apple.quarantine "0083;$(printf %x $(date +%s));Safari;" <app>`，
  之後 `spctl -a -vv` 應回 `rejected`、`open` 應被擋。驗完 `xattr -d com.apple.quarantine` 還原。
  但**「系統設定」裡的實際措辭與位置仍然驗不到**（需要 GUI），那部分只能標成未驗證，
  不要憑印象寫進給使用者的文件。
