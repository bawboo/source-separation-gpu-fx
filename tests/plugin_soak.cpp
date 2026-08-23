#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    int blockSize = 256;
    double audioMinutes = 60.0;
    std::filesystem::path report = "results/m5/plugin_soak.json";
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto take = [&]() -> std::string {
            if (++index >= argc) {
                throw std::runtime_error("missing value for " + argument);
            }
            return argv[index];
        };
        if (argument == "--block") {
            options.blockSize = std::stoi(take());
        } else if (argument == "--minutes") {
            options.audioMinutes = std::stod(take());
        } else if (argument == "--report") {
            options.report = std::filesystem::path(take());
        } else {
            throw std::runtime_error("unknown argument " + argument);
        }
    }
    if (options.blockSize <= 0 || options.audioMinutes <= 0.0) {
        throw std::runtime_error("block size and minutes must be positive");
    }
    return options;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
    return values[(std::min)(index, values.size() - 1)];
}

double mean(const std::vector<double>& values) {
    return values.empty()
               ? 0.0
               : std::accumulate(values.begin(), values.end(), 0.0) /
                     static_cast<double>(values.size());
}

void fillSignal(
    juce::AudioBuffer<float>& buffer, double& leftPhase, double& rightPhase) {
    constexpr double leftStep = 220.0 / 44'100.0;
    constexpr double rightStep = 329.63 / 44'100.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        leftPhase += leftStep;
        rightPhase += rightStep;
        if (leftPhase >= 1.0) {
            leftPhase -= 1.0;
        }
        if (rightPhase >= 1.0) {
            rightPhase -= 1.0;
        }
        buffer.setSample(0, sample, static_cast<float>((leftPhase - 0.5) * 0.1));
        buffer.setSample(1, sample, static_cast<float>((rightPhase - 0.5) * 0.1));
    }
}

