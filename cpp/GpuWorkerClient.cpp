#include "GpuWorkerClient.h"

#include "ipc_protocol.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace htfx {
namespace {

using ipc::Command;
using ipc::SharedHeader;
using ipc::State;

class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept {
        const auto result = handle_;
        handle_ = nullptr;
        return result;
    }
    void reset(HANDLE replacement = nullptr) noexcept {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

class UniqueView final {
public:
    UniqueView() = default;
    explicit UniqueView(void* view) noexcept : view_(view) {}
    ~UniqueView() { reset(); }
    UniqueView(const UniqueView&) = delete;
    UniqueView& operator=(const UniqueView&) = delete;
    [[nodiscard]] void* get() const noexcept { return view_; }
    void reset(void* replacement = nullptr) noexcept {
        if (view_ != nullptr) {
            UnmapViewOfFile(view_);
        }
        view_ = replacement;
    }

private:
    void* view_ = nullptr;
};

[[noreturn]] void throwLastError(const char* operation) {
    throw std::system_error(
        static_cast<int>(GetLastError()), std::system_category(), operation);
}

std::wstring quoteArgument(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) {
        return value;
    }
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(ch);
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::string readHeaderError(const SharedHeader& header) {
    const auto length = strnlen_s(header.last_error, sizeof(header.last_error));
    return std::string(header.last_error, length);
}

std::string workerExitText(HANDLE process) {
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process, &exitCode)) {
        return "worker exited (exit code unavailable)";
    }
    return "worker exited with code " + std::to_string(exitCode);
}

UniqueHandle createEventChecked(BOOL manualReset, const std::wstring& name) {
    UniqueHandle event(CreateEventW(nullptr, manualReset, FALSE, name.c_str()));
    if (!event) {
        throwLastError("CreateEventW");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("unexpected event name collision");
    }
    return event;
}

DWORD timeoutValue(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0) {
        return INFINITE;
    }
    return static_cast<DWORD>(
        (std::min)(timeout.count(), static_cast<long long>(MAXDWORD - 1)));
}

}  // namespace

class GpuWorkerClient::Impl final {
public:
    friend class GpuWorkerClient;

    bool start(const GpuWorkerConfig& requested, std::uint64_t initialEpoch) {
        stop();
        error_.clear();
        config_ = requested;
        const bool selfContainedWorker = !config_.workerExecutable.empty();
        if (selfContainedWorker) {
            if (!std::filesystem::is_regular_file(config_.workerExecutable)) {
                throw std::runtime_error(
                    "Self-contained Demucs worker not found: " +
                    config_.workerExecutable.string());
            }
        } else {
            if (!std::filesystem::is_regular_file(config_.pythonExecutable)) {
                throw std::runtime_error(
                    "Python executable not found: " + config_.pythonExecutable.string());
            }
            if (!std::filesystem::is_regular_file(config_.workerScript)) {
                throw std::runtime_error(
                    "GPU worker script not found: " + config_.workerScript.string());
            }
        }
        const bool registryMode = !config_.modelsDirectory.empty();
        if (registryMode) {
            if (!std::filesystem::is_directory(config_.modelsDirectory)) {
                throw std::runtime_error(
                    "Demucs models directory not found: " +
                    config_.modelsDirectory.string());
            }
            if (config_.modelName.empty()) {
                throw std::runtime_error("Demucs model name is empty");
            }
        } else if (!std::filesystem::is_regular_file(config_.checkpoint)) {
            throw std::runtime_error(
                "HTDemucs checkpoint not found: " + config_.checkpoint.string());
        }
        if (config_.sourceCount == 0 ||
            config_.sourceCount > ipc::kMaxSources) {
            throw std::runtime_error("invalid Demucs source count");
        }
        if (config_.hopFrames == 0 || config_.hopFrames > ipc::kMaxFrames ||
            config_.segmentFrames <= config_.hopFrames) {
            throw std::runtime_error("invalid Demucs segment/hop geometry");
        }

        session_ = L"HTFX_GPU_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                   std::to_wstring(GetTickCount64()) + L"_" +
                   std::to_wstring(reinterpret_cast<std::uintptr_t>(this));
        const auto localName = [this](const wchar_t* suffix) {
            return L"Local\\" + session_ + suffix;
        };

        mapping_.reset(CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            ipc::kTotalBytes,
            localName(L"_shm").c_str()));
        if (!mapping_) {
            throwLastError("CreateFileMappingW");
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            throw std::runtime_error("unexpected shared-memory name collision");
        }
        view_.reset(MapViewOfFile(
            mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, ipc::kTotalBytes));
        if (view_.get() == nullptr) {
            throwLastError("MapViewOfFile");
        }
        bytes_ = static_cast<std::byte*>(view_.get());
        header_ = reinterpret_cast<SharedHeader*>(view_.get());
        std::memset(header_, 0, sizeof(*header_));

