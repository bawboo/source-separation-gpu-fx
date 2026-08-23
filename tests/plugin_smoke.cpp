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

void setFloatParameter(
    HTDemucsGpuFXAudioProcessor& processor, const char* id, float plainValue) {
    auto* parameter = processor.parameters().getParameter(id);
    if (parameter == nullptr) {
        throw std::runtime_error(std::string("missing parameter ") + id);
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

struct CaseResult {
    int blockSize = 0;
    bool bypassed = false;
    float targetOutput = 0.0f;
    std::uint64_t overruns = 0;
    std::uint64_t underruns = 0;
    juce::String status;
    bool passed = false;
};

CaseResult runCase(int blockSize, bool bypassed) {
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    setFloatParameter(*processor, "operatingMode", 1.0f);
    setFloatParameter(*processor, "drumsGain", 0.0f);
    setFloatParameter(*processor, "bassGain", -60.0f);
    setFloatParameter(*processor, "otherGain", -60.0f);
    setFloatParameter(*processor, "vocalsGain", -60.0f);
    setFloatParameter(*processor, "outputTrim", 0.0f);
    setFloatParameter(*processor, "bypass", bypassed ? 1.0f : 0.0f);
    processor->prepareToPlay(44'100.0, blockSize);

    CaseResult result;
    result.blockSize = blockSize;
    result.bypassed = bypassed;
    if (processor->getLatencySamples() !=
        HTDemucsGpuFXAudioProcessor::kReportedLatencySamples) {
        return result;
    }

    const int targetSample = HTDemucsGpuFXAudioProcessor::kReportedLatencySamples;
    const int totalSamples = targetSample + std::max(blockSize, 1024);
    const int waitBoundary =
        ((HTDemucsGpuFXAudioProcessor::kHopSamples + blockSize - 1) / blockSize) *
        blockSize;
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    int processed = 0;
    bool captured = false;
    bool waitedForBridge = false;

    while (processed < totalSamples) {
        buffer.clear();
        if (processed == 0) {
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, 1.0f);
        }
        processor->processBlock(buffer, midi);
        if (targetSample >= processed && targetSample < processed + blockSize) {
            result.targetOutput = buffer.getSample(0, targetSample - processed);
            captured = true;
        }
        processed += blockSize;

        if (!waitedForBridge && processed >= waitBoundary) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (processor->getBridgeStatusText() != "Running (fake worker)" &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            waitedForBridge = true;
        }
    }

    result.overruns = processor->getInputOverruns();
    result.underruns = processor->getOutputUnderruns();
    result.status = processor->getBridgeStatusText();
    processor->releaseResources();
    const float expected = bypassed ? 1.0f : 0.4f;
    result.passed = captured && std::abs(result.targetOutput - expected) < 1.0e-5f &&
                    result.overruns == 0 && result.underruns == 0 &&
                    result.status == "Running (fake worker)";
    return result;
}

}  // namespace

int main() {
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"1");
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    constexpr std::array<int, 5> blockSizes{64, 128, 256, 512, 1024};
    bool passed = true;
    for (const int blockSize : blockSizes) {
        const auto result = runCase(blockSize, false);
        passed = passed && result.passed;
        std::cout << "block=" << result.blockSize
                  << " bypass=false target=" << result.targetOutput
                  << " overruns=" << result.overruns
                  << " underruns=" << result.underruns
                  << " status=" << result.status
                  << " pass=" << std::boolalpha << result.passed << '\n';
    }
    const auto bypass = runCase(256, true);
    passed = passed && bypass.passed;
    std::cout << "block=" << bypass.blockSize
              << " bypass=true target=" << bypass.targetOutput
              << " overruns=" << bypass.overruns
              << " underruns=" << bypass.underruns
              << " status=" << bypass.status
              << " pass=" << std::boolalpha << bypass.passed << '\n'
              << "latency_samples="
              << HTDemucsGpuFXAudioProcessor::kReportedLatencySamples << '\n'
              << "PASS=" << std::boolalpha << passed << '\n';
    return passed ? 0 : 1;
}
