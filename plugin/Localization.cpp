#include "Localization.h"

#include <array>
#include <unordered_map>

namespace htfx {

namespace {

using StringPair = std::array<juce::String, 2>;

const std::unordered_map<std::string, StringPair>& stringTable() {
    static const std::unordered_map<std::string, StringPair> table{
        // key                            { zh-TW,                          en }
        {"editor.title", StringPair{u8"HTDemucs 快速分離", "HTDemucs Quick Separation"}},
        {"label.separationMode", StringPair{u8"分離模式", "Separation mode"}},
        {"label.mode", StringPair{u8"模式", "Mode"}},
        {"label.outputTrim", StringPair{u8"輸出增益", "Output Trim"}},
        {"button.bypass",
         StringPair{u8"略過（預覽原始錄音）",
                    "Bypass (preview original recording)"}},
        {"button.separate", StringPair{u8"分離", "Separate"}},
        {"button.cancel", StringPair{u8"取消", "Cancel"}},
        {"group.preview", StringPair{u8"預覽", "Preview"}},
        {"button.previewStop", StringPair{u8"停止", "Stop"}},
        {"label.inferenceWindow", StringPair{u8"推論視窗長度", "Inference window"}},
        {"label.demucsModel", StringPair{u8"Demucs 模型", "Demucs model"}},
        {"label.roformerCategory", StringPair{u8"RoFormer 分類", "RoFormer category"}},
        {"label.roformerSearch", StringPair{u8"搜尋模型", "Search models"}},
        {"label.roformerModel", StringPair{u8"RoFormer 模型", "RoFormer model"}},
        {"label.roformerStatus", StringPair{u8"下載狀態", "Download status"}},
        {"label.computeDevice", StringPair{u8"運算裝置", "Compute device"}},
        {"label.gpuIndex", StringPair{u8"CUDA GPU 編號", "CUDA GPU index"}},
        {"button.resetWorker", StringPair{u8"重啟 Worker", "Reset worker"}},
        // Shows the language you'd switch TO, so it is intentionally
        // indexed by the CURRENT language rather than its own value.
        {"button.languageToggle", StringPair{"EN", u8"中文"}},
        {"button.exportVocalsOnly", StringPair{u8"僅匯出人聲", "Export Vocals only"}},
        {"button.exportAccompanyOnly",
         StringPair{u8"僅匯出伴奏", "Export Accompany only"}},
        {"button.import", StringPair{u8"匯入音訊／影片", "Import audio/video"}},
        {"button.export", StringPair{u8"匯出", "Export"}},
        {"button.scaleUi", StringPair{u8"縮放介面", "Scale UI"}},
        {"combo.separationModePlaceholder",
         StringPair{u8"選擇分離模式…", "Choose a separation mode..."}},
        {"combo.separationMode4Stem",
         StringPair{u8"4 軌分離", "4-stem separation"}},
        {"combo.separationMode6Stem",
         StringPair{u8"6 軌分離", "6-stem separation"}},
        {"combo.modeRecord", StringPair{u8"錄音模式", "Record mode"}},
        {"combo.modeRealtime",
         StringPair{u8"即時模式（極高延遲）",
                    "Realtime mode (Ultra high latency)"}},
        {"combo.roformerAllCategories", StringPair{u8"全部分類", "All categories"}},
        {"placeholder.roformerSearch",
         StringPair{u8"模型名稱或代號", "Name or model ID"}},
        {"label.noMediaSelected",
         StringPair{u8"尚未選擇音訊或影片檔案", "No audio or video selected"}},
        {"button.record", StringPair{u8"錄音", "Record"}},
        {"button.stopRecording", StringPair{u8"停止錄音", "Stop recording"}},
        {"button.play", StringPair{u8"播放", "Play"}},
        {"button.pause", StringPair{u8"暫停", "Pause"}},
        {"button.modelInstalled", StringPair{u8"已安裝", "Installed"}},
        {"button.modelDownloading", StringPair{u8"下載中…", "Downloading..."}},
        {"button.downloadModel",
         StringPair{u8"下載所選模型", "Download selected model"}},
        {"button.fullScreen", StringPair{u8"全螢幕", "Full screen"}},
        {"button.exitFullScreen", StringPair{u8"結束全螢幕", "Exit full screen"}},
        {"button.advancedOptionsExpand",
         StringPair{u8"進階選項 >", "Advanced options >"}},
        {"button.advancedOptionsCollapse",
         StringPair{u8"進階選項 v", "Advanced options v"}},
        {"button.advancedPanel", StringPair{u8"進階面板", "Advanced panel"}},
        {"button.generalPanel", StringPair{u8"一般面板", "General panel"}},
        {"dialog.exportChooseTitle",
         StringPair{
             u8"選擇要以原始音量匯出的音軌，或匯出目前介面所聽到的混音。",
             "Choose original-volume stems, or export the mix currently "
             "heard in the interface."}},
        {"dialog.exportSelectedStems", StringPair{u8"匯出所選音軌", "Export selected stems"}},
        {"dialog.exportAllStems", StringPair{u8"匯出全部音軌", "Export all stems"}},
        {"dialog.exportMix", StringPair{u8"匯出混音", "Export mix"}},
        {"dialog.close", StringPair{u8"關閉", "Close"}},
        {"dialog.audioOnlyWav", StringPair{u8"僅音訊（.wav）", "Audio only (.wav)"}},
        {"dialog.videoWithMixedAudio",
         StringPair{u8"含混音音訊的影片（.mp4）", "Video with mixed audio (.mp4)"}},
        {"dialog.noteVideoExport",
         StringPair{u8"MP4 匯出會複製原始影像串流，僅取代其音訊。",
                    "MP4 export copies the original video stream and "
                    "replaces only its audio."}},
        {"dialog.noteStemExport",
         StringPair{u8"個別音軌不受介面增益控制影響，保留 Demucs 原始輸出音量。",
                    "Individual stems ignore the interface gain controls "
                    "and preserve Demucs output level."}},
        {"alert.noStemsSelectedTitle", StringPair{u8"尚未選擇音軌", "No stems selected"}},
        {"alert.noStemsSelectedMessage",
         StringPair{u8"請至少選擇一個音軌後再匯出。",
                    "Select at least one stem before exporting."}},
        {"filechooser.stemFolderTitle",
         StringPair{u8"選擇音軌 WAV 檔案的儲存資料夾",
                    "Choose a folder for the stem WAV files"}},
        {"filechooser.exportVideoWithMix",
         StringPair{u8"匯出含介面混音的影片", "Export video with the interface mix"}},
        {"filechooser.exportMix", StringPair{u8"匯出介面混音", "Export the interface mix"}},
        {"error.ffmpegNotStarted",
         StringPair{u8"無法啟動 FFmpeg。可攜式 FFmpeg 執行環境遺失或無效。",
                    "FFmpeg could not be started. The portable FFmpeg runtime "
                    "is missing or invalid."}},
        {"error.mediaOperationCancelled",
         StringPair{u8"媒體操作已取消", "Media operation cancelled"}},
        {"error.ffmpegFailedPrefix",
         StringPair{u8"FFmpeg 失敗（結束碼 ", "FFmpeg failed (exit "}},
        {"error.ffmpegFailedSuffix", StringPair{u8"）：", "): "}},
        {"error.unsupportedAudioFile",
         StringPair{u8"不支援或無法讀取的音訊檔案：",
                    "Unsupported or unreadable audio file: "}},
        {"error.mediaDurationInvalid",
         StringPair{u8"媒體時長為零，或過長而無法載入記憶體",
                    "The media duration is empty or too large to hold in "
                    "memory"}},
        {"error.audioStreamDecodeFailed",
         StringPair{u8"無法解碼音訊串流", "The audio stream could not be decoded"}},
        {"error.audioStreamNoSampleRate",
         StringPair{u8"音訊串流沒有有效的取樣率",
                    "The audio stream has no valid sample rate"}},
        {"error.resampledAudioTooLarge",
         StringPair{u8"重新取樣後的音訊過大，無法載入記憶體",
                    "The resampled audio is too large to hold in memory"}},
        {"error.noAudioToExport",
         StringPair{u8"沒有可匯出的音訊", "There is no audio to export"}},
        {"error.couldNotCreateOutputFolder",
         StringPair{u8"無法建立輸出資料夾：", "Could not create output folder: "}},
        {"error.couldNotOpenOutputFile",
         StringPair{u8"無法開啟輸出檔案：", "Could not open output file: "}},
        {"error.couldNotCreateWavWriter",
         StringPair{u8"無法建立 32-bit float WAV 寫入器",
                    "Could not create a 32-bit float WAV writer"}},
        {"error.wavWriteFailed",
         StringPair{u8"寫入 WAV 檔案失敗", "Writing the WAV file failed"}},
        {"error.couldNotReplaceExistingFile",
         StringPair{u8"無法取代既有檔案：", "Could not replace existing file: "}},
        {"error.couldNotCommitOutputFile",
         StringPair{u8"無法完成輸出檔案：", "Could not commit output file: "}},
        {"status.switchToRecordModeFirst",
         StringPair{u8"請先切換到錄音模式", "Switch to Record mode first"}},
        {"status.recordingRequires44100Hz",
         StringPair{u8"錄音需要 44,100 Hz 取樣率", "Recording requires 44,100 Hz"}},
        {"status.recordingStereoInput",
         StringPair{u8"正在錄製立體聲輸入", "Recording stereo input"}},
        {"status.noInputRecorded",
         StringPair{u8"未錄製到任何輸入", "No input was recorded"}},
        {"status.recordedPrefix", StringPair{u8"已錄製 ", "Recorded "}},
        {"status.recordedSuffix",
         StringPair{u8" 秒，請按下「分離」", " seconds · press Separate"}},
        {"status.waitForMediaOperation",
         StringPair{u8"請等待媒體操作完成", "Wait for the media operation to finish"}},
        {"status.modelNotInstalledPrefix", StringPair{u8"模型 ", "Model "}},
        {"status.modelNotInstalledSuffix",
         StringPair{u8"尚未安裝。請開啟「進階選項」並下載此模型。",
                    " is not installed. Open Advanced options and download it "
                    "first."}},
        {"status.recordBeforeSeparating",
         StringPair{u8"請先錄製立體聲音訊再進行分離",
                    "Record some stereo audio before separating"}},
        {"status.startingDemucsWorker",
         StringPair{u8"正在啟動 Demucs worker · 等待模型載入",
                    "Starting Demucs worker · waiting for model load"}},
        {"status.cancellingAfterBlock",
         StringPair{u8"正在取消 · 將於目前推論區塊結束後停止",
                    "Cancelling after the current inference block"}},
        {"status.separationCancelled",
         StringPair{u8"分離已取消", "Separation cancelled"}},
        {"status.readyToPreviewFakeWorker",
         StringPair{u8"已可預覽 - 模擬 worker", "Ready to preview - fake worker"}},
        {"status.roformerPathsUnavailable",
         StringPair{
             u8"RoFormer 的 Python、worker、模型快取或輸出資料夾無法使用",
             "RoFormer Python, worker, model cache, or output directory is "
             "unavailable"}},
        {"status.loadingRoformerPrefix",
         StringPair{u8"正在載入 RoFormer ", "Loading RoFormer "}},
        {"status.couldNotStartRoformerWorker",
         StringPair{u8"無法啟動 RoFormer Python worker",
                    "Could not start the RoFormer Python worker"}},
        {"status.roformerSeparationCancelled",
         StringPair{u8"RoFormer 分離已取消", "RoFormer separation cancelled"}},
        {"status.roformerWorkerFailedPrefix",
         StringPair{u8"RoFormer worker 執行失敗（結束碼 ",
                    "RoFormer worker failed (exit "}},
        {"status.roformerWorkerFailedSuffix", StringPair{u8"）：", "): "}},
        {"status.roformerWrongStemCountPrefix",
         StringPair{u8"RoFormer worker 回傳了 ", "RoFormer worker returned "}},
        {"status.roformerWrongStemCountSuffix",
         StringPair{u8" 個 WAV 檔案，預期應為兩軌",
                    " WAV files; expected two stems"}},
        {"status.roformerStemDurationMismatch",
         StringPair{u8"RoFormer 音軌時長與輸入不符",
                    "RoFormer stem duration does not match the input"}},
        {"status.readyToPreviewPrefix",
         StringPair{u8"準備預覽 · ", "Ready to preview · "}},
        {"status.readyToPreviewRoformerSuffix",
         StringPair{u8" · 雙軌", " · two stems"}},
        {"status.loadingModelPrefix", StringPair{u8"正在載入 ", "Loading "}},
        {"status.loadingModelDeviceMiddle",
         StringPair{u8"，裝置：", " on "}},
        {"status.loadingModelDeviceSuffix",
         StringPair{u8" · 首次載入可能需要一些時間",
                    " · first load can take a while"}},
        {"status.cpuInferenceSlowSuffix",
         StringPair{u8" · CPU 推論可能需要較長時間",
                    " · CPU inference may take a long time"}},
        {"status.separatingPrefix", StringPair{u8"分離中 · ", "Separating · "}},
        {"status.separatingBlockMiddle",
         StringPair{u8" · 區塊 ", " · block "}},
        {"status.decodingImportedMedia",
         StringPair{u8"正在解碼匯入的媒體", "Decoding imported media"}},
        {"status.importingPrefix", StringPair{u8"匯入中 ", "Importing "}},
        {"status.mediaImportCancelled",
         StringPair{u8"媒體匯入已取消", "Media import cancelled"}},
        {"status.importedPrefix", StringPair{u8"已匯入 ", "Imported "}},
        {"status.importedSuffix",
         StringPair{u8" 秒) - 請按下「分離」", " s) - press Separate"}},
        {"status.mediaImportFailedPrefix",
         StringPair{u8"媒體匯入失敗：", "Media import failed: "}},
        {"status.cancellingMediaOperation",
         StringPair{u8"正在取消媒體操作", "Cancelling media operation"}},
        {"status.switchToRecordModeBeforeImport",
         StringPair{u8"請先切換到錄音模式再匯入媒體",
                    "Switch to Record mode before importing media"}},
        {"status.selectedMediaFileNotFound",
         StringPair{u8"所選媒體檔案不存在", "The selected media file does not exist"}},
        {"status.separateBeforeExportingStems",
         StringPair{u8"請先分離音訊再匯出音軌",
                    "Separate some audio before exporting stems"}},
        {"status.selectAtLeastOneStemToExport",
         StringPair{u8"請至少選擇一個音軌以匯出",
                    "Select at least one stem to export"}},
        {"status.exportingOriginalVolumeStems",
         StringPair{u8"正在匯出原始音量音軌", "Exporting original-volume stems"}},
        {"status.stemExportCancelled",
         StringPair{u8"音軌匯出已取消", "Stem export cancelled"}},
        {"status.separateBeforeQuickExport",
         StringPair{u8"請先分離音訊再使用快速匯出",
                    "Separate some audio before using quick export"}},
        {"status.quickExportRequiresHtdemucs",
         StringPair{u8"快速匯出需要預設 htdemucs 模型的分離結果",
                    "Quick export requires a result from the default "
                    "htdemucs model"}},
        {"status.chooseDifferentOutputNameProtected",
         StringPair{u8"請選擇不同的輸出檔名；匯入的來源檔案受保護",
                    "Choose a different output name; the imported source "
                    "is protected"}},
        {"status.exportingVocalsOriginalLevel",
         StringPair{u8"正在以 Demucs 原始音量匯出人聲",
                    "Exporting vocals at original Demucs level"}},
        {"status.exportingAccompanyOriginalLevel",
         StringPair{u8"正在以 Demucs 原始音量匯出伴奏",
                    "Exporting accompaniment at original Demucs level"}},
        {"status.quickExportCancelled",
         StringPair{u8"快速匯出已取消", "Quick export cancelled"}},
        {"status.separateBeforeExportingMix",
         StringPair{u8"請先分離音訊再匯出混音",
                    "Separate some audio before exporting a mix"}},
        {"status.videoExportRequiresImportedVideo",
         StringPair{u8"僅在匯入影片時才能匯出影片",
                    "Video export is available only when a video was "
                    "imported"}},
        {"status.mixingReplacingVideoAudio",
         StringPair{u8"正在混音並取代影片音軌",
                    "Mixing and replacing the video audio track"}},
        {"status.exportingCurrentInterfaceMix",
         StringPair{u8"正在匯出目前介面混音", "Exporting the current interface mix"}},
        {"status.mixExportCancelled",
         StringPair{u8"混音匯出已取消", "Mix export cancelled"}},
        {"status.stemExportSuccessPrefix", StringPair{u8"已匯出 ", "Exported "}},
        {"status.stemExportSuccessMiddle",
         StringPair{u8" 個原始音量音軌 WAV 檔案至 ",
                    " original-volume stem WAV file(s) to "}},
        {"status.stemExportFailedPrefix",
         StringPair{u8"音軌匯出失敗：", "Stem export failed: "}},
        {"status.quickExportedVocalsPrefix",
         StringPair{u8"已匯出人聲：", "Exported vocals: "}},
        {"status.quickExportedAccompanyPrefix",
         StringPair{u8"已匯出伴奏：", "Exported accompaniment: "}},
        {"status.quickExportFailedPrefix",
         StringPair{u8"快速匯出失敗：", "Quick export failed: "}},
        {"status.mixExportedPrefix",
         StringPair{u8"已匯出介面混音：", "Exported interface mix: "}},
        {"status.mp4StreamCopyIncompatibleSuffix",
         StringPair{u8"（來源影片編碼可能與 MP4 串流複製不相容。）",
                    " (The source video codec may not be compatible with "
                    "MP4 stream copy.)"}},
        {"status.couldNotReplaceMp4OutputPrefix",
         StringPair{u8"無法取代所選 MP4 輸出檔案：",
                    "Could not replace the selected MP4 output file: "}},
        {"status.mixExportedMp4Prefix",
         StringPair{u8"已匯出含介面混音的 MP4：",
                    "Exported MP4 with the interface mix: "}},
        {"status.mixExportFailedPrefix",
         StringPair{u8"混音匯出失敗：", "Mix export failed: "}},
    };
    return table;
}

// Deliberately independent of the HTFX_DATA_DIR override used elsewhere in
// this codebase (models/checkpoint/worker resolution) — redirecting that
// shared variable just to isolate a language-preference test would risk
// side effects on unrelated model/worker path resolution. Tests that need
// isolation set HTFX_UI_LANGUAGE_FILE instead (see settingsFile() below).
juce::File settingsDirectory() {
#if JUCE_WINDOWS
    const auto localAppData =
        juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {}).trim();
    if (localAppData.isNotEmpty()) {
        return juce::File(localAppData).getChildFile("HTDemucs GPU FX");
    }
#endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HTDemucs GPU FX");
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
    language_ = file.existsAsFile() ? languageFromTag(file.loadFileAsString())
                                     : Language::zhTW;
}

void Localization::setLanguage(Language language) {
    language_ = language;
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(tagFromLanguage(language_));
}

juce::String Localization::tr(const juce::String& key) const {
    const auto& table = stringTable();
    const auto entry = table.find(key.toStdString());
    if (entry == table.end()) {
        return key;
    }
    return entry->second[static_cast<std::size_t>(language_)];
}

}  // namespace htfx
