#include "MmeAudioIODevice.h"

#if JUCE_WINDOWS

#include <windows.h>
#include <mmsystem.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace juce {
namespace {

constexpr int kChannels = 2;
constexpr int kBufferCount = 4;

String mmError(MMRESULT result, bool input) {
    if (result == MMSYSERR_NOERROR) {
        return {};
    }
    std::array<wchar_t, MAXERRORLENGTH> text{};
    const auto translated = input
                                ? waveInGetErrorTextW(
                                      result, text.data(), static_cast<UINT>(text.size()))
                                : waveOutGetErrorTextW(
                                      result, text.data(), static_cast<UINT>(text.size()));
    return translated == MMSYSERR_NOERROR
               ? String(text.data())
               : "WinMM error " + String(static_cast<int>(result));
}

String uniqueName(const String& requested, const StringArray& existing) {
    if (!existing.contains(requested)) {
        return requested;
    }
    for (int suffix = 2;; ++suffix) {
        const auto candidate = requested + " (" + String(suffix) + ")";
        if (!existing.contains(candidate)) {
            return candidate;
        }
    }
}

class MmeAudioIODevice final : public AudioIODevice {
public:
    MmeAudioIODevice(
        String displayName,
        std::optional<UINT> inputId,
        std::optional<UINT> outputId)
        : AudioIODevice(std::move(displayName), "MME (WinMM)"),
          inputId_(inputId),
          outputId_(outputId) {}

    ~MmeAudioIODevice() override { close(); }

    StringArray getOutputChannelNames() override {
        return outputId_.has_value()
                   ? StringArray{"Output 1", "Output 2"}
                   : StringArray{};
    }

    StringArray getInputChannelNames() override {
        return inputId_.has_value()
                   ? StringArray{"Input 1", "Input 2"}
                   : StringArray{};
    }

    std::optional<BigInteger> getDefaultOutputChannels() const override {
        return outputId_.has_value() ? std::optional<BigInteger>(stereoMask())
                                     : std::optional<BigInteger>(BigInteger{});
    }

    std::optional<BigInteger> getDefaultInputChannels() const override {
        return inputId_.has_value() ? std::optional<BigInteger>(stereoMask())
                                    : std::optional<BigInteger>(BigInteger{});
    }

    Array<double> getAvailableSampleRates() override {
        return {44'100.0, 48'000.0, 88'200.0, 96'000.0};
    }

    Array<int> getAvailableBufferSizes() override {
        return {256, 441, 480, 512, 1024, 2048, 4096};
    }

    int getDefaultBufferSize() override { return 1024; }

