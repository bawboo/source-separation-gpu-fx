#include "GpuWorkerClient.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path python;
    std::filesystem::path worker;
    std::filesystem::path models;
    std::uint32_t gpuIndex = 0;
};

struct TestCase {
    const char* model;
    double segmentSeconds;
    std::uint32_t sources;
};

std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

Options parseOptions(int argc, wchar_t** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        const auto take = [&]() -> std::wstring {
            if (++index >= argc) {
                throw std::runtime_error("missing value for " + narrow(argument));
            }
            return argv[index];
        };
        if (argument == L"--python") {
            options.python = take();
        } else if (argument == L"--worker") {
            options.worker = take();
        } else if (argument == L"--models") {
            options.models = take();
        } else if (argument == L"--gpu") {
            options.gpuIndex = static_cast<std::uint32_t>(std::stoul(take()));
        } else {
            throw std::runtime_error("unknown argument " + narrow(argument));
        }
    }
    if (options.python.empty() || options.worker.empty() || options.models.empty()) {
        throw std::runtime_error("--python, --worker and --models are required");
    }
    return options;
}

void fillInput(std::vector<float>& input, std::uint32_t frames) {
    constexpr double sampleRate = 44'100.0;
    constexpr double pi = 3.1415926535897932384626433832795;
    for (std::uint32_t sample = 0; sample < frames; ++sample) {
        const double time = static_cast<double>(sample) / sampleRate;
        input[sample] = static_cast<float>(
            0.05 * std::sin(2.0 * pi * 110.0 * time) +
            0.02 * std::sin(2.0 * pi * 997.0 * time));
        input[frames + sample] = static_cast<float>(
            0.04 * std::sin(2.0 * pi * 146.83 * time + 0.31) +
            0.015 * std::sin(2.0 * pi * 733.0 * time + 0.17));
    }
}

int run(const Options& options) {
    // Five segment settings are exercised with the default model; the other
    // registry models then verify bag loading, six-source layout and MMI loading.
    constexpr std::array<TestCase, 8> cases{{
        {"htdemucs", 2.0, 4},
        {"htdemucs", 3.0, 4},
        {"htdemucs", 4.0, 4},
        {"htdemucs", 5.0, 4},
        {"htdemucs", 7.8, 4},
        {"htdemucs_ft", 2.0, 4},
        {"htdemucs_6s", 3.0, 6},
        {"hdemucs_mmi", 4.0, 4},
    }};

    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& test = cases[index];
        const auto segmentFrames = static_cast<std::uint32_t>(
            std::llround(test.segmentSeconds * 44'100.0));
        const auto hopFrames = segmentFrames * 3 / 4;

        htfx::GpuWorkerConfig config;
        config.pythonExecutable = options.python;
        config.workerScript = options.worker;
        config.modelsDirectory = options.models;
        config.modelName = test.model;
        config.backend = htfx::WorkerBackend::cuda;
        config.gpuIndex = options.gpuIndex;
        config.sourceCount = test.sources;
        config.segmentFrames = segmentFrames;
        config.hopFrames = hopFrames;
        config.readyTimeout = std::chrono::minutes(3);
        config.processTimeout = std::chrono::seconds(30);

        htfx::GpuWorkerClient client;
        if (!client.start(config, static_cast<std::uint64_t>(index + 1))) {
            throw std::runtime_error(
                std::string(test.model) + " " + std::to_string(test.segmentSeconds) +
                "s start failed: " + client.lastError());
        }
        if (client.activeSourceCount() != test.sources ||
            client.activeSegmentFrames() != segmentFrames ||
            client.activeHopFrames() != hopFrames ||
            client.resolvedBackend() != htfx::WorkerBackend::cuda) {
            throw std::runtime_error(std::string(test.model) + " worker metadata mismatch");
        }

        std::vector<float> input(static_cast<std::size_t>(2) * hopFrames);
        fillInput(input, hopFrames);
        const float* output = nullptr;
        double elapsedMs = 0.0;
        if (!client.process(
                static_cast<std::uint64_t>(index + 1),
                input.data(),
                hopFrames,
                output,
                &elapsedMs)) {
            throw std::runtime_error(
                std::string(test.model) + " process failed: " + client.lastError());
        }

        double energy = 0.0;
        for (std::uint32_t source = 0; source < test.sources; ++source) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const float* plane = output +
                    (static_cast<std::size_t>(source) * 2 + channel) *
                        htfx::GpuWorkerClient::kMaxFrames;
                for (std::uint32_t sample = 0; sample < hopFrames; ++sample) {
                    if (!std::isfinite(plane[sample])) {
                        throw std::runtime_error(std::string(test.model) + " non-finite output");
                    }
                    energy += static_cast<double>(plane[sample]) * plane[sample];
                }
            }
        }
        const auto gpuName = client.gpuName();
        const auto inferenceMs =
            static_cast<double>(client.lastInferenceMicroseconds()) / 1000.0;
        client.stop();
        std::cout << "model=" << test.model
                  << " segment_s=" << test.segmentSeconds
                  << " segment_frames=" << segmentFrames
                  << " hop_frames=" << hopFrames
                  << " sources=" << test.sources
                  << " request_ms=" << elapsedMs
                  << " inference_ms=" << inferenceMs
                  << " energy=" << energy
                  << " device=" << gpuName
                  << " PASS\n" << std::flush;
    }
    std::cout << "matrix_cases=" << cases.size() << " PASS\n";
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "worker_registry_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
