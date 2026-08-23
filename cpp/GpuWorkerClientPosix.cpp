#include "GpuWorkerClient.h"

#include "ipc_protocol.h"

#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

extern char** environ;

namespace htfx {
namespace {

using ipc::Command;
using ipc::SharedHeader;
using ipc::State;

[[noreturn]] void throwErrno(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

void memoryBarrier() noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

std::string readHeaderError(const SharedHeader& header) {
    return std::string(header.last_error, strnlen(header.last_error, sizeof(header.last_error)));
}

class UniqueFd final {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }
    int release() noexcept { return std::exchange(fd_, -1); }
    void reset(int replacement = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = replacement;
    }

private:
    int fd_ = -1;
};

class UniqueMapping final {
public:
    ~UniqueMapping() { reset(); }
    UniqueMapping() = default;
    UniqueMapping(const UniqueMapping&) = delete;
    UniqueMapping& operator=(const UniqueMapping&) = delete;
    [[nodiscard]] void* get() const noexcept { return address_; }
    void reset(void* replacement = nullptr) noexcept {
        if (address_ != nullptr) {
            ::munmap(address_, ipc::kTotalBytes);
        }
        address_ = replacement;
    }

private:
    void* address_ = nullptr;
};

class NamedSemaphore final {
public:
    ~NamedSemaphore() { reset(); }
    NamedSemaphore() = default;
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    void create(std::string name) {
        reset();
        name_ = std::move(name);
        value_ = ::sem_open(name_.c_str(), O_CREAT | O_EXCL, 0600, 0);
        if (value_ == SEM_FAILED) {
            value_ = nullptr;
            throwErrno("sem_open");
        }
    }

    void post() {
        if (value_ == nullptr || ::sem_post(value_) != 0) {
            throwErrno("sem_post");
        }
    }

    void postNoThrow() noexcept {
        if (value_ != nullptr) {
            ::sem_post(value_);
        }
    }

    [[nodiscard]] sem_t* get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

    void reset() noexcept {
        if (value_ != nullptr) {
            ::sem_close(value_);
            value_ = nullptr;
        }
        if (!name_.empty()) {
            ::sem_unlink(name_.c_str());
            name_.clear();
        }
    }

private:
    sem_t* value_ = nullptr;
    std::string name_;
};

enum class WaitResult { signalled, timeout, processExited, failed };

std::string deviceArgument(const GpuWorkerConfig& config) {
    switch (config.backend) {
        case WorkerBackend::cuda:
            return "cuda:" + std::to_string(config.gpuIndex);
        case WorkerBackend::cpu: return "cpu";
        case WorkerBackend::mps: return "mps";
        case WorkerBackend::autoSelect: return "auto";
    }
    return "auto";
}

}  // namespace

class GpuWorkerClient::Impl final {
public:
    friend class GpuWorkerClient;

    ~Impl() { stop(); }

