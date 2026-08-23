#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

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

void createTestWav(
    const juce::File& output,
    int sampleRate = HTDemucsGpuFXAudioProcessor::kSampleRate) {
    const int samples = sampleRate;
    juce::AudioBuffer<float> audio(2, samples);
    constexpr double pi = 3.1415926535897932384626433832795;
    for (int sample = 0; sample < samples; ++sample) {
        audio.setSample(
            0,
            sample,
            static_cast<float>(
                0.25 * std::sin(2.0 * pi * 220.0 * sample / sampleRate)));
        audio.setSample(
            1,
            sample,
            static_cast<float>(
                0.20 * std::sin(2.0 * pi * 330.0 * sample / sampleRate)));
    }

    std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
    require(stream != nullptr, "could not create test WAV stream");
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withChannelLayout(juce::AudioChannelSet::stereo())
                             .withBitsPerSample(32)
                             .withSampleFormat(
                                 juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);
    require(writer != nullptr, "could not create test WAV writer");
    require(
        writer->writeFromAudioSampleBuffer(audio, 0, samples),
        "could not write test WAV");
}

double readEnergy(const juce::File& input) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(input));
    require(reader != nullptr, "could not read " + input.getFullPathName().toStdString());
    require(
        reader->lengthInSamples > 0 &&
            reader->lengthInSamples <= std::numeric_limits<int>::max(),
        "invalid test output duration");
    const int samples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> audio(2, samples);
    require(
        reader->read(&audio, 0, samples, 0, true, true),
        "could not decode test output");
    double energy = 0.0;
    for (int channel = 0; channel < 2; ++channel) {
        const auto* data = audio.getReadPointer(channel);
        for (int sample = 0; sample < samples; ++sample) {
            energy += static_cast<double>(data[sample]) * data[sample];
        }
    }
    return energy;
}

juce::MemoryBlock loadBytes(const juce::File& input) {
    juce::MemoryBlock bytes;
    require(input.loadFileAsData(bytes), "could not read exported bytes");
    return bytes;
}

bool sameBytes(const juce::MemoryBlock& first, const juce::MemoryBlock& second) {
    return first.getSize() == second.getSize() &&
           std::memcmp(first.getData(), second.getData(), first.getSize()) == 0;
}

