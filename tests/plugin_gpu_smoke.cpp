#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void selectRealtimeMode(HTDemucsGpuFXAudioProcessor& processor) {
    auto* parameter = processor.parameters().getParameter("operatingMode");
    if (parameter == nullptr) {
        throw std::runtime_error("operatingMode parameter missing");
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(1.0f));
}

std::vector<float> readFloats(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const auto bytes = stream.tellg();
    if (bytes < 0 || bytes % static_cast<std::streamoff>(sizeof(float)) != 0) {
        throw std::runtime_error("invalid float file size");
    }
    std::vector<float> result(
        static_cast<std::size_t>(bytes / static_cast<std::streamoff>(sizeof(float))));
    stream.seekg(0);
    stream.read(
        reinterpret_cast<char*>(result.data()),
        static_cast<std::streamsize>(result.size() * sizeof(float)));
    if (!stream) {
        throw std::runtime_error("failed reading " + path.string());
    }
    return result;
}

float inputSample(
    const std::vector<float>& input,
    std::uint64_t absoluteSample,
    std::size_t channel) {
    constexpr auto hop = static_cast<std::uint64_t>(
        HTDemucsGpuFXAudioProcessor::kHopSamples);
    const auto hopIndex = absoluteSample / hop;
    const auto withinHop = absoluteSample % hop;
    const auto index = hopIndex * 2 * hop + channel * hop + withinHop;
    return index < input.size() ? input[static_cast<std::size_t>(index)] : 0.0f;
}

float expectedMix(
    const std::vector<float>& output,
    std::size_t channel,
    std::size_t alignedSample) {
    constexpr std::size_t hop = HTDemucsGpuFXAudioProcessor::kHopSamples;
    float mixed = 0.0f;
    for (std::size_t source = 0; source < 4; ++source) {
        const auto plane = source * 2 + channel;
        mixed += output[plane * hop + alignedSample];
    }
    return mixed;
}

int run(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath) {
    constexpr int blockSize = 256;
    constexpr int captureSamples = 8'192;
    const auto input = readFloats(inputPath);
    const auto expected = readFloats(outputPath);
    const std::size_t requiredInput =
        2u * 2u * HTDemucsGpuFXAudioProcessor::kHopSamples;
    const std::size_t requiredOutput =
        4u * 2u * HTDemucsGpuFXAudioProcessor::kHopSamples;
    if (input.size() < requiredInput || expected.size() < requiredOutput) {
        throw std::runtime_error("raw M4 vectors are too short");
    }

    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    selectRealtimeMode(*processor);
    processor->prepareToPlay(44'100.0, blockSize);
    if (processor->getLatencySamples() !=
        HTDemucsGpuFXAudioProcessor::kReportedLatencySamples) {
        throw std::runtime_error("reported latency changed");
    }

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    const std::uint64_t waitBoundary =
        ((HTDemucsGpuFXAudioProcessor::kHopSamples + blockSize - 1) / blockSize) *
        blockSize;
    const std::uint64_t totalSamples =
        HTDemucsGpuFXAudioProcessor::kReportedLatencySamples + captureSamples;
    std::uint64_t processed = 0;
    bool waited = false;
    std::vector<float> capturedLeft;
    std::vector<float> capturedRight;
    capturedLeft.reserve(captureSamples);
    capturedRight.reserve(captureSamples);

    while (processed < totalSamples) {
        for (int sample = 0; sample < blockSize; ++sample) {
            const auto absolute = processed + static_cast<std::uint64_t>(sample);
            buffer.setSample(0, sample, inputSample(input, absolute, 0));
            buffer.setSample(1, sample, inputSample(input, absolute, 1));
        }
        processor->processBlock(buffer, midi);
        for (int sample = 0; sample < blockSize; ++sample) {
            const auto absolute = processed + static_cast<std::uint64_t>(sample);
            if (absolute >= HTDemucsGpuFXAudioProcessor::kReportedLatencySamples &&
                capturedLeft.size() < captureSamples) {
                capturedLeft.push_back(buffer.getSample(0, sample));
                capturedRight.push_back(buffer.getSample(1, sample));
            }
        }
        processed += blockSize;

        if (!waited && processed >= waitBoundary) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!processor->getBridgeStatusText().startsWith("Running") &&
                   std::chrono::steady_clock::now() < deadline) {
                if (processor->getBridgeStatusText().startsWith("Error") ||
                    processor->getBridgeStatusText().startsWith("Recovering")) {
                    throw std::runtime_error(
                        processor->getBridgeStatusText().toStdString());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!processor->getBridgeStatusText().startsWith("Running")) {
                throw std::runtime_error(
                    "plugin GPU worker timeout: " +
                    processor->getBridgeStatusText().toStdString());
            }
            waited = true;
        }
    }

    const auto status = processor->getBridgeStatusText();
    const auto overruns = processor->getInputOverruns();
    const auto underruns = processor->getOutputUnderruns();
    processor->releaseResources();

    if (capturedLeft.size() != captureSamples || capturedRight.size() != captureSamples) {
        throw std::runtime_error("capture length mismatch");
    }
    float peakError = 0.0f;
    double squaredError = 0.0;
    std::uint64_t compared = 0;
    for (std::size_t sample = 0; sample < capturedLeft.size(); ++sample) {
        const auto aligned = static_cast<std::size_t>(
            HTDemucsGpuFXAudioProcessor::kOverlapSamples) + sample;
        const float errors[2]{
            capturedLeft[sample] - expectedMix(expected, 0, aligned),
            capturedRight[sample] - expectedMix(expected, 1, aligned),
        };
        for (const float error : errors) {
            peakError = (std::max)(peakError, std::abs(error));
            squaredError += static_cast<double>(error) * error;
            ++compared;
        }
    }
    const double rmsError = std::sqrt(squaredError / static_cast<double>(compared));
    const bool passed = peakError <= 1.0e-6f && overruns == 0 && underruns == 0 &&
                        status.startsWith("Running");
    std::cout << "captured_stereo_samples=" << compared
              << " peak_error=" << peakError
              << " rms_error=" << rmsError
              << " input_overruns=" << overruns
              << " output_underruns=" << underruns
              << " status=" << status
              << " PASS=" << std::boolalpha << passed << '\n';
    return passed ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: plugin_gpu_smoke <worker_input.f32> <worker_output.f32>\n";
            return 2;
        }
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run(
            std::filesystem::u8path(argv[1]),
            std::filesystem::u8path(argv[2]));
    } catch (const std::exception& error) {
        std::cerr << "plugin_gpu_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
