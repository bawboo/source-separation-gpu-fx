#pragma once

#include <juce_core/juce_core.h>

namespace htfx {

// UI display language. Model IDs and other stable identifiers are never
// translated; this only governs user-facing label/button/menu text.
enum class Language : int {
    zhTW = 0,
    en = 1,
};

// Process-wide UI string table + persisted language preference.
//
// The persisted preference lives outside the repository (see
// settingsFile()), so editor instances created later in the same process
// (e.g. a smoke test that opens a second editor after toggling the
// language) observe the saved choice once reload() re-reads it — this
// mirrors a real DAW/session restart without requiring a new process.
class Localization {
public:
    static Localization& instance();

    [[nodiscard]] Language getLanguage() const noexcept { return language_; }

    // Persists the choice to disk immediately.
    void setLanguage(Language language);

    // Re-reads the persisted preference from disk into the in-memory
    // language, falling back to zh-TW when no preference file exists yet.
    // Callers (editor construction) invoke this so a freshly opened editor
    // reflects the latest saved choice.
    void reload();

    [[nodiscard]] juce::String tr(const juce::String& key) const;

    // Exposed for tests: where the language preference is persisted.
    // Honours the HTFX_UI_LANGUAGE_FILE override so tests can isolate
    // themselves from a real user's saved preference.
    [[nodiscard]] static juce::File settingsFile();

private:
    Localization();

    Language language_ = Language::zhTW;
};

[[nodiscard]] inline juce::String tr(const juce::String& key) {
    return Localization::instance().tr(key);
}

}  // namespace htfx
