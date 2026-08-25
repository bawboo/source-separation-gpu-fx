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
