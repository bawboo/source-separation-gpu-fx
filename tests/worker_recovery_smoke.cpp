#include "PluginProcessor.h"

#include <juce_events/juce_events.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

void terminateWorker(std::uint32_t pid) {
    const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        throw std::runtime_error("OpenProcess failed");
    }
    if (!TerminateProcess(process, 86)) {
        CloseHandle(process);
        throw std::runtime_error("TerminateProcess failed");
    }
    if (WaitForSingleObject(process, 5'000) != WAIT_OBJECT_0) {
        CloseHandle(process);
        throw std::runtime_error("terminated worker did not exit");
    }
    CloseHandle(process);
}

void fillSignal(juce::AudioBuffer<float>& buffer, std::uint64_t startSample) {
    constexpr double pi = 3.1415926535897932384626433832795;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const double time = static_cast<double>(startSample + sample) / 44'100.0;
        buffer.setSample(0, sample, static_cast<float>(0.05 * std::sin(2.0 * pi * 220.0 * time)));
        buffer.setSample(1, sample, static_cast<float>(0.05 * std::sin(2.0 * pi * 330.0 * time)));
    }
}

double processTimed(
    HTDemucsGpuFXAudioProcessor& processor,
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi,
    std::uint64_t sample) {
    fillSignal(buffer, sample);
    const auto start = std::chrono::steady_clock::now();
    processor.processBlock(buffer, midi);
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

int main() {
    try {
        _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
        auto* mode = processor->parameters().getParameter("operatingMode");
        if (mode == nullptr) {
            throw std::runtime_error("operatingMode parameter missing");
        }
        mode->setValueNotifyingHost(mode->convertTo0to1(1.0f));
        constexpr int blockSize = 256;
        processor->prepareToPlay(44'100.0, blockSize);
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        std::uint64_t samplePosition = 0;
        double maxProcessBlockMs = processTimed(
            *processor, buffer, midi, samplePosition);
        samplePosition += blockSize;

        const auto firstPidDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (processor->getWorkerPid() == 0 &&
               std::chrono::steady_clock::now() < firstPidDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto firstPid = processor->getWorkerPid();
        if (firstPid == 0) {
            throw std::runtime_error(
                "initial worker did not start: " +
                processor->getBridgeStatusText().toStdString());
        }
        const auto initialEpoch = processor->getStreamEpoch();
        terminateWorker(firstPid);

        const auto feedDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (processor->getStreamEpoch() == initialEpoch &&
               std::chrono::steady_clock::now() < feedDeadline) {
            maxProcessBlockMs = (std::max)(
                maxProcessBlockMs,
                processTimed(*processor, buffer, midi, samplePosition));
            samplePosition += blockSize;
            if (samplePosition % 8'192 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (processor->getStreamEpoch() == initialEpoch) {
            throw std::runtime_error(
                "worker death did not request a new stream epoch: " +
                processor->getBridgeStatusText().toStdString());
        }

        const auto restartDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while ((processor->getWorkerPid() == 0 ||
                processor->getWorkerPid() == firstPid) &&
               std::chrono::steady_clock::now() < restartDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto secondPid = processor->getWorkerPid();
        const auto restarts = processor->getWorkerRestarts();
        const auto finalEpoch = processor->getStreamEpoch();
        const auto status = processor->getBridgeStatusText();
        const auto overruns = processor->getInputOverruns();
        processor->releaseResources();

        const bool passed = secondPid != 0 && secondPid != firstPid &&
                            finalEpoch > initialEpoch && restarts >= 1 &&
                            maxProcessBlockMs < 20.0 && overruns == 0;
        std::cout << "first_pid=" << firstPid
                  << " second_pid=" << secondPid
                  << " initial_epoch=" << initialEpoch
                  << " final_epoch=" << finalEpoch
                  << " restarts=" << restarts
                  << " max_process_block_ms=" << maxProcessBlockMs
                  << " input_overruns=" << overruns
                  << " status=" << status
                  << " PASS=" << std::boolalpha << passed << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "worker_recovery_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