    bool start(const GpuWorkerConfig& requested, std::uint64_t initialEpoch) {
        stop();
        error_.clear();
        config_ = requested;
        const bool selfContainedWorker = !config_.workerExecutable.empty();
        const auto programPath = selfContainedWorker ? config_.workerExecutable
                                                     : config_.pythonExecutable;
        if (!std::filesystem::is_regular_file(programPath)) {
            throw std::runtime_error(
                std::string(selfContainedWorker ? "Self-contained Demucs worker not found: "
                                                : "Python executable not found: ") +
                programPath.string());
        }
        if (::access(programPath.c_str(), X_OK) != 0) {
            throw std::runtime_error("Demucs worker is not executable: " + programPath.string());
        }
        if (!selfContainedWorker && !std::filesystem::is_regular_file(config_.workerScript)) {
            throw std::runtime_error("GPU worker script not found: " + config_.workerScript.string());
        }

        const bool registryMode = !config_.modelsDirectory.empty();
        if (registryMode) {
            if (!std::filesystem::is_directory(config_.modelsDirectory)) {
                throw std::runtime_error(
                    "Demucs models directory not found: " + config_.modelsDirectory.string());
            }
            if (config_.modelName.empty()) {
                throw std::runtime_error("Demucs model name is empty");
            }
        } else if (!std::filesystem::is_regular_file(config_.checkpoint)) {
            throw std::runtime_error("HTDemucs checkpoint not found: " + config_.checkpoint.string());
        }
        if (config_.sourceCount == 0 || config_.sourceCount > ipc::kMaxSources) {
            throw std::runtime_error("invalid Demucs source count");
        }
        if (config_.hopFrames == 0 || config_.hopFrames > ipc::kMaxFrames ||
            config_.segmentFrames <= config_.hopFrames) {
            throw std::runtime_error("invalid Demucs segment/hop geometry");
        }

        static std::atomic<std::uint32_t> counter{0};
        std::ostringstream token;
        const auto clockToken = static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        token << 'h' << std::hex << static_cast<unsigned>(::getpid()) << clockToken <<
            counter.fetch_add(1, std::memory_order_relaxed);
        session_ = token.str();
        shmName_ = "/" + session_ + "_m";

        const int fd = ::shm_open(shmName_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) {
            throwErrno("shm_open");
        }
        shm_.reset(fd);
        if (::ftruncate(shm_.get(), ipc::kTotalBytes) != 0) {
            throwErrno("ftruncate shared memory");
        }
        auto* address = ::mmap(
            nullptr,
            ipc::kTotalBytes,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            shm_.get(),
            0);
        if (address == MAP_FAILED) {
            throwErrno("mmap shared memory");
        }
        view_.reset(address);
        bytes_ = static_cast<std::byte*>(address);
        header_ = static_cast<SharedHeader*>(address);
        std::memset(address, 0, ipc::kTotalBytes);

        request_.create("/" + session_ + "_q");
        response_.create("/" + session_ + "_p");
        ready_.create("/" + session_ + "_y");
        shutdown_.create("/" + session_ + "_s");

        header_->magic = ipc::kMagic;
        header_->abi_version = ipc::kAbiVersion;
        header_->header_bytes = ipc::kHeaderBytes;
        header_->total_bytes = ipc::kTotalBytes;
        header_->sample_rate = ipc::kSampleRate;
        header_->channels = ipc::kChannels;
        header_->sources = config_.sourceCount;
        header_->max_frames = ipc::kMaxFrames;
        header_->epoch = initialEpoch;
        header_->state = static_cast<std::uint32_t>(State::kLaunching);
        header_->command = static_cast<std::uint32_t>(Command::kNone);
        header_->input_slot_stride = ipc::kInputSlotStride;
        header_->output_slot_stride = ipc::kOutputSlotStride;
        header_->input_base_offset = ipc::kInputBaseOffset;
        header_->output_base_offset = ipc::kOutputBaseOffset;
        header_->client_pid = static_cast<std::uint32_t>(::getpid());
        header_->segment_frames = config_.segmentFrames;
        header_->hop_frames = config_.hopFrames;
        header_->requested_backend = static_cast<std::uint32_t>(config_.backend);
        header_->compute_backend = static_cast<std::uint32_t>(WorkerBackend::autoSelect);
        std::snprintf(header_->model_name, sizeof(header_->model_name), "%s", config_.modelName.c_str());
        memoryBarrier();

        std::vector<std::string> arguments;
        arguments.push_back(std::filesystem::absolute(programPath).string());
        if (!selfContainedWorker) {
            arguments.push_back(std::filesystem::absolute(config_.workerScript).string());
        }
        arguments.insert(arguments.end(), {"--session", session_});
        if (registryMode) {
            arguments.insert(
                arguments.end(),
                {"--models-dir", std::filesystem::absolute(config_.modelsDirectory).string(),
                 "--model", config_.modelName});
        } else {
            arguments.insert(
                arguments.end(),
                {"--checkpoint", std::filesystem::absolute(config_.checkpoint).string()});
        }
        arguments.insert(arguments.end(), {"--device", deviceArgument(config_)});
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (auto& argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

        std::vector<std::string> environment;
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            if (std::strncmp(*entry, "PYTORCH_ENABLE_MPS_FALLBACK=", 28) != 0) {
                environment.emplace_back(*entry);
            }
        }
        environment.emplace_back("PYTORCH_ENABLE_MPS_FALLBACK=1");
        std::vector<char*> envp;
        envp.reserve(environment.size() + 1);
        for (auto& entry : environment) {
            envp.push_back(entry.data());
        }
        envp.push_back(nullptr);

        const int spawnResult = ::posix_spawn(
            &processPid_, arguments.front().c_str(), nullptr, nullptr, argv.data(), envp.data());
        if (spawnResult != 0) {
            processPid_ = -1;
            throw std::system_error(spawnResult, std::generic_category(), "posix_spawn worker");
        }
        processExitStatus_.reset();

        const auto result = waitOn(ready_, config_.readyTimeout);
        memoryBarrier();
        if (result == WaitResult::processExited) {
            throw std::runtime_error(processExitText() + readyContext());
        }
        if (result == WaitResult::timeout) {
            throw std::runtime_error("Demucs worker READY timeout" + readyContext());
        }
        if (result != WaitResult::signalled) {
            throw std::runtime_error("Demucs worker READY wait failed" + readyContext());
        }
        if (header_->status_code != 0 ||
            header_->state != static_cast<std::uint32_t>(State::kReady)) {
            throw std::runtime_error("Demucs worker failed during startup" + readyContext());
        }
        sequence_ = 0;
        nextSlot_ = 0;
        return true;
    }