int run(const Options& options) {
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    if (auto* mode = processor->parameters().getParameter("operatingMode")) {
        mode->setValueNotifyingHost(mode->convertTo0to1(1.0f));
    } else {
        throw std::runtime_error("operatingMode parameter missing");
    }
    processor->prepareToPlay(44'100.0, options.blockSize);
    juce::AudioBuffer<float> buffer(2, options.blockSize);
    juce::MidiBuffer midi;
    double leftPhase = 0.0;
    double rightPhase = 0.0;
    const auto requestedSamples = static_cast<std::uint64_t>(
        std::ceil(options.audioMinutes * 60.0 * 44'100.0));
    const auto targetHops =
        (requestedSamples + HTDemucsGpuFXAudioProcessor::kHopSamples - 1) /
        HTDemucsGpuFXAudioProcessor::kHopSamples;
    const auto targetSamples =
        targetHops * HTDemucsGpuFXAudioProcessor::kHopSamples;
    const auto initialEpoch = processor->getStreamEpoch();
    std::uint64_t processedSamples = 0;
    std::uint64_t waitedProcesses = 0;
    std::vector<double> callbackTimes;
    std::vector<double> inferenceTimes;
    callbackTimes.reserve(
        static_cast<std::size_t>(targetSamples / options.blockSize + 2));
    inferenceTimes.reserve(static_cast<std::size_t>(targetHops));
    const auto wallStarted = std::chrono::steady_clock::now();

    while (waitedProcesses < targetHops) {
        fillSignal(buffer, leftPhase, rightPhase);
        const auto callbackStarted = std::chrono::steady_clock::now();
        processor->processBlock(buffer, midi);
        callbackTimes.push_back(std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - callbackStarted)
                                    .count());
        processedSamples += static_cast<std::uint64_t>(options.blockSize);

        const auto completedInputHops =
            processedSamples / HTDemucsGpuFXAudioProcessor::kHopSamples;
        while (waitedProcesses < completedInputHops && waitedProcesses < targetHops) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  (waitedProcesses == 0 ? std::chrono::seconds(30)
                                                        : std::chrono::seconds(3));
            while (processor->getWorkerProcesses() <= waitedProcesses &&
                   std::chrono::steady_clock::now() < deadline) {
                const auto status = processor->getBridgeStatusText();
                if (status.startsWith("Error") || status.startsWith("Recovering")) {
                    throw std::runtime_error(status.toStdString());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (processor->getWorkerProcesses() <= waitedProcesses) {
                throw std::runtime_error(
                    "inference wait timeout: " +
                    processor->getBridgeStatusText().toStdString());
            }
            inferenceTimes.push_back(processor->getLastInferenceMilliseconds());
            ++waitedProcesses;
            if (waitedProcesses == 1 || waitedProcesses % 25 == 0 ||
                waitedProcesses == targetHops) {
                std::cout << "block=" << options.blockSize
                          << " progress=" << waitedProcesses << "/" << targetHops
                          << " last_ms=" << std::fixed << std::setprecision(2)
                          << inferenceTimes.back() << '\n' << std::flush;
            }
        }
    }

    const auto status = processor->getBridgeStatusText();
    const auto inputOverruns = processor->getInputOverruns();
    const auto outputUnderruns = processor->getOutputUnderruns();
    const auto finalEpoch = processor->getStreamEpoch();
    const auto cudaAllocated = processor->getCudaAllocatedBytes();
    const auto cudaReserved = processor->getCudaReservedBytes();
    const auto cudaMaxAllocated = processor->getCudaMaxAllocatedBytes();
    const auto cudaMaxReserved = processor->getCudaMaxReservedBytes();
    processor->releaseResources();
    const double wallSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - wallStarted).count();

    const double callbackP99 = percentile(callbackTimes, 0.99);
    const double deadlineMs = 1000.0 * options.blockSize / 44'100.0;
    const double deadlineBudgetMs = deadlineMs * 0.20;
    const bool passed = waitedProcesses == targetHops && inputOverruns == 0 &&
                        outputUnderruns == 0 && finalEpoch == initialEpoch &&
                        callbackP99 < deadlineBudgetMs &&
                        status.startsWith("Running");

    if (const auto parent = options.report.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream report(options.report, std::ios::binary | std::ios::trunc);
    report << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"passed\": " << (passed ? "true" : "false") << ",\n"
           << "  \"host_block_size\": " << options.blockSize << ",\n"
           << "  \"requested_audio_minutes\": " << options.audioMinutes << ",\n"
           << "  \"equivalent_audio_samples\": " << targetSamples << ",\n"
           << "  \"inferences\": " << waitedProcesses << ",\n"
           << "  \"wall_seconds\": " << wallSeconds << ",\n"
           << "  \"input_overruns\": " << inputOverruns << ",\n"
           << "  \"output_underruns\": " << outputUnderruns << ",\n"
           << "  \"epoch_changes\": " << (finalEpoch - initialEpoch) << ",\n"
           << "  \"process_block_calls\": " << callbackTimes.size() << ",\n"
           << "  \"process_block_mean_ms\": " << mean(callbackTimes) << ",\n"
           << "  \"process_block_p95_ms\": " << percentile(callbackTimes, 0.95) << ",\n"
           << "  \"process_block_p99_ms\": " << callbackP99 << ",\n"
           << "  \"process_block_max_ms\": " << percentile(callbackTimes, 1.0) << ",\n"
           << "  \"buffer_deadline_ms\": " << deadlineMs << ",\n"
           << "  \"twenty_percent_deadline_ms\": " << deadlineBudgetMs << ",\n"
           << "  \"inference_mean_ms\": " << mean(inferenceTimes) << ",\n"
           << "  \"inference_p50_ms\": " << percentile(inferenceTimes, 0.50) << ",\n"
           << "  \"inference_p95_ms\": " << percentile(inferenceTimes, 0.95) << ",\n"
           << "  \"inference_p99_ms\": " << percentile(inferenceTimes, 0.99) << ",\n"
           << "  \"inference_max_ms\": " << percentile(inferenceTimes, 1.0) << ",\n"
           << "  \"cuda_allocated_bytes\": " << cudaAllocated << ",\n"
           << "  \"cuda_reserved_bytes\": " << cudaReserved << ",\n"
           << "  \"cuda_max_allocated_bytes\": " << cudaMaxAllocated << ",\n"
           << "  \"cuda_max_reserved_bytes\": " << cudaMaxReserved << "\n"
           << "}\n";
    std::cout << "block=" << options.blockSize
              << " equivalent_minutes=" << options.audioMinutes
              << " inferences=" << waitedProcesses
              << " callback_p99_ms=" << callbackP99
              << " deadline20_ms=" << deadlineBudgetMs
              << " inference_p99_ms=" << percentile(inferenceTimes, 0.99)
              << " overrun=" << inputOverruns
              << " underrun=" << outputUnderruns
              << " PASS=" << std::boolalpha << passed << '\n';
    return passed ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "plugin_soak fatal: " << error.what() << '\n';
        return 2;
    }
}
