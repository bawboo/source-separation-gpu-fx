#include "ipc_protocol.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using htfx::ipc::Command;
using htfx::ipc::SharedHeader;
using htfx::ipc::State;

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
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
    [[nodiscard]] HANDLE get() const { return handle_; }
    [[nodiscard]] explicit operator bool() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() {
        const auto result = handle_;
        handle_ = nullptr;
        return result;
    }
    void reset(HANDLE replacement = nullptr) {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

class UniqueView {
public:
    UniqueView() = default;
    explicit UniqueView(void* view) : view_(view) {}
    ~UniqueView() {
        if (view_ != nullptr) {
            UnmapViewOfFile(view_);
        }
    }
    UniqueView(const UniqueView&) = delete;
    UniqueView& operator=(const UniqueView&) = delete;
    [[nodiscard]] void* get() const { return view_; }

private:
    void* view_ = nullptr;
};

[[noreturn]] void ThrowLastError(const char* operation) {
    throw std::system_error(
        static_cast<int>(GetLastError()), std::system_category(), operation);
}

std::uint32_t GetHandleCount() {
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count)) {
        ThrowLastError("GetProcessHandleCount");
    }
    return count;
}

std::wstring QuoteArgument(const std::wstring& value) {
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

std::string Narrow(const std::wstring& value) {
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

std::string JsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(ch) << std::dec;
                } else {
                    output << static_cast<char>(ch);
                }
        }
    }
    return output.str();
}

std::string ReadHeaderError(const SharedHeader& header) {
    const auto length = strnlen_s(header.last_error, sizeof(header.last_error));
    return std::string(header.last_error, length);
}

std::uint64_t Fnv1aUpdate(std::uint64_t hash, const void* memory, std::size_t bytes) {
    const auto* data = static_cast<const std::uint8_t*>(memory);
    for (std::size_t index = 0; index < bytes; ++index) {
        hash ^= data[index];
        hash *= 1'099'511'628'211ull;
    }
    return hash;
}

std::uint64_t HashInput(
    const std::byte* mapping, std::uint32_t slot, std::uint32_t validFrames) {
    std::uint64_t hash = 14'695'981'039'346'656'037ull;
    const auto slotOffset = htfx::ipc::InputSlotOffset(slot);
    for (std::uint32_t channel = 0; channel < htfx::ipc::kChannels; ++channel) {
        const auto offset = slotOffset + channel * htfx::ipc::kMaxFrames * sizeof(float);
        hash = Fnv1aUpdate(hash, mapping + offset, validFrames * sizeof(float));
    }
    return hash;
}

std::uint64_t HashOutput(
    const std::byte* mapping, std::uint32_t slot, std::uint32_t validFrames) {
    std::uint64_t hash = 14'695'981'039'346'656'037ull;
    const auto slotOffset = htfx::ipc::OutputSlotOffset(slot);
    for (std::uint32_t source = 0; source < htfx::ipc::kSources; ++source) {
        for (std::uint32_t channel = 0; channel < htfx::ipc::kChannels; ++channel) {
            const auto index = source * htfx::ipc::kChannels + channel;
            const auto offset = slotOffset + index * htfx::ipc::kMaxFrames * sizeof(float);
            hash = Fnv1aUpdate(hash, mapping + offset, validFrames * sizeof(float));
        }
    }
    return hash;
}

struct Options {
    std::filesystem::path python;
    std::filesystem::path worker;
    std::filesystem::path report = L"results/m2/ipc_report.json";
    std::uint64_t cycles = 10'000;
    std::uint32_t frames = 256;
    std::uint64_t resetEvery = 997;
};

