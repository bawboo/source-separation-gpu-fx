#include "GpuWorkerClient.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path python;
    std::filesystem::path worker;
    std::filesystem::path checkpoint;
    std::filesystem::path inputRaw = L"results/m4/worker_input.f32";
    std::filesystem::path outputRaw = L"results/m4/worker_output.f32";
    std::filesystem::path report = L"results/m4/worker_client_report.json";
    std::uint32_t gpuIndex = 0;
    std::uint32_t hops = 3;
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
        } else if (argument == L"--checkpoint") {
            options.checkpoint = take();
        } else if (argument == L"--input") {
            options.inputRaw = take();
        } else if (argument == L"--output") {
            options.outputRaw = take();
        } else if (argument == L"--report") {
            options.report = take();
        } else if (argument == L"--gpu") {
            options.gpuIndex = static_cast<std::uint32_t>(std::stoul(take()));
        } else if (argument == L"--hops") {
            options.hops = static_cast<std::uint32_t>(std::stoul(take()));
        } else {
            throw std::runtime_error("unknown argument " + narrow(argument));
        }
    }
    if (options.python.empty() || options.worker.empty() || options.checkpoint.empty()) {
        throw std::runtime_error("--python, --worker and --checkpoint are required");
    }
    if (options.hops == 0) {
        throw std::runtime_error("--hops must be positive");
    }
    return options;
}

void ensureParent(const std::filesystem::path& path) {
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void fillHop(std::vector<float>& input, std::uint64_t hopIndex) {
    constexpr double sampleRate = 44'100.0;
    constexpr double pi = 3.1415926535897932384626433832795;
    for (std::uint32_t sample = 0; sample < htfx::GpuWorkerClient::kHopSamples; ++sample) {
        const auto absoluteSample =
            hopIndex * htfx::GpuWorkerClient::kHopSamples + sample;
        const double time = static_cast<double>(absoluteSample) / sampleRate;
        const double envelope = 0.65 + 0.35 * std::sin(2.0 * pi * 0.37 * time);
        input[sample] = static_cast<float>(
            envelope * (0.08 * std::sin(2.0 * pi * 110.0 * time) +
                        0.025 * std::sin(2.0 * pi * 997.0 * time)));
        input[htfx::GpuWorkerClient::kHopSamples + sample] = static_cast<float>(
            envelope * (0.07 * std::sin(2.0 * pi * 146.83 * time + 0.31) +
                        0.02 * std::sin(2.0 * pi * 733.0 * time + 0.17)));
    }
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

void writeReport(
    const Options& options,
    bool passed,
    std::uint32_t workerPid,
    const std::vector<double>& timings,
    const std::string& error) {
    ensureParent(options.report);
    std::ofstream report(options.report, std::ios::binary | std::ios::trunc);
    if (!report) {
        throw std::runtime_error("cannot open report file");
    }
    const double mean = timings.empty()
                            ? 0.0
                            : std::accumulate(timings.begin(), timings.end(), 0.0) /
                                  static_cast<double>(timings.size());
    report << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"passed\": " << (passed ? "true" : "false") << ",\n"
           << "  \"worker_pid\": " << workerPid << ",\n"
           << "  \"gpu_index\": " << options.gpuIndex << ",\n"
           << "  \"hops_requested\": " << options.hops << ",\n"
           << "  \"hops_completed\": " << timings.size() << ",\n"
           << "  \"hop_samples\": " << htfx::GpuWorkerClient::kHopSamples << ",\n"
           << "  \"mean_ms\": " << mean << ",\n"
           << "  \"p50_ms\": " << percentile(timings, 0.50) << ",\n"
           << "  \"p95_ms\": " << percentile(timings, 0.95) << ",\n"
           << "  \"max_ms\": " << percentile(timings, 1.0) << ",\n"
           << "  \"process_timeout_ms\": 500,\n"
           << "  \"error\": \"";
    for (const char ch : error) {
        if (ch == '\\' || ch == '"') {
            report << '\\';
        }
        if (ch == '\n') {
            report << "\\n";
        } else if (ch != '\r') {
            report << ch;
        }
    }
    report << "\"\n}\n";
}

int run(const Options& options) {
    ensureParent(options.inputRaw);
    ensureParent(options.outputRaw);
    std::ofstream inputFile(options.inputRaw, std::ios::binary | std::ios::trunc);
    std::ofstream outputFile(options.outputRaw, std::ios::binary | std::ios::trunc);
    if (!inputFile || !outputFile) {
        throw std::runtime_error("cannot open raw input/output files");
    }

    htfx::GpuWorkerConfig config;
    config.pythonExecutable = options.python;
    config.workerScript = options.worker;
    config.checkpoint = options.checkpoint;
    config.gpuIndex = options.gpuIndex;
    config.readyTimeout = std::chrono::seconds(60);
    config.processTimeout = std::chrono::milliseconds(500);

    htfx::GpuWorkerClient client;
    std::vector<double> timings;
    std::vector<float> input(htfx::GpuWorkerClient::kInputFloatCount);
    std::string error;
    bool passed = client.start(config, 1);
    const std::uint32_t workerPid = client.workerPid();
    if (!passed) {
        error = client.lastError();
    }
    for (std::uint32_t hop = 0; passed && hop < options.hops; ++hop) {
        fillHop(input, hop);
        inputFile.write(
            reinterpret_cast<const char*>(input.data()),
            static_cast<std::streamsize>(input.size() * sizeof(float)));
        const float* output = nullptr;
        double elapsed = 0.0;
        if (!client.process(1, input.data(), output, &elapsed)) {
            passed = false;
            error = client.lastError();
            break;
        }
        const bool finite = std::all_of(
            output,
            output + htfx::GpuWorkerClient::kOutputFloatCount,
            [](float value) { return std::isfinite(value); });
        if (!finite) {
            passed = false;
            error = "worker output contains NaN or Inf";
            break;
        }
        outputFile.write(
            reinterpret_cast<const char*>(output),
            static_cast<std::streamsize>(
                htfx::GpuWorkerClient::kOutputFloatCount * sizeof(float)));
        if (!inputFile || !outputFile) {
            passed = false;
            error = "failed while writing raw input/output";
            break;
        }
        timings.push_back(elapsed);
        std::cout << "hop=" << hop << " process_ms=" << std::fixed
                  << std::setprecision(3) << elapsed << '\n' << std::flush;
    }
    client.stop();
    inputFile.close();
    outputFile.close();
    writeReport(options, passed, workerPid, timings, error);
    std::cout << "worker_pid=" << workerPid << " completed=" << timings.size()
              << "/" << options.hops << " PASS=" << std::boolalpha << passed;
    if (!error.empty()) {
        std::cout << " error=" << error;
    }
    std::cout << '\n';
    return passed ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "gpu_worker_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