    void stop() noexcept {
        if (processPid_ > 0 && !pollProcessExited()) {
            if (header_ != nullptr && request_) {
                header_->command = static_cast<std::uint32_t>(Command::kShutdown);
                header_->active_slot = 0;
                header_->valid_frames = 0;
                header_->request_sequence = ++sequence_;
                memoryBarrier();
                request_.postNoThrow();
                waitForExit(std::chrono::seconds(2));
            }
            if (processPid_ > 0 && !pollProcessExited()) {
                shutdown_.postNoThrow();
                waitForExit(std::chrono::seconds(2));
            }
            if (processPid_ > 0 && !pollProcessExited()) {
                ::kill(processPid_, SIGTERM);
                waitForExit(std::chrono::seconds(2));
            }
            if (processPid_ > 0 && !pollProcessExited()) {
                ::kill(processPid_, SIGKILL);
                waitForExit(std::chrono::seconds(2));
            }
        }
        processPid_ = -1;
        shutdown_.reset();
        ready_.reset();
        response_.reset();
        request_.reset();
        header_ = nullptr;
        bytes_ = nullptr;
        view_.reset();
        shm_.reset();
        if (!shmName_.empty()) {
            ::shm_unlink(shmName_.c_str());
            shmName_.clear();
        }
        session_.clear();
        sequence_ = 0;
        nextSlot_ = 0;
    }

    bool reset(std::uint64_t newEpoch) {
        if (!isRunning()) {
            error_ = "GPU worker is not running";
            return false;
        }
        if (newEpoch <= header_->epoch) {
            error_ = "RESET epoch must increase";
            return false;
        }
        header_->epoch = newEpoch;
        header_->active_slot = 0;
        header_->valid_frames = 0;
        if (!sendAndWait(Command::kReset, config_.processTimeout)) {
            return false;
        }
        nextSlot_ = 0;
        return true;
    }

