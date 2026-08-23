#pragma once

#include <cstddef>
#include <cstdint>

namespace htfx::ipc {

inline constexpr std::uint32_t kMagic = 0x58465448;  // "HTFX" on little-endian hosts.
inline constexpr std::uint32_t kAbiVersion = 2;
inline constexpr std::uint32_t kHeaderBytes = 4096;
inline constexpr std::uint32_t kSampleRate = 44100;
inline constexpr std::uint32_t kChannels = 2;
inline constexpr std::uint32_t kDefaultSources = 4;
inline constexpr std::uint32_t kMaxSources = 6;
inline constexpr std::uint32_t kSources = kDefaultSources;  // ABI v1 test compatibility.
inline constexpr std::uint32_t kMaxFrames = 257985;
inline constexpr std::uint32_t kSlotCount = 2;

constexpr std::uint32_t Align64(std::uint64_t value) {
    return static_cast<std::uint32_t>((value + 63u) & ~std::uint64_t{63u});
}

inline constexpr std::uint32_t kInputSlotBytes =
    kChannels * kMaxFrames * static_cast<std::uint32_t>(sizeof(float));
inline constexpr std::uint32_t kOutputSlotBytes =
    kMaxSources * kChannels * kMaxFrames * static_cast<std::uint32_t>(sizeof(float));
inline constexpr std::uint32_t kInputSlotStride = Align64(kInputSlotBytes);
inline constexpr std::uint32_t kOutputSlotStride = Align64(kOutputSlotBytes);
inline constexpr std::uint32_t kInputBaseOffset = kHeaderBytes;
inline constexpr std::uint32_t kOutputBaseOffset =
    kInputBaseOffset + kSlotCount * kInputSlotStride;
inline constexpr std::uint32_t kTotalBytes =
    kOutputBaseOffset + kSlotCount * kOutputSlotStride;

enum class State : std::uint32_t {
    kInit = 0,
    kLaunching = 1,
    kReady = 2,
    kProcessing = 3,
    kError = 4,
    kStopped = 5,
    kLoading = 6,
    kWarming = 7,
    kPriming = 8,
    kRecovering = 9,
};

enum class Command : std::uint32_t {
    kNone = 0,
    kProcess = 1,
    kReset = 2,
    kShutdown = 3,
};

enum class ComputeBackend : std::uint32_t {
    kAuto = 0,
    kCuda = 1,
    kCpu = 2,
    kMps = 3,
};

struct alignas(8) SharedHeader {
    std::uint32_t magic;                        // 0
    std::uint32_t abi_version;                  // 4
    std::uint32_t header_bytes;                 // 8
    std::uint32_t total_bytes;                  // 12
    std::uint32_t sample_rate;                  // 16
    std::uint32_t channels;                     // 20
    std::uint32_t sources;                      // 24 (active source count, 4 or 6)
    std::uint32_t max_frames;                   // 28
    std::uint64_t epoch;                        // 32
    std::uint64_t request_sequence;             // 40
    std::uint64_t response_sequence;            // 48
    std::uint64_t heartbeat_counter;             // 56
    std::uint64_t heartbeat_monotonic_ms;        // 64
    std::uint32_t state;                        // 72
    std::uint32_t command;                      // 76
    std::uint32_t active_slot;                  // 80
    std::uint32_t valid_frames;                 // 84
    std::int32_t status_code;                   // 88
    std::uint32_t reserved0;                    // 92
    std::uint64_t input_checksum;               // 96
    std::uint64_t output_checksum;              // 104
    std::uint32_t input_slot_stride;            // 112
    std::uint32_t output_slot_stride;           // 116
    std::uint32_t input_base_offset;            // 120
    std::uint32_t output_base_offset;           // 124
    std::uint32_t client_pid;                   // 128
    std::uint32_t worker_pid;                   // 132
    std::uint64_t resets_completed;             // 136
    std::uint64_t processes_completed;          // 144
    std::uint64_t protocol_errors;              // 152
    std::uint32_t worker_handle_count_start;     // 160
    std::uint32_t worker_handle_count_end;       // 164
    char last_error[512];                       // 168
    std::uint64_t last_inference_us;             // 680
    std::uint64_t cuda_allocated_bytes;          // 688
    std::uint64_t cuda_reserved_bytes;           // 696
    std::uint64_t cuda_max_allocated_bytes;      // 704
    std::uint64_t cuda_max_reserved_bytes;       // 712
    std::uint32_t cuda_device_index;             // 720
    std::uint32_t reserved1;                     // 724
    char gpu_name[128];                          // 728 (accelerator name or "CPU")
    std::uint32_t segment_frames;               // 856
    std::uint32_t hop_frames;                   // 860
    std::uint32_t compute_backend;              // 864 (resolved ComputeBackend)
    std::uint32_t requested_backend;            // 868 (requested ComputeBackend)
    char model_name[64];                        // 872
    std::byte reserved[kHeaderBytes - 936];
};

static_assert(sizeof(SharedHeader) == kHeaderBytes);
static_assert(offsetof(SharedHeader, epoch) == 32);
static_assert(offsetof(SharedHeader, request_sequence) == 40);
static_assert(offsetof(SharedHeader, state) == 72);
static_assert(offsetof(SharedHeader, input_checksum) == 96);
static_assert(offsetof(SharedHeader, last_error) == 168);
static_assert(offsetof(SharedHeader, last_inference_us) == 680);
static_assert(offsetof(SharedHeader, gpu_name) == 728);
static_assert(offsetof(SharedHeader, segment_frames) == 856);
static_assert(offsetof(SharedHeader, model_name) == 872);
static_assert(kTotalBytes == 28898560);

constexpr std::uint32_t InputSlotOffset(std::uint32_t slot) {
    return kInputBaseOffset + slot * kInputSlotStride;
}

constexpr std::uint32_t OutputSlotOffset(std::uint32_t slot) {
    return kOutputBaseOffset + slot * kOutputSlotStride;
}

}  // namespace htfx::ipc
