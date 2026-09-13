#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

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

    [[nodiscard]] Language getLanguage() const noexcept { return language_.load(std::memory_order_acquire); }

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

    // Read by tr() on worker threads while the language button writes it.
    std::atomic<Language> language_{Language::zhTW};
};

[[nodiscard]] inline juce::String tr(const juce::String& key) {
    return Localization::instance().tr(key);
}

// A separation mode or stem name with its Chinese meaning appended, e.g.
// "Vocals" -> "Vocals（人聲）". The model world names these things in English
// and the app shows those names as-is, which tells a Chinese reader nothing
// about what "Dereverb" or "Aspiration" actually separates. English UI gets
// the bare name back. Unknown names pass through unchanged, so a newly
// catalogued category never turns into a broken label.
[[nodiscard]] juce::String glossed(const juce::String& englishName);

// The Chinese meaning alone, or an empty string when there is none (or when
// the UI is in English). Exposed so tests can check a label against the name
// it is glossing.
[[nodiscard]] juce::String glossFor(const juce::String& englishName);

// The bracket glossed() opens the meaning with, so callers can split a label
// back into the English name and its gloss without hard-coding the character.
[[nodiscard]] juce::String glossOpen();

}  // namespace htfx