        request_ = createEventChecked(FALSE, localName(L"_request"));
        response_ = createEventChecked(FALSE, localName(L"_response"));
        ready_ = createEventChecked(TRUE, localName(L"_ready"));
        shutdown_ = createEventChecked(TRUE, localName(L"_shutdown"));

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
        header_->client_pid = GetCurrentProcessId();
        header_->segment_frames = config_.segmentFrames;
        header_->hop_frames = config_.hopFrames;
        header_->requested_backend = static_cast<std::uint32_t>(config_.backend);
        header_->compute_backend = static_cast<std::uint32_t>(WorkerBackend::autoSelect);
        strncpy_s(
            header_->model_name,
            sizeof(header_->model_name),
            config_.modelName.c_str(),
            _TRUNCATE);
        MemoryBarrier();

        const auto program = std::filesystem::absolute(
            selfContainedWorker ? config_.workerExecutable : config_.pythonExecutable).wstring();
        const auto worker = selfContainedWorker
                                ? std::wstring{}
                                : std::filesystem::absolute(config_.workerScript).wstring();
        const auto checkpoint = config_.checkpoint.empty()
                                    ? std::wstring{}
                                    : std::filesystem::absolute(config_.checkpoint).wstring();
        const auto modelsDirectory = config_.modelsDirectory.empty()
                                         ? std::wstring{}
                                         : std::filesystem::absolute(
                                               config_.modelsDirectory).wstring();
        const auto modelName = std::wstring(
            config_.modelName.begin(), config_.modelName.end());
        const auto device = [&]() -> std::wstring {
            switch (config_.backend) {
                case WorkerBackend::cuda:
                    return L"cuda:" + std::to_wstring(config_.gpuIndex);
                case WorkerBackend::cpu:
                    return L"cpu";
                case WorkerBackend::mps:
                    return L"mps";
                case WorkerBackend::autoSelect:
                    return L"auto";
            }
            return L"auto";
        }();
        std::wstring commandLine = quoteArgument(program);
        if (!selfContainedWorker) {
            commandLine += L" " + quoteArgument(worker);
        }
        commandLine += L" --session " + quoteArgument(session_);
        if (registryMode) {
            commandLine += L" --models-dir " + quoteArgument(modelsDirectory) +
                           L" --model " + quoteArgument(modelName);
        } else {
            commandLine += L" --checkpoint " + quoteArgument(checkpoint);
        }
        commandLine += L" --device " + quoteArgument(device);
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(
                program.c_str(),
                mutableCommand.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                &processInfo)) {
            throwLastError("CreateProcessW Demucs worker");
        }
        process_.reset(processInfo.hProcess);
        UniqueHandle processThread(processInfo.hThread);

        std::array<HANDLE, 2> waits{ready_.get(), process_.get()};
        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(waits.size()),
            waits.data(),
            FALSE,
            timeoutValue(config_.readyTimeout));
        MemoryBarrier();
        if (result == WAIT_OBJECT_0 + 1) {
            throw std::runtime_error(workerExitText(process_.get()) + readyContext());
        }
        if (result == WAIT_TIMEOUT) {
            throw std::runtime_error("Demucs worker READY timeout" + readyContext());
        }
        if (result != WAIT_OBJECT_0) {
            throwLastError("WaitForMultipleObjects Demucs worker READY");
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
        if (process_) {
            if (WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT) {
                if (header_ != nullptr && request_) {
                    header_->command = static_cast<std::uint32_t>(Command::kShutdown);
                    header_->active_slot = 0;
                    header_->valid_frames = 0;
                    header_->request_sequence = ++sequence_;
                    MemoryBarrier();
                    SetEvent(request_.get());
                    if (WaitForSingleObject(process_.get(), 2'000) == WAIT_TIMEOUT) {
                        SetEvent(shutdown_.get());
                    }
                } else if (shutdown_) {
                    SetEvent(shutdown_.get());
                }
                if (WaitForSingleObject(process_.get(), 2'000) == WAIT_TIMEOUT) {
                    TerminateProcess(process_.get(), 99);
                    WaitForSingleObject(process_.get(), 2'000);
                }
            }
        }
        process_.reset();
        shutdown_.reset();
        ready_.reset();
        response_.reset();
        request_.reset();
        header_ = nullptr;
        bytes_ = nullptr;
        view_.reset();
        mapping_.reset();
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
        auto* destination = reinterpret_cast<float*>(
            bytes_ + ipc::InputSlotOffset(slot));
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
        const auto elapsed = std::chrono::duration<double, std::milli>(
            clock::now() - started).count();
        if (elapsedMilliseconds != nullptr) {
            *elapsedMilliseconds = elapsed;
        }
        if (!okay) {
            return false;
        }
        planarOutput = reinterpret_cast<const float*>(
            bytes_ + ipc::OutputSlotOffset(slot));
        return true;
    }

    [[nodiscard]] bool isRunning() const noexcept {
        return process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
    }

    [[nodiscard]] std::uint32_t state() const noexcept {
        return header_ == nullptr ? static_cast<std::uint32_t>(State::kStopped)
                                  : header_->state;
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
        const auto raw = header_->compute_backend;
        if (raw == static_cast<std::uint32_t>(WorkerBackend::cuda)) {
            return WorkerBackend::cuda;
        }
        if (raw == static_cast<std::uint32_t>(WorkerBackend::cpu)) {
            return WorkerBackend::cpu;
        }
        if (raw == static_cast<std::uint32_t>(WorkerBackend::mps)) {
            return WorkerBackend::mps;
        }
        return WorkerBackend::autoSelect;
    }

    [[nodiscard]] std::string gpuName() const {
        if (header_ == nullptr) {
            return {};
        }
        const auto length = strnlen_s(header_->gpu_name, sizeof(header_->gpu_name));
        return std::string(header_->gpu_name, length);
    }

    [[nodiscard]] std::string lastError() const { return error_; }

private:
    bool sendAndWait(Command command, std::chrono::milliseconds timeout) {
        header_->command = static_cast<std::uint32_t>(command);
        const auto requested = ++sequence_;
        header_->request_sequence = requested;
        MemoryBarrier();
        if (!SetEvent(request_.get())) {
            error_ = std::system_error(
                static_cast<int>(GetLastError()), std::system_category(), "SetEvent request")
                         .what();
            return false;
        }
        std::array<HANDLE, 2> waits{response_.get(), process_.get()};
        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(waits.size()),
            waits.data(),
            FALSE,
            timeoutValue(timeout));
        if (result == WAIT_TIMEOUT) {
            error_ = "GPU worker response timeout; state=" +
                     std::to_string(header_->state) + "; heartbeat=" +
                     std::to_string(header_->heartbeat_counter);
            return false;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            error_ = workerExitText(process_.get()) + responseContext();
            return false;
        }
        if (result != WAIT_OBJECT_0) {
            error_ = std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "WaitForMultipleObjects response")
                         .what();
            return false;
        }
        MemoryBarrier();
        if (header_->response_sequence != requested) {
            error_ = "GPU worker response sequence mismatch";
            return false;
        }
        if (header_->status_code != 0) {
            error_ = "GPU worker error: " + readHeaderError(*header_);
            return false;
        }
        if (header_->state != static_cast<std::uint32_t>(State::kReady)) {
            error_ = "GPU worker response has non-ready state " +
                     std::to_string(header_->state);
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
                             "; heartbeat=" +
                             std::to_string(header_->heartbeat_counter);
        const auto workerError = readHeaderError(*header_);
        if (!workerError.empty()) {
            result += "; error=" + workerError;
        }
        return result;
    }

    [[nodiscard]] std::string responseContext() const {
        return readyContext();
    }

    GpuWorkerConfig config_;
    std::wstring session_;
    UniqueHandle mapping_;
    UniqueView view_;
    UniqueHandle request_;
    UniqueHandle response_;
    UniqueHandle ready_;
    UniqueHandle shutdown_;
    UniqueHandle process_;
    std::byte* bytes_ = nullptr;
    SharedHeader* header_ = nullptr;
    std::uint64_t sequence_ = 0;
    std::uint32_t nextSlot_ = 0;
    std::string error_;
};

GpuWorkerClient::GpuWorkerClient() : impl_(std::make_unique<Impl>()) {}
GpuWorkerClient::~GpuWorkerClient() = default;

bool GpuWorkerClient::start(
    const GpuWorkerConfig& config, std::uint64_t initialEpoch) noexcept {
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
    return process(
        epoch,
        planarInput,
        impl_->config_.hopFrames,
        planarOutput,
        elapsedMilliseconds);
}

bool GpuWorkerClient::process(
    std::uint64_t epoch,
    const float* planarInput,
    std::uint32_t validFrames,
    const float*& planarOutput,
    double* elapsedMilliseconds) noexcept {
    try {
        return impl_->process(
            epoch, planarInput, validFrames, planarOutput, elapsedMilliseconds);
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
std::uint64_t GpuWorkerClient::heartbeatCounter() const noexcept {
    return impl_->heartbeatCounter();
}
std::uint64_t GpuWorkerClient::lastInferenceMicroseconds() const noexcept {
    return impl_->lastInferenceMicroseconds();
}
std::uint64_t GpuWorkerClient::cudaAllocatedBytes() const noexcept {
    return impl_->cudaAllocatedBytes();
}
std::uint64_t GpuWorkerClient::cudaReservedBytes() const noexcept {
    return impl_->cudaReservedBytes();
}
std::uint64_t GpuWorkerClient::cudaMaxAllocatedBytes() const noexcept {
    return impl_->cudaMaxAllocatedBytes();
}
std::uint64_t GpuWorkerClient::cudaMaxReservedBytes() const noexcept {
    return impl_->cudaMaxReservedBytes();
}
std::uint32_t GpuWorkerClient::activeSourceCount() const noexcept {
    return impl_->activeSourceCount();
}
std::uint32_t GpuWorkerClient::activeSegmentFrames() const noexcept {
    return impl_->activeSegmentFrames();
}
std::uint32_t GpuWorkerClient::activeHopFrames() const noexcept {
    return impl_->activeHopFrames();
}
WorkerBackend GpuWorkerClient::resolvedBackend() const noexcept {
    return impl_->resolvedBackend();
}
std::string GpuWorkerClient::gpuName() const { return impl_->gpuName(); }
std::string GpuWorkerClient::lastError() const { return impl_->lastError(); }

}  // namespace htfx
