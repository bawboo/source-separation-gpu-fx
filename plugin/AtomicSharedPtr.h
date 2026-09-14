#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <utility>

namespace htfx {

// std::atomic<std::shared_ptr<T>> with the same load/store surface, because
// only some standard libraries have that C++20 specialisation. Microsoft's STL
// does, which is why the Windows build never needed this; Apple's libc++ does
// not, so the primary template is selected instead and its
// is_trivially_copyable requirement fails to compile.
//
// Guarded by a spin lock rather than a mutex: the audio thread reads this
// (processBlock -> processRecordMode), so it must not block in the kernel.
// That matches how the MSVC specialisation behaves anyway -- it spins on a bit
// in the control block and reports is_always_lock_free as false -- so this
// keeps both platforms on the same footing rather than trading one set of
// timing characteristics for another.
template <typename T>
class AtomicSharedPtr final {
public:
    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> initial) noexcept
        : value_(std::move(initial)) {}

    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

    [[nodiscard]] std::shared_ptr<T> load(
        std::memory_order = std::memory_order_seq_cst) const noexcept {
        const juce::SpinLock::ScopedLockType lock(guard_);
        return value_;
    }

    void store(
        std::shared_ptr<T> desired,
        std::memory_order = std::memory_order_seq_cst) noexcept {
        // The outgoing value is released after the lock, so a destructor that
        // frees whole decoded stems never runs inside the critical section.
        std::shared_ptr<T> previous;
        {
            const juce::SpinLock::ScopedLockType lock(guard_);
            previous = std::exchange(value_, std::move(desired));
        }
    }

private:
    mutable juce::SpinLock guard_;
    std::shared_ptr<T> value_;
};

}  // namespace htfx