    bool process(
        std::uint64_t epoch,
        const float* planarInput,
        std::uint32_t validFrames,
        const float*& planarOutput,
        double* elapsedMilliseconds) {
        using clock = std::chrono::steady_clock;
        planarOutput = nullptr;
        if (elapsedMilliseconds != nullptr) {
            *elapsedMilliseconds = 0.0;
        }
        if (!isRunning()) {
            error_ = "GPU worker is not running";
            return false;
        }
        if (planarInput == nullptr) {
            error_ = "planarInput is null";
            return false;
        }
        if (epoch != header_->epoch) {
            error_ = "PROCESS epoch does not match worker epoch";
            return false;
        }
        if (validFrames == 0 || validFrames > ipc::kMaxFrames ||
            validFrames != header_->hop_frames) {
            error_ = "PROCESS validFrames does not match configured hop";
            return false;
        }

        const std::uint32_t slot = nextSlot_++ % ipc::kSlotCount;
        auto* destination = reinterpret_cast<float*>(bytes_ + ipc::InputSlotOffset(slot));
        for (std::uint32_t channel = 0; channel < ipc::kChannels; ++channel) {
            std::memcpy(
                destination + static_cast<std::size_t>(channel) * ipc::kMaxFrames,
                planarInput + static_cast<std::size_t>(channel) * validFrames,
                static_cast<std::size_t>(validFrames) * sizeof(float));
        }
        header_->active_slot = slot;
        header_->valid_frames = validFrames;
        header_->input_checksum = 0;
        const auto started = clock::now();
        const bool okay = sendAndWait(Command::kProcess, config_.processTimeout);
        const auto elapsed = std::chrono::duration<double, std::milli>(clock::now() - started).count();
        if (elapsedMilliseconds != nullptr) {
            *elapsedMilliseconds = elapsed;
        }
        if (!okay) {
            return false;
        }
        planarOutput = reinterpret_cast<const float*>(bytes_ + ipc::OutputSlotOffset(slot));
        return true;
    }

    [[nodiscard]] bool isRunning() const noexcept {
        return processPid_ > 0 && !pollProcessExited();
    }
    [[nodiscard]] std::uint32_t state() const noexcept {
        return header_ == nullptr ? static_cast<std::uint32_t>(State::kStopped) : header_->state;
    }
    [[nodiscard]] std::uint32_t workerPid() const noexcept {
        return header_ == nullptr ? 0u : header_->worker_pid;
    }
    [[nodiscard]] std::uint64_t heartbeatCounter() const noexcept {
        return header_ == nullptr ? 0u : header_->heartbeat_counter;
    }
    [[nodiscard]] std::uint64_t lastInferenceMicroseconds() const noexcept {
        return header_ == nullptr ? 0u : header_->last_inference_us;
    }
    [[nodiscard]] std::uint64_t cudaAllocatedBytes() const noexcept {
        return header_ == nullptr ? 0u : header_->cuda_allocated_bytes;
    }
    [[nodiscard]] std::uint64_t cudaReservedBytes() const noexcept {
        return header_ == nullptr ? 0u : header_->cuda_reserved_bytes;
    }
    [[nodiscard]] std::uint64_t cudaMaxAllocatedBytes() const noexcept {
        return header_ == nullptr ? 0u : header_->cuda_max_allocated_bytes;
    }
    [[nodiscard]] std::uint64_t cudaMaxReservedBytes() const noexcept {
        return header_ == nullptr ? 0u : header_->cuda_max_reserved_bytes;
    }
    [[nodiscard]] std::uint32_t activeSourceCount() const noexcept {
        return header_ == nullptr ? 0u : header_->sources;
    }
    [[nodiscard]] std::uint32_t activeSegmentFrames() const noexcept {
        return header_ == nullptr ? 0u : header_->segment_frames;
    }
    [[nodiscard]] std::uint32_t activeHopFrames() const noexcept {
        return header_ == nullptr ? 0u : header_->hop_frames;
    }
    [[nodiscard]] WorkerBackend resolvedBackend() const noexcept {
        if (header_ == nullptr) {
            return WorkerBackend::autoSelect;
        }
        switch (header_->compute_backend) {
            case static_cast<std::uint32_t>(WorkerBackend::cuda): return WorkerBackend::cuda;
            case static_cast<std::uint32_t>(WorkerBackend::cpu): return WorkerBackend::cpu;
            case static_cast<std::uint32_t>(WorkerBackend::mps): return WorkerBackend::mps;
            default: return WorkerBackend::autoSelect;
        }
    }
    [[nodiscard]] std::string gpuName() const {
        if (header_ == nullptr) {
            return {};
        }
        return std::string(header_->gpu_name, strnlen(header_->gpu_name, sizeof(header_->gpu_name)));
    }
    [[nodiscard]] std::string lastError() const { return error_; }

private:
    [[nodiscard]] bool pollProcessExited() const noexcept {
        if (processPid_ <= 0) {
            return true;
        }
        int status = 0;
        const pid_t result = ::waitpid(processPid_, &status, WNOHANG);
        if (result == processPid_) {
            processExitStatus_ = status;
            processPid_ = -1;
            return true;
        }
        if (result < 0 && errno == ECHILD) {
            processPid_ = -1;
            return true;
        }
        return false;
    }