void setParameter(
    juce::AudioProcessorValueTreeState& state,
    const juce::String& id,
    float plainValue) {
    auto* parameter = state.getParameter(id);
    require(parameter != nullptr, "missing parameter " + id.toStdString());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

bool runProcess(const juce::StringArray& command, juce::String& output) {
    juce::ChildProcess process;
    if (!process.start(
            command,
            juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        output = "could not start process";
        return false;
    }
    if (!process.waitForProcessToFinish(60'000)) {
        process.kill();
        output = "process timeout";
        return false;
    }
    output = process.readAllProcessOutput();
    return process.getExitCode() == 0;
}

void waitForMedia(HTDemucsGpuFXAudioProcessor& processor) {
    require(
        waitUntil([&processor] { return !processor.isMediaBusy(); },
                  std::chrono::seconds(60)),
        "media operation timeout: " +
            processor.getMediaStatusText().toStdString());
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
            std::chrono::seconds(15)),
        "fake separation timeout");
    require(
        processor.hasPreview(),
        "separation failed: " + processor.getRecordStatusText().toStdString());
}

void processSilentFrames(
    HTDemucsGpuFXAudioProcessor& processor,
    int frameCount) {
    juce::MidiBuffer midi;
    int remaining = frameCount;
    while (remaining > 0) {
        const int blockSamples = (std::min)(remaining, 256);
        juce::AudioBuffer<float> block(2, blockSamples);
        block.clear();
        processor.processBlock(block, midi);
        remaining -= blockSamples;
    }
}

int run() {
    const juce::File ffmpeg{R"(C:\ffmpeg-master\bin\ffmpeg.exe)"};
    require(ffmpeg.existsAsFile(), "test FFmpeg executable is missing");
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"1");
    _wputenv_s(L"HTFX_FFMPEG", ffmpeg.getFullPathName().toWideCharPointer());

    auto temporaryRoot =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("htfx-media-io-smoke", {}, false);
    require(temporaryRoot.createDirectory(), "could not create test directory");
    const auto inputWav = temporaryRoot.getChildFile("input.wav");
    const auto input48kWav = temporaryRoot.getChildFile("input_48000.wav");
    const auto input96kWav = temporaryRoot.getChildFile("input_96000.wav");
    createTestWav(inputWav);
    createTestWav(input48kWav, 48'000);
    createTestWav(input96kWav, 96'000);

    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);

    require(processor->beginMediaImport(input48kWav), "48 kHz audio import did not start");
    waitForMedia(*processor);
    require(
        std::abs(processor->getRecordedSeconds() - 1.0) < 0.02,
        "48 kHz imported WAV duration mismatch");
    require(processor->beginSeparation(), "48 kHz fake separation did not start");
    waitForPreview(*processor);
    setParameter(processor->parameters(), "bypass", 1.0f);
    processor->prepareToPlay(48'000.0, 256);
    processor->stopPreview();
    processor->togglePreviewPlayback();
    processSilentFrames(*processor, 45'000);
    require(
        processor->isPreviewPlaying(),
        "44.1 kHz preview ended early on a 48 kHz playback device");
    processSilentFrames(*processor, 3'000);
    require(
        !processor->isPreviewPlaying(),
        "44.1 kHz preview did not end after one second on a 48 kHz playback device");

    processor->prepareToPlay(96'000.0, 256);
    processor->stopPreview();
    processor->togglePreviewPlayback();
    processSilentFrames(*processor, 90'000);
    require(
        processor->isPreviewPlaying(),
        "44.1 kHz preview ended early on a 96 kHz playback device");
    processSilentFrames(*processor, 6'000);
    require(
        !processor->isPreviewPlaying(),
        "44.1 kHz preview did not end after one second on a 96 kHz playback device");

    processor->prepareToPlay(44'100.0, 256);
    setParameter(processor->parameters(), "bypass", 0.0f);
    require(processor->beginMediaImport(input96kWav), "96 kHz audio import did not start");
    waitForMedia(*processor);
    require(
        std::abs(processor->getRecordedSeconds() - 1.0) < 0.02,
        "96 kHz imported WAV duration mismatch");

    require(processor->beginMediaImport(inputWav), "audio import did not start");
    waitForMedia(*processor);
    require(!processor->importedFromVideo(), "WAV was classified as video");
    require(
        std::abs(processor->getRecordedSeconds() - 1.0) < 0.02,
        "imported WAV duration mismatch");
    require(processor->beginSeparation(), "fake separation did not start");
    waitForPreview(*processor);
    require(
        processor->previewUsesModel("htdemucs"),
        "preview did not retain its model identity");

    const auto quickVocals = temporaryRoot.getChildFile("input_vocals.wav");
    require(
        processor->beginQuickExport(
            quickVocals,
            HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals),
        "quick vocals export did not start");
    waitForMedia(*processor);
    require(quickVocals.existsAsFile(), "quick vocals file was not exported");
    const double quickVocalsEnergy = readEnergy(quickVocals);

    const auto quickAccompany = temporaryRoot.getChildFile("input_accompany.wav");
    require(
        processor->beginQuickExport(
            quickAccompany,
            HTDemucsGpuFXAudioProcessor::QuickExportKind::accompaniment),
        "quick accompaniment export did not start");
    waitForMedia(*processor);
    require(
        quickAccompany.existsAsFile(),
        "quick accompaniment file was not exported");
    const double quickAccompanyEnergy = readEnergy(quickAccompany);
    require(
        quickAccompanyEnergy > quickVocalsEnergy * 20.0,
        "quick accompaniment did not sum drums, bass, and other");

    const auto stemFolder = temporaryRoot.getChildFile("stems");
    require(
        processor->beginStemExport(stemFolder, {0}),
        "stem export did not start");
    waitForMedia(*processor);
    const auto drums = stemFolder.getChildFile("input_drums.wav");
    require(drums.existsAsFile(), "drums stem was not exported");
    const auto rawStemBefore = loadBytes(drums);

    const auto fullMix = temporaryRoot.getChildFile("full_mix.wav");
    require(
        processor->beginMixExport(fullMix, false),
        "full mix export did not start");
    waitForMedia(*processor);
    const double fullMixEnergy = readEnergy(fullMix);

    constexpr std::array<const char*, 6> stemIds{
        "drumsGain", "bassGain", "otherGain", "vocalsGain", "guitarGain", "pianoGain"};
    for (const auto* id : stemIds) {
        setParameter(processor->parameters(), id, -60.0f);
    }
    setParameter(processor->parameters(), "outputTrim", -6.0f);
    const auto mutedMix = temporaryRoot.getChildFile("muted_mix.wav");
    require(
        processor->beginMixExport(mutedMix, false),
        "muted mix export did not start");
    waitForMedia(*processor);
    const double mutedMixEnergy = readEnergy(mutedMix);
    require(
        mutedMixEnergy < fullMixEnergy * 1.0e-5,
        "interface mix gains were not applied to export");

    require(
        processor->beginStemExport(stemFolder, {0}),
        "second stem export did not start");
    waitForMedia(*processor);
    const auto rawStemAfter = loadBytes(drums);
    require(
        sameBytes(rawStemBefore, rawStemAfter),
        "raw stem export changed after moving interface gain controls");

    setParameter(processor->parameters(), "bypass", 1.0f);
    const auto bypassMix = temporaryRoot.getChildFile("bypass_mix.wav");
    require(
        processor->beginMixExport(bypassMix, false),
        "bypass mix export did not start");
    waitForMedia(*processor);
    const double bypassEnergy = readEnergy(bypassMix);
    require(
        bypassEnergy > mutedMixEnergy * 100'000.0,
        "bypass did not export the original input");

    const auto inputVideo = temporaryRoot.getChildFile("input_video.mp4");
    juce::String processOutput;
    const juce::StringArray createVideo{
        ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "color=c=black:s=320x180:r=25:d=1", "-i",
        inputWav.getFullPathName(), "-shortest", "-c:v", "mpeg4", "-q:v", "5",
        "-c:a", "aac", inputVideo.getFullPathName()};
    require(
        runProcess(createVideo, processOutput),
        "could not create test video: " + processOutput.toStdString());
    require(processor->beginMediaImport(inputVideo), "video import did not start");
    waitForMedia(*processor);
    require(processor->importedFromVideo(), "MP4 was not classified as video");
    require(processor->beginSeparation(), "video separation did not start");
    waitForPreview(*processor);

    const auto outputVideo = temporaryRoot.getChildFile("video_mix.mp4");
    require(
        processor->beginMixExport(outputVideo, true),
        "video mix export did not start");
    waitForMedia(*processor);
    require(
        outputVideo.existsAsFile() && outputVideo.getSize() > 1024,
        "mixed MP4 was not created: " +
            processor->getMediaStatusText().toStdString());
    const juce::StringArray probeVideo{
        ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-i",
        outputVideo.getFullPathName(), "-map", "0:v:0", "-map", "0:a:0", "-f",
        "null", "NUL"};
    require(
        runProcess(probeVideo, processOutput),
        "mixed MP4 could not be decoded: " + processOutput.toStdString());

    processor->releaseResources();
    processor.reset();
    const auto bytes = outputVideo.getSize();
    require(temporaryRoot.deleteRecursively(), "could not clean test directory");
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    _wputenv_s(L"HTFX_FFMPEG", L"");
    std::cout << "audio_import=true quick_vocals=true quick_accompany=true "
                 "raw_stem_unchanged=true mix_controls=true "
                 "video_import=true mp4_replace_audio=true mp4_bytes="
              << bytes << " PASS\n";
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
        _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
        _wputenv_s(L"HTFX_FFMPEG", L"");
        std::cerr << "media_io_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
