#pragma once

#include "SpscRing.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class HTDemucsGpuFXAudioProcessor final : public juce::AudioProcessor {
public:
    static constexpr int kSampleRate = 44'100;
    static constexpr int kMaxSources = 6;
    static constexpr int kHopSamples = 257'985;
    static constexpr int kOverlapSamples = 85'995;
    static constexpr int kSegmentSamples = 343'980;
    static constexpr int kProcessingGuardSamples = 22'050;
    static constexpr int kReportedLatencySamples =
        kSegmentSamples + kProcessingGuardSamples;

    enum class OperatingMode : int {
        record = 0,
        realtime = 1,
    };

    enum class SeparationState : int {
        idle = 0,
        recording,
        recorded,
        loading,
        separating,
        previewReady,
        error,
        cancelled,
    };

    enum class QuickExportKind : int {
        vocals = 0,
        accompaniment = 1,
    };

    struct RoformerModel {
        juce::String id;
        juce::String name;
        juce::String category;
        bool audited = false;
        bool experimental = true;
    };

    HTDemucsGpuFXAudioProcessor();
    ~HTDemucsGpuFXAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    [[nodiscard]] const juce::String getName() const override;
    [[nodiscard]] bool acceptsMidi() const override { return false; }
    [[nodiscard]] bool producesMidi() const override { return false; }
    [[nodiscard]] bool isMidiEffect() const override { return false; }
    [[nodiscard]] double getTailLengthSeconds() const override { return 0.0; }

    [[nodiscard]] int getNumPrograms() override { return 1; }
    [[nodiscard]] int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    [[nodiscard]] const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    [[nodiscard]] bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    [[nodiscard]] bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }
    [[nodiscard]] std::uint64_t getInputOverruns() const noexcept {
        return inputOverruns_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t getOutputUnderruns() const noexcept {
        return outputUnderruns_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t getWorkerPid() const noexcept {
        return workerPid_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t getWorkerRestarts() const noexcept {
        return workerRestarts_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t getStreamEpoch() const noexcept {
        return streamEpoch_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t getWorkerProcesses() const noexcept {
        return workerProcesses_.load(std::memory_order_acquire);
    }
    [[nodiscard]] double getLastInferenceMilliseconds() const noexcept {
        return lastInferenceMilliseconds_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t getCudaAllocatedBytes() const noexcept {
        return cudaAllocatedBytes_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t getCudaReservedBytes() const noexcept {
        return cudaReservedBytes_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t getCudaMaxAllocatedBytes() const noexcept {
        return cudaMaxAllocatedBytes_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t getCudaMaxReservedBytes() const noexcept {
        return cudaMaxReservedBytes_.load(std::memory_order_acquire);
    }
    void requestWorkerRecovery() noexcept {
        bridgeRecoveryRequested_.store(true, std::memory_order_release);
    }
    [[nodiscard]] juce::String getBridgeStatusText() const;
    [[nodiscard]] juce::String getRecordStatusText() const;
    [[nodiscard]] juce::String getResolvedDeviceName() const;
    [[nodiscard]] OperatingMode getOperatingMode() const noexcept;
    [[nodiscard]] SeparationState getSeparationState() const noexcept {
        return separationState_.load(std::memory_order_acquire);
    }
    [[nodiscard]] double getSeparationProgress() const noexcept {
        return separationProgress_.load(std::memory_order_acquire);
    }
    [[nodiscard]] double getRecordedSeconds() const noexcept;
    [[nodiscard]] double getPreviewDurationSeconds() const noexcept;
    [[nodiscard]] double getPreviewPositionSeconds() const noexcept;
    [[nodiscard]] bool isPreviewPlaying() const noexcept {
        return previewPlaying_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool hasPreview() const noexcept {
        return previewResult_.load(std::memory_order_acquire) != nullptr;
    }
    [[nodiscard]] bool resolvedToCpu() const noexcept {
        return resolvedBackend_.load(std::memory_order_acquire) == 2;
    }
    [[nodiscard]] int getActiveSourceCount() const noexcept {
        return activeSourceCount_.load(std::memory_order_acquire);
    }
    [[nodiscard]] int getActiveLatencySamples() const noexcept {
        return activeLatencySamples_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool isMediaBusy() const noexcept {
        return mediaBusy_.load(std::memory_order_acquire);
    }
    [[nodiscard]] double getMediaProgress() const noexcept {
        return mediaProgress_.load(std::memory_order_acquire);
    }
    [[nodiscard]] juce::String getMediaStatusText() const;
    [[nodiscard]] juce::String getImportedMediaName() const;
    [[nodiscard]] juce::File getImportedMediaFile() const;
    [[nodiscard]] bool previewUsesModel(const juce::String& modelName) const;
    [[nodiscard]] bool isModelDownloadBusy() const noexcept {
        return modelDownloadBusy_.load(std::memory_order_acquire);
    }
    [[nodiscard]] double getModelDownloadProgress() const noexcept {
        return modelDownloadProgress_.load(std::memory_order_acquire);
    }
    [[nodiscard]] juce::String getModelDownloadStatusText() const;
    [[nodiscard]] bool isModelInstalled(const juce::String& modelName) const;
    [[nodiscard]] bool importedFromVideo() const noexcept {
        return importedVideo_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::vector<RoformerModel> getRoformerModels() const;
    bool selectRoformerModel(const juce::String& modelId);
    void clearRoformerModel();
    [[nodiscard]] juce::String getSelectedRoformerModel() const;
    [[nodiscard]] static juce::String sourceName(int sourceIndex);
    [[nodiscard]] juce::String getStemLabel(int sourceIndex) const;
    [[nodiscard]] static juce::String deriveRoformerStemLabel(
        const juce::File& outputFile, const juce::String& inputStem);

    void applyUserConfiguration();
    bool beginRecording();
    void endRecording();
    bool beginSeparation();
    void cancelSeparation();
    void togglePreviewPlayback() noexcept;
    void stopPreview() noexcept;
    void setPreviewPosition(double normalizedPosition) noexcept;
    bool beginMediaImport(const juce::File& mediaFile);

    // ── Multi-clip support ────────────────────────────────────────────────
    // Importing several files keeps one Clip per file; the "active" clip is
    // the one wired to the preview transport and the mixer parameters, so the
    // existing single-file behaviour is just the one-clip case.
    struct ClipInfo {
        juce::String name;
        bool selected = true;
        bool separated = false;
        juce::String status;
        double seconds = 0.0;
    };

    bool beginMultiMediaImport(const juce::Array<juce::File>& mediaFiles);
    [[nodiscard]] int getClipCount() const;
    [[nodiscard]] int getActiveClipIndex() const noexcept {
        return activeClipIndex_.load(std::memory_order_acquire);
    }
    [[nodiscard]] ClipInfo getClipInfo(int index) const;
    void setActiveClip(int index);
    void setClipSelected(int index, bool selected);
    [[nodiscard]] int getSelectedClipCount() const;
    // Runs separation over every clip in turn (already-separated clips are
    // skipped); each finished clip becomes previewable immediately.
    bool beginBatchSeparation();
    // Exports every selected clip into `folder`, separating any clip that has
    // no result yet, applying that clip's own mixer settings.
    bool beginBatchExport(const juce::File& folder, QuickExportKind kind);
    bool beginStemExport(
        const juce::File& outputDirectory,
        std::vector<int> sourceIndices);
    bool beginQuickExport(
        const juce::File& outputFile,
        QuickExportKind kind);
    bool beginMixExport(const juce::File& outputFile, bool replaceVideoAudio);
    void cancelMediaOperation();
    bool beginModelDownload(const juce::String& modelName);
    void cancelModelDownload();

private:
    struct InputFrame {
        std::uint32_t epoch = 0;
        float left = 0.0f;
        float right = 0.0f;
    };

    struct StemFrame {
        std::uint32_t epoch = 0;
        std::array<float, kMaxSources * 2> samples{};
    };

    struct RecordFrame {
        float left = 0.0f;
        float right = 0.0f;
    };

    struct RuntimeConfiguration {
        std::string modelName{"htdemucs"};
        int sourceCount = 4;
        int segmentSamples = kSegmentSamples;
        int hopSamples = kHopSamples;
        int overlapSamples = kOverlapSamples;
        int backend = 0;  // 0 auto, 1 CUDA, 2 CPU, 3 Apple MPS.
        int gpuIndex = 0;

        [[nodiscard]] bool operator==(const RuntimeConfiguration&) const = default;
    };

    struct SeparationResult {
        int sourceCount = 0;
        std::uint64_t sampleCount = 0;
        std::string modelName;
        std::vector<float> stems;
        std::vector<float> originalLeft;
        std::vector<float> originalRight;
        // Per-source category-correct stem id (e.g. "vocals", "dry", "noise"),
        // aligned by index with `stems`; empty for models with fixed source
        // names (htdemucs), where sourceName()/getStemLabel() fall back.
        std::vector<std::string> stemLabels;
    };

    struct MixSettings {
        std::array<float, kMaxSources> stemGains{};
        float outputTrim = 1.0f;
        bool bypass = false;
    };

    enum class BridgeStatus : std::uint32_t {
        stopped,
        waitingForAudio,
        loading,
        priming,
        processing,
        running,
        recovering,
        error,
        unsupportedSampleRate,
    };

    static constexpr std::size_t kRingCapacity = 1u << 19;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float decibelsToGain(float decibels) noexcept;

    void startBridge();
    void stopBridge();
    void bridgeLoop(std::stop_token stopToken);
    void recordLoop();
    void separationLoop(
        std::stop_token stopToken,
        RuntimeConfiguration configuration,
        std::vector<float> left,
        std::vector<float> right);
    void stopRecordingThread();
    void stopSeparationThread();
    void stopMediaThread();
    void stopModelDownloadThread();
    void modelDownloadLoop(std::stop_token stopToken, juce::String modelName);
    void importMediaLoop(std::stop_token stopToken, juce::File mediaFile);
    void stemExportLoop(
        std::stop_token stopToken,
        juce::File outputDirectory,
        std::vector<int> sourceIndices,
        std::shared_ptr<const SeparationResult> result,
        juce::String baseName);
    void quickExportLoop(
        std::stop_token stopToken,
        juce::File outputFile,
        QuickExportKind kind,
        std::shared_ptr<const SeparationResult> result);
    void mixExportLoop(
        std::stop_token stopToken,
        juce::File outputFile,
        bool replaceVideoAudio,
        std::shared_ptr<const SeparationResult> result,
        MixSettings settings,
        juce::File originalMediaFile);
    [[nodiscard]] RuntimeConfiguration currentRuntimeConfiguration() const;
    void loadRoformerModels();
    [[nodiscard]] MixSettings currentMixSettings() const;
    void setSeparationMessage(const juce::String& message);
    void setMediaMessage(const juce::String& message);
    void setModelDownloadMessage(const juce::String& message);
    void processRecordMode(juce::AudioBuffer<float>& buffer);
    void initialiseSmoothers(double sampleRate);
    void setBridgeMessage(const juce::String& message);
    [[nodiscard]] bool detectTransportDiscontinuity(int blockSamples) noexcept;
    void resetStreamForDiscontinuity() noexcept;

    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, kMaxSources> stemGainParameters_{};
    std::atomic<float>* outputTrimParameter_ = nullptr;
    std::atomic<float>* bypassParameter_ = nullptr;
    std::atomic<float>* gpuIndexParameter_ = nullptr;
    std::atomic<float>* operatingModeParameter_ = nullptr;
    std::atomic<float>* segmentLengthParameter_ = nullptr;
    std::atomic<float>* modelParameter_ = nullptr;
    std::atomic<float>* computeBackendParameter_ = nullptr;
    std::array<juce::SmoothedValue<float>, kMaxSources> stemGains_;
    juce::SmoothedValue<float> outputTrim_;
    juce::SmoothedValue<float> wetMix_;

    htfx::SpscRing<InputFrame, kRingCapacity> inputRing_;
    htfx::SpscRing<StemFrame, kRingCapacity> outputRing_;
    htfx::SpscRing<RecordFrame, kRingCapacity> recordRing_;
    std::jthread bridgeThread_;
    std::jthread recordThread_;
    std::jthread separationThread_;
    std::jthread mediaThread_;
    std::jthread modelDownloadThread_;
    std::atomic<std::uint32_t> streamEpoch_{1};
    std::atomic<BridgeStatus> bridgeStatus_{BridgeStatus::stopped};
    std::atomic<std::uint64_t> inputOverruns_{0};
    std::atomic<std::uint64_t> outputUnderruns_{0};
    std::atomic<std::uint32_t> workerPid_{0};
    std::atomic<std::uint64_t> workerRestarts_{0};
    std::atomic<std::uint64_t> workerProcesses_{0};
    std::atomic<double> lastInferenceMilliseconds_{0.0};
    std::atomic<std::uint64_t> cudaAllocatedBytes_{0};
    std::atomic<std::uint64_t> cudaReservedBytes_{0};
    std::atomic<std::uint64_t> cudaMaxAllocatedBytes_{0};
    std::atomic<std::uint64_t> cudaMaxReservedBytes_{0};
    std::atomic<bool> bridgeRecoveryRequested_{false};
    std::atomic<bool> gpuRestartRequested_{false};
    mutable juce::CriticalSection bridgeMessageLock_;
    juce::String bridgeMessage_;
    mutable juce::CriticalSection separationMessageLock_;
    juce::String separationMessage_;
    mutable juce::CriticalSection mediaMessageLock_;
    juce::String mediaMessage_;
    mutable juce::CriticalSection modelDownloadMessageLock_;
    juce::String modelDownloadMessage_;
    mutable juce::CriticalSection mediaMetadataLock_;
    juce::File importedMediaFile_;
    juce::String importedBaseName_{"recording"};

    std::atomic<bool> prepared_{false};
    std::atomic<bool> realtimeEnabled_{false};
    std::atomic<bool> recording_{false};
    std::atomic<SeparationState> separationState_{SeparationState::idle};
    std::atomic<double> separationProgress_{0.0};
    std::atomic<std::uint64_t> recordedSamples_{0};
    std::atomic<std::uint64_t> recordOverruns_{0};
    std::atomic<double> previewCursor_{0.0};
    std::atomic<bool> previewPlaying_{false};
    std::atomic<double> playbackSampleRate_{static_cast<double>(kSampleRate)};
    std::atomic<bool> mediaBusy_{false};
    std::atomic<double> mediaProgress_{0.0};
    std::atomic<bool> modelDownloadBusy_{false};
    std::atomic<double> modelDownloadProgress_{0.0};
    std::atomic<bool> importedVideo_{false};
    std::atomic<int> resolvedBackend_{0};
    std::atomic<int> activeSourceCount_{4};
    std::atomic<int> activeLatencySamples_{kReportedLatencySamples};
    std::atomic<std::shared_ptr<const SeparationResult>> previewResult_;
    std::vector<float> recordedLeft_;
    std::vector<float> recordedRight_;

    struct Clip {
        juce::File sourceFile;
        juce::String name;
        std::vector<float> left;
        std::vector<float> right;
        std::shared_ptr<const SeparationResult> result;
        bool selected = true;
        bool fromVideo = false;
        juce::String status;
        // Per-clip mixer state: each clip remembers its own fader positions.
        std::array<float, kMaxSources> stemGains{};
        float outputTrim = 0.0f;
    };
    mutable juce::CriticalSection clipsLock_;
    std::vector<Clip> clips_;
    std::atomic<int> activeClipIndex_{-1};
    std::jthread batchThread_;

    void storeActiveClipMixerState();
    void applyClipMixerState(const Clip& clip);
    void activateClipLocked(int index);
    void batchSeparationLoop(std::stop_token stopToken);
    void batchExportLoop(
        std::stop_token stopToken, juce::File folder, QuickExportKind kind);
    bool separateClipBlocking(
        std::stop_token stopToken, Clip& clip, juce::String& error);
    mutable std::mutex runtimeControlMutex_;
    RuntimeConfiguration activeRuntimeConfiguration_{};
    mutable std::mutex roformerMutex_;
    std::vector<RoformerModel> roformerModels_;
    juce::String selectedRoformerModel_;

    juce::AudioBuffer<float> dryDelay_;
    int dryDelayIndex_ = 0;
    int dryWarmupRemaining_ = kReportedLatencySamples;
    int processedDelayRemaining_ = kReportedLatencySamples;
    bool sampleRateSupported_ = false;
    bool haveHostPosition_ = false;
    bool lastTransportPlaying_ = false;
    std::int64_t expectedHostSamplePosition_ = 0;
    int activeGpuIndex_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HTDemucsGpuFXAudioProcessor)
};
