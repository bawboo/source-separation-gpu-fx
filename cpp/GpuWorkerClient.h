#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace htfx {

enum class WorkerBackend : std::uint32_t {
    autoSelect = 0,
    cuda = 1,
    cpu = 2,
    mps = 3,
};

struct GpuWorkerConfig {
    // When present, launch this self-contained worker directly and ignore
    // pythonExecutable/workerScript. This is used by the portable Standalone.
    std::filesystem::path workerExecutable;
    std::filesystem::path pythonExecutable;
    std::filesystem::path workerScript;
    // checkpoint is the legacy single-model entrypoint. New callers should
    // provide modelsDirectory + modelName so bags and six-source models work.
    std::filesystem::path checkpoint;
    std::filesystem::path modelsDirectory;
    std::string modelName{"htdemucs"};
    WorkerBackend backend = WorkerBackend::autoSelect;
    std::uint32_t gpuIndex = 0;
    std::uint32_t sourceCount = 4;
    std::uint32_t segmentFrames = 343'980;
    std::uint32_t hopFrames = 257'985;
    std::chrono::milliseconds readyTimeout{60'000};
    std::chrono::milliseconds processTimeout{500};
};

class GpuWorkerClient final {
public:
    static constexpr std::uint32_t kChannels = 2;
    static constexpr std::uint32_t kSources = 4;
    static constexpr std::uint32_t kMaxSources = 6;
    static constexpr std::uint32_t kHopSamples = 257'985;
    static constexpr std::uint32_t kMaxFrames = 257'985;
    static constexpr std::size_t kInputFloatCount =
        static_cast<std::size_t>(kChannels) * kHopSamples;
    static constexpr std::size_t kOutputFloatCount =
        static_cast<std::size_t>(kSources) * kChannels * kHopSamples;

    GpuWorkerClient();
    ~GpuWorkerClient();

    GpuWorkerClient(const GpuWorkerClient&) = delete;
    GpuWorkerClient& operator=(const GpuWorkerClient&) = delete;

    bool start(const GpuWorkerConfig& config, std::uint64_t initialEpoch = 1) noexcept;
    void stop() noexcept;

    bool reset(std::uint64_t newEpoch) noexcept;

    // planarInput is [channel][sample]. planarOutput remains valid until the
    // next process()/stop() call and is laid out [source][channel][sample].
    bool process(
        std::uint64_t epoch,
        const float* planarInput,
        const float*& planarOutput,
        double* elapsedMilliseconds = nullptr) noexcept;

    // Dynamic ABI v2 entrypoint. The returned buffer uses kMaxFrames as the
    // stride between source/channel planes, regardless of validFrames.
    bool process(
        std::uint64_t epoch,
        const float* planarInput,
        std::uint32_t validFrames,
        const float*& planarOutput,
        double* elapsedMilliseconds = nullptr) noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] std::uint32_t state() const noexcept;
    [[nodiscard]] std::uint32_t workerPid() const noexcept;
    [[nodiscard]] std::uint64_t heartbeatCounter() const noexcept;
    [[nodiscard]] std::uint64_t lastInferenceMicroseconds() const noexcept;
    [[nodiscard]] std::uint64_t cudaAllocatedBytes() const noexcept;
    [[nodiscard]] std::uint64_t cudaReservedBytes() const noexcept;
    [[nodiscard]] std::uint64_t cudaMaxAllocatedBytes() const noexcept;
    [[nodiscard]] std::uint64_t cudaMaxReservedBytes() const noexcept;
    [[nodiscard]] std::uint32_t activeSourceCount() const noexcept;
    [[nodiscard]] std::uint32_t activeSegmentFrames() const noexcept;
    [[nodiscard]] std::uint32_t activeHopFrames() const noexcept;
    [[nodiscard]] WorkerBackend resolvedBackend() const noexcept;
    [[nodiscard]] std::string gpuName() const;
    [[nodiscard]] std::string lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace htfx
