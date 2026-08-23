#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kHop = 257'985;
constexpr std::size_t kOverlap = 85'995;
constexpr std::uint64_t kLatency = 366'030;

std::vector<float> readFloats(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const auto bytes = stream.tellg();
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
    const auto hopIndex = absoluteSample / kHop;
    const auto withinHop = absoluteSample % kHop;
    const auto index = hopIndex * 2 * kHop + channel * kHop + withinHop;
    return index < input.size() ? input[static_cast<std::size_t>(index)] : 0.0f;
}

float expectedMix(
    const std::vector<float>& output,
    std::size_t channel,
    std::size_t sample) {
    float result = 0.0f;
    for (std::size_t source = 0; source < 4; ++source) {
        result += output[(source * 2 + channel) * kHop + sample];
    }
    return result;
}

void selectRealtimeMode(juce::AudioProcessor& processor) {
    for (auto* parameter : processor.getParameters()) {
        if (parameter->getName(64) == "Mode") {
            parameter->setValueNotifyingHost(1.0f);
            return;
        }
    }
    throw std::runtime_error("VST3 Mode parameter missing");
}

int run(
    const std::filesystem::path& bundle,
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath) {
    constexpr int blockSize = 256;
    constexpr std::size_t captureSamples = 8'192;
    const auto input = readFloats(inputPath);
    const auto expected = readFloats(outputPath);
    if (input.size() < 4 * kHop || expected.size() < 8 * kHop) {
        throw std::runtime_error("raw M4 vectors are too short");
    }

    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    _wputenv_s(L"HTFX_GPU_WORKER", L"");
    _wputenv_s(L"HTFX_CHECKPOINT", L"");
    _wputenv_s(L"HTFX_REQUIRE_BUNDLED_SIDECAR", L"1");
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, bundle.string());
    if (descriptions.size() != 1) {
        throw std::runtime_error("VST3 type count is not one");
    }
    juce::String error;
    auto instance = format.createInstanceFromDescription(
        *descriptions[0], 44'100.0, blockSize, error);
    if (instance == nullptr) {
        throw std::runtime_error("instance creation failed: " + error.toStdString());
    }
    selectRealtimeMode(*instance);
    instance->setPlayConfigDetails(2, 2, 44'100.0, blockSize);
    instance->prepareToPlay(44'100.0, blockSize);
    if (instance->getLatencySamples() != static_cast<int>(kLatency)) {
        throw std::runtime_error("VST3 latency mismatch");
    }

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    const std::uint64_t waitBoundary = ((kHop + blockSize - 1) / blockSize) * blockSize;
    const std::uint64_t totalSamples = kLatency + captureSamples;
    std::uint64_t processed = 0;
    bool waited = false;
    std::vector<float> captured[2];
    captured[0].reserve(captureSamples);
    captured[1].reserve(captureSamples);

    while (processed < totalSamples) {
        for (int sample = 0; sample < blockSize; ++sample) {
            const auto absolute = processed + static_cast<std::uint64_t>(sample);
            buffer.setSample(0, sample, inputSample(input, absolute, 0));
            buffer.setSample(1, sample, inputSample(input, absolute, 1));
        }
        instance->processBlock(buffer, midi);
        for (int sample = 0; sample < blockSize; ++sample) {
            const auto absolute = processed + static_cast<std::uint64_t>(sample);
            if (absolute >= kLatency && captured[0].size() < captureSamples) {
                captured[0].push_back(buffer.getSample(0, sample));
                captured[1].push_back(buffer.getSample(1, sample));
            }
        }
        processed += blockSize;
        if (!waited && processed >= waitBoundary) {
            // The VST3 interface intentionally hides the concrete processor's
            // lifecycle state. Wait well beyond the measured 3-6 s load/warm-up.
            std::this_thread::sleep_for(std::chrono::seconds(12));
            waited = true;
        }
    }
    instance->releaseResources();
    instance.reset();

    float peakError = 0.0f;
    double squaredError = 0.0;
    std::uint64_t values = 0;
    for (std::size_t sample = 0; sample < captureSamples; ++sample) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const float difference = captured[channel][sample] -
                                     expectedMix(expected, channel, kOverlap + sample);
            peakError = (std::max)(peakError, std::abs(difference));
            squaredError += static_cast<double>(difference) * difference;
            ++values;
        }
    }
    const double rmsError = std::sqrt(squaredError / static_cast<double>(values));
    const bool passed = peakError <= 1.0e-6f;
    std::cout << "bundle_gpu_values=" << values
              << " peak_error=" << peakError
              << " rms_error=" << rmsError
              << " PASS=" << std::boolalpha << passed << '\n';
    return passed ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: vst3_gpu_smoke <bundle> <input.f32> <output.f32>\n";
            return 2;
        }
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run(argv[1], argv[2], argv[3]);
    } catch (const std::exception& error) {
        std::cerr << "vst3_gpu_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
