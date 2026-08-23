#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

float runImpulse(
    HTDemucsGpuFXAudioProcessor& processor,
    int blockSize,
    bool waitForError) {
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    const int target = HTDemucsGpuFXAudioProcessor::kReportedLatencySamples;
    const int waitBoundary =
        ((HTDemucsGpuFXAudioProcessor::kHopSamples + blockSize - 1) / blockSize) *
        blockSize;
    int processed = 0;
    bool waited = false;
    float captured = 0.0f;
    while (processed <= target) {
        buffer.clear();
        if (processed == 0) {
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, -1.0f);
        }
        processor.processBlock(buffer, midi);
        if (target >= processed && target < processed + blockSize) {
            captured = buffer.getSample(0, target - processed);
        }
        processed += blockSize;
        if (waitForError && !waited && processed >= waitBoundary) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!processor.getBridgeStatusText().startsWith("Error") &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!processor.getBridgeStatusText().startsWith("Error")) {
                throw std::runtime_error(
                    "invalid GPU did not enter Error: " +
                    processor.getBridgeStatusText().toStdString());
            }
            waited = true;
        }
    }
    return captured;
}

void setGpuIndex(HTDemucsGpuFXAudioProcessor& processor, int index) {
    auto* parameter = processor.parameters().getParameter("gpuIndex");
    if (parameter == nullptr) {
        throw std::runtime_error("gpuIndex parameter missing");
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
}

void selectRealtimeMode(HTDemucsGpuFXAudioProcessor& processor) {
    auto* parameter = processor.parameters().getParameter("operatingMode");
    if (parameter == nullptr) {
        throw std::runtime_error("operatingMode parameter missing");
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(1.0f));
}

}  // namespace

int main() {
    try {
        _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
        juce::ScopedJuceInitialiser_GUI juceInitialiser;

        auto unsupported = std::make_unique<HTDemucsGpuFXAudioProcessor>();
        selectRealtimeMode(*unsupported);
        unsupported->prepareToPlay(48'000.0, 256);
        const float unsupportedDry = runImpulse(*unsupported, 256, false);
        const auto unsupportedStatus = unsupported->getBridgeStatusText();
        unsupported->releaseResources();

        auto invalidGpu = std::make_unique<HTDemucsGpuFXAudioProcessor>();
        selectRealtimeMode(*invalidGpu);
        setGpuIndex(*invalidGpu, 7);
        invalidGpu->prepareToPlay(44'100.0, 256);
        const float invalidGpuDry = runImpulse(*invalidGpu, 256, true);
        const auto invalidStatus = invalidGpu->getBridgeStatusText();
        const auto invalidPid = invalidGpu->getWorkerPid();
        invalidGpu->releaseResources();

        const bool passed =
            std::abs(unsupportedDry - 1.0f) < 1.0e-6f &&
            unsupportedStatus == "Unsupported sample rate" &&
            std::abs(invalidGpuDry - 1.0f) < 1.0e-6f &&
            invalidStatus.startsWith("Error") && invalidPid == 0;
        std::cout << "unsupported_dry=" << unsupportedDry
                  << " unsupported_status=" << unsupportedStatus
                  << " invalid_gpu_dry=" << invalidGpuDry
                  << " invalid_status=" << invalidStatus
                  << " invalid_pid=" << invalidPid
                  << " PASS=" << std::boolalpha << passed << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "fallback_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