struct RunResult {
    std::uint64_t requestedCycles = 0;
    std::uint64_t completedProcesses = 0;
    std::uint64_t completedResets = 0;
    std::uint64_t expectedResets = 0;
    std::uint64_t checksumErrors = 0;
    std::uint64_t formulaErrors = 0;
    std::uint64_t slotErrors = 0;
    std::uint64_t sequenceErrors = 0;
    std::uint64_t heartbeatStart = 0;
    std::uint64_t heartbeatEnd = 0;
    std::uint64_t protocolErrors = 0;
    std::uint32_t workerHandleStart = 0;
    std::uint32_t workerHandleEnd = 0;
    std::uint32_t workerPid = 0;
    DWORD workerExitCode = 0;
    double wallMilliseconds = 0.0;
    std::uint32_t validFrames = 0;
    std::uint32_t mappingBytes = htfx::ipc::kTotalBytes;
    std::uint32_t clientHandleBefore = 0;
    std::uint32_t clientHandleAfter = 0;
    std::int64_t clientHandleDelta = 0;
    std::uint32_t clientActiveHandleStart = 0;
    std::uint32_t clientActiveHandleEnd = 0;
    std::int64_t clientActiveHandleDelta = 0;
    bool passed = false;
};

UniqueHandle CreateEventChecked(
    BOOL manualReset, const std::wstring& name) {
    UniqueHandle event(CreateEventW(nullptr, manualReset, FALSE, name.c_str()));
    if (!event) {
        ThrowLastError("CreateEventW");
    }
    return event;
}

