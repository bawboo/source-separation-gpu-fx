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
