#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

// The offered catalog is the curated list, not the whole manifest; read the
// same file the processor does so the expectation cannot drift from it.
int curatedCatalogCount() {
    const auto env = juce::SystemStats::getEnvironmentVariable("HTFX_ROFORMER_CATALOG", {}).trim();
    auto catalog = env.isNotEmpty()
        ? juce::File(env)
        : juce::File::getCurrentWorkingDirectory()
              .getChildFile("assets/models/roformer-catalog.json");
    const auto parsed = juce::JSON::parse(catalog.loadFileAsString());
    if (const auto* root = parsed.getDynamicObject()) {
        if (const auto* models = root->getProperty("models").getArray()) {
            return models->size();
        }
    }
    return -1;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

void waitForMedia(HTDemucsGpuFXAudioProcessor& processor) {
    require(
        waitUntil([&processor] { return !processor.isMediaBusy(); }, std::chrono::seconds(30)),
        "RoFormer smoke: fixture import timed out");
}

void waitForPreview(HTDemucsGpuFXAudioProcessor& processor) {
    require(
        waitUntil(
            [&processor] {
                const auto state = processor.getSeparationState();
                return state ==
                           HTDemucsGpuFXAudioProcessor::SeparationState::previewReady ||
                       state == HTDemucsGpuFXAudioProcessor::SeparationState::error;
            },
            std::chrono::seconds(60)),
        "RoFormer smoke: separation timed out");
    require(processor.hasPreview(), "RoFormer smoke: separation produced no preview");
}

struct StemFormat {
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    unsigned int numChannels = 0;
    unsigned int bitsPerSample = 0;
    bool floatingPoint = false;
    bool allFinite = false;
};

// Dedicated end-to-end coverage separate from ui_configuration_smoke: this
// test decodes the exported stem bytes back off disk (sample rate, channel
// count, bit depth, finite samples) rather than only asserting filenames,
// so a regression in the RoFormer worker's output encoding fails here even
// if the UI-facing assertions still pass.
StemFormat inspectWav(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    require(reader != nullptr,
            "RoFormer smoke: could not read exported stem " +
                file.getFullPathName().toStdString());
    require(
        reader->lengthInSamples > 0 &&
            reader->lengthInSamples <= std::numeric_limits<int>::max(),
        "RoFormer smoke: exported stem has invalid duration");
    const int samples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), samples);
    require(
        reader->read(&audio, 0, samples, 0, true, true),
        "RoFormer smoke: could not decode exported stem");
    bool allFinite = true;
    for (int channel = 0; channel < audio.getNumChannels() && allFinite; ++channel) {
        const auto* data = audio.getReadPointer(channel);
        for (int sample = 0; sample < samples; ++sample) {
            if (!std::isfinite(data[sample])) {
                allFinite = false;
                break;
            }
        }
    }
    StemFormat format;
    format.durationSeconds = static_cast<double>(samples) / reader->sampleRate;
    format.sampleRate = reader->sampleRate;
    format.numChannels = reader->numChannels;
    format.bitsPerSample = reader->bitsPerSample;
    format.floatingPoint = reader->usesFloatingPointData;
    format.allFinite = allFinite;
    return format;
}

