#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace htfx {

template <typename Item, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2);
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<Item>);

public:
    SpscRing() noexcept { std::memset(storage_.data(), 0, sizeof(storage_)); }

    bool tryPush(const Item& item) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= Capacity) {
            return false;
        }
        storage_[static_cast<std::size_t>(write) & (Capacity - 1)] = item;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(Item& item) noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        item = storage_[static_cast<std::size_t>(read) & (Capacity - 1)];
        readIndex_.store(read + 1, std::memory_order_release);
        return true;
    }

    std::uint64_t availableToRead() const noexcept {
        const auto write = writeIndex_.load(std::memory_order_acquire);
        const auto read = readIndex_.load(std::memory_order_acquire);
        return write - read;
    }

    // Only the consumer may call this while the producer is active.
    void discardAllByConsumer() noexcept {
        readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
    }

    // Call only when both producer and consumer are stopped.
    void clearWhenStopped() noexcept {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
    }

private:
    // Zeroed at run time by the constructor rather than with "{}": at
    // Capacity = 2^19 the value-initialised form made the compiler
    // materialise ~25 MB of zero-init per ring (three rings per processor)
    // and exhausted the compiler heap (C1060). A single memset keeps the
    // original semantics without the compile-time blowup.
    std::array<Item, Capacity> storage_;
    alignas(64) std::atomic<std::uint64_t> writeIndex_{0};
    alignas(64) std::atomic<std::uint64_t> readIndex_{0};
};

}  // namespace htfx
