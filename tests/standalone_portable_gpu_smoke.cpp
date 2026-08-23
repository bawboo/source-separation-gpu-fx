#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

void clearDevelopmentOverrides() {
    _wputenv_s(L"HTFX_PYTHON", L"");
    _wputenv_s(L"HTFX_GPU_WORKER", L"");
    _wputenv_s(L"HTFX_WORKER_EXECUTABLE", L"");
    _wputenv_s(L"HTFX_CHECKPOINT", L"");
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    _wputenv_s(L"HTFX_REQUIRE_BUNDLED_SIDECAR", L"1");
}

std::filesystem::path executableDirectory() {
    return std::filesystem::path(
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory()
            .getFullPathName()
            .toWideCharPointer());
}

void verifyPortableLayout(const std::filesystem::path& root) {
    const auto worker = root / L"Resources" / L"sidecar" / L"Runtime" /
                        L"htdemucs-worker" / L"htdemucs-worker.exe";
    const auto ffmpeg = root / L"Resources" / L"sidecar" / L"Runtime" /
                        L"ffmpeg" / L"bin" / L"ffmpeg.exe";
    const auto checkpoint = root / L"Resources" / L"sidecar" / L"models" /
                            L"955717e8-8726e21a.th";
    const auto registry = root / L"Resources" / L"sidecar" / L"models" /
                          L"model-manifest.json";
    const auto sixStemCheckpoint = root / L"Resources" / L"sidecar" / L"models" /
                                   L"5c90dfd2-34c22ccb.th";
    if (!std::filesystem::is_regular_file(worker)) {
        throw std::runtime_error("missing self-contained portable worker");
    }
    if (!std::filesystem::is_regular_file(ffmpeg)) {
        throw std::runtime_error("missing bundled FFmpeg");
    }
    if (!std::filesystem::is_regular_file(checkpoint) ||
        !std::filesystem::is_regular_file(registry) ||
        !std::filesystem::is_regular_file(sixStemCheckpoint)) {
        throw std::runtime_error("portable multi-model registry is incomplete");
    }
}

int run() {
    constexpr int blockSize = 512;
    clearDevelopmentOverrides();
    const auto portableRoot = executableDirectory();
    verifyPortableLayout(portableRoot);

    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);
    if (processor->getOperatingMode() !=
            HTDemucsGpuFXAudioProcessor::OperatingMode::record ||
        processor->getLatencySamples() != 0) {
        throw std::runtime_error("portable default is not zero-latency Record mode");
    }
    auto* mode = processor->parameters().getParameter("operatingMode");
    if (mode == nullptr) {
        throw std::runtime_error("operatingMode parameter missing");
    }
    mode->setValueNotifyingHost(mode->convertTo0to1(1.0f));
    processor->prepareToPlay(44'100.0, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    buffer.clear();

    const auto blocksToOneHop =
        (HTDemucsGpuFXAudioProcessor::kHopSamples + blockSize - 1) / blockSize;
    for (int block = 0; block < blocksToOneHop; ++block) {
        buffer.clear();
        processor->processBlock(buffer, midi);
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(90);
    while (!processor->getBridgeStatusText().startsWith("Running") &&
           std::chrono::steady_clock::now() < deadline) {
        const auto status = processor->getBridgeStatusText();
        if (status.startsWith("Error") || status.startsWith("Recovering")) {
            throw std::runtime_error(status.toStdString());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto status = processor->getBridgeStatusText();
    const auto workerPid = processor->getWorkerPid();
    const auto processes = processor->getWorkerProcesses();
    const auto inferenceMilliseconds = processor->getLastInferenceMilliseconds();
    const auto cudaAllocated = processor->getCudaAllocatedBytes();
    const auto cudaReserved = processor->getCudaReservedBytes();
    const auto cudaMaxAllocated = processor->getCudaMaxAllocatedBytes();
    const auto cudaMaxReserved = processor->getCudaMaxReservedBytes();
    const auto overruns = processor->getInputOverruns();
    const auto underruns = processor->getOutputUnderruns();

    const bool passed = status.startsWith("Running") && workerPid != 0 &&
                        processes >= 1 && inferenceMilliseconds > 0.0 &&
                        cudaMaxAllocated > 0 && overruns == 0 && underruns == 0;
    std::wcout << L"portable_root=" << portableRoot.wstring() << L'\n';
    std::cout << "worker_pid=" << workerPid
              << " worker_processes=" << processes
              << " inference_ms=" << inferenceMilliseconds
              << " cuda_allocated_bytes=" << cudaAllocated
              << " cuda_reserved_bytes=" << cudaReserved
              << " cuda_max_allocated_bytes=" << cudaMaxAllocated
              << " cuda_max_reserved_bytes=" << cudaMaxReserved
              << " input_overruns=" << overruns
              << " output_underruns=" << underruns
              << " status=" << status
              << " PASS=" << std::boolalpha << passed << '\n';

    processor->releaseResources();
    return passed ? 0 : 1;
}

}  // namespace

int main() {
    try {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run();
    } catch (const std::exception& error) {
        juce::AudioProcessor::setTypeOfNextNewPlugin(
            juce::AudioProcessor::wrapperType_Undefined);
        std::cerr << "standalone_portable_gpu_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
