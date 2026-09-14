#include "Localization.h"

#include <array>
#include <unordered_map>

namespace htfx {

namespace {

using StringPair = std::array<juce::String, 2>;

const std::unordered_map<std::string, StringPair>& stringTable() {
    static const std::unordered_map<std::string, StringPair> table{
        // key                            { zh-TW,                          en }
        {"editor.title", StringPair{u8"Music SSP FX — 音源分離", u8"Music SSP FX — Source Separation"}},
        {"label.separationMode", StringPair{u8"分離模式", u8"Separation mode"}},
        {"label.mode", StringPair{u8"模式", u8"Mode"}},
        {"label.outputTrim", StringPair{u8"輸出增益", u8"Output Trim"}},
        {"button.bypass",
         StringPair{u8"略過（預覽原始錄音）",
                    u8"Bypass (preview original recording)"}},
        {"button.separate", StringPair{u8"分離", u8"Separate"}},
        {"button.cancel", StringPair{u8"取消", u8"Cancel"}},
        {"group.preview", StringPair{u8"預覽", u8"Preview"}},
        {"button.previewStop", StringPair{u8"停止", u8"Stop"}},
        {"label.inferenceWindow", StringPair{u8"推論視窗長度", u8"Inference window"}},
        {"label.demucsModel", StringPair{u8"Demucs 模型", u8"Demucs model"}},
        {"label.roformerCategory", StringPair{u8"RoFormer 分類", u8"RoFormer category"}},
        {"label.roformerSearch", StringPair{u8"搜尋模型", u8"Search models"}},
        {"label.roformerModel", StringPair{u8"RoFormer 模型", u8"RoFormer model"}},
        {"label.roformerStatus", StringPair{u8"下載狀態", u8"Download status"}},
        {"label.computeDevice", StringPair{u8"運算裝置", u8"Compute device"}},
        {"label.gpuIndex", StringPair{u8"CUDA GPU 編號", u8"CUDA GPU index"}},
        {"button.resetWorker", StringPair{u8"重啟 Worker", u8"Reset worker"}},
        // Shows the language you'd switch TO, so it is intentionally
        // indexed by the CURRENT language rather than its own value.
        {"button.languageToggle", StringPair{"EN", u8"中文"}},
        // 初階面板的兩選一。軸線是「品質 vs 時間」，不是模型名稱——初階面板的
        // 使用者不需要知道 HTDemucs 與 RoFormer 的差別，只需要知道代價。
        {"button.qualityStandard", StringPair{u8"一般", u8"Standard"}},
        {"button.qualityHigh", StringPair{u8"高品質", u8"High quality"}},
        {"hint.qualityStandard",
         StringPair{u8"一般情況下這樣就分得夠好了。",
                    u8"Good enough for most songs."}},
        {"hint.qualityHigh",
         StringPair{u8"保留和聲（只拿掉主唱），品質更好。",
                    u8"Keeps the backing vocals; only the lead is removed."}},
        {"hint.qualityNeedsGpu",
         StringPair{u8"高品質模式需要 NVIDIA 顯示卡；你安裝的是 CPU 版，"
                    u8"它在 CPU 上一首歌要 50 分鐘左右。",
                    u8"High quality needs an NVIDIA GPU. This is the CPU build, "
                    u8"where it would take about 50 minutes per song."}},
        {"hint.qualityNeedsGpuMac",
         StringPair{u8"高品質模式需要 Apple Silicon 的 GPU；這台是 Intel Mac，"
                    u8"它在 CPU 上一首歌要 50 分鐘左右。",
                    u8"High quality needs an Apple Silicon GPU. This is an Intel "
                    u8"Mac, where it would take about 50 minutes per song."}},
        // 量測自本專案自己的 runtime，不是估計值。CPU/CUDA 量在 RTX 4050
        // Laptop 的開發機；MPS 量在 MacBook Air M1，換算成 4 分鐘的歌。
        // 兩個 high 的數字量的都是 karaoke-gabox 本人，也就是「高品質」實際會
        // 跑的那個模型：MPS 10.2x realtime（41 分鐘）、RTX 4050 0.911x
        // （3.6 分鐘），各自取整往上留餘裕。不要拿同家族的其他模型推論它們：
        // 同為 913 MB 的 kim-vocals 在 MPS 上只要 2.3-2.9x，差了四倍。
        // 41 分鐘是冷機值：M1 Air 無風扇，連續分離第二首時同一個工作慢約
        // 25%（189 秒冷 / 243 秒熱）。取整到 45 留的餘裕就是給這件事的。
        // highCpu 同樣量的是 karaoke-gabox：本機 x86 CPU 11.56x realtime
        // （46 分鐘），M1 的 CPU 核心量到 10.95x，兩個不同 ISA 收斂在同一個
        // 數字上。先前寫的 97 分鐘沒有出處，高估了一倍以上。
        {"estimate.standardCpu", StringPair{u8"3 分鐘", u8"3 minutes"}},
        {"estimate.standardGpu", StringPair{u8"10 秒", u8"10 seconds"}},
        {"estimate.standardMps", StringPair{u8"1 分鐘", u8"1 minute"}},
        {"estimate.highCpu", StringPair{u8"50 分鐘", u8"50 minutes"}},
        {"estimate.highGpu", StringPair{u8"4 分鐘", u8"4 minutes"}},
        {"estimate.highMps", StringPair{u8"45 分鐘", u8"45 minutes"}},
        {"hint.estimatePrefix", StringPair{u8"一首 4 分鐘的歌約 ", u8"About "}},
        {"hint.estimateSuffix", StringPair{u8"", u8" for a 4-minute song"}},
        {"button.exportVocalsOnly", StringPair{u8"僅匯出人聲", u8"Export Vocals only"}},
        {"button.exportAccompanyOnly",
         StringPair{u8"僅匯出伴奏", u8"Export Accompany only"}},
        {"clip.statusPending", StringPair{u8"待分離", u8"Pending"}},
        {"clip.statusSeparating", StringPair{u8"分離中…", u8"Separating..."}},
        {"clip.statusDone", StringPair{u8"完成", u8"Done"}},
        {"clip.statusFailed", StringPair{u8"失敗", u8"Failed"}},
        {"clip.importedCountPrefix", StringPair{u8"已匯入 ", u8"Imported "}},
        {"clip.importedCountSuffix", StringPair{u8" 個檔案，按「僅匯出人聲／伴奏」即可整批處理（進階面板可先按「分離」）", u8" files - Export Vocals/Accompany only processes them all (or press Separate on the advanced panel)"}},
        {"clip.importSkippedPrefix", StringPair{u8"；", u8"; "}},
        {"clip.importSkippedMiddle", StringPair{u8" 個無法讀取，已略過：", u8" could not be read and were skipped: "}},
        {"clip.batchSeparationFinished", StringPair{u8"全部分離完成", u8"All clips separated"}},
        {"clip.exportFinishedPrefix", StringPair{u8"已匯出 ", u8"Exported "}},
        {"clip.exportFinishedSuffix", StringPair{u8" 個檔案", u8" files"}},
        {"clip.noneSelected", StringPair{u8"沒有勾選任何檔案", u8"No files selected"}},
        {"clip.exportingPrefix", StringPair{u8"正在匯出 ", u8"Exporting "}},
        {"status.cpuModeWarning", StringPair{u8"CPU 模式：一首 4 分鐘的歌約 3 分鐘。RoFormer 的高品質模式需要 NVIDIA 顯示卡，在 CPU 上一首歌要 50 分鐘左右，因此未提供。", u8"CPU mode: about 3 minutes for a 4-minute song. The high-quality RoFormer modes need an NVIDIA GPU -- on CPU they take about 50 minutes per song, so they are not offered."}},
        {"clip.chooseExportFolder", StringPair{u8"選擇匯出資料夾", u8"Choose export folder"}},
        {"button.import", StringPair{u8"匯入音訊／影片", u8"Import audio/video"}},
        {"status.readyToRecord", StringPair{u8"準備就緒，可以錄音或匯入", u8"Ready to record or import"}},
        {"status.recording", StringPair{u8"錄音中", u8"Recording"}},
        {"status.readyToSeparate", StringPair{u8"音訊已就緒", u8"Audio ready"}},
        // Which button to press next, chosen by the panel that is showing.
        {"hint.pressSeparate", StringPair{u8" — 請按下「分離」", u8" - press Separate"}},
        {"hint.downloadsModelFirst",
         StringPair{u8"（第一次會先自動下載 84 MB 的預設模型）",
                    u8" (the 84 MB default model is downloaded first)"}},
        {"hint.pressQuickExport",
         StringPair{u8" — 請按下「僅匯出人聲」或「僅匯出伴奏」",
                    u8" - press Export Vocals or Export Accompaniment"}},
        {"status.loadingModel", StringPair{u8"載入模型中", u8"Loading model"}},
        {"status.separating", StringPair{u8"分離中", u8"Separating"}},
        {"status.readyToPreview", StringPair{u8"分離完成，可試聽或匯出", u8"Ready to preview or export"}},
        {"status.error", StringPair{u8"發生錯誤", u8"Error"}},
        {"status.cancelled", StringPair{u8"已取消", u8"Cancelled"}},
        {"status.unknown", StringPair{u8"未知狀態", u8"Unknown"}},
        {"step.import", StringPair{u8"1  匯入檔案", u8"1  Import"}},
        {"phase.downloadModel", StringPair{u8"下載模型", u8"Downloading the model"}},
        {"phase.import", StringPair{u8"匯入檔案", u8"Importing"}},
        {"phase.loadingModel", StringPair{u8"載入模型", u8"Loading the model"}},
        {"phase.separating", StringPair{u8"分離中", u8"Separating"}},
        {"phase.exportVocals", StringPair{u8"匯出人聲", u8"Exporting the vocals"}},
        {"phase.exportAccompaniment", StringPair{u8"匯出伴奏", u8"Exporting the accompaniment"}},
        {"phase.exportStems", StringPair{u8"匯出各軌", u8"Exporting the stems"}},
        {"phase.exportMix", StringPair{u8"匯出混音", u8"Exporting the mix"}},
        {"phase.exportVideo", StringPair{u8"把混音寫回影片", u8"Writing the mix into the video"}},
        {"phase.batchSeparate", StringPair{u8"批次分離", u8"Separating all clips"}},
        {"phase.batchExport", StringPair{u8"批次匯出", u8"Exporting all clips"}},
        {"phase.recording", StringPair{u8"錄音中", u8"Recording"}},
        {"step.separate", StringPair{u8"2  分離", u8"2  Separate"}},
        {"step.export", StringPair{u8"3  匯出", u8"3  Export"}},
        {"button.export", StringPair{u8"匯出", u8"Export"}},
        {"button.scaleUi", StringPair{u8"縮放介面", u8"Scale UI"}},
        {"combo.separationModePlaceholder",
         StringPair{u8"選擇分離模式…", u8"Choose a separation mode..."}},
        {"combo.separationMode4Stem",
         StringPair{u8"4 軌分離", u8"4-stem separation"}},
        {"combo.separationMode6Stem",
         StringPair{u8"6 軌分離", u8"6-stem separation"}},
        {"combo.modeRecord", StringPair{u8"錄音模式", u8"Record mode"}},
        {"combo.modeRealtime",
         StringPair{u8"即時模式（極高延遲）",
                    u8"Realtime mode (Ultra high latency)"}},
        {"combo.roformerAllCategories", StringPair{u8"全部分類", u8"All categories"}},
        {"placeholder.roformerSearch",
         StringPair{u8"模型名稱或代號", u8"Name or model ID"}},
        {"label.noMediaSelected",
         StringPair{u8"尚未選擇檔案 — 按下方按鈕，或直接把音訊／影片拖進視窗",
                    u8"No file yet — use the button below, or drop audio/video onto the window"}},
        {"hint.dropToImport", StringPair{u8"放開即匯入", u8"Drop to import"}},
        {"button.openOutput", StringPair{u8"開啟輸出位置", u8"Show output"}},
        {"tip.openOutput", StringPair{u8"在檔案總管中顯示最近一次匯出的檔案或資料夾",
                                      u8"Reveal the most recent export in Explorer"}},
        {"status.dropNothingUsable", StringPair{u8"拖進來的項目不是可讀取的檔案（例如壓縮檔或手機裡的內容），請先複製到磁碟再匯入",
                                                u8"The dropped items are not readable files (for example inside a zip or on a phone); copy them to disk first"}},
        {"status.mixExportCancelledVideo", StringPair{u8"影片回填已取消", u8"Video mix-back cancelled"}},
        {"status.mediaImportInProgress", StringPair{u8"正在匯入", u8"Importing"}},
        {"clip.importSkippedMore", StringPair{u8" 等", u8" and more"}},
        {"status.dropIgnoredBusy", StringPair{u8"目前有工作進行中，等它完成後再拖入檔案", u8"Busy right now; drop the file again once the current job finishes"}},
        {"tip.import", StringPair{u8"選擇一個或多個音訊／影片檔（Ctrl+O；也可以直接拖進視窗）",
                                  u8"Choose one or more audio/video files (Ctrl+O; or drop them onto the window)"}},
        {"tip.exportVocals", StringPair{u8"只輸出人聲軌為 WAV；尚未分離時會先自動分離",
                                        u8"Write only the vocals as WAV; separates first if needed"}},
        {"tip.exportAccompany", StringPair{u8"只輸出伴奏（去人聲）為 WAV；尚未分離時會先自動分離",
                                           u8"Write only the accompaniment (no vocals) as WAV; separates first if needed"}},
        {"tip.separate", StringPair{u8"用目前的分離模式處理已匯入的檔案",
                                    u8"Run the chosen separation on the imported file(s)"}},
        {"tip.export", StringPair{u8"匯出各軌、混音，或把混音回填進原本的影片（Ctrl+E）",
                                  u8"Export stems, the mix, or write the mix back into the video (Ctrl+E)"}},
        {"tip.record", StringPair{u8"從輸入裝置錄音，錄完再分離", u8"Record from the input device, then separate"}},
        {"tip.cancel", StringPair{u8"停止目前的匯入、下載或分離（Esc）", u8"Stop the current import, download or separation (Esc)"}},
        {"tip.panelSwitch", StringPair{u8"切換簡易面板與進階面板（分軌音量、預覽、模型設定）",
                                       u8"Switch between the simple panel and the advanced panel (stem levels, preview, model settings)"}},
        {"tip.language", StringPair{u8"切換介面語言", u8"Switch the interface language"}},
        {"tip.separationMode", StringPair{u8"要分出什麼：4／6 軌、人聲、伴奏、卡拉 OK、吉他、去混響、去噪…",
                                          u8"What to separate: 4/6 stems, vocals, accompaniment, karaoke, guitar, de-reverb, denoise..."}},
        {"tip.preview", StringPair{u8"試聽目前的混音（空白鍵）", u8"Listen to the current mix (Space)"}},
        {"tip.bypass", StringPair{u8"勾選後預覽原始錄音，方便比較", u8"Preview the original recording for comparison"}},
        {"button.record", StringPair{u8"錄音", u8"Record"}},
        {"button.stopRecording", StringPair{u8"停止錄音", u8"Stop recording"}},
        {"button.play", StringPair{u8"播放", u8"Play"}},
        {"button.pause", StringPair{u8"暫停", u8"Pause"}},
        {"button.modelInstalled", StringPair{u8"已安裝", u8"Installed"}},
        {"button.modelDownloading", StringPair{u8"下載中…", u8"Downloading..."}},
        {"button.downloadModel",
         StringPair{u8"下載所選模型", u8"Download selected model"}},
        {"button.fullScreen", StringPair{u8"全螢幕", u8"Full screen"}},
        {"button.exitFullScreen", StringPair{u8"結束全螢幕", u8"Exit full screen"}},
        {"button.advancedOptionsExpand",
         StringPair{u8"進階選項 >", u8"Advanced options >"}},
        {"button.advancedOptionsCollapse",
         StringPair{u8"進階選項 v", u8"Advanced options v"}},
        {"button.advancedPanel", StringPair{u8"進階面板", u8"Advanced panel"}},
        {"button.generalPanel", StringPair{u8"一般面板", u8"General panel"}},
        {"dialog.exportChooseTitle",
         StringPair{
             u8"選擇要以原始音量匯出的音軌，或匯出目前介面所聽到的混音。",
             u8"Choose original-volume stems, or export the mix currently "
             "heard in the interface."}},
        {"dialog.exportSelectedStems", StringPair{u8"匯出所選音軌", u8"Export selected stems"}},
        {"dialog.exportAllStems", StringPair{u8"匯出全部音軌", u8"Export all stems"}},
        {"dialog.exportMix", StringPair{u8"匯出混音", u8"Export mix"}},
        {"dialog.close", StringPair{u8"關閉", u8"Close"}},
        {"dialog.audioOnlyWav", StringPair{u8"僅音訊（.wav）", u8"Audio only (.wav)"}},
        {"dialog.videoWithMixedAudio",
         StringPair{u8"含混音音訊的影片（.mp4）", u8"Video with mixed audio (.mp4)"}},
        {"dialog.noteVideoExport",
         StringPair{u8"MP4 匯出會複製原始影像串流，僅取代其音訊。",
                    u8"MP4 export copies the original video stream and "
                    "replaces only its audio."}},
        {"dialog.noteStemExport",
         StringPair{u8"個別音軌不受介面增益控制影響，保留 Demucs 原始輸出音量。",
                    u8"Individual stems ignore the interface gain controls "
                    "and preserve Demucs output level."}},
        {"alert.noStemsSelectedTitle", StringPair{u8"尚未選擇音軌", u8"No stems selected"}},
        {"alert.noStemsSelectedMessage",
         StringPair{u8"請至少選擇一個音軌後再匯出。",
                    u8"Select at least one stem before exporting."}},
        {"filechooser.stemFolderTitle",
         StringPair{u8"選擇音軌 WAV 檔案的儲存資料夾",
                    u8"Choose a folder for the stem WAV files"}},
        {"filechooser.exportVideoWithMix",
         StringPair{u8"匯出含介面混音的影片", u8"Export video with the interface mix"}},
        {"filechooser.exportMix", StringPair{u8"匯出介面混音", u8"Export the interface mix"}},
        {"filechooser.importMediaTitle",
         StringPair{u8"匯入音訊或影片檔案", u8"Import an audio or video file"}},
        {"filechooser.exportVocalsTitle", StringPair{u8"匯出人聲", u8"Export vocals"}},
        {"filechooser.exportAccompanyTitle",
         StringPair{u8"匯出伴奏", u8"Export accompaniment"}},
        {"alert.importMediaFirstTitle",
         StringPair{u8"請先匯入媒體", u8"Import media first"}},
        {"alert.importMediaFirstMessage",
         StringPair{u8"請先選擇音訊或影片檔案再匯出。",
                    u8"Choose an audio or video file before exporting."}},
        {"alert.nothingToExportTitle",
         StringPair{u8"沒有可匯出的內容", u8"Nothing to export"}},
        {"alert.nothingToExportMessage",
         StringPair{u8"請先匯入或錄製音訊，並執行分離後再匯出。",
                    u8"Import or record audio, then run Separate before "
                    "exporting."}},
        {"dialog.exportStemsOrMixTitle",
         StringPair{u8"匯出 HTDemucs 音軌或混音", u8"Export HTDemucs stems or mix"}},
        {"roformer.tagExperimental",
         StringPair{u8" 【實驗性】", u8" [Experimental]"}},
        {"roformer.tagAudited", StringPair{u8" 【已審核】", u8" [Audited]"}},
        {"roformer.noMatchingModels",
         StringPair{u8"沒有符合的模型", u8"No matching models"}},
        {"roformer.unknownModel", StringPair{u8"未知模型", u8"Unknown model"}},
        {"roformer.statusExperimental", StringPair{u8"實驗性", u8"Experimental"}},
        {"roformer.statusAudited", StringPair{u8"已審核", u8"Audited"}},
        {"roformer.statusDownloaded", StringPair{u8"已下載", u8"Downloaded"}},
        {"roformer.statusNotDownloaded",
         StringPair{u8"未下載", u8"Not downloaded"}},
        {"error.ffmpegNotStarted",
         StringPair{u8"無法啟動 FFmpeg。可攜式 FFmpeg 執行環境遺失或無效。",
                    u8"FFmpeg could not be started. The portable FFmpeg runtime "
                    "is missing or invalid."}},
        {"error.mediaOperationCancelled",
         StringPair{u8"媒體操作已取消", u8"Media operation cancelled"}},
        {"error.ffmpegFailedPrefix",
         StringPair{u8"FFmpeg 失敗（結束碼 ", u8"FFmpeg failed (exit "}},
        {"error.ffmpegFailedSuffix", StringPair{u8"）：", u8"): "}},
        {"error.unsupportedAudioFile",
         StringPair{u8"不支援或無法讀取的音訊檔案：",
                    u8"Unsupported or unreadable audio file: "}},
        {"error.mediaDurationInvalid",
         StringPair{u8"媒體時長為零，或過長而無法載入記憶體",
                    u8"The media duration is empty or too large to hold in "
                    "memory"}},
        {"error.audioStreamDecodeFailed",
         StringPair{u8"無法解碼音訊串流", u8"The audio stream could not be decoded"}},
        {"error.audioStreamNoSampleRate",
         StringPair{u8"音訊串流沒有有效的取樣率",
                    u8"The audio stream has no valid sample rate"}},
        {"error.resampledAudioTooLarge",
         StringPair{u8"重新取樣後的音訊過大，無法載入記憶體",
                    u8"The resampled audio is too large to hold in memory"}},
        {"error.noAudioToExport",
         StringPair{u8"沒有可匯出的音訊", u8"There is no audio to export"}},
        {"error.couldNotCreateOutputFolder",
         StringPair{u8"無法建立輸出資料夾：", u8"Could not create output folder: "}},
        {"error.couldNotOpenOutputFile",
         StringPair{u8"無法開啟輸出檔案：", u8"Could not open output file: "}},
        {"error.couldNotCreateWavWriter",
         StringPair{u8"無法建立 32-bit float WAV 寫入器",
                    u8"Could not create a 32-bit float WAV writer"}},
        {"error.wavWriteFailed",
         StringPair{u8"寫入 WAV 檔案失敗", u8"Writing the WAV file failed"}},
        {"error.couldNotReplaceExistingFile",
         StringPair{u8"無法取代既有檔案：", u8"Could not replace existing file: "}},
        {"error.couldNotCommitOutputFile",
         StringPair{u8"無法完成輸出檔案：", u8"Could not commit output file: "}},
        {"status.switchToRecordModeFirst",
         StringPair{u8"請先切換到錄音模式", u8"Switch to Record mode first"}},
        {"status.recordingRequires44100Hz",
         StringPair{u8"錄音需要 44,100 Hz 取樣率", u8"Recording requires 44,100 Hz"}},
        {"status.recordingStereoInput",
         StringPair{u8"正在錄製立體聲輸入", u8"Recording stereo input"}},
        {"status.noInputRecorded",
         StringPair{u8"未錄製到任何輸入", u8"No input was recorded"}},
        {"status.recordedPrefix", StringPair{u8"已錄製 ", u8"Recorded "}},
        {"status.recordedSuffix", StringPair{u8" 秒", u8" seconds"}},
        {"status.waitForMediaOperation",
         StringPair{u8"請等待媒體操作完成", u8"Wait for the media operation to finish"}},
        {"status.modelNotInstalledPrefix", StringPair{u8"模型 ", u8"Model "}},
        {"status.roformerStartingEngine",
         StringPair{u8"正在啟動分離引擎（首次啟動較慢）…",
                    u8"Starting the separation engine (slower on first run)..."}},
        {"status.roformerPreparing",
         StringPair{u8"正在準備模型…", u8"Preparing the model..."}},
        {"status.roformerDownloading",
         StringPair{u8"正在下載模型 ", u8"Downloading "}},
        {"status.roformerLoadingModel",
         StringPair{u8"正在載入模型到運算裝置：", u8"Loading onto the device: "}},
        {"status.roformerSeparating", StringPair{u8"分離中", u8"Separating"}},
        {"status.roformerRemainingPrefix",
         StringPair{u8"剩餘約 ", u8"about "}},
        {"status.roformerVerifying",
         StringPair{u8"正在驗證輸出…", u8"Verifying the output..."}},
        {"status.secondsSuffix", StringPair{u8" 秒", u8"s"}},
        {"status.minutesSuffix", StringPair{u8" 分 ", u8"m "}},
        {"status.downloadingModelPrefix", StringPair{u8"正在下載模型 ", u8"Downloading "}},
        {"status.downloadingModelSuffix",
         StringPair{u8"，下載完成後會自動開始分離。",
                    u8" — separation starts automatically once it finishes."}},
        {"status.modelNotInstalledSuffix",
         StringPair{u8"尚未安裝。請開啟「進階選項」並下載此模型。",
                    u8" is not installed. Open Advanced options and download it "
                    "first."}},
        {"status.recordBeforeSeparating",
         StringPair{u8"請先錄製立體聲音訊再進行分離",
                    u8"Record some stereo audio before separating"}},
        {"status.startingDemucsWorker",
         StringPair{u8"正在準備分離…", u8"Preparing to separate..."}},
        {"status.cancellingAfterBlock",
         StringPair{u8"正在取消 · 將於目前推論區塊結束後停止",
                    u8"Cancelling after the current inference block"}},
        {"status.separationCancelled",
         StringPair{u8"分離已取消", u8"Separation cancelled"}},
        {"status.readyToPreviewFakeWorker",
         StringPair{u8"已可預覽 - 模擬 worker", u8"Ready to preview - fake worker"}},
        {"status.roformerPathsUnavailable",
         StringPair{
             u8"RoFormer 的 Python、worker、模型快取或輸出資料夾無法使用",
             u8"RoFormer Python, worker, model cache, or output directory is "
             "unavailable"}},
        {"status.loadingRoformerPrefix",
         StringPair{u8"正在載入 RoFormer ", u8"Loading RoFormer "}},
        {"status.couldNotStartRoformerWorker",
         StringPair{u8"無法啟動 RoFormer Python worker",
                    u8"Could not start the RoFormer Python worker"}},
        {"status.roformerSeparationCancelled",
         StringPair{u8"RoFormer 分離已取消", u8"RoFormer separation cancelled"}},
        {"status.roformerWorkerFailedPrefix",
         StringPair{u8"RoFormer worker 執行失敗（結束碼 ",
                    u8"RoFormer worker failed (exit "}},
        {"status.roformerWorkerFailedSuffix", StringPair{u8"）：", u8"): "}},
        {"status.roformerWrongStemCountPrefix",
         StringPair{u8"RoFormer worker 回傳了 ", u8"RoFormer worker returned "}},
        {"status.roformerWrongStemCountSuffix",
         StringPair{u8" 個 WAV 檔案，預期應為兩軌",
                    u8" WAV files; expected two stems"}},
        {"status.roformerStemDurationMismatch",
         StringPair{u8"RoFormer 音軌時長與輸入不符",
                    u8"RoFormer stem duration does not match the input"}},
        {"status.readyToPreviewPrefix",
         StringPair{u8"準備預覽 · ", u8"Ready to preview · "}},
        {"status.readyToPreviewRoformerSuffix",
         StringPair{u8" · 雙軌", u8" · two stems"}},
        {"status.loadingModelPrefix", StringPair{u8"正在載入 ", u8"Loading "}},
        {"status.loadingModelDeviceMiddle",
         StringPair{u8"，裝置：", u8" on "}},
        {"status.loadingModelDeviceSuffix",
         StringPair{u8" · 首次載入可能需要一些時間",
                    u8" · first load can take a while"}},
        {"status.cpuInferenceSlowSuffix",
         StringPair{u8" · CPU 推論可能需要較長時間",
                    u8" · CPU inference may take a long time"}},
        {"status.separatingPrefix", StringPair{u8"分離中 · ", u8"Separating · "}},
        {"status.separatingBlockMiddle",
         StringPair{u8" · 區塊 ", u8" · block "}},
        {"status.decodingImportedMedia",
         StringPair{u8"正在解碼匯入的媒體", u8"Decoding imported media"}},
        {"status.importingPrefix", StringPair{u8"匯入中 ", u8"Importing "}},
        {"status.mediaImportCancelled",
         StringPair{u8"媒體匯入已取消", u8"Media import cancelled"}},
        {"status.importedPrefix", StringPair{u8"已匯入 ", u8"Imported "}},
        {"status.importedSuffix", StringPair{u8" 秒)", u8" s)"}},
        {"status.mediaImportFailedPrefix",
         StringPair{u8"媒體匯入失敗：", u8"Media import failed: "}},
        {"status.cancellingMediaOperation",
         StringPair{u8"正在取消媒體操作", u8"Cancelling media operation"}},
        {"status.switchToRecordModeBeforeImport",
         StringPair{u8"請先切換到錄音模式再匯入媒體",
                    u8"Switch to Record mode before importing media"}},
        {"status.mediaPathTooLong",
         StringPair{u8"檔案路徑太長（超過 260 字元），請把檔案移到較短的資料夾後再匯入",
                    u8"The file path is longer than 260 characters; move the file to a shorter folder and import again"}},
        {"status.mediaBusyRetryLater",
         StringPair{u8"目前有匯入或匯出進行中，請等它完成", u8"An import or export is still running; wait for it to finish"}},
        {"status.selectedMediaFileNotFound",
         StringPair{u8"所選媒體檔案不存在", u8"The selected media file does not exist"}},
        {"status.separateBeforeExportingStems",
         StringPair{u8"請先分離音訊再匯出音軌",
                    u8"Separate some audio before exporting stems"}},
        {"status.selectAtLeastOneStemToExport",
         StringPair{u8"請至少選擇一個音軌以匯出",
                    u8"Select at least one stem to export"}},
        {"status.exportingOriginalVolumeStems",
         StringPair{u8"正在匯出原始音量音軌", u8"Exporting original-volume stems"}},
        {"status.stemExportCancelled",
         StringPair{u8"音軌匯出已取消", u8"Stem export cancelled"}},
        {"status.separateBeforeQuickExport",
         StringPair{u8"請先分離音訊再使用快速匯出",
                    u8"Separate some audio before using quick export"}},
        {"status.quickExportRequiresHtdemucs",
         StringPair{u8"快速匯出需要預設 htdemucs 模型的分離結果",
                    u8"Quick export requires a result from the default "
                    "htdemucs model"}},
        {"status.chooseDifferentOutputNameProtected",
         StringPair{u8"請選擇不同的輸出檔名；匯入的來源檔案受保護",
                    u8"Choose a different output name; the imported source "
                    "is protected"}},
        // The quick exports write the separated stem with the output gain
        // applied, so the file matches what the preview plays.
        {"status.exportingVocalsOriginalLevel",
         StringPair{u8"正在匯出人聲（套用輸出增益）",
                    u8"Exporting vocals (output gain applied)"}},
        {"status.exportingAccompanyOriginalLevel",
         StringPair{u8"正在匯出伴奏（套用輸出增益）",
                    u8"Exporting accompaniment (output gain applied)"}},
        {"status.quickExportCancelled",
         StringPair{u8"快速匯出已取消", u8"Quick export cancelled"}},
        {"status.separateBeforeExportingMix",
         StringPair{u8"請先分離音訊再匯出混音",
                    u8"Separate some audio before exporting a mix"}},
        {"status.videoExportRequiresImportedVideo",
         StringPair{u8"僅在匯入影片時才能匯出影片",
                    u8"Video export is available only when a video was "
                    "imported"}},
        {"status.mixingReplacingVideoAudio",
         StringPair{u8"正在混音並取代影片音軌",
                    u8"Mixing and replacing the video audio track"}},
        {"status.exportingCurrentInterfaceMix",
         StringPair{u8"正在匯出目前介面混音", u8"Exporting the current interface mix"}},
        {"status.mixExportCancelled",
         StringPair{u8"混音匯出已取消", u8"Mix export cancelled"}},
        {"status.stemExportSuccessPrefix", StringPair{u8"已匯出 ", u8"Exported "}},
        {"status.stemExportSuccessMiddle",
         StringPair{u8" 個原始音量音軌 WAV 檔案至 ",
                    u8" original-volume stem WAV file(s) to "}},
        {"status.stemExportFailedPrefix",
         StringPair{u8"音軌匯出失敗：", u8"Stem export failed: "}},
        {"status.quickExportedVocalsPrefix",
         StringPair{u8"已匯出人聲：", u8"Exported vocals: "}},
        {"status.quickExportedAccompanyPrefix",
         StringPair{u8"已匯出伴奏：", u8"Exported accompaniment: "}},
        {"status.quickExportFailedPrefix",
         StringPair{u8"快速匯出失敗：", u8"Quick export failed: "}},
        {"status.mixExportedPrefix",
         StringPair{u8"已匯出介面混音：", u8"Exported interface mix: "}},
        {"status.mixReencodingVideo",
         StringPair{u8"來源影片的編碼無法直接放進 MP4，正在重新編碼影片…",
                    u8"The source video can't be stream-copied into MP4; re-encoding the video…"}},
        {"status.mp4StreamCopyIncompatibleSuffix",
         StringPair{u8"（來源影片編碼可能與 MP4 串流複製不相容。）",
                    u8" (The source video codec may not be compatible with "
                    "MP4 stream copy.)"}},
        {"status.couldNotReplaceMp4OutputPrefix",
         StringPair{u8"無法取代所選 MP4 輸出檔案：",
                    u8"Could not replace the selected MP4 output file: "}},
        {"status.mixExportedMp4Prefix",
         StringPair{u8"已匯出含介面混音的 MP4：",
                    u8"Exported MP4 with the interface mix: "}},
        {"status.mixExportFailedPrefix",
         StringPair{u8"混音匯出失敗：", u8"Mix export failed: "}},
    };
    return table;
}

// Deliberately independent of the HTFX_DATA_DIR override used elsewhere in
// this codebase (models/checkpoint/worker resolution) — redirecting that
// shared variable just to isolate a language-preference test would risk
// side effects on unrelated model/worker path resolution. Tests that need
// isolation set HTFX_UI_LANGUAGE_FILE instead (see settingsFile() below).
juce::File settingsRoot(const juce::String& productName) {
#if JUCE_WINDOWS
    const auto localAppData =
        juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {}).trim();
    if (localAppData.isNotEmpty()) {
        return juce::File(localAppData).getChildFile(productName);
    }
#endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(productName);
}

// The product was renamed from "HTDemucs GPU FX" to "Music SSP FX"; keep the
// user's saved language/startup choices by migrating the old directory once.
juce::File settingsDirectory() {
    const auto current = settingsRoot("Music SSP FX");
    if (!current.isDirectory()) {
        const auto legacy = settingsRoot("HTDemucs GPU FX");
        if (legacy.isDirectory()) {
            current.getParentDirectory().createDirectory();
            legacy.copyDirectoryTo(current);
        }
    }
    return current;
}

Language languageFromTag(const juce::String& tag) {
    return tag.trim().equalsIgnoreCase("en") ? Language::en : Language::zhTW;
}

const char* tagFromLanguage(Language language) {
    return language == Language::en ? "en" : "zh-TW";
}

}  // namespace

