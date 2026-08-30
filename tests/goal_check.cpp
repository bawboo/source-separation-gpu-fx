// goal_check — end-to-end acceptance run against a real song.
//
// Drives the processor exactly like the general panel does: import the file,
// pick a separation mode, separate, then quick-export vocals and accompany.
// Prints one line per mode so a failure names the mode that broke.
//
// Usage: htdemucs_goal_check.exe "<path to audio file>"

#include "Localization.h"
#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool waitUntil(const std::function<bool()>& predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        juce::Timer::callPendingTimersSynchronously();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return predicate();
}

struct ModeCase {
    const char* label;
    const char* roformerModel;  // empty => HTDemucs
    const char* htdemucsModel;  // used when roformerModel is empty
};

bool runMode(
    HTDemucsGpuFXAudioProcessor& processor,
    const juce::File& input,
    const juce::File& outputDir,
    const ModeCase& mode) {
    const auto started = std::chrono::steady_clock::now();
    std::cout << "[" << mode.label << "] importing..." << std::endl;

    if (!processor.beginMediaImport(input)) {
        std::cout << "[" << mode.label << "] FAIL: import did not start" << std::endl;
        return false;
    }
    if (!waitUntil([&processor] { return !processor.isMediaBusy(); },
                   std::chrono::seconds(180))) {
        std::cout << "[" << mode.label << "] FAIL: import timed out" << std::endl;
        return false;
    }

    if (juce::String(mode.roformerModel).isNotEmpty()) {
        if (!processor.selectRoformerModel(mode.roformerModel)) {
            std::cout << "[" << mode.label << "] FAIL: model not in catalog" << std::endl;
            return false;
        }
    } else {
        processor.clearRoformerModel();
        // kModelNames order: htdemucs, htdemucs_ft, htdemucs_6s, hdemucs_mmi
        const int modelIndex =
            juce::String(mode.htdemucsModel) == "htdemucs_6s" ? 2 : 0;
        if (auto* parameter = processor.parameters().getParameter("model")) {
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(static_cast<float>(modelIndex)));
        }
        processor.applyUserConfiguration();
    }

    std::cout << "[" << mode.label << "] separating..." << std::endl;
    if (!processor.beginSeparation()) {
        if (!processor.isModelDownloadBusy()) {
            std::cout << "[" << mode.label
                      << "] FAIL: separation did not start: "
                      << processor.getRecordStatusText() << std::endl;
            return false;
        }
        // The checkpoint is being fetched; the run resumes by itself.
        std::cout << "[" << mode.label << "] downloading the model first..."
                  << std::endl;
        if (!waitUntil([&processor] { return !processor.isModelDownloadBusy(); },
                       std::chrono::seconds(900))) {
            std::cout << "[" << mode.label << "] FAIL: model download timed out"
                      << std::endl;
            return false;
        }
        // Give the queued resume a chance to start the run.
        if (!waitUntil(
                [&processor] {
                    using State = HTDemucsGpuFXAudioProcessor::SeparationState;
                    const auto state = processor.getSeparationState();
                    return state == State::loading || state == State::separating ||
                           state == State::previewReady || state == State::error;
                },
                std::chrono::seconds(30))) {
            std::cout << "[" << mode.label
                      << "] FAIL: separation did not resume after the download: "
                      << processor.getRecordStatusText() << std::endl;
            return false;
        }
    }
    if (!waitUntil(
            [&processor] {
                const auto state = processor.getSeparationState();
                return state == HTDemucsGpuFXAudioProcessor::SeparationState::previewReady ||
                       state == HTDemucsGpuFXAudioProcessor::SeparationState::error;
            },
            std::chrono::seconds(900))) {
        std::cout << "[" << mode.label << "] FAIL: separation timed out" << std::endl;
        return false;
    }
    if (!processor.hasPreview()) {
        std::cout << "[" << mode.label << "] FAIL: no preview: "
                  << processor.getRecordStatusText() << std::endl;
        return false;
    }

    const auto base = juce::String(mode.label).replaceCharacter(' ', '_');
    for (const auto kind : {HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals,
                            HTDemucsGpuFXAudioProcessor::QuickExportKind::accompaniment}) {
        const bool vocals =
            kind == HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals;
        const auto out =
            outputDir.getChildFile(base + (vocals ? "_vocals.wav" : "_accompany.wav"));
        out.deleteFile();
        if (!processor.beginQuickExport(out, kind)) {
            std::cout << "[" << mode.label << "] FAIL: quick export ("
                      << (vocals ? "vocals" : "accompany")
                      << ") refused: " << processor.getMediaStatusText() << std::endl;
            return false;
        }
        if (!waitUntil([&processor] { return !processor.isMediaBusy(); },
                       std::chrono::seconds(300))) {
            std::cout << "[" << mode.label << "] FAIL: quick export timed out" << std::endl;
            return false;
        }
        if (!out.existsAsFile() || out.getSize() < 1024) {
            std::cout << "[" << mode.label << "] FAIL: export produced no file"
                      << std::endl;
            return false;
        }
    }

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::cout << "[" << mode.label << "] OK (" << seconds << "s, "
              << processor.getActiveSourceCount() << " stems)" << std::endl;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc < 2) {
        std::cerr << "usage: htdemucs_goal_check <audio file>" << std::endl;
        return 2;
    }
    // argv on Windows is the ANSI codepage view, which mangles non-ASCII
    // paths (e.g. Chinese folder names). Take the real UTF-16 command line.
    juce::String inputPath;
    {
        int wideCount = 0;
        if (auto** wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideCount);
            wideArgv != nullptr) {
            if (wideCount > 1) {
                inputPath = juce::String(wideArgv[1]);
            }
            LocalFree(wideArgv);
        }
    }
    if (inputPath.isEmpty()) {
        inputPath = juce::String::fromUTF8(argv[1]);
    }
    const juce::File input{inputPath};
    if (!input.existsAsFile()) {
        std::cerr << "input file not found: " << input.getFullPathName() << std::endl;
        return 2;
    }
    const auto outputDir =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("htfx-goal-check");
    outputDir.createDirectory();

    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);

    const std::vector<ModeCase> modes{
        {"htdemucs 4-stem", "", "htdemucs"},
        {"htdemucs 6-stem", "", "htdemucs_6s"},
        {"roformer vocals", "melband-roformer-kim-vocals", ""},
        {"roformer guitar", "roformer-model-melband-roformer-guitar-by-becruily", ""},
    };

    int failures = 0;
    for (const auto& mode : modes) {
        if (!runMode(*processor, input, outputDir, mode)) {
            ++failures;
        }
    }
    processor->releaseResources();
    std::cout << (failures == 0 ? "GOAL CHECK PASS" : "GOAL CHECK FAIL")
              << " (" << (modes.size() - failures) << "/" << modes.size()
              << " modes ok, outputs in " << outputDir.getFullPathName() << ")"
              << std::endl;
    return failures == 0 ? 0 : 1;
}