    void waitForExit(std::chrono::milliseconds timeout) const noexcept {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!pollProcessExited() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    WaitResult waitOn(NamedSemaphore& semaphore, std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            if (::sem_trywait(semaphore.get()) == 0) {
                return WaitResult::signalled;
            }
            if (errno != EAGAIN && errno != EINTR) {
                return WaitResult::failed;
            }
            if (pollProcessExited()) {
                return WaitResult::processExited;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return WaitResult::timeout;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    [[nodiscard]] std::string processExitText() const {
        if (!processExitStatus_.has_value()) {
            return "worker exited (status unavailable)";
        }
        const int status = *processExitStatus_;
        if (WIFEXITED(status)) {
            return "worker exited with code " + std::to_string(WEXITSTATUS(status));
        }
        if (WIFSIGNALED(status)) {
            return "worker terminated by signal " + std::to_string(WTERMSIG(status));
        }
        return "worker exited with status " + std::to_string(status);
    }

    bool sendAndWait(Command command, std::chrono::milliseconds timeout) {
        header_->command = static_cast<std::uint32_t>(command);
        const auto requested = ++sequence_;
        header_->request_sequence = requested;
        memoryBarrier();
        try {
            request_.post();
        } catch (const std::exception& exception) {
            error_ = exception.what();
            return false;
        }
        const auto result = waitOn(response_, timeout);
        if (result == WaitResult::timeout) {
            error_ = "GPU worker response timeout; state=" + std::to_string(header_->state) +
                     "; heartbeat=" + std::to_string(header_->heartbeat_counter);
            return false;
        }
        if (result == WaitResult::processExited) {
            error_ = processExitText() + responseContext();
            return false;
        }
        if (result != WaitResult::signalled) {
            error_ = "GPU worker response wait failed" + responseContext();
            return false;
        }
        memoryBarrier();
        if (header_->response_sequence != requested) {
            error_ = "GPU worker response sequence mismatch";
            return false;
        }
        if (header_->status_code != 0) {
            error_ = "GPU worker error: " + readHeaderError(*header_);
            return false;
        }
        if (header_->state != static_cast<std::uint32_t>(State::kReady)) {
            error_ = "GPU worker response has non-ready state " + std::to_string(header_->state);
            return false;
        }
        error_.clear();
        return true;
    }

    [[nodiscard]] std::string readyContext() const {
        if (header_ == nullptr) {
            return {};
        }
        std::string result = "; state=" + std::to_string(header_->state) +
                             "; heartbeat=" + std::to_string(header_->heartbeat_counter);
        const auto workerError = readHeaderError(*header_);
        if (!workerError.empty()) {
            result += "; error=" + workerError;
        }
        return result;
    }
    [[nodiscard]] std::string responseContext() const { return readyContext(); }

    GpuWorkerConfig config_;
    std::string session_;
    std::string shmName_;
    UniqueFd shm_;
    UniqueMapping view_;
    NamedSemaphore request_;
    NamedSemaphore response_;
    NamedSemaphore ready_;
    NamedSemaphore shutdown_;
    mutable pid_t processPid_ = -1;
    mutable std::optional<int> processExitStatus_;
    std::byte* bytes_ = nullptr;
    SharedHeader* header_ = nullptr;
    std::uint64_t sequence_ = 0;
    std::uint32_t nextSlot_ = 0;
    std::string error_;
};

GpuWorkerClient::GpuWorkerClient() : impl_(std::make_unique<Impl>()) {}
GpuWorkerClient::~GpuWorkerClient() = default;

bool GpuWorkerClient::start(const GpuWorkerConfig& config, std::uint64_t initialEpoch) noexcept {
    try {
        return impl_->start(config, initialEpoch);
    } catch (const std::exception& error) {
        impl_->error_ = error.what();
        impl_->stop();
        return false;
    } catch (...) {
        impl_->error_ = "unknown GPU worker startup failure";
        impl_->stop();
        return false;
    }
}
void GpuWorkerClient::stop() noexcept { impl_->stop(); }
bool GpuWorkerClient::reset(std::uint64_t newEpoch) noexcept {
    try {
        return impl_->reset(newEpoch);
    } catch (const std::exception& error) {
        impl_->error_ = error.what();
        return false;
    } catch (...) {
        impl_->error_ = "unknown GPU worker RESET failure";
        return false;
    }
}
bool GpuWorkerClient::process(
    std::uint64_t epoch,
    const float* planarInput,
    const float*& planarOutput,
    double* elapsedMilliseconds) noexcept {
    return process(epoch, planarInput, impl_->config_.hopFrames, planarOutput, elapsedMilliseconds);
}
bool GpuWorkerClient::process(
    std::uint64_t epoch,
    const float* planarInput,
    std::uint32_t validFrames,
    const float*& planarOutput,
    double* elapsedMilliseconds) noexcept {
    try {
        return impl_->process(epoch, planarInput, validFrames, planarOutput, elapsedMilliseconds);
    } catch (const std::exception& error) {
        impl_->error_ = error.what();
        planarOutput = nullptr;
        return false;
    } catch (...) {
        impl_->error_ = "unknown GPU worker PROCESS failure";
        planarOutput = nullptr;
        return false;
    }
}
bool GpuWorkerClient::isRunning() const noexcept { return impl_->isRunning(); }
std::uint32_t GpuWorkerClient::state() const noexcept { return impl_->state(); }
std::uint32_t GpuWorkerClient::workerPid() const noexcept { return impl_->workerPid(); }
std::uint64_t GpuWorkerClient::heartbeatCounter() const noexcept { return impl_->heartbeatCounter(); }
std::uint64_t GpuWorkerClient::lastInferenceMicroseconds() const noexcept { return impl_->lastInferenceMicroseconds(); }
std::uint64_t GpuWorkerClient::cudaAllocatedBytes() const noexcept { return impl_->cudaAllocatedBytes(); }
std::uint64_t GpuWorkerClient::cudaReservedBytes() const noexcept { return impl_->cudaReservedBytes(); }
std::uint64_t GpuWorkerClient::cudaMaxAllocatedBytes() const noexcept { return impl_->cudaMaxAllocatedBytes(); }
std::uint64_t GpuWorkerClient::cudaMaxReservedBytes() const noexcept { return impl_->cudaMaxReservedBytes(); }
std::uint32_t GpuWorkerClient::activeSourceCount() const noexcept { return impl_->activeSourceCount(); }
std::uint32_t GpuWorkerClient::activeSegmentFrames() const noexcept { return impl_->activeSegmentFrames(); }
std::uint32_t GpuWorkerClient::activeHopFrames() const noexcept { return impl_->activeHopFrames(); }
WorkerBackend GpuWorkerClient::resolvedBackend() const noexcept { return impl_->resolvedBackend(); }
std::string GpuWorkerClient::gpuName() const { return impl_->gpuName(); }
std::string GpuWorkerClient::lastError() const { return impl_->lastError(); }

}  // namespace htfx