    String open(
        const BigInteger& inputChannels,
        const BigInteger& outputChannels,
        double sampleRate,
        int bufferSizeSamples) override {
        close();
        lastError_.clear();
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44'100.0;
        bufferSize_ = bufferSizeSamples > 0 ? bufferSizeSamples : getDefaultBufferSize();
        activeInput_ = inputId_.has_value() && !inputChannels.isZero()
                           ? stereoMask()
                           : BigInteger{};
        activeOutput_ = outputId_.has_value() && !outputChannels.isZero()
                            ? stereoMask()
                            : BigInteger{};
        if (activeInput_.isZero() && activeOutput_.isZero()) {
            return lastError_ = "MME requires at least one active input or output";
        }

        inputEvent_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        outputEvent_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if ((!activeInput_.isZero() && inputEvent_.get() == nullptr) ||
            (!activeOutput_.isZero() && outputEvent_.get() == nullptr)) {
            close();
            return lastError_ = "Could not create WinMM audio events";
        }

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = kChannels;
        format.nSamplesPerSec = static_cast<DWORD>(std::lround(sampleRate_));
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(
            format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        if (!activeInput_.isZero()) {
            const auto result = waveInOpen(
                &waveIn_,
                *inputId_,
                &format,
                reinterpret_cast<DWORD_PTR>(inputEvent_.get()),
                0,
                CALLBACK_EVENT);
            if (result != MMSYSERR_NOERROR) {
                close();
                return lastError_ = "MME input: " + mmError(result, true);
            }
        }
        if (!activeOutput_.isZero()) {
            const auto result = waveOutOpen(
                &waveOut_,
                *outputId_,
                &format,
                reinterpret_cast<DWORD_PTR>(outputEvent_.get()),
                0,
                CALLBACK_EVENT);
            if (result != MMSYSERR_NOERROR) {
                close();
                return lastError_ = "MME output: " + mmError(result, false);
            }
        }

        inputSamples_.assign(
            kBufferCount,
            std::vector<std::int16_t>(
                static_cast<std::size_t>(bufferSize_) * kChannels));
        outputSamples_.assign(
            kBufferCount,
            std::vector<std::int16_t>(
                static_cast<std::size_t>(bufferSize_) * kChannels));
        inputHeaders_.assign(kBufferCount, WAVEHDR{});
        outputHeaders_.assign(kBufferCount, WAVEHDR{});

        if (waveIn_ != nullptr) {
            for (int index = 0; index < kBufferCount; ++index) {
                auto& header = inputHeaders_[static_cast<std::size_t>(index)];
                header.lpData = reinterpret_cast<LPSTR>(
                    inputSamples_[static_cast<std::size_t>(index)].data());
                header.dwBufferLength = static_cast<DWORD>(
                    inputSamples_[static_cast<std::size_t>(index)].size() *
                    sizeof(std::int16_t));
                const auto result = waveInPrepareHeader(waveIn_, &header, sizeof(header));
                if (result != MMSYSERR_NOERROR) {
                    close();
                    return lastError_ = "MME input prepare: " + mmError(result, true);
                }
            }
        }
        if (waveOut_ != nullptr) {
            for (int index = 0; index < kBufferCount; ++index) {
                auto& header = outputHeaders_[static_cast<std::size_t>(index)];
                header.lpData = reinterpret_cast<LPSTR>(
                    outputSamples_[static_cast<std::size_t>(index)].data());
                header.dwBufferLength = static_cast<DWORD>(
                    outputSamples_[static_cast<std::size_t>(index)].size() *
                    sizeof(std::int16_t));
                const auto result = waveOutPrepareHeader(waveOut_, &header, sizeof(header));
                if (result != MMSYSERR_NOERROR) {
                    close();
                    return lastError_ = "MME output prepare: " + mmError(result, false);
                }
            }
        }

        inputFloat_[0].resize(static_cast<std::size_t>(bufferSize_));
        inputFloat_[1].resize(static_cast<std::size_t>(bufferSize_));
        outputFloat_[0].resize(static_cast<std::size_t>(bufferSize_));
        outputFloat_[1].resize(static_cast<std::size_t>(bufferSize_));
        open_.store(true, std::memory_order_release);
        return {};
    }

    void close() override {
        stop();
        if (waveIn_ != nullptr) {
            waveInReset(waveIn_);
            for (auto& header : inputHeaders_) {
                if ((header.dwFlags & WHDR_PREPARED) != 0) {
                    waveInUnprepareHeader(waveIn_, &header, sizeof(header));
                }
            }
            waveInClose(waveIn_);
            waveIn_ = nullptr;
        }
        if (waveOut_ != nullptr) {
            waveOutReset(waveOut_);
            for (auto& header : outputHeaders_) {
                if ((header.dwFlags & WHDR_PREPARED) != 0) {
                    waveOutUnprepareHeader(waveOut_, &header, sizeof(header));
                }
            }
            waveOutClose(waveOut_);
            waveOut_ = nullptr;
        }
        inputHeaders_.clear();
        outputHeaders_.clear();
        inputSamples_.clear();
        outputSamples_.clear();
        inputEvent_.reset();
        outputEvent_.reset();
        open_.store(false, std::memory_order_release);
    }

    bool isOpen() override { return open_.load(std::memory_order_acquire); }

    void start(AudioIODeviceCallback* callback) override {
        stop();
        if (!isOpen() || callback == nullptr) {
            return;
        }
        callback_.store(callback, std::memory_order_release);
        callback->audioDeviceAboutToStart(this);
        inputCursor_ = 0;
        outputCursor_ = 0;
        xruns_.store(0, std::memory_order_relaxed);
        if (waveIn_ != nullptr) {
            for (auto& header : inputHeaders_) {
                header.dwFlags &= ~WHDR_DONE;
                const auto result = waveInAddBuffer(waveIn_, &header, sizeof(header));
                if (result != MMSYSERR_NOERROR) {
                    reportStreamError("MME input queue: " + mmError(result, true));
                    stop();
                    return;
                }
            }
            const auto result = waveInStart(waveIn_);
            if (result != MMSYSERR_NOERROR) {
                reportStreamError("MME input start: " + mmError(result, true));
                stop();
                return;
            }
        }
        playing_.store(true, std::memory_order_release);
        streamThread_ = std::jthread(
            [this](std::stop_token stopToken) { streamLoop(stopToken); });
    }

    void stop() override {
        if (!playing_.exchange(false, std::memory_order_acq_rel) &&
            !streamThread_.joinable()) {
            return;
        }
        if (waveIn_ != nullptr) {
            waveInStop(waveIn_);
            waveInReset(waveIn_);
        }
        if (waveOut_ != nullptr) {
            waveOutReset(waveOut_);
        }
        if (inputEvent_.get() != nullptr) {
            SetEvent(inputEvent_.get());
        }
        if (outputEvent_.get() != nullptr) {
            SetEvent(outputEvent_.get());
        }
        if (streamThread_.joinable()) {
            streamThread_.request_stop();
            streamThread_.join();
        }
        if (auto* callback = callback_.exchange(nullptr, std::memory_order_acq_rel)) {
            callback->audioDeviceStopped();
        }
    }

    bool isPlaying() override {
        return playing_.load(std::memory_order_acquire) &&
               callback_.load(std::memory_order_acquire) != nullptr;
    }

    String getLastError() override { return lastError_; }
    int getCurrentBufferSizeSamples() override { return bufferSize_; }
    double getCurrentSampleRate() override { return sampleRate_; }
    int getCurrentBitDepth() override { return 16; }
    BigInteger getActiveOutputChannels() const override { return activeOutput_; }
    BigInteger getActiveInputChannels() const override { return activeInput_; }
    int getOutputLatencyInSamples() override {
        return activeOutput_.isZero() ? 0 : bufferSize_ * (kBufferCount - 1);
    }
    int getInputLatencyInSamples() override {
        return activeInput_.isZero() ? 0 : bufferSize_;
    }
    int getXRunCount() const noexcept override {
        return xruns_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::optional<UINT> inputId() const noexcept { return inputId_; }
    [[nodiscard]] std::optional<UINT> outputId() const noexcept { return outputId_; }

private:
    class EventHandle final {
    public:
        ~EventHandle() { reset(); }
        HANDLE get() const noexcept { return handle_; }
        void reset(HANDLE replacement = nullptr) noexcept {
            if (handle_ != nullptr) {
                CloseHandle(handle_);
            }
            handle_ = replacement;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    static BigInteger stereoMask() {
        BigInteger result;
        result.setRange(0, kChannels, true);
        return result;
    }

    void reportStreamError(const String& error) {
        lastError_ = error;
        xruns_.fetch_add(1, std::memory_order_relaxed);
        if (auto* callback = callback_.load(std::memory_order_acquire)) {
            callback->audioDeviceError(error);
        }
    }

    bool waitUntilDone(
        WAVEHDR& header,
        HANDLE event,
        std::stop_token stopToken) {
        while ((header.dwFlags & WHDR_DONE) == 0) {
            if (stopToken.stop_requested() ||
                !playing_.load(std::memory_order_acquire)) {
                return false;
            }
            const auto wait = WaitForSingleObject(event, 100);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) {
                reportStreamError("MME event wait failed");
                return false;
            }
        }
        return true;
    }

    void streamLoop(std::stop_token stopToken) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        const float scaleIn = 1.0f / 32768.0f;
        while (!stopToken.stop_requested() &&
               playing_.load(std::memory_order_acquire)) {
            WAVEHDR* inputHeader = nullptr;
            std::vector<std::int16_t>* inputSamples = nullptr;
            if (waveIn_ != nullptr) {
                const auto index = inputCursor_++ % kBufferCount;
                inputHeader = &inputHeaders_[static_cast<std::size_t>(index)];
                inputSamples = &inputSamples_[static_cast<std::size_t>(index)];
                if (!waitUntilDone(*inputHeader, inputEvent_.get(), stopToken)) {
                    break;
                }
                for (int sample = 0; sample < bufferSize_; ++sample) {
                    inputFloat_[0][static_cast<std::size_t>(sample)] =
                        static_cast<float>((*inputSamples)[static_cast<std::size_t>(sample) * 2]) *
                        scaleIn;
                    inputFloat_[1][static_cast<std::size_t>(sample)] =
                        static_cast<float>((*inputSamples)[static_cast<std::size_t>(sample) * 2 + 1]) *
                        scaleIn;
                }
            } else {
                std::fill(inputFloat_[0].begin(), inputFloat_[0].end(), 0.0f);
                std::fill(inputFloat_[1].begin(), inputFloat_[1].end(), 0.0f);
            }

            WAVEHDR* outputHeader = nullptr;
            std::vector<std::int16_t>* outputSamples = nullptr;
            if (waveOut_ != nullptr) {
                const auto index = outputCursor_++ % kBufferCount;
                outputHeader = &outputHeaders_[static_cast<std::size_t>(index)];
                outputSamples = &outputSamples_[static_cast<std::size_t>(index)];
                if ((outputHeader->dwFlags & WHDR_INQUEUE) != 0 &&
                    !waitUntilDone(*outputHeader, outputEvent_.get(), stopToken)) {
                    break;
                }
            }

            const std::array<const float*, kChannels> inputPointers{
                inputFloat_[0].data(), inputFloat_[1].data()};
            const std::array<float*, kChannels> outputPointers{
                outputFloat_[0].data(), outputFloat_[1].data()};
            if (auto* callback = callback_.load(std::memory_order_acquire)) {
                callback->audioDeviceIOCallbackWithContext(
                    activeInput_.isZero() ? nullptr : inputPointers.data(),
                    activeInput_.isZero() ? 0 : kChannels,
                    activeOutput_.isZero() ? nullptr : outputPointers.data(),
                    activeOutput_.isZero() ? 0 : kChannels,
                    bufferSize_,
                    {});
            } else {
                break;
            }

            if (outputHeader != nullptr && outputSamples != nullptr) {
                for (int sample = 0; sample < bufferSize_; ++sample) {
                    for (int channel = 0; channel < kChannels; ++channel) {
                        const float value = std::clamp(
                            outputFloat_[static_cast<std::size_t>(channel)]
                                        [static_cast<std::size_t>(sample)],
                            -1.0f,
                            1.0f);
                        (*outputSamples)[static_cast<std::size_t>(sample) * 2 + channel] =
                            static_cast<std::int16_t>(std::lround(value * 32767.0f));
                    }
                }
                outputHeader->dwFlags &= ~WHDR_DONE;
                const auto result = waveOutWrite(waveOut_, outputHeader, sizeof(*outputHeader));
                if (result != MMSYSERR_NOERROR) {
                    reportStreamError("MME output queue: " + mmError(result, false));
                    break;
                }
            }
            if (inputHeader != nullptr) {
                inputHeader->dwFlags &= ~WHDR_DONE;
                const auto result = waveInAddBuffer(waveIn_, inputHeader, sizeof(*inputHeader));
                if (result != MMSYSERR_NOERROR) {
                    reportStreamError("MME input requeue: " + mmError(result, true));
                    break;
                }
            }
        }
        playing_.store(false, std::memory_order_release);
    }

    std::optional<UINT> inputId_;
    std::optional<UINT> outputId_;
    HWAVEIN waveIn_ = nullptr;
    HWAVEOUT waveOut_ = nullptr;
    EventHandle inputEvent_;
    EventHandle outputEvent_;
    std::vector<std::vector<std::int16_t>> inputSamples_;
    std::vector<std::vector<std::int16_t>> outputSamples_;
    std::vector<WAVEHDR> inputHeaders_;
    std::vector<WAVEHDR> outputHeaders_;
    std::array<std::vector<float>, kChannels> inputFloat_;
    std::array<std::vector<float>, kChannels> outputFloat_;
    std::jthread streamThread_;
    std::atomic<AudioIODeviceCallback*> callback_{nullptr};
    std::atomic<bool> open_{false};
    std::atomic<bool> playing_{false};
    std::atomic<int> xruns_{0};
    double sampleRate_ = 44'100.0;
    int bufferSize_ = 1024;
    BigInteger activeInput_;
    BigInteger activeOutput_;
    String lastError_;
    int inputCursor_ = 0;
    int outputCursor_ = 0;
};

class MmeAudioIODeviceType final : public AudioIODeviceType {
public:
    MmeAudioIODeviceType() : AudioIODeviceType("MME (WinMM)") { scanForDevices(); }

    void scanForDevices() override {
        inputNames_.clear();
        outputNames_.clear();
        inputIds_.clear();
        outputIds_.clear();
        const UINT inputCount = waveInGetNumDevs();
        for (UINT index = 0; index < inputCount; ++index) {
            WAVEINCAPSW caps{};
            if (waveInGetDevCapsW(index, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                inputNames_.add(uniqueName(String(caps.szPname), inputNames_));
                inputIds_.push_back(index);
            }
        }
        const UINT outputCount = waveOutGetNumDevs();
        for (UINT index = 0; index < outputCount; ++index) {
            WAVEOUTCAPSW caps{};
            if (waveOutGetDevCapsW(index, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                outputNames_.add(uniqueName(String(caps.szPname), outputNames_));
                outputIds_.push_back(index);
            }
        }
    }

    StringArray getDeviceNames(bool wantInputNames) const override {
        return wantInputNames ? inputNames_ : outputNames_;
    }

    int getDefaultDeviceIndex(bool forInput) const override {
        const auto& names = forInput ? inputNames_ : outputNames_;
        return names.isEmpty() ? -1 : 0;
    }

    int getIndexOfDevice(AudioIODevice* device, bool asInput) const override {
        const auto* mme = dynamic_cast<MmeAudioIODevice*>(device);
        if (mme == nullptr) {
            return -1;
        }
        const auto id = asInput ? mme->inputId() : mme->outputId();
        if (!id.has_value()) {
            return -1;
        }
        const auto& ids = asInput ? inputIds_ : outputIds_;
        const auto found = std::find(ids.begin(), ids.end(), *id);
        return found == ids.end() ? -1 : static_cast<int>(found - ids.begin());
    }

    bool hasSeparateInputsAndOutputs() const override { return true; }

    AudioIODevice* createDevice(
        const String& outputDeviceName,
        const String& inputDeviceName) override {
        const int outputIndex = outputNames_.indexOf(outputDeviceName);
        const int inputIndex = inputNames_.indexOf(inputDeviceName);
        if (outputIndex < 0 && inputIndex < 0) {
            return nullptr;
        }
        const std::optional<UINT> outputId =
            outputIndex >= 0
                ? std::optional<UINT>(outputIds_[static_cast<std::size_t>(outputIndex)])
                : std::nullopt;
        const std::optional<UINT> inputId =
            inputIndex >= 0
                ? std::optional<UINT>(inputIds_[static_cast<std::size_t>(inputIndex)])
                : std::nullopt;
        const auto displayName =
            inputDeviceName.isNotEmpty() && outputDeviceName.isNotEmpty()
                ? inputDeviceName + " / " + outputDeviceName
            : inputDeviceName.isNotEmpty() ? inputDeviceName
                                           : outputDeviceName;
        return new MmeAudioIODevice(displayName, inputId, outputId);
    }

private:
    StringArray inputNames_;
    StringArray outputNames_;
    std::vector<UINT> inputIds_;
    std::vector<UINT> outputIds_;
};

}  // namespace

std::unique_ptr<AudioIODeviceType> createHTFXMMEAudioIODeviceType() {
    return std::make_unique<MmeAudioIODeviceType>();
}

}  // namespace juce

#else

namespace juce {
std::unique_ptr<AudioIODeviceType> createHTFXMMEAudioIODeviceType() {
    return {};
}
}  // namespace juce

#endif
