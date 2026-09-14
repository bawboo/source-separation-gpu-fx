// Exercise the POSIX shared-memory worker client end to end.
//
// gpu_worker_smoke.cpp is the equivalent for Windows and cannot build here: it
// is written against wmain and <windows.h>. This is deliberately the smallest
// program that still drives the whole transport -- launch the frozen worker,
// hand it a segment of audio through shared memory, get four stems back -- so
// that a failure in cpp/GpuWorkerClientPosix.cpp is isolated from the audio
// engine, the editor and the media importer.
//
//   htdemucs_posix_ipc_check <worker executable> <models dir> [model] [backend]
//
// backend is auto (default), mps or cpu. Exit code 0 means the transport, the
// model load and one inference all completed.
#include "GpuWorkerClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kChannels = htfx::GpuWorkerClient::kChannels;

const char* backendName(htfx::WorkerBackend backend) {
    switch (backend) {
        case htfx::WorkerBackend::autoSelect: return "auto";
        case htfx::WorkerBackend::cuda: return "cuda";
        case htfx::WorkerBackend::cpu: return "cpu";
        case htfx::WorkerBackend::mps: return "mps";
    }
    return "?";
}

bool parseBackend(const std::string& text, htfx::WorkerBackend& out) {
    if (text == "auto") { out = htfx::WorkerBackend::autoSelect; return true; }
    if (text == "mps") { out = htfx::WorkerBackend::mps; return true; }
    if (text == "cpu") { out = htfx::WorkerBackend::cpu; return true; }
    if (text == "cuda") { out = htfx::WorkerBackend::cuda; return true; }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " <worker executable> <models dir> [model] [auto|mps|cpu]\n";
        return 2;
    }

    htfx::GpuWorkerConfig config;
    config.workerExecutable = std::filesystem::path(argv[1]);
    config.modelsDirectory = std::filesystem::path(argv[2]);
    config.modelName = argc > 3 ? argv[3] : "htdemucs";
    config.sourceCount = config.modelName == "htdemucs_6s" ? 6u : 4u;
    if (argc > 4 && !parseBackend(argv[4], config.backend)) {
        std::cerr << "unknown backend: " << argv[4] << "\n";
        return 2;
    }
    // Loading a checkpoint and warming the graph is far slower than the
    // Windows default allows for on first run, and MPS compiles its kernels
    // the first time each one is reached.
    config.readyTimeout = std::chrono::milliseconds{600'000};
    config.processTimeout = std::chrono::milliseconds{600'000};

    std::cout << "worker   : " << config.workerExecutable << "\n"
              << "models   : " << config.modelsDirectory << "\n"
              << "model    : " << config.modelName << " (" << config.sourceCount
              << " stems)\n"
              << "requested: " << backendName(config.backend) << "\n"
              << std::flush;

    htfx::GpuWorkerClient client;
    const auto startedAt = std::chrono::steady_clock::now();
    if (!client.start(config)) {
        std::cerr << "FAIL start: " << client.lastError() << "\n";
        return 1;
    }
    const auto readyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    std::cout << "ready in " << readyMs << " ms\n"
              << "worker pid " << client.workerPid() << "\n"
              << "resolved backend: " << backendName(client.resolvedBackend())
              << "\n"
              << "device: " << client.gpuName() << "\n"
              << std::flush;

    const std::uint32_t frames = client.activeHopFrames();
    const std::uint32_t sources = client.activeSourceCount();
    std::cout << "hop frames " << frames << ", active sources " << sources << "\n";

    // A tone plus a noise-free sweep: any stem that comes back bit-identical to
    // the input, or all zeros, is visible in the summary below.
    std::vector<float> input(static_cast<std::size_t>(kChannels) * frames, 0.0f);
    for (std::uint32_t sample = 0; sample < frames; ++sample) {
        const double t = static_cast<double>(sample) / 44100.0;
        const auto left = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 220.0 * t));
        const auto right = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 330.0 * t));
        input[sample] = left;
        input[static_cast<std::size_t>(frames) + sample] = right;
    }

    const float* output = nullptr;
    double elapsed = 0.0;
    const auto processStartedAt = std::chrono::steady_clock::now();
    if (!client.process(1, input.data(), frames, output, &elapsed)) {
        std::cerr << "FAIL process: " << client.lastError() << "\n";
        client.stop();
        return 1;
    }
    const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - processStartedAt).count();
    std::cout << "inference " << elapsed << " ms (wall " << wallMs << " ms)\n";

    if (output == nullptr) {
        std::cerr << "FAIL: process reported success but returned no buffer\n";
        client.stop();
        return 1;
    }

    // Separation that silently produced nothing is the failure this catches:
    // NaNs and all-zero stems both look like success at the transport level.
    bool everyStemSilent = true;
    for (std::uint32_t source = 0; source < sources; ++source) {
        double peak = 0.0;
        double energy = 0.0;
        bool finite = true;
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            const auto plane =
                (static_cast<std::size_t>(source) * kChannels + channel) *
                htfx::GpuWorkerClient::kMaxFrames;
            for (std::uint32_t sample = 0; sample < frames; ++sample) {
                const float value = output[plane + sample];
                if (!std::isfinite(value)) {
                    finite = false;
                    break;
                }
                peak = std::max(peak, std::abs(static_cast<double>(value)));
                energy += static_cast<double>(value) * value;
            }
            if (!finite) {
                break;
            }
        }
        if (!finite) {
            std::cerr << "FAIL: stem " << source << " contains NaN or infinity\n";
            client.stop();
            return 1;
        }
        const double rms = std::sqrt(
            energy / (static_cast<double>(frames) * kChannels));
        std::cout << "stem " << source << ": peak " << peak << ", rms " << rms << "\n";
        if (peak > 1e-6) {
            everyStemSilent = false;
        }
    }
    if (everyStemSilent) {
        std::cerr << "FAIL: every stem is silent, so nothing was separated\n";
        client.stop();
        return 1;
    }

    client.stop();
    std::cout << "PASS\n";
    return 0;
}