Localization& Localization::instance() {
    static Localization singleton;
    return singleton;
}

Localization::Localization() { reload(); }

juce::File Localization::settingsFile() {
    const auto overridePath =
        juce::SystemStats::getEnvironmentVariable("HTFX_UI_LANGUAGE_FILE", {}).trim();
    if (overridePath.isNotEmpty()) {
        return juce::File(overridePath);
    }
    return settingsDirectory().getChildFile("ui-language.txt");
}

void Localization::reload() {
    const auto file = settingsFile();
    language_.store(file.existsAsFile() ? languageFromTag(file.loadFileAsString())
                                     : Language::zhTW, std::memory_order_release);
}

void Localization::setLanguage(Language language) {
    language_.store(language, std::memory_order_release);
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(tagFromLanguage(language));
}

namespace {

// English separation-mode and stem names to their Chinese meaning. Keyed on
// the lowercased English name so both the mode list and the fader labels --
// which derive their names independently, one from the catalogue's category
// and one from the stem ids the worker reports -- resolve through one table.
const std::unordered_map<std::string, juce::String>& glossTable() {
    static const std::unordered_map<std::string, juce::String> table{
        // separation modes / RoFormer categories
        {"vocals", u8"人聲"},
        {"instrumental", u8"伴奏"},
        {"karaoke", u8"卡拉 OK"},
        {"guitar", u8"吉他"},
        {"denoise", u8"去噪"},
        {"dereverb", u8"去混響"},
        {"aspiration", u8"氣音"},
        {"crowd", u8"群聲"},
        // stems that only appear on the faders
        {"residual", u8"其餘"},
        {"target", u8"目標"},
        {"clean", u8"乾淨"},
        {"noise", u8"雜訊"},
        {"dry", u8"無混響"},
        {"reverb", u8"混響"},
        {"bleed", u8"串音"},
        // HTDemucs stems
        {"drums", u8"鼓"},
        {"bass", u8"貝斯"},
        {"other", u8"其他"},
        {"piano", u8"鋼琴"},
    };
    return table;
}

}  // namespace

juce::String glossFor(const juce::String& englishName) {
    if (Localization::instance().getLanguage() != Language::zhTW) {
        return {};
    }
    const auto& table = glossTable();
    const auto entry = table.find(englishName.trim().toLowerCase().toStdString());
    return entry == table.end() ? juce::String{} : entry->second;
}

juce::String glossOpen() {
    return juce::String(u8"（");
}

juce::String glossed(const juce::String& englishName) {
    const auto gloss = glossFor(englishName);
    return gloss.isEmpty() ? englishName
                           : englishName + glossOpen() + gloss + juce::String(u8"）");
}

juce::String Localization::tr(const juce::String& key) const {
    const auto& table = stringTable();
    const auto entry = table.find(key.toStdString());
    if (entry == table.end()) {
        return key;
    }
    return entry->second[static_cast<std::size_t>(language_.load(std::memory_order_acquire))];
}

}  // namespace htfx
