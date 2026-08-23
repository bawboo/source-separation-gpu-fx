#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

void setChoice(
    juce::AudioProcessorValueTreeState& state,
    const juce::String& id,
    int index) {
    auto* parameter = state.getParameter(id);
    if (parameter == nullptr) {
        throw std::runtime_error("missing parameter " + id.toStdString());
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
}

void setParameter(
    juce::AudioProcessorValueTreeState& state,
    const juce::String& id,
    float plainValue) {
    auto* parameter = state.getParameter(id);
    if (parameter == nullptr) {
        throw std::runtime_error("missing parameter " + id.toStdString());
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

int run(bool cpu) {
    constexpr int blockSize = 256;
    constexpr int samplesToRecord = 44'100;
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);

    setChoice(processor->parameters(), "operatingMode", 0);
    setChoice(processor->parameters(), "segmentLength", 0);
    setChoice(processor->parameters(), "model", 0);
    setChoice(processor->parameters(), "computeBackend", cpu ? 2 : 0);
    processor->prepareToPlay(44'100.0, blockSize);
    processor->applyUserConfiguration();
    if (processor->getLatencySamples() != 0 ||
        processor->getOperatingMode() !=
            HTDemucsGpuFXAudioProcessor::OperatingMode::record) {
        throw std::runtime_error("standalone did not enter zero-latency record mode");
    }
    if (!processor->beginRecording()) {
        throw std::runtime_error(processor->getRecordStatusText().toStdString());
    }

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    int processed = 0;
    constexpr double pi = 3.1415926535897932384626433832795;
    while (processed < samplesToRecord) {
        for (int sample = 0; sample < blockSize; ++sample) {
            const int absolute = processed + sample;
            buffer.setSample(
                0,
                sample,
                absolute < samplesToRecord
                    ? static_cast<float>(0.1 * std::sin(2.0 * pi * 220.0 * absolute / 44'100.0))
                    : 0.0f);
            buffer.setSample(
                1,
                sample,
                absolute < samplesToRecord
                    ? static_cast<float>(0.08 * std::sin(2.0 * pi * 330.0 * absolute / 44'100.0))
                    : 0.0f);
        }
        processor->processBlock(buffer, midi);
        processed += blockSize;
    }
    processor->endRecording();
    if (std::abs(processor->getRecordedSeconds() - 1.0) > 0.02) {
        throw std::runtime_error("recorded duration mismatch");
    }
    if (!processor->beginSeparation()) {
        throw std::runtime_error(processor->getRecordStatusText().toStdString());
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::minutes(cpu ? 5 : 2);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto state = processor->getSeparationState();
        if (state == HTDemucsGpuFXAudioProcessor::SeparationState::previewReady) {
            break;
        }
        if (state == HTDemucsGpuFXAudioProcessor::SeparationState::error) {
            throw std::runtime_error(processor->getRecordStatusText().toStdString());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!processor->hasPreview() || processor->getSeparationProgress() < 1.0) {
        throw std::runtime_error(
            "separation timeout: " + processor->getRecordStatusText().toStdString());
    }
    if (cpu != processor->resolvedToCpu()) {
        throw std::runtime_error("resolved compute backend mismatch");
    }

    const auto renderPreview = [&]() {
        processor->stopPreview();
        processor->togglePreviewPlayback();
        double energy = 0.0;
        int previewSamples = 0;
        while (processor->isPreviewPlaying() &&
               previewSamples < samplesToRecord + blockSize) {
            buffer.clear();
            processor->processBlock(buffer, midi);
            for (int channel = 0; channel < 2; ++channel) {
                for (int sample = 0; sample < blockSize; ++sample) {
                    const float value = buffer.getSample(channel, sample);
                    if (!std::isfinite(value)) {
                        throw std::runtime_error("preview produced non-finite audio");
                    }
                    energy += static_cast<double>(value) * value;
                }
            }
            previewSamples += blockSize;
        }
        return energy;
    };

    const double fullMixEnergy = renderPreview();
    constexpr std::array<const char*, 6> stemIds{
        "drumsGain", "bassGain", "otherGain", "vocalsGain", "guitarGain", "pianoGain"};
    for (const auto* id : stemIds) {
        setParameter(processor->parameters(), id, -60.0f);
    }
    const double mutedStemEnergy = renderPreview();
    setParameter(processor->parameters(), "bypass", 1.0f);
    const double bypassEnergy = renderPreview();

    const bool mixControlsPassed =
        fullMixEnergy > 1.0e-8 &&
        mutedStemEnergy < fullMixEnergy * 0.10 &&
        bypassEnergy > mutedStemEnergy * 5.0;
    const bool passed = mixControlsPassed && !processor->isPreviewPlaying() &&
                        processor->getInputOverruns() == 0 &&
                        processor->getOutputUnderruns() == 0;
    std::cout << "backend=" << (cpu ? "cpu" : "auto")
              << " recorded_seconds=" << processor->getRecordedSeconds()
              << " preview_seconds=" << processor->getPreviewDurationSeconds()
              << " progress=" << processor->getSeparationProgress()
              << " inference_ms=" << processor->getLastInferenceMilliseconds()
              << " full_mix_energy=" << fullMixEnergy
              << " muted_stem_energy=" << mutedStemEnergy
              << " bypass_original_energy=" << bypassEnergy
              << " mix_controls=" << std::boolalpha << mixControlsPassed
              << " status=" << processor->getRecordStatusText()
              << " PASS=" << std::boolalpha << passed << '\n';
    processor->releaseResources();
    return passed ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run(argc > 1 && std::string(argv[1]) == "--cpu");
    } catch (const std::exception& error) {
        juce::AudioProcessor::setTypeOfNextNewPlugin(
            juce::AudioProcessor::wrapperType_Undefined);
        std::cerr << "record_mode_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