RunResult RunIpc(const Options& options) {
    using clock = std::chrono::steady_clock;
    const std::wstring session =
        L"HTFX_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64());
    const auto localName = [&](const wchar_t* suffix) {
        return L"Local\\" + session + suffix;
    };

    UniqueHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        htfx::ipc::kTotalBytes,
        localName(L"_shm").c_str()));
    if (!mapping) {
        ThrowLastError("CreateFileMappingW");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("unexpected shared-memory name collision");
    }
    UniqueView view(MapViewOfFile(
        mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, htfx::ipc::kTotalBytes));
    if (view.get() == nullptr) {
        ThrowLastError("MapViewOfFile");
    }
    auto* bytes = static_cast<std::byte*>(view.get());
    auto* header = reinterpret_cast<SharedHeader*>(view.get());
    std::memset(header, 0, sizeof(*header));

    UniqueHandle request = CreateEventChecked(FALSE, localName(L"_request"));
    UniqueHandle response = CreateEventChecked(FALSE, localName(L"_response"));
    UniqueHandle ready = CreateEventChecked(TRUE, localName(L"_ready"));
    UniqueHandle shutdown = CreateEventChecked(TRUE, localName(L"_shutdown"));

    header->magic = htfx::ipc::kMagic;
    header->abi_version = htfx::ipc::kAbiVersion;
    header->header_bytes = htfx::ipc::kHeaderBytes;
    header->total_bytes = htfx::ipc::kTotalBytes;
    header->sample_rate = htfx::ipc::kSampleRate;
    header->channels = htfx::ipc::kChannels;
    header->sources = htfx::ipc::kSources;
    header->max_frames = htfx::ipc::kMaxFrames;
    header->epoch = 1;
    header->state = static_cast<std::uint32_t>(State::kLaunching);
    header->input_slot_stride = htfx::ipc::kInputSlotStride;
    header->output_slot_stride = htfx::ipc::kOutputSlotStride;
    header->input_base_offset = htfx::ipc::kInputBaseOffset;
    header->output_base_offset = htfx::ipc::kOutputBaseOffset;
    header->client_pid = GetCurrentProcessId();
    MemoryBarrier();

    const auto python = std::filesystem::absolute(options.python).wstring();
    const auto worker = std::filesystem::absolute(options.worker).wstring();
    std::wstring commandLine = QuoteArgument(python) + L" " + QuoteArgument(worker) +
                               L" --session " + QuoteArgument(session);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(
            python.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &processInfo)) {
        ThrowLastError("CreateProcessW worker");
    }
    UniqueHandle workerProcess(processInfo.hProcess);
    UniqueHandle workerThread(processInfo.hThread);
    workerThread.reset();

    try {
        std::array<HANDLE, 2> readyWait{ready.get(), workerProcess.get()};
        const DWORD readyResult = WaitForMultipleObjects(
            static_cast<DWORD>(readyWait.size()), readyWait.data(), FALSE, 30'000);
        if (readyResult == WAIT_OBJECT_0 + 1) {
            throw std::runtime_error("worker exited before READY");
        }
        if (readyResult == WAIT_TIMEOUT) {
            throw std::runtime_error("worker READY timeout");
        }
        if (readyResult != WAIT_OBJECT_0) {
            ThrowLastError("WaitForMultipleObjects READY");
        }
        if (header->state != static_cast<std::uint32_t>(State::kReady)) {
            throw std::runtime_error("worker signaled READY with invalid state");
        }

        RunResult result;
        result.requestedCycles = options.cycles;
        result.validFrames = options.frames;
        result.workerPid = header->worker_pid;
        result.workerHandleStart = header->worker_handle_count_start;
        result.heartbeatStart = header->heartbeat_counter;
        std::wcout << L"worker_ready pid=" << result.workerPid << L'\n' << std::flush;
        result.clientActiveHandleStart = GetHandleCount();
        std::uint64_t sequence = 0;
        std::uint64_t epoch = header->epoch;

        auto send = [&](Command command) {
            header->command = static_cast<std::uint32_t>(command);
            const auto requested = ++sequence;
            header->request_sequence = requested;
            MemoryBarrier();
            if (!SetEvent(request.get())) {
                ThrowLastError("SetEvent request");
            }
            std::array<HANDLE, 2> waits{response.get(), workerProcess.get()};
            const DWORD waitResult = WaitForMultipleObjects(
                static_cast<DWORD>(waits.size()), waits.data(), FALSE, 3'000);
            if (waitResult == WAIT_OBJECT_0 + 1) {
                throw std::runtime_error("worker exited while request was outstanding");
            }
            if (waitResult == WAIT_TIMEOUT) {
                throw std::runtime_error(
                    "response timeout; heartbeat=" +
                    std::to_string(header->heartbeat_counter));
            }
            if (waitResult != WAIT_OBJECT_0) {
                ThrowLastError("WaitForMultipleObjects response");
            }
            MemoryBarrier();
            if (header->response_sequence != requested) {
                ++result.sequenceErrors;
                throw std::runtime_error("response sequence mismatch");
            }
            if (header->status_code != 0) {
                throw std::runtime_error(
                    "worker status error: " + ReadHeaderError(*header));
            }
            return requested;
        };

        const auto started = clock::now();
        const std::uint64_t progressInterval = std::max<std::uint64_t>(1, options.cycles / 10);
        for (std::uint64_t cycle = 0; cycle < options.cycles; ++cycle) {
            if (options.resetEvery > 0 && cycle > 0 && cycle % options.resetEvery == 0) {
                header->epoch = ++epoch;
                header->active_slot = 0;
                header->valid_frames = 0;
                header->input_checksum = 0;
                header->output_checksum = 0;
                send(Command::kReset);
            }

            const std::uint32_t slot = static_cast<std::uint32_t>(cycle % 2);
            const std::uint64_t nextSequence = sequence + 1;
            auto* input = reinterpret_cast<float*>(bytes + htfx::ipc::InputSlotOffset(slot));
            for (std::uint32_t channel = 0; channel < htfx::ipc::kChannels; ++channel) {
                for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
                    input[channel * htfx::ipc::kMaxFrames + frame] =
                        static_cast<float>((nextSequence % 997) * 0.0001) +
                        static_cast<float>(channel) * 0.01f +
                        static_cast<float>(frame) * 0.00001f;
                }
            }
            header->epoch = epoch;
            header->active_slot = slot;
            header->valid_frames = options.frames;
            header->input_checksum = HashInput(bytes, slot, options.frames);
            header->output_checksum = 0;
            const auto processSequence = send(Command::kProcess);

            if (header->active_slot != slot) {
                ++result.slotErrors;
            }
            const auto outputChecksum = HashOutput(bytes, slot, options.frames);
            if (outputChecksum != header->output_checksum) {
                ++result.checksumErrors;
            }

            const auto* output = reinterpret_cast<const float*>(
                bytes + htfx::ipc::OutputSlotOffset(slot));
            constexpr std::array<float, 4> gains{0.25f, 0.5f, 0.75f, 1.0f};
            const float offset = static_cast<float>(processSequence % 1000) * 1.0e-7f +
                                 static_cast<float>(epoch % 1000) * 1.0e-6f;
            const std::array<std::uint32_t, 3> probes{
                0u, options.frames / 2u, options.frames - 1u};
            for (std::uint32_t source = 0; source < htfx::ipc::kSources; ++source) {
                for (std::uint32_t channel = 0; channel < htfx::ipc::kChannels; ++channel) {
                    for (const auto frame : probes) {
                        const float expected =
                            input[channel * htfx::ipc::kMaxFrames + frame] * gains[source] +
                            offset;
                        const auto outputIndex =
                            (source * htfx::ipc::kChannels + channel) *
                                htfx::ipc::kMaxFrames +
                            frame;
                        if (std::abs(output[outputIndex] - expected) > 2.0e-6f) {
                            ++result.formulaErrors;
                        }
                    }
                }
            }

            if ((cycle + 1) % progressInterval == 0 || cycle + 1 == options.cycles) {
                std::wcout << L"progress=" << (cycle + 1) << L"/" << options.cycles
                           << L" heartbeat=" << header->heartbeat_counter << L'\n';
            }
        }
        result.wallMilliseconds = std::chrono::duration<double, std::milli>(
                                      clock::now() - started)
                                      .count();
        result.expectedResets =
            options.resetEvery == 0 ? 0 : (options.cycles - 1) / options.resetEvery;
        result.clientActiveHandleEnd = GetHandleCount();
        result.clientActiveHandleDelta =
            static_cast<std::int64_t>(result.clientActiveHandleEnd) -
            result.clientActiveHandleStart;

        header->active_slot = 0;
        header->valid_frames = 0;
        send(Command::kShutdown);
        const DWORD processWait = WaitForSingleObject(workerProcess.get(), 10'000);
        if (processWait == WAIT_TIMEOUT) {
            throw std::runtime_error("worker shutdown timeout");
        }
        if (processWait != WAIT_OBJECT_0) {
            ThrowLastError("WaitForSingleObject worker shutdown");
        }
        if (!GetExitCodeProcess(workerProcess.get(), &result.workerExitCode)) {
            ThrowLastError("GetExitCodeProcess");
        }
        result.completedProcesses = header->processes_completed;
        result.completedResets = header->resets_completed;
        result.protocolErrors = header->protocol_errors;
        result.heartbeatEnd = header->heartbeat_counter;
        result.workerHandleEnd = header->worker_handle_count_end;
        return result;
    } catch (...) {
        SetEvent(shutdown.get());
        if (WaitForSingleObject(workerProcess.get(), 2'000) == WAIT_TIMEOUT) {
            TerminateProcess(workerProcess.get(), 99);
            WaitForSingleObject(workerProcess.get(), 2'000);
        }
        throw;
    }
}

Options ParseOptions(int argc, wchar_t** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        const auto take = [&]() -> std::wstring {
            if (++index >= argc) {
                throw std::runtime_error("missing value for " + Narrow(argument));
            }
            return argv[index];
        };
        if (argument == L"--python") {
            options.python = take();
        } else if (argument == L"--worker") {
            options.worker = take();
        } else if (argument == L"--report") {
            options.report = take();
        } else if (argument == L"--cycles") {
            options.cycles = std::stoull(take());
        } else if (argument == L"--frames") {
            options.frames = static_cast<std::uint32_t>(std::stoul(take()));
        } else if (argument == L"--reset-every") {
            options.resetEvery = std::stoull(take());
        } else {
            throw std::runtime_error("unknown argument: " + Narrow(argument));
        }
    }
    if (options.python.empty() || options.worker.empty()) {
        throw std::runtime_error("--python and --worker are required");
    }
    if (!std::filesystem::is_regular_file(options.python)) {
        throw std::runtime_error("python executable does not exist");
    }
    if (!std::filesystem::is_regular_file(options.worker)) {
        throw std::runtime_error("worker script does not exist");
    }
    if (options.cycles == 0) {
        throw std::runtime_error("--cycles must be positive");
    }
    if (options.frames == 0 || options.frames > htfx::ipc::kMaxFrames) {
        throw std::runtime_error("--frames is outside the ABI capacity");
    }
    return options;
}

void WriteReport(const Options& options, const RunResult& result) {
    const auto reportPath = std::filesystem::absolute(options.report);
    std::filesystem::create_directories(reportPath.parent_path());
    std::ofstream output(reportPath, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open report path");
    }
    const auto workerDelta =
        static_cast<std::int64_t>(result.workerHandleEnd) - result.workerHandleStart;
    output << std::boolalpha << std::setprecision(12)
           << "{\n"
           << "  \"pass\": " << result.passed << ",\n"
           << "  \"cycles_requested\": " << result.requestedCycles << ",\n"
           << "  \"processes_completed\": " << result.completedProcesses << ",\n"
           << "  \"resets_expected\": " << result.expectedResets << ",\n"
           << "  \"resets_completed\": " << result.completedResets << ",\n"
           << "  \"valid_frames_per_process\": " << result.validFrames << ",\n"
           << "  \"mapping_bytes\": " << result.mappingBytes << ",\n"
           << "  \"wall_milliseconds\": " << result.wallMilliseconds << ",\n"
           << "  \"roundtrips_per_second\": "
           << result.requestedCycles / (result.wallMilliseconds / 1000.0) << ",\n"
           << "  \"checksum_errors\": " << result.checksumErrors << ",\n"
           << "  \"formula_errors\": " << result.formulaErrors << ",\n"
           << "  \"slot_errors\": " << result.slotErrors << ",\n"
           << "  \"sequence_errors\": " << result.sequenceErrors << ",\n"
           << "  \"protocol_errors\": " << result.protocolErrors << ",\n"
           << "  \"heartbeat_start\": " << result.heartbeatStart << ",\n"
           << "  \"heartbeat_end\": " << result.heartbeatEnd << ",\n"
           << "  \"worker_pid\": " << result.workerPid << ",\n"
           << "  \"worker_exit_code\": " << result.workerExitCode << ",\n"
           << "  \"worker_handle_start\": " << result.workerHandleStart << ",\n"
           << "  \"worker_handle_end\": " << result.workerHandleEnd << ",\n"
           << "  \"worker_handle_delta\": " << workerDelta << ",\n"
           << "  \"client_handle_before\": " << result.clientHandleBefore << ",\n"
           << "  \"client_handle_after\": " << result.clientHandleAfter << ",\n"
           << "  \"client_handle_delta\": " << result.clientHandleDelta << ",\n"
           << "  \"client_active_handle_start\": " << result.clientActiveHandleStart << ",\n"
           << "  \"client_active_handle_end\": " << result.clientActiveHandleEnd << ",\n"
           << "  \"client_active_handle_delta\": " << result.clientActiveHandleDelta << ",\n"
           << "  \"python\": \"" << JsonEscape(Narrow(std::filesystem::absolute(options.python).wstring())) << "\",\n"
           << "  \"worker\": \"" << JsonEscape(Narrow(std::filesystem::absolute(options.worker).wstring())) << "\"\n"
           << "}\n";
    std::wcout << L"report=" << reportPath.wstring() << L'\n';
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const Options options = ParseOptions(argc, argv);
        const auto handleBefore = GetHandleCount();
        RunResult result = RunIpc(options);
        const auto handleAfter = GetHandleCount();
        result.clientHandleBefore = handleBefore;
        result.clientHandleAfter = handleAfter;
        result.clientHandleDelta =
            static_cast<std::int64_t>(handleAfter) - handleBefore;
        const auto workerHandleDelta =
            static_cast<std::int64_t>(result.workerHandleEnd) - result.workerHandleStart;
        result.passed =
            result.completedProcesses == result.requestedCycles &&
            result.completedResets == result.expectedResets &&
            result.checksumErrors == 0 && result.formulaErrors == 0 &&
            result.slotErrors == 0 && result.sequenceErrors == 0 &&
            result.protocolErrors == 0 && result.heartbeatEnd > result.heartbeatStart &&
            result.workerExitCode == 0 && workerHandleDelta == 0 &&
            result.clientActiveHandleDelta == 0;
        WriteReport(options, result);
        std::wcout << L"PASS=" << (result.passed ? L"true" : L"false") << L'\n';
        return result.passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "ipc_client fatal: " << error.what() << '\n';
        return 2;
    }
}