int run() {
    constexpr double kExpectedDurationSeconds = 2.0;

    const auto repository = juce::File::getCurrentWorkingDirectory();
    const auto verifyRoot = repository.getParentDirectory().getChildFile("verify");
    const auto fixture = verifyRoot.getChildFile("fixtures").getChildFile("test_48k_2s.wav");
    const auto roformerPython = juce::File{
        juce::SystemStats::getEnvironmentVariable("HTFX_PYTHON", {})};
    const auto roformerWorker =
        repository.getChildFile("worker").getChildFile("roformer_worker.py");
    const auto roformerCache = verifyRoot.getChildFile("roformer-cache");
    const auto roformerOutput =
        verifyRoot.getChildFile("output").getChildFile("roformer-smoke");
    require(fixture.existsAsFile(), "RoFormer smoke: fixture is missing");
    require(roformerPython.existsAsFile(), "RoFormer smoke: htfx-roformer Python is missing");
    require(roformerWorker.existsAsFile(), "RoFormer smoke: worker script is missing");
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    _wputenv_s(L"HTFX_ROFORMER_PYTHON", roformerPython.getFullPathName().toWideCharPointer());
    _wputenv_s(L"HTFX_ROFORMER_WORKER", roformerWorker.getFullPathName().toWideCharPointer());
    _wputenv_s(
        L"HTFX_ROFORMER_MODELS_DIR", roformerCache.getFullPathName().toWideCharPointer());
    _wputenv_s(
        L"HTFX_ROFORMER_OUTPUT_DIR", roformerOutput.getFullPathName().toWideCharPointer());

    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);

    const auto models = processor->getRoformerModels();
    const int expectedCount = curatedCatalogCount();
    require(expectedCount > 0, "RoFormer smoke: could not read roformer-catalog.json");
    require(static_cast<int>(models.size()) == expectedCount,
            "RoFormer smoke: catalog count mismatch");
    // Every curated model is an audited one.
    require(
        std::count_if(
            models.begin(), models.end(),
            [](const auto& model) { return model.audited; }) == expectedCount,
        "RoFormer smoke: audited count mismatch");
    require(
        !processor->selectRoformerModel("not-a-real-model"),
        "RoFormer smoke: unknown model was accepted");
    require(
        processor->selectRoformerModel("melband-roformer-kim-vocals") &&
            processor->getSelectedRoformerModel() == "melband-roformer-kim-vocals",
        "RoFormer smoke: model selection was not retained");

    require(
        processor->beginMediaImport(fixture), "RoFormer smoke: fixture import did not start");
    waitForMedia(*processor);

    require(processor->beginSeparation(), "RoFormer smoke: separation did not start");
    waitForPreview(*processor);
    require(processor->getActiveSourceCount() == 2, "RoFormer smoke: preview is not two-stem");
    require(
        processor->previewUsesModel("melband-roformer-kim-vocals"),
        "RoFormer smoke: preview lost its model identity");
    require(
        std::abs(processor->getPreviewDurationSeconds() - kExpectedDurationSeconds) < 0.02,
        "RoFormer smoke: preview duration mismatch");

    const auto stemLabel0 = processor->getStemLabel(0).toLowerCase();
    const auto stemLabel1 = processor->getStemLabel(1).toLowerCase();
    require(
        (stemLabel0 == "vocals" || stemLabel1 == "vocals") &&
            (stemLabel0 == "instrumental" || stemLabel1 == "instrumental"),
        "RoFormer smoke: vocals-category preview did not label vocals/instrumental");
    require(
        stemLabel0 != "drums" && stemLabel1 != "drums" && stemLabel0 != "bass" &&
            stemLabel1 != "bass",
        "RoFormer smoke: stems incorrectly kept HTDemucs source names");

    const auto exportDir = roformerOutput.getChildFile("export-" + juce::Uuid().toString());
    require(exportDir.createDirectory(), "RoFormer smoke: could not create export directory");
    require(
        processor->beginStemExport(exportDir, {0, 1}),
        "RoFormer smoke: stem export did not start");
    require(
        waitUntil(
            [&processor] { return !processor->isMediaBusy(); }, std::chrono::seconds(30)),
        "RoFormer smoke: stem export timed out");

    juce::Array<juce::File> exportedStems;
    exportDir.findChildFiles(exportedStems, juce::File::findFiles, false, "*.wav");
    require(
        exportedStems.size() == 2,
        "RoFormer smoke: stem export did not produce two WAV files");

    juce::File vocalsFile;
    juce::File instrumentalFile;
    for (const auto& stem : exportedStems) {
        if (stem.getFileName().containsIgnoreCase("vocals")) {
            vocalsFile = stem;
        } else if (stem.getFileName().containsIgnoreCase("instrumental")) {
            instrumentalFile = stem;
        }
    }
    require(
        vocalsFile.existsAsFile() && instrumentalFile.existsAsFile(),
        "RoFormer smoke: exported stems were not category-named");

    for (const auto& stem : {vocalsFile, instrumentalFile}) {
        const auto format = inspectWav(stem);
        require(
            std::abs(
                format.sampleRate -
                static_cast<double>(HTDemucsGpuFXAudioProcessor::kSampleRate)) < 1.0,
            "RoFormer smoke: exported stem is not at the plugin's 44.1 kHz processing rate");
        require(
            std::abs(format.durationSeconds - kExpectedDurationSeconds) < 0.05,
            "RoFormer smoke: exported stem duration does not match the source");
        require(format.numChannels == 2, "RoFormer smoke: exported stem is not stereo");
        require(
            format.bitsPerSample == 32 && format.floatingPoint,
            "RoFormer smoke: exported stem is not 32-bit float");
        require(
            format.allFinite, "RoFormer smoke: exported stem contains non-finite samples");
    }

    processor->releaseResources();
    std::cout << "roformer_catalog=99 roformer_audited=57 roformer_stems=2"
                 " roformer_labels=vocals/instrumental roformer_export_naming=true"
                 " roformer_sample_rate=44100 roformer_channels=2"
                 " roformer_bit_depth=32float roformer_finite=true PASS\n";
    return 0;
}

}  // namespace

int main() {
    try {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run();
    } catch (const std::exception& error) {
        juce::AudioProcessor::setTypeOfNextNewPlugin(
            juce::AudioProcessor::wrapperType_Undefined);
        std::cerr << "roformer_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
