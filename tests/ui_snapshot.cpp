// ui_snapshot — renders the editor to PNG files so its appearance can be
// reviewed without a desktop session: the general panel and the advanced
// panel, in both languages, before any media is loaded, after an import,
// and (when a media file is given) after a separation, so every state of the
// step strip and the status dot is on record.
//
// Usage: htdemucs_ui_snapshot.exe <output-dir> [media-file]

#include "Localization.h"
#include "PluginProcessor.h"

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

using Processor = HTDemucsGpuFXAudioProcessor;

void collect(juce::Component& parent, std::vector<juce::Component*>& out) {
    for (int i = 0; i < parent.getNumChildComponents(); ++i) {
        auto* child = parent.getChildComponent(i);
        out.push_back(child);
        collect(*child, out);
    }
}

juce::TextButton* buttonWithText(juce::Component& root, const juce::String& text) {
    std::vector<juce::Component*> all;
    collect(root, all);
    for (auto* c : all) {
        if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == text) {
            return b;
        }
    }
    return nullptr;
}

void pump(int milliseconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < until) {
        juce::Timer::callPendingTimersSynchronously();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool waitUntil(const std::function<bool()>& predicate, int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until) {
        juce::Timer::callPendingTimersSynchronously();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return predicate();
}

bool snapshot(juce::Component& editor, const juce::File& file) {
    pump(250);  // let the 10 Hz timer refresh labels and the frame state
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), false, 2.0f);
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk()) {
        return false;
    }
    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream(image, stream);
    std::cout << (ok ? "  wrote " : "  FAILED ") << file.getFileName() << "  "
              << image.getWidth() << "x" << image.getHeight() << std::endl;
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    std::vector<juce::String> args;
    {
        int wideCount = 0;
        if (auto** wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideCount); wideArgv != nullptr) {
            for (int i = 1; i < wideCount; ++i) {
                args.emplace_back(wideArgv[i]);
            }
            LocalFree(wideArgv);
        }
    }
    if (args.empty()) {
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(juce::String::fromUTF8(argv[i]));
        }
    }
    if (args.empty()) {
        std::cerr << "usage: htdemucs_ui_snapshot.exe <output-dir> [media-file]" << std::endl;
        return 2;
    }
    const juce::File outDir{args[0]};
    outDir.createDirectory();
    const juce::File media = args.size() > 1 ? juce::File{args[1]} : juce::File{};

    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<Processor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);
    htfx::Localization::instance().setLanguage(htfx::Language::zhTW);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    if (editor == nullptr) {
        std::cerr << "no editor" << std::endl;
        return 1;
    }
    // A snapshot needs a peer-less component tree with real bounds; the
    // editor sizes itself in its constructor.
    editor->setVisible(true);

    bool ok = true;
    ok = snapshot(*editor, outDir.getChildFile("01-general-empty-zh.png")) && ok;

    auto* language = buttonWithText(*editor, htfx::tr("button.languageToggle"));
    if (language != nullptr) {
        language->onClick();
        ok = snapshot(*editor, outDir.getChildFile("02-general-empty-en.png")) && ok;
        language->onClick();
    }

    if (media.existsAsFile()) {
        if (processor->beginMediaImport(media) &&
            waitUntil([&] { return !processor->isMediaBusy(); }, 300)) {
            ok = snapshot(*editor, outDir.getChildFile("03-general-imported-zh.png")) && ok;
            if (processor->beginSeparation() || processor->isModelDownloadBusy()) {
                // catch a mid-separation frame for the busy tone
                waitUntil([&] {
                    return processor->getSeparationState() == Processor::SeparationState::separating;
                }, 120);
                ok = snapshot(*editor, outDir.getChildFile("04-general-separating-zh.png")) && ok;
                waitUntil([&] {
                    const auto s = processor->getSeparationState();
                    return s == Processor::SeparationState::previewReady ||
                           s == Processor::SeparationState::error;
                }, 900);
                ok = snapshot(*editor, outDir.getChildFile("05-general-ready-zh.png")) && ok;
            }
        } else {
            std::cout << "  import did not complete: " << processor->getMediaStatusText() << std::endl;
        }
    }

    if (auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(editor.get()); target != nullptr) {
        target->fileDragEnter({"C:\\x\\song.mp3"}, 10, 10);
        ok = snapshot(*editor, outDir.getChildFile("09-general-dragover-zh.png")) && ok;
        target->fileDragExit({"C:\\x\\song.mp3"});
    }

    auto* panel = buttonWithText(*editor, htfx::tr("button.advancedPanel"));
    if (panel != nullptr) {
        panel->onClick();
        ok = snapshot(*editor, outDir.getChildFile("06-advanced-zh.png")) && ok;
        auto* advanced = buttonWithText(*editor, htfx::tr("button.advancedOptionsExpand"));
        if (advanced != nullptr) {
            advanced->onClick();
            ok = snapshot(*editor, outDir.getChildFile("07-advanced-expanded-zh.png")) && ok;
            advanced->onClick();
        }
        if (language != nullptr) {
            language->onClick();
            ok = snapshot(*editor, outDir.getChildFile("08-advanced-en.png")) && ok;
            language->onClick();
        }
    }

    editor.reset();
    processor->releaseResources();
    std::cout << (ok ? "UI SNAPSHOT OK" : "UI SNAPSHOT FAILED") << std::endl;
    return ok ? 0 : 1;
}
