#include "GpuWorkerClient.h"

#include <algorithm>
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
    std::filesystem::path workerExecutable;
    std::filesystem::path models;
    std::string model{"htdemucs"};
    htfx::WorkerBackend backend{htfx::WorkerBackend::autoSelect};
    double segmentSeconds{2.0};
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
        if (argument == "--worker-executable") {
            options.workerExecutable = take();
        } else if (argument == "--models") {
            options.models = take();
        } else if (argument == "--model") {
            options.model = take();
        } else if (argument == "--segment") {
            options.segmentSeconds = std::stod(take());
        } else if (argument == "--backend") {
            const auto value = take();
            if (value == "auto") options.backend = htfx::WorkerBackend::autoSelect;
            else if (value == "mps") options.backend = htfx::WorkerBackend::mps;
            else if (value == "cpu") options.backend = htfx::WorkerBackend::cpu;
            else throw std::runtime_error("unsupported backend " + value);
        } else {
            throw std::runtime_error("unknown argument " + argument);
        }
    }
    if (options.workerExecutable.empty() || options.models.empty()) {
        throw std::runtime_error("--worker-executable and --models are required");
    }
    if (options.segmentSeconds < 2.0 || options.segmentSeconds > 7.8) {
        throw std::runtime_error("--segment must be between 2.0 and 7.8 seconds");
    }
    return options;
}

std::uint32_t sourceCountFor(const std::string& model) {
    return model == "htdemucs_6s" ? 6u : 4u;
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
    const auto segmentFrames = static_cast<std::uint32_t>(
        std::llround(options.segmentSeconds * 44'100.0));
    const auto hopFrames = segmentFrames * 3 / 4;
    const auto sources = sourceCountFor(options.model);

    htfx::GpuWorkerConfig config;
    config.workerExecutable = options.workerExecutable;
    config.modelsDirectory = options.models;
    config.modelName = options.model;
    config.backend = options.backend;
    config.sourceCount = sources;
    config.segmentFrames = segmentFrames;
    config.hopFrames = hopFrames;
    config.readyTimeout = std::chrono::minutes(15);
    config.processTimeout = std::chrono::minutes(5);

    htfx::GpuWorkerClient client;
    if (!client.start(config, 1)) {
        throw std::runtime_error("worker start failed: " + client.lastError());
    }
    if (client.activeSourceCount() != sources ||
        client.activeSegmentFrames() != segmentFrames ||
        client.activeHopFrames() != hopFrames) {
        throw std::runtime_error("worker metadata mismatch");
    }
    if (options.backend != htfx::WorkerBackend::autoSelect &&
        client.resolvedBackend() != options.backend) {
        throw std::runtime_error("requested backend did not resolve exactly");
    }

    std::vector<float> input(static_cast<std::size_t>(2) * hopFrames);
    fillInput(input, hopFrames);
    const float* output = nullptr;
    double elapsedMs = 0.0;
    if (!client.process(1, input.data(), hopFrames, output, &elapsedMs)) {
        throw std::runtime_error("worker process failed: " + client.lastError());
    }

    double energy = 0.0;
    for (std::uint32_t source = 0; source < sources; ++source) {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const float* plane = output +
                (static_cast<std::size_t>(source) * 2 + channel) *
                    htfx::GpuWorkerClient::kMaxFrames;
            for (std::uint32_t sample = 0; sample < hopFrames; ++sample) {
                if (!std::isfinite(plane[sample])) {
                    throw std::runtime_error("non-finite output");
                }
                energy += static_cast<double>(plane[sample]) * plane[sample];
            }
        }
    }
    if (!(energy > 0.0)) {
        throw std::runtime_error("zero-energy output");
    }
    if (options.backend == htfx::WorkerBackend::mps &&
        client.cudaMaxAllocatedBytes() == 0) {
        throw std::runtime_error("MPS inference reported zero accelerator memory");
    }
    if (options.backend == htfx::WorkerBackend::cpu &&
        (client.cudaAllocatedBytes() != 0 || client.cudaMaxAllocatedBytes() != 0)) {
        throw std::runtime_error("CPU inference reported accelerator memory");
    }

    std::cout << std::fixed << std::setprecision(3)
              << "model=" << options.model
              << " segment_s=" << options.segmentSeconds
              << " sources=" << sources
              << " request_ms=" << elapsedMs
              << " inference_ms="
              << static_cast<double>(client.lastInferenceMicroseconds()) / 1000.0
              << " accelerator_bytes=" << client.cudaAllocatedBytes()
              << " accelerator_max_bytes=" << client.cudaMaxAllocatedBytes()
              << " energy=" << energy
              << " device=" << client.gpuName()
              << " PASS\n";
    client.stop();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "worker_registry_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
