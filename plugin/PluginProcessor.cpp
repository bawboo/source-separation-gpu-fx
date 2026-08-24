#include "PluginProcessor.h"

#include "GpuWorkerClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifndef HTFX_DEFAULT_PYTHON_PATH
#define HTFX_DEFAULT_PYTHON_PATH ""
#endif
#ifndef HTFX_DEFAULT_GPU_WORKER_PATH
#define HTFX_DEFAULT_GPU_WORKER_PATH ""
#endif
#ifndef HTFX_DEFAULT_CHECKPOINT_PATH
#define HTFX_DEFAULT_CHECKPOINT_PATH ""
#endif
#ifndef HTFX_DEFAULT_MODELS_PATH
#define HTFX_DEFAULT_MODELS_PATH ""
#endif
#ifndef HTFX_DEFAULT_FFMPEG_PATH
#define HTFX_DEFAULT_FFMPEG_PATH ""
#endif

namespace {

constexpr std::array<const char*, HTDemucsGpuFXAudioProcessor::kMaxSources>
    kStemParameterIds{
        "drumsGain", "bassGain", "otherGain", "vocalsGain", "guitarGain", "pianoGain"};

constexpr std::array<double, 5> kSegmentSeconds{2.0, 3.0, 4.0, 5.0, 7.8};
constexpr std::array<const char*, 4> kModelNames{
    "htdemucs", "htdemucs_ft", "htdemucs_6s", "hdemucs_mmi"};

juce::NormalisableRange<float> stemGainRange() {
    return {-60.0f, 6.0f, 0.01f, 0.35f};
}

bool environmentFlag(const char* name) {
    return juce::SystemStats::getEnvironmentVariable(name, {}).trim() == "1";
}

std::filesystem::path utf8Path(const juce::String& value) {
    const auto utf8 = value.toUTF8();
    return std::filesystem::path(
        std::u8string(
            reinterpret_cast<const char8_t*>(utf8.getAddress()),
            reinterpret_cast<const char8_t*>(utf8.getAddress()) + utf8.sizeInBytes() - 1));
}

juce::String displayPath(const std::filesystem::path& path) {
#if JUCE_WINDOWS
    return juce::String(path.c_str());
#else
    return juce::String::fromUTF8(path.c_str());
#endif
}

std::vector<juce::File> sidecarRootsForCurrentProcess() {
    const auto executable = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    const auto executableDirectory = executable.getParentDirectory();
    std::vector<juce::File> roots{
        executableDirectory.getChildFile("Resources").getChildFile("sidecar"),
        executableDirectory.getParentDirectory()
            .getChildFile("Resources")
            .getChildFile("sidecar")};
#if JUCE_WINDOWS
    const auto localAppData =
        juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {}).trim();
    if (localAppData.isNotEmpty()) {
        roots.push_back(
            juce::File(localAppData)
                .getChildFile("Programs")
                .getChildFile("HTDemucs GPU FX")
                .getChildFile("Resources")
                .getChildFile("sidecar"));
    }
#endif
    return roots;
}

std::filesystem::path bundledSidecarPath(const char* relativePath) {
    const auto sidecarRoots = sidecarRootsForCurrentProcess();
    for (const auto& root : sidecarRoots) {
        const auto candidate = root.getChildFile(relativePath);
        if (candidate.existsAsFile()) {
            return utf8Path(candidate.getFullPathName());
        }
    }
    return utf8Path(sidecarRoots.front().getChildFile(relativePath).getFullPathName());
}

std::filesystem::path bundledSidecarDirectory(const char* relativePath) {
    const auto sidecarRoots = sidecarRootsForCurrentProcess();
    for (const auto& root : sidecarRoots) {
        const auto candidate = root.getChildFile(relativePath);
        if (candidate.isDirectory()) {
            return utf8Path(candidate.getFullPathName());
        }
    }
    return utf8Path(sidecarRoots.front().getChildFile(relativePath).getFullPathName());
}

juce::File installedDataDirectory() {
    const auto overridePath =
        juce::SystemStats::getEnvironmentVariable("HTFX_DATA_DIR", {}).trim();
    if (overridePath.isNotEmpty()) {
        return juce::File(overridePath);
    }
#if JUCE_WINDOWS
    const auto localAppData =
        juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {}).trim();
    if (localAppData.isNotEmpty()) {
        return juce::File(localAppData).getChildFile("HTDemucs GPU FX");
    }
#endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HTDemucs GPU FX");
}

std::filesystem::path configuredPythonPath() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_PYTHON", {}).trim();
    if (environment.isNotEmpty()) {
        return utf8Path(environment);
    }

    const auto executable = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    const auto executableDirectory = executable.getParentDirectory();
    const auto configFile = executableDirectory.getChildFile("htfx-python.txt");
    if (configFile.existsAsFile()) {
        const auto configured = configFile.loadFileAsString()
                                    .upToFirstOccurrenceOf("\n", false, false)
                                    .trim();
        if (configured.isNotEmpty()) {
            const auto configuredFile = juce::File::isAbsolutePath(configured)
                                            ? juce::File(configured)
                                            : executableDirectory.getChildFile(configured);
            return utf8Path(configuredFile.getFullPathName());
        }
    }

    const auto bundledRuntime = executableDirectory
                                    .getChildFile("Runtime")
#if JUCE_WINDOWS
                                    .getChildFile("python.exe");
#else
                                    .getChildFile("python3");
#endif
    if (bundledRuntime.existsAsFile()) {
        return utf8Path(bundledRuntime.getFullPathName());
    }

    const auto userHome = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const std::array<const char*, 2> commonPythonLocations{
#if JUCE_WINDOWS
        "anaconda3/python.exe", "miniconda3/python.exe"};
#else
        "anaconda3/bin/python3", "miniconda3/bin/python3"};
#endif
    for (const auto* relativePath : commonPythonLocations) {
        const auto candidate = userHome.getChildFile(relativePath);
        if (candidate.existsAsFile()) {
            return utf8Path(candidate.getFullPathName());
        }
    }
    return utf8Path(HTFX_DEFAULT_PYTHON_PATH);
}

std::filesystem::path configuredModelsDirectory() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_MODELS_DIR", {}).trim();
    if (environment.isNotEmpty()) {
        return utf8Path(environment);
    }
    const auto executable = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    const auto portableData = executable.getParentDirectory().getChildFile("PortableData");
    const auto bundled = bundledSidecarDirectory("models");
    if (portableData.isDirectory() && std::filesystem::is_directory(bundled)) {
        return bundled;
    }

    const auto installedModels = installedDataDirectory().getChildFile("Models");
    if (installedModels.getChildFile("model-manifest.json").existsAsFile()) {
        return utf8Path(installedModels.getFullPathName());
    }
    if (std::filesystem::is_directory(bundled)) {
        return bundled;
    }
    return utf8Path(HTFX_DEFAULT_MODELS_PATH);
}

juce::File configuredRoformerManifest() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_ROFORMER_MANIFEST", {}).trim();
    if (environment.isNotEmpty()) {
        return juce::File(environment);
    }
    const auto bundled = displayPath(
        bundledSidecarPath("models/roformer-manifest.json"));
    if (juce::File(bundled).existsAsFile()) {
        return juce::File(bundled);
    }
    return juce::File::getCurrentWorkingDirectory()
        .getChildFile("assets")
        .getChildFile("models")
        .getChildFile("roformer-manifest.json");
}

bool isRoformerModelName(const juce::String& modelName) {
    return modelName.startsWith("melband-roformer-");
}

juce::File configuredRoformerPython() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_ROFORMER_PYTHON", {}).trim();
    return environment.isNotEmpty()
               ? juce::File(environment)
               : juce::File(displayPath(configuredPythonPath()));
}

juce::File configuredRoformerWorker() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_ROFORMER_WORKER", {}).trim();
    return environment.isNotEmpty()
               ? juce::File(environment)
               : juce::File::getCurrentWorkingDirectory()
                     .getChildFile("worker")
                     .getChildFile("roformer_worker.py");
}

juce::File configuredRoformerModelsDirectory() {
    const auto environment = juce::SystemStats::getEnvironmentVariable(
        "HTFX_ROFORMER_MODELS_DIR", {}).trim();
    return environment.isNotEmpty()
               ? juce::File(environment)
               : juce::File::getCurrentWorkingDirectory()
                     .getParentDirectory()
                     .getChildFile("verify")
                     .getChildFile("roformer-cache");
}

juce::File configuredRoformerOutputDirectory() {
    const auto environment = juce::SystemStats::getEnvironmentVariable(
        "HTFX_ROFORMER_OUTPUT_DIR", {}).trim();
    return environment.isNotEmpty()
               ? juce::File(environment)
               : juce::File::getCurrentWorkingDirectory()
                     .getParentDirectory()
                     .getChildFile("verify")
                     .getChildFile("output")
                     .getChildFile("roformer-runtime");
}

std::filesystem::path configuredWorkerExecutable() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_WORKER_EXECUTABLE", {}).trim();
    if (environment.isNotEmpty()) {
        return utf8Path(environment);
    }
    const auto bundled = bundledSidecarPath(
#if JUCE_WINDOWS
        "Runtime/htdemucs-worker/htdemucs-worker.exe");
#else
        "Runtime/htdemucs-worker/htdemucs-worker");
#endif
    return std::filesystem::is_regular_file(bundled) ? bundled : std::filesystem::path{};
}

std::filesystem::path configuredPath(
    const char* environmentName,
    const char* bundledRelativePath,
    const char* fallback) {
    const auto environment = juce::SystemStats::getEnvironmentVariable(environmentName, {});
    if (environment.isNotEmpty()) {
        return utf8Path(environment);
    }
    if (bundledRelativePath != nullptr && bundledRelativePath[0] != '\0') {
        const auto bundled = bundledSidecarPath(bundledRelativePath);
        if (std::filesystem::is_regular_file(bundled)) {
            return bundled;
        }
        if (environmentFlag("HTFX_REQUIRE_BUNDLED_SIDECAR")) {
            return {};
        }
    }
    return utf8Path(fallback);
}

juce::String configuredFfmpegCommand() {
    const auto environment =
        juce::SystemStats::getEnvironmentVariable("HTFX_FFMPEG", {}).trim();
    if (environment.isNotEmpty()) {
        return environment;
    }

    const auto bundled = bundledSidecarPath(
#if JUCE_WINDOWS
        "Runtime/ffmpeg/bin/ffmpeg.exe");
#else
        "Runtime/ffmpeg/bin/ffmpeg");
#endif
    if (std::filesystem::is_regular_file(bundled)) {
        return juce::String(bundled.wstring().c_str());
    }

    const auto executable = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    const auto executableDirectory = executable.getParentDirectory();
    const std::array<juce::File, 3> configFiles{
        executableDirectory.getChildFile("htfx-ffmpeg.txt"),
        executableDirectory.getChildFile("Resources")
            .getChildFile("sidecar")
            .getChildFile("ffmpeg-path.txt"),
        executableDirectory.getParentDirectory()
            .getChildFile("Resources")
            .getChildFile("sidecar")
            .getChildFile("ffmpeg-path.txt")};
    for (const auto& configFile : configFiles) {
        if (!configFile.existsAsFile()) {
            continue;
        }
        auto configured = configFile.loadFileAsString()
                              .upToFirstOccurrenceOf("\n", false, false)
                              .trim();
        if (configured.isEmpty()) {
            continue;
        }
        if (!juce::File::isAbsolutePath(configured)) {
            configured = configFile.getParentDirectory()
                             .getChildFile(configured)
                             .getFullPathName();
        }
        return configured;
    }

    const juce::String fallback{HTFX_DEFAULT_FFMPEG_PATH};
    return fallback.isNotEmpty() ? fallback
#if JUCE_WINDOWS
                                 : "ffmpeg.exe";
#else
                                 : "ffmpeg";
#endif
}

bool runFfmpeg(
    const juce::StringArray& arguments,
    std::stop_token stopToken,
    juce::String& diagnostics) {
    juce::StringArray command;
    command.add(configuredFfmpegCommand());
    command.addArray(arguments);

    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut |
                                    juce::ChildProcess::wantStdErr)) {
        diagnostics = "FFmpeg could not be started. The portable FFmpeg runtime is missing or invalid.";
        return false;
    }

    juce::MemoryOutputStream captured;
    std::array<char, 4096> outputBuffer{};
    while (process.isRunning()) {
        if (stopToken.stop_requested()) {
            process.kill();
            diagnostics = "Media operation cancelled";
            return false;
        }
        const int bytesRead = process.readProcessOutput(
            outputBuffer.data(), static_cast<int>(outputBuffer.size()));
        if (bytesRead > 0) {
            captured.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    for (;;) {
        const int bytesRead = process.readProcessOutput(
            outputBuffer.data(), static_cast<int>(outputBuffer.size()));
        if (bytesRead <= 0) {
            break;
        }
        captured.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
    }
    diagnostics = juce::String::fromUTF8(
                      static_cast<const char*>(captured.getData()),
                      static_cast<int>(captured.getDataSize()))
                      .trim();
    const int exitCode = process.getExitCode();
    if (exitCode != 0) {
        if (diagnostics.length() > 1800) {
            diagnostics = diagnostics.substring(diagnostics.length() - 1800);
        }
        diagnostics = "FFmpeg failed (exit " + juce::String(exitCode) + "): " +
                      diagnostics;
        return false;
    }
    return true;
}

bool hasVideoExtension(const juce::File& file) {
    const auto extension = file.getFileExtension().toLowerCase();
    static constexpr std::array<const char*, 8> extensions{
        ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".wmv", ".mpeg"};
    return std::ranges::any_of(
        extensions,
        [&extension](const char* candidate) { return extension == candidate; });
}

bool readAudioFileAtProjectRate(
    const juce::File& file,
    std::vector<float>& left,
    std::vector<float>& right,
    juce::String& error) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(file));
    if (reader == nullptr) {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return false;
    }
    if (reader->lengthInSamples <= 0 ||
        reader->lengthInSamples > std::numeric_limits<int>::max()) {
        error = "The media duration is empty or too large to hold in memory";
        return false;
    }

    const int sourceSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> source(2, sourceSamples);
    if (!reader->read(&source, 0, sourceSamples, 0, true, true)) {
        error = "The audio stream could not be decoded";
        return false;
    }

    const auto sourceRate = reader->sampleRate;
    if (!(sourceRate > 0.0)) {
        error = "The audio stream has no valid sample rate";
        return false;
    }
    const auto targetSamples64 = static_cast<std::int64_t>(std::llround(
        static_cast<double>(sourceSamples) *
        HTDemucsGpuFXAudioProcessor::kSampleRate / sourceRate));
    if (targetSamples64 <= 0 ||
        targetSamples64 > std::numeric_limits<int>::max()) {
        error = "The resampled audio is too large to hold in memory";
        return false;
    }

    const auto targetSamples = static_cast<std::size_t>(targetSamples64);
    left.resize(targetSamples);
    right.resize(targetSamples);
    const auto* sourceLeft = source.getReadPointer(0);
    const auto* sourceRight = source.getReadPointer(1);
    const double sourceStep =
        sourceRate / HTDemucsGpuFXAudioProcessor::kSampleRate;
    for (std::size_t sample = 0; sample < targetSamples; ++sample) {
        const double sourcePosition = static_cast<double>(sample) * sourceStep;
        const auto first = static_cast<int>(sourcePosition);
        const auto second = (std::min)(first + 1, sourceSamples - 1);
        const float fraction = static_cast<float>(sourcePosition - first);
        const int safeFirst = (std::min)(first, sourceSamples - 1);
        left[sample] = juce::jmap(
            fraction, sourceLeft[safeFirst], sourceLeft[second]);
        right[sample] = juce::jmap(
            fraction, sourceRight[safeFirst], sourceRight[second]);
    }
    return true;
}

bool decodeMediaWithFfmpeg(
    const juce::File& input,
    std::stop_token stopToken,
    std::vector<float>& left,
    std::vector<float>& right,
    juce::String& error) {
    auto temporary = juce::File::createTempFile(".wav");
    temporary.deleteFile();
    const juce::StringArray arguments{
        "-hide_banner", "-loglevel", "error", "-y", "-i",
        input.getFullPathName(), "-vn", "-ac", "2", "-ar", "44100",
        "-c:a", "pcm_f32le", temporary.getFullPathName()};
    const bool converted = runFfmpeg(arguments, stopToken, error);
    if (!converted) {
        temporary.deleteFile();
        return false;
    }
    const bool decoded = readAudioFileAtProjectRate(temporary, left, right, error);
    temporary.deleteFile();
    return decoded;
}

bool writeFloatWav(
    const juce::File& output,
    const float* left,
    const float* right,
    std::size_t sampleCount,
    juce::String& error) {
    if (sampleCount == 0 || left == nullptr || right == nullptr) {
        error = "There is no audio to export";
        return false;
    }
    if (!output.getParentDirectory().createDirectory()) {
        error = "Could not create output folder: " +
                output.getParentDirectory().getFullPathName();
        return false;
    }
    const auto temporaryOutput = output.getParentDirectory().getNonexistentChildFile(
        output.getFileNameWithoutExtension() + ".htfx-part",
        output.getFileExtension(),
        false);

    std::unique_ptr<juce::OutputStream> stream = temporaryOutput.createOutputStream();
    if (stream == nullptr) {
        error = "Could not open output file: " + output.getFullPathName();
        return false;
    }
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(
                                 HTDemucsGpuFXAudioProcessor::kSampleRate)
                             .withChannelLayout(juce::AudioChannelSet::stereo())
                             .withBitsPerSample(32)
                             .withSampleFormat(
                                 juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) {
        error = "Could not create a 32-bit float WAV writer";
        return false;
    }

    constexpr std::size_t blockSize = 1u << 20;
    for (std::size_t offset = 0; offset < sampleCount; offset += blockSize) {
        const auto count = static_cast<int>(
            (std::min)(blockSize, sampleCount - offset));
        const std::array<const float*, 2> channels{left + offset, right + offset};
        if (!writer->writeFromFloatArrays(channels.data(), 2, count)) {
            error = "Writing the WAV file failed";
            writer.reset();
            temporaryOutput.deleteFile();
            return false;
        }
    }
    writer.reset();
    if (output.existsAsFile() && !output.deleteFile()) {
        temporaryOutput.deleteFile();
        error = "Could not replace existing file: " + output.getFullPathName();
        return false;
    }
    if (!temporaryOutput.moveFileTo(output)) {
        temporaryOutput.deleteFile();
        error = "Could not commit output file: " + output.getFullPathName();
        return false;
    }
    return true;
}

juce::String legalMediaBaseName(const juce::File& file) {
    auto base = juce::File::createLegalFileName(file.getFileNameWithoutExtension());
    return base.isNotEmpty() ? base : "htdemucs";
}

}  // namespace

HTDemucsGpuFXAudioProcessor::HTDemucsGpuFXAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "HTDemucsGpuFXState", createParameterLayout()) {
    loadRoformerModels();
    for (std::size_t index = 0; index < stemGainParameters_.size(); ++index) {
        stemGainParameters_[index] = parameters_.getRawParameterValue(kStemParameterIds[index]);
        jassert(stemGainParameters_[index] != nullptr);
    }
    outputTrimParameter_ = parameters_.getRawParameterValue("outputTrim");
    bypassParameter_ = parameters_.getRawParameterValue("bypass");
    gpuIndexParameter_ = parameters_.getRawParameterValue("gpuIndex");
    operatingModeParameter_ = parameters_.getRawParameterValue("operatingMode");
    segmentLengthParameter_ = parameters_.getRawParameterValue("segmentLength");
    modelParameter_ = parameters_.getRawParameterValue("model");
    computeBackendParameter_ = parameters_.getRawParameterValue("computeBackend");
    jassert(
        outputTrimParameter_ != nullptr && bypassParameter_ != nullptr &&
        gpuIndexParameter_ != nullptr && operatingModeParameter_ != nullptr &&
        segmentLengthParameter_ != nullptr && modelParameter_ != nullptr &&
        computeBackendParameter_ != nullptr);

    // New standalone and plug-in instances are record-first. A host restoring
    // an older saved state can still select the realtime mode parameter.
    activeRuntimeConfiguration_ = currentRuntimeConfiguration();
    const auto initialLatency = getOperatingMode() == OperatingMode::realtime
                                    ? activeRuntimeConfiguration_.segmentSamples +
                                          kProcessingGuardSamples
                                    : 0;
    activeLatencySamples_.store(initialLatency, std::memory_order_relaxed);
    setLatencySamples(initialLatency);
}

HTDemucsGpuFXAudioProcessor::~HTDemucsGpuFXAudioProcessor() {
    recording_.store(false, std::memory_order_release);
    stopRecordingThread();
    stopMediaThread();
    stopModelDownloadThread();
    stopSeparationThread();
    stopBridge();
}

const juce::String HTDemucsGpuFXAudioProcessor::getName() const {
    return "HTDemucs GPU FX";
}

juce::AudioProcessorValueTreeState::ParameterLayout
HTDemucsGpuFXAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bassGain", 1}, "Bass", stemGainRange(), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drumsGain", 1}, "Drums", stemGainRange(), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"vocalsGain", 1}, "Vocals", stemGainRange(), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"otherGain", 1}, "Other", stemGainRange(), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"guitarGain", 1}, "Guitar", stemGainRange(), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pianoGain", 1}, "Piano", stemGainRange(), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputTrim", 1},
        "Output Trim",
        juce::NormalisableRange<float>{-24.0f, 6.0f, 0.01f},
        0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"bypass", 1}, "Bypass", false));
    parameters.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"gpuIndex", 1}, "GPU Index", 0, 7, 0));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"operatingMode", 1},
        "Mode",
        juce::StringArray{"Record mode", "Realtime mode (Ultra high latency)"},
        0));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"segmentLength", 1},
        "Inference window",
        juce::StringArray{"2 seconds", "3 seconds", "4 seconds", "5 seconds", "7.8 seconds"},
        4));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"model", 1},
        "Model",
        juce::StringArray{"htdemucs", "htdemucs_ft", "htdemucs_6s", "hdemucs_mmi"},
        0));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"computeBackend", 1},
        "Compute",
        juce::StringArray{
            "Auto (GPU, otherwise CPU)", "NVIDIA CUDA", "CPU", "Apple Metal (MPS)"},
        0));
    return {parameters.begin(), parameters.end()};
}

float HTDemucsGpuFXAudioProcessor::decibelsToGain(float decibels) noexcept {
    return decibels <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain(decibels);
}

HTDemucsGpuFXAudioProcessor::OperatingMode
HTDemucsGpuFXAudioProcessor::getOperatingMode() const noexcept {
    return operatingModeParameter_->load(std::memory_order_relaxed) >= 0.5f
               ? OperatingMode::realtime
               : OperatingMode::record;
}

HTDemucsGpuFXAudioProcessor::RuntimeConfiguration
HTDemucsGpuFXAudioProcessor::currentRuntimeConfiguration() const {
    RuntimeConfiguration configuration;
    const int segmentIndex = std::clamp(
        static_cast<int>(std::lround(
            segmentLengthParameter_->load(std::memory_order_relaxed))),
        0,
        static_cast<int>(kSegmentSeconds.size()) - 1);
    const int modelIndex = std::clamp(
        static_cast<int>(std::lround(
            modelParameter_->load(std::memory_order_relaxed))),
        0,
        static_cast<int>(kModelNames.size()) - 1);
    configuration.modelName = kModelNames[static_cast<std::size_t>(modelIndex)];
    configuration.sourceCount = modelIndex == 2 ? 6 : 4;
    {
        const std::scoped_lock lock(roformerMutex_);
        if (selectedRoformerModel_.isNotEmpty()) {
            configuration.modelName = selectedRoformerModel_.toStdString();
            configuration.sourceCount = 2;
        }
    }
    configuration.segmentSamples = static_cast<int>(std::lround(
        kSegmentSeconds[static_cast<std::size_t>(segmentIndex)] * kSampleRate));
    configuration.hopSamples = configuration.segmentSamples * 3 / 4;
    configuration.overlapSamples =
        configuration.segmentSamples - configuration.hopSamples;
    configuration.backend = std::clamp(
        static_cast<int>(std::lround(
            computeBackendParameter_->load(std::memory_order_relaxed))),
        0,
        3);
    configuration.gpuIndex = std::clamp(
        static_cast<int>(std::lround(
            gpuIndexParameter_->load(std::memory_order_relaxed))),
        0,
        7);
    return configuration;
}

void HTDemucsGpuFXAudioProcessor::loadRoformerModels() {
    std::vector<RoformerModel> loaded;
    const auto manifest = configuredRoformerManifest();
    const auto parsed = juce::JSON::parse(manifest.loadFileAsString());
    const auto* root = parsed.getDynamicObject();
    if (root != nullptr) {
        if (const auto* models = root->getProperty("models").getArray()) {
            loaded.reserve(static_cast<std::size_t>(models->size()));
            for (const auto& value : *models) {
                const auto* object = value.getDynamicObject();
                if (object == nullptr) {
                    continue;
                }
                RoformerModel model;
                model.id = object->getProperty("id").toString();
                model.name = object->getProperty("name").toString();
                model.category = object->getProperty("category").toString();
                model.audited = static_cast<bool>(object->getProperty("audited"));
                model.experimental =
                    static_cast<bool>(object->getProperty("experimental"));
                if (model.id.isNotEmpty()) {
                    loaded.push_back(std::move(model));
                }
            }
        }
    }
    const std::scoped_lock lock(roformerMutex_);
    roformerModels_ = std::move(loaded);
}

std::vector<HTDemucsGpuFXAudioProcessor::RoformerModel>
HTDemucsGpuFXAudioProcessor::getRoformerModels() const {
    const std::scoped_lock lock(roformerMutex_);
    return roformerModels_;
}

bool HTDemucsGpuFXAudioProcessor::selectRoformerModel(
    const juce::String& modelId) {
    const std::scoped_lock lock(roformerMutex_);
    const auto found = std::find_if(
        roformerModels_.begin(),
        roformerModels_.end(),
        [&modelId](const RoformerModel& model) { return model.id == modelId; });
    if (found == roformerModels_.end()) {
        return false;
    }
    selectedRoformerModel_ = found->id;
    return true;
}

juce::String HTDemucsGpuFXAudioProcessor::getSelectedRoformerModel() const {
    const std::scoped_lock lock(roformerMutex_);
    return selectedRoformerModel_;
}

double HTDemucsGpuFXAudioProcessor::getRecordedSeconds() const noexcept {
    return static_cast<double>(recordedSamples_.load(std::memory_order_acquire)) /
           static_cast<double>(kSampleRate);
}

double HTDemucsGpuFXAudioProcessor::getPreviewDurationSeconds() const noexcept {
    const auto result = previewResult_.load(std::memory_order_acquire);
    return result == nullptr
               ? 0.0
               : static_cast<double>(result->sampleCount) /
                     static_cast<double>(kSampleRate);
}

double HTDemucsGpuFXAudioProcessor::getPreviewPositionSeconds() const noexcept {
    return previewCursor_.load(std::memory_order_acquire) /
           static_cast<double>(kSampleRate);
}

void HTDemucsGpuFXAudioProcessor::applyUserConfiguration() {
    std::scoped_lock control(runtimeControlMutex_);
    realtimeEnabled_.store(false, std::memory_order_release);
    stopBridge();
    inputRing_.clearWhenStopped();
    outputRing_.clearWhenStopped();
    streamEpoch_.fetch_add(1, std::memory_order_acq_rel);

    activeRuntimeConfiguration_ = currentRuntimeConfiguration();
    activeGpuIndex_ = activeRuntimeConfiguration_.gpuIndex;
    activeSourceCount_.store(
        activeRuntimeConfiguration_.sourceCount, std::memory_order_release);
    const int latency = getOperatingMode() == OperatingMode::realtime
                            ? activeRuntimeConfiguration_.segmentSamples +
                                  kProcessingGuardSamples
                            : 0;
    activeLatencySamples_.store(latency, std::memory_order_release);
    setLatencySamples(latency);
    dryDelayIndex_ = 0;
    dryWarmupRemaining_ = latency;
    processedDelayRemaining_ = latency;

    if (prepared_.load(std::memory_order_acquire) && sampleRateSupported_ &&
        getOperatingMode() == OperatingMode::realtime) {
        startBridge();
        realtimeEnabled_.store(true, std::memory_order_release);
    }
}

void HTDemucsGpuFXAudioProcessor::initialiseSmoothers(double sampleRate) {
    for (std::size_t index = 0; index < stemGains_.size(); ++index) {
        stemGains_[index].reset(sampleRate, 0.02);
        stemGains_[index].setCurrentAndTargetValue(
            decibelsToGain(stemGainParameters_[index]->load(std::memory_order_relaxed)));
    }
    outputTrim_.reset(sampleRate, 0.02);
    outputTrim_.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(
            outputTrimParameter_->load(std::memory_order_relaxed)));
    wetMix_.reset(sampleRate, 0.02);
    wetMix_.setCurrentAndTargetValue(
        bypassParameter_->load(std::memory_order_relaxed) >= 0.5f ? 0.0f : 1.0f);
}

void HTDemucsGpuFXAudioProcessor::prepareToPlay(
    double sampleRate, int /*maximumExpectedSamplesPerBlock*/) {
    playbackSampleRate_.store(
        sampleRate > 0.0 ? sampleRate : static_cast<double>(kSampleRate),
        std::memory_order_release);
    prepared_.store(false, std::memory_order_release);
    recording_.store(false, std::memory_order_release);
    stopRecordingThread();
    realtimeEnabled_.store(false, std::memory_order_release);
    stopBridge();
    inputRing_.clearWhenStopped();
    outputRing_.clearWhenStopped();
    recordRing_.clearWhenStopped();
    streamEpoch_.fetch_add(1, std::memory_order_relaxed);
    inputOverruns_.store(0, std::memory_order_relaxed);
    outputUnderruns_.store(0, std::memory_order_relaxed);
    workerPid_.store(0, std::memory_order_relaxed);
    workerRestarts_.store(0, std::memory_order_relaxed);
    workerProcesses_.store(0, std::memory_order_relaxed);
    lastInferenceMilliseconds_.store(0.0, std::memory_order_relaxed);
    cudaAllocatedBytes_.store(0, std::memory_order_relaxed);
    cudaReservedBytes_.store(0, std::memory_order_relaxed);
    cudaMaxAllocatedBytes_.store(0, std::memory_order_relaxed);
    cudaMaxReservedBytes_.store(0, std::memory_order_relaxed);
    bridgeRecoveryRequested_.store(false, std::memory_order_relaxed);
    gpuRestartRequested_.store(false, std::memory_order_relaxed);
    haveHostPosition_ = false;
    lastTransportPlaying_ = false;
    expectedHostSamplePosition_ = 0;
    activeRuntimeConfiguration_ = currentRuntimeConfiguration();
    activeGpuIndex_ = activeRuntimeConfiguration_.gpuIndex;
    activeSourceCount_.store(
        activeRuntimeConfiguration_.sourceCount, std::memory_order_relaxed);

    dryDelay_.setSize(2, kReportedLatencySamples, false, true, false);
    dryDelay_.clear();
    dryDelayIndex_ = 0;
    const int latency = getOperatingMode() == OperatingMode::realtime
                            ? activeRuntimeConfiguration_.segmentSamples +
                                  kProcessingGuardSamples
                            : 0;
    activeLatencySamples_.store(latency, std::memory_order_relaxed);
    dryWarmupRemaining_ = latency;
    processedDelayRemaining_ = latency;
    initialiseSmoothers(sampleRate);
    setLatencySamples(latency);

    sampleRateSupported_ = std::abs(sampleRate - static_cast<double>(kSampleRate)) < 0.5;
    prepared_.store(true, std::memory_order_release);
    if (sampleRateSupported_ && getOperatingMode() == OperatingMode::realtime) {
        startBridge();
        realtimeEnabled_.store(true, std::memory_order_release);
    } else {
        bridgeStatus_.store(
            sampleRateSupported_ ? BridgeStatus::stopped
                                 : BridgeStatus::unsupportedSampleRate,
            std::memory_order_release);
    }
}

void HTDemucsGpuFXAudioProcessor::releaseResources() {
    prepared_.store(false, std::memory_order_release);
    recording_.store(false, std::memory_order_release);
    stopRecordingThread();
    realtimeEnabled_.store(false, std::memory_order_release);
    stopBridge();
}

bool HTDemucsGpuFXAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void HTDemucsGpuFXAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    jassert(buffer.getNumChannels() >= 2);

    for (std::size_t index = 0; index < stemGains_.size(); ++index) {
        stemGains_[index].setTargetValue(
            decibelsToGain(stemGainParameters_[index]->load(std::memory_order_relaxed)));
    }
    outputTrim_.setTargetValue(juce::Decibels::decibelsToGain(
        outputTrimParameter_->load(std::memory_order_relaxed)));
    const bool bypassed = bypassParameter_->load(std::memory_order_relaxed) >= 0.5f;
    wetMix_.setTargetValue(bypassed ? 0.0f : 1.0f);

    if (getOperatingMode() == OperatingMode::record) {
        processRecordMode(buffer);
        return;
    }

    bool resetRequested = bridgeRecoveryRequested_.exchange(
        false, std::memory_order_acq_rel);
    resetRequested = detectTransportDiscontinuity(buffer.getNumSamples()) || resetRequested;
    const int requestedGpuIndex = std::clamp(
        static_cast<int>(std::lround(
            gpuIndexParameter_->load(std::memory_order_relaxed))),
        0,
        7);
    if (requestedGpuIndex != activeGpuIndex_) {
        activeGpuIndex_ = requestedGpuIndex;
        gpuRestartRequested_.store(true, std::memory_order_release);
        resetRequested = true;
    }
    if (resetRequested) {
        resetStreamForDiscontinuity();
    }

    const auto epoch = streamEpoch_.load(std::memory_order_relaxed);
    const int latency = activeLatencySamples_.load(std::memory_order_acquire);
    const int sourceCount = activeSourceCount_.load(std::memory_order_acquire);
    const bool realtimeActive = realtimeEnabled_.load(std::memory_order_acquire);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const float inputLeft = left[sample];
        const float inputRight = right[sample];

        float dryLeft = dryDelay_.getSample(0, dryDelayIndex_);
        float dryRight = dryDelay_.getSample(1, dryDelayIndex_);
        dryDelay_.setSample(0, dryDelayIndex_, inputLeft);
        dryDelay_.setSample(1, dryDelayIndex_, inputRight);
        if (++dryDelayIndex_ == latency) {
            dryDelayIndex_ = 0;
        }
        if (dryWarmupRemaining_ > 0) {
            --dryWarmupRemaining_;
            dryLeft = 0.0f;
            dryRight = 0.0f;
        }

        if (sampleRateSupported_ && realtimeActive) {
            const InputFrame frame{epoch, inputLeft, inputRight};
            if (!inputRing_.tryPush(frame)) {
                inputOverruns_.fetch_add(1, std::memory_order_relaxed);
                bridgeRecoveryRequested_.store(true, std::memory_order_release);
            }
        }

        StemFrame processed;
        bool hasProcessed = false;
        if (processedDelayRemaining_ > 0) {
            --processedDelayRemaining_;
        } else if (realtimeActive && outputRing_.tryPop(processed)) {
            hasProcessed = processed.epoch == epoch;
        } else if (sampleRateSupported_ && realtimeActive) {
            outputUnderruns_.fetch_add(1, std::memory_order_relaxed);
            bridgeRecoveryRequested_.store(true, std::memory_order_release);
        }

        std::array<float, kMaxSources> gains{};
        for (std::size_t index = 0; index < gains.size(); ++index) {
            gains[index] = stemGains_[index].getNextValue();
        }
        const float trim = outputTrim_.getNextValue();
        const float wet = wetMix_.getNextValue();
        if (hasProcessed) {
            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;
            for (int source = 0; source < sourceCount; ++source) {
                mixedLeft += processed.samples[source * 2] * gains[source];
                mixedRight += processed.samples[source * 2 + 1] * gains[source];
            }
            left[sample] = (mixedLeft * wet + dryLeft * (1.0f - wet)) * trim;
            right[sample] = (mixedRight * wet + dryRight * (1.0f - wet)) * trim;
        } else {
            left[sample] = dryLeft * trim;
            right[sample] = dryRight * trim;
        }
    }
}

bool HTDemucsGpuFXAudioProcessor::detectTransportDiscontinuity(
    int blockSamples) noexcept {
    const auto* hostPlayHead = getPlayHead();
    if (hostPlayHead == nullptr) {
        haveHostPosition_ = false;
        return false;
    }
    const auto position = hostPlayHead->getPosition();
    if (!position.hasValue()) {
        haveHostPosition_ = false;
        return false;
    }
    const auto samplePosition = position->getTimeInSamples();
    if (!samplePosition.hasValue()) {
        haveHostPosition_ = false;
        return false;
    }
    const bool playing = position->getIsPlaying();
    bool discontinuity = false;
    if (haveHostPosition_) {
        if (playing &&
            (!lastTransportPlaying_ ||
             *samplePosition != expectedHostSamplePosition_)) {
            discontinuity = true;
        } else if (!playing && lastTransportPlaying_) {
            discontinuity = true;
        }
    }
    haveHostPosition_ = true;
    lastTransportPlaying_ = playing;
    expectedHostSamplePosition_ =
        *samplePosition + (playing ? static_cast<std::int64_t>(blockSamples) : 0);
    return discontinuity;
}

void HTDemucsGpuFXAudioProcessor::resetStreamForDiscontinuity() noexcept {
    streamEpoch_.fetch_add(1, std::memory_order_acq_rel);
    outputRing_.discardAllByConsumer();
    const int latency = activeLatencySamples_.load(std::memory_order_acquire);
    dryWarmupRemaining_ = latency;
    processedDelayRemaining_ = latency;
}

bool HTDemucsGpuFXAudioProcessor::beginRecording() {
    if (!sampleRateSupported_ || getOperatingMode() != OperatingMode::record) {
        setSeparationMessage(
            sampleRateSupported_ ? "Switch to Record mode first"
                                 : "Recording requires 44,100 Hz");
        separationState_.store(SeparationState::error, std::memory_order_release);
        return false;
    }
    const auto state = separationState_.load(std::memory_order_acquire);
    if (state == SeparationState::loading || state == SeparationState::separating) {
        return false;
    }

    recording_.store(false, std::memory_order_release);
    stopRecordingThread();
    stopMediaThread();
    stopSeparationThread();
    recordRing_.clearWhenStopped();
    recordedLeft_.clear();
    recordedRight_.clear();
    recordedSamples_.store(0, std::memory_order_relaxed);
    recordOverruns_.store(0, std::memory_order_relaxed);
    previewPlaying_.store(false, std::memory_order_release);
    previewCursor_.store(0, std::memory_order_release);
    previewResult_.store(
        std::shared_ptr<const SeparationResult>{}, std::memory_order_release);
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        importedMediaFile_ = {};
        importedBaseName_ = "recording";
    }
    importedVideo_.store(false, std::memory_order_release);
    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage({});
    separationProgress_.store(0.0, std::memory_order_release);
    separationState_.store(SeparationState::recording, std::memory_order_release);
    setSeparationMessage("Recording stereo input");
    recording_.store(true, std::memory_order_release);
    recordThread_ = std::jthread([this] { recordLoop(); });
    return true;
}

void HTDemucsGpuFXAudioProcessor::endRecording() {
    if (!recording_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    stopRecordingThread();
    const auto samples = recordedSamples_.load(std::memory_order_acquire);
    separationState_.store(
        samples == 0 ? SeparationState::idle : SeparationState::recorded,
        std::memory_order_release);
    setSeparationMessage(
        samples == 0
            ? "No input was recorded"
            : "Recorded " + juce::String(
                  static_cast<double>(samples) / kSampleRate, 1) +
                  " seconds · press Separate");
}

void HTDemucsGpuFXAudioProcessor::recordLoop() {
    while (recording_.load(std::memory_order_acquire) ||
           recordRing_.availableToRead() > 0) {
        bool consumed = false;
        RecordFrame frame;
        while (recordRing_.tryPop(frame)) {
            recordedLeft_.push_back(frame.left);
            recordedRight_.push_back(frame.right);
            recordedSamples_.fetch_add(1, std::memory_order_release);
            consumed = true;
        }
        if (!consumed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void HTDemucsGpuFXAudioProcessor::stopRecordingThread() {
    if (recordThread_.joinable()) {
        recordThread_.join();
    }
}

bool HTDemucsGpuFXAudioProcessor::beginSeparation() {
    if (getOperatingMode() != OperatingMode::record) {
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        setSeparationMessage("Wait for the media operation to finish");
        return false;
    }
    const auto configuration = currentRuntimeConfiguration();
    if (!isModelInstalled(configuration.modelName)) {
        separationState_.store(SeparationState::error, std::memory_order_release);
        setSeparationMessage(
            "Model " + juce::String(configuration.modelName) +
            " is not installed. Open Advanced options and download it first.");
        return false;
    }
    endRecording();
    if (recordedLeft_.empty() || recordedLeft_.size() != recordedRight_.size()) {
        separationState_.store(SeparationState::error, std::memory_order_release);
        setSeparationMessage("Record some stereo audio before separating");
        return false;
    }
    stopSeparationThread();
    setMediaMessage({});
    previewPlaying_.store(false, std::memory_order_release);
    previewCursor_.store(0, std::memory_order_release);
    previewResult_.store(
        std::shared_ptr<const SeparationResult>{}, std::memory_order_release);
    // JUCE uses a negative progress value for its indeterminate animation.
    // Model and CUDA initialization cannot report a truthful percentage.
    separationProgress_.store(-1.0, std::memory_order_release);
    separationState_.store(SeparationState::loading, std::memory_order_release);
    setSeparationMessage("Starting Demucs worker · waiting for model load");
    auto left = recordedLeft_;
    auto right = recordedRight_;
    separationThread_ = std::jthread(
        [this,
         configuration,
         left = std::move(left),
         right = std::move(right)](std::stop_token stopToken) mutable {
            separationLoop(
                stopToken,
                configuration,
                std::move(left),
                std::move(right));
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::cancelSeparation() {
    if (separationThread_.joinable()) {
        separationThread_.request_stop();
        separationState_.store(SeparationState::cancelled, std::memory_order_release);
        setSeparationMessage("Cancelling after the current inference block");
    }
}

void HTDemucsGpuFXAudioProcessor::stopSeparationThread() {
    if (separationThread_.joinable()) {
        separationThread_.request_stop();
        separationThread_.join();
    }
}

void HTDemucsGpuFXAudioProcessor::separationLoop(
    std::stop_token stopToken,
    RuntimeConfiguration configuration,
    std::vector<float> left,
    std::vector<float> right) {
    try {
        if (environmentFlag("HTFX_USE_FAKE_WORKER")) {
            constexpr std::array<float, kMaxSources> fakeSourceGains{
                0.4f, 0.25f, 0.2f, 0.15f, 0.0f, 0.0f};
            auto result = std::make_shared<SeparationResult>();
            result->sourceCount = configuration.sourceCount;
            result->sampleCount = left.size();
            result->modelName = configuration.modelName;
            result->stems.assign(
                static_cast<std::size_t>(configuration.sourceCount) * 2 *
                    left.size(),
                0.0f);
            for (std::size_t sample = 0; sample < left.size(); ++sample) {
                if (stopToken.stop_requested()) {
                    separationState_.store(
                        SeparationState::cancelled, std::memory_order_release);
                    setSeparationMessage("Separation cancelled");
                    return;
                }
                for (int source = 0; source < configuration.sourceCount; ++source) {
                    const auto leftPlane = static_cast<std::size_t>(source) * 2;
                    const auto rightPlane = leftPlane + 1;
                    result->stems[leftPlane * left.size() + sample] =
                        left[sample] * fakeSourceGains[static_cast<std::size_t>(source)];
                    result->stems[rightPlane * left.size() + sample] =
                        right[sample] * fakeSourceGains[static_cast<std::size_t>(source)];
                }
            }
            result->originalLeft = std::move(left);
            result->originalRight = std::move(right);
            activeSourceCount_.store(
                configuration.sourceCount, std::memory_order_release);
            resolvedBackend_.store(1, std::memory_order_release);
            previewResult_.store(
                std::shared_ptr<const SeparationResult>(std::move(result)),
                std::memory_order_release);
            previewCursor_.store(0, std::memory_order_release);
            previewPlaying_.store(false, std::memory_order_release);
            separationProgress_.store(1.0, std::memory_order_release);
            separationState_.store(
                SeparationState::previewReady, std::memory_order_release);
            setSeparationMessage("Ready to preview - fake worker");
            return;
        }

        if (isRoformerModelName(juce::String::fromUTF8(configuration.modelName.c_str()))) {
            const auto python = configuredRoformerPython();
            const auto workerScript = configuredRoformerWorker();
            const auto modelsDirectory = configuredRoformerModelsDirectory();
            auto workingDirectory = configuredRoformerOutputDirectory()
                                        .getNonexistentChildFile(
                                            "cpp-route-" + juce::Uuid().toString(),
                                            {}, false);
            const auto inputFile = workingDirectory.getChildFile("input.wav");
            const auto outputDirectory = workingDirectory.getChildFile("stems");
            if (!python.existsAsFile() || !workerScript.existsAsFile() ||
                !modelsDirectory.isDirectory() ||
                !workingDirectory.createDirectory() ||
                !outputDirectory.createDirectory()) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(
                    "RoFormer Python, worker, model cache, or output directory is unavailable");
                return;
            }

            juce::String error;
            if (!writeFloatWav(
                    inputFile, left.data(), right.data(), left.size(), error)) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(error);
                return;
            }

            const juce::String device =
                configuration.backend == 2
                    ? "cpu"
                    : configuration.backend == 3
                          ? "mps"
                          : configuration.backend == 1
                                ? "cuda:" + juce::String(configuration.gpuIndex)
                                : "auto";
            const juce::StringArray command{
                python.getFullPathName(),
                workerScript.getFullPathName(),
                "--input", inputFile.getFullPathName(),
                "--output-dir", outputDirectory.getFullPathName(),
                "--model", juce::String::fromUTF8(configuration.modelName.c_str()),
                "--models-dir", modelsDirectory.getFullPathName(),
                "--device", device};
            juce::ChildProcess process;
            setSeparationMessage(
                "Loading RoFormer " +
                juce::String::fromUTF8(configuration.modelName.c_str()) +
                " · " + device);
            if (!process.start(
                    command,
                    juce::ChildProcess::wantStdOut |
                        juce::ChildProcess::wantStdErr)) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage("Could not start the RoFormer Python worker");
                return;
            }

            const auto startedAt = juce::Time::getMillisecondCounterHiRes();
            juce::MemoryOutputStream diagnostics;
            std::array<char, 4096> outputBuffer{};
            separationState_.store(SeparationState::separating, std::memory_order_release);
            separationProgress_.store(-1.0, std::memory_order_release);
            while (process.isRunning()) {
                if (stopToken.stop_requested()) {
                    process.kill();
                    separationState_.store(
                        SeparationState::cancelled, std::memory_order_release);
                    setSeparationMessage("RoFormer separation cancelled");
                    return;
                }
                const int bytesRead = process.readProcessOutput(
                    outputBuffer.data(), static_cast<int>(outputBuffer.size()));
                if (bytesRead > 0) {
                    diagnostics.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            for (;;) {
                const int bytesRead = process.readProcessOutput(
                    outputBuffer.data(), static_cast<int>(outputBuffer.size()));
                if (bytesRead <= 0) {
                    break;
                }
                diagnostics.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
            }
            if (process.getExitCode() != 0) {
                auto message = juce::String::fromUTF8(
                    static_cast<const char*>(diagnostics.getData()),
                    static_cast<int>(diagnostics.getDataSize())).trim();
                if (message.length() > 1800) {
                    message = message.substring(message.length() - 1800);
                }
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(
                    "RoFormer worker failed (exit " +
                    juce::String(process.getExitCode()) + "): " + message);
                return;
            }

            juce::Array<juce::File> outputFiles;
            outputDirectory.findChildFiles(
                outputFiles, juce::File::findFiles, false, "*.wav");
            if (outputFiles.size() != 2) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(
                    "RoFormer worker returned " + juce::String(outputFiles.size()) +
                    " WAV files; expected two stems");
                return;
            }

            auto result = std::make_shared<SeparationResult>();
            result->sourceCount = 2;
            result->sampleCount = left.size();
            result->modelName = configuration.modelName;
            result->stems.assign(4 * left.size(), 0.0f);
            const auto inputStem = inputFile.getFileNameWithoutExtension();
            for (int source = 0; source < outputFiles.size(); ++source) {
                const auto& outputFile = outputFiles.getReference(source);
                std::vector<float> stemLeft;
                std::vector<float> stemRight;
                if (!readAudioFileAtProjectRate(
                        outputFile, stemLeft, stemRight, error) ||
                    stemLeft.size() != left.size() || stemRight.size() != left.size()) {
                    separationState_.store(
                        SeparationState::error, std::memory_order_release);
                    setSeparationMessage(
                        error.isNotEmpty() ? error
                                           : "RoFormer stem duration does not match the input");
                    return;
                }
                const auto leftPlane = static_cast<std::size_t>(source) * 2;
                const auto rightPlane = leftPlane + 1;
                std::copy(stemLeft.begin(), stemLeft.end(),
                          result->stems.begin() + leftPlane * left.size());
                std::copy(stemRight.begin(), stemRight.end(),
                          result->stems.begin() + rightPlane * left.size());
                // The upstream worker names each file "<input>_<output_id>.wav",
                // where output_id is the model's own category-correct stem name
                // (vocals/instrumental, dry/reverb, dry/noise, ...); deriving the
                // label from that filename -- rather than assuming a fixed
                // Demucs-style source order -- keeps naming correct regardless
                // of which two stems a given category produces or what order the
                // filesystem enumerates them in.
                result->stemLabels.push_back(
                    deriveRoformerStemLabel(outputFile, inputStem).toStdString());
            }
            result->originalLeft = std::move(left);
            result->originalRight = std::move(right);
            activeSourceCount_.store(2, std::memory_order_release);
            resolvedBackend_.store(
                configuration.backend == 2 ? 2 : 1, std::memory_order_release);
            workerProcesses_.store(1, std::memory_order_release);
            lastInferenceMilliseconds_.store(
                juce::Time::getMillisecondCounterHiRes() - startedAt,
                std::memory_order_release);
            previewResult_.store(
                std::shared_ptr<const SeparationResult>(std::move(result)),
                std::memory_order_release);
            previewCursor_.store(0.0, std::memory_order_release);
            previewPlaying_.store(false, std::memory_order_release);
            separationProgress_.store(1.0, std::memory_order_release);
            separationState_.store(
                SeparationState::previewReady, std::memory_order_release);
            setSeparationMessage(
                "Ready to preview · " +
                juce::String::fromUTF8(configuration.modelName.c_str()) +
                " · two stems");
            return;
        }

        htfx::GpuWorkerClient worker;
        htfx::GpuWorkerConfig workerConfig;
        workerConfig.workerExecutable = configuredWorkerExecutable();
        workerConfig.pythonExecutable = configuredPythonPath();
        workerConfig.workerScript = configuredPath(
            "HTFX_GPU_WORKER",
            "worker/gpu_ipc_worker.py",
            HTFX_DEFAULT_GPU_WORKER_PATH);
        workerConfig.modelsDirectory = configuredModelsDirectory();
        workerConfig.modelName = configuration.modelName;
        workerConfig.sourceCount = static_cast<std::uint32_t>(configuration.sourceCount);
        workerConfig.segmentFrames = static_cast<std::uint32_t>(configuration.segmentSamples);
        workerConfig.hopFrames = static_cast<std::uint32_t>(configuration.hopSamples);
        workerConfig.gpuIndex = static_cast<std::uint32_t>(configuration.gpuIndex);
        workerConfig.backend = configuration.backend == 1
                                   ? htfx::WorkerBackend::cuda
                               : configuration.backend == 2
                                   ? htfx::WorkerBackend::cpu
                               : configuration.backend == 3
                                   ? htfx::WorkerBackend::mps
                                   : htfx::WorkerBackend::autoSelect;
        workerConfig.readyTimeout = std::chrono::minutes(15);
        workerConfig.processTimeout = std::chrono::minutes(30);

        const auto requestedDevice =
            configuration.backend == 2
                ? juce::String{"CPU"}
                : configuration.backend == 3 ? juce::String{"MPS"}
                                             : juce::String{"CUDA GPU"};
        setSeparationMessage(
            "Loading " + juce::String(configuration.modelName) + " on " +
            requestedDevice + " · first load can take a while");
        if (!worker.start(workerConfig, 1)) {
            separationState_.store(SeparationState::error, std::memory_order_release);
            setSeparationMessage(juce::String::fromUTF8(worker.lastError().c_str()));
            return;
        }
        workerPid_.store(worker.workerPid(), std::memory_order_release);
        const int resolved = worker.resolvedBackend() == htfx::WorkerBackend::cpu
                                 ? 2
                             : worker.resolvedBackend() == htfx::WorkerBackend::mps
                                 ? 3
                                 : 1;
        resolvedBackend_.store(resolved, std::memory_order_release);
        activeSourceCount_.store(
            static_cast<int>(worker.activeSourceCount()),
            std::memory_order_release);
        separationState_.store(SeparationState::separating, std::memory_order_release);
        setSeparationMessage(
            juce::String::fromUTF8(worker.gpuName().c_str()) + " · " +
            juce::String(configuration.modelName) +
            (resolved == 2 ? " · CPU inference may take a long time" : ""));

        const std::size_t sampleCount = left.size();
        const std::size_t hopSamples = static_cast<std::size_t>(configuration.hopSamples);
        const std::size_t overlapSamples =
            static_cast<std::size_t>(configuration.overlapSamples);
        const std::size_t realHops = (sampleCount + hopSamples - 1) / hopSamples;
        const std::size_t totalHops = realHops + 1;  // Flush the OLA tail.
        separationProgress_.store(0.05, std::memory_order_release);
        setSeparationMessage(
            "Separating · " + juce::String(resolved == 2 ? "CPU" : "GPU") +
            " · block 0/" + juce::String(totalHops));
        auto result = std::make_shared<SeparationResult>();
        result->sourceCount = configuration.sourceCount;
        result->sampleCount = sampleCount;
        result->modelName = configuration.modelName;
        result->stems.assign(
            static_cast<std::size_t>(configuration.sourceCount) * 2 * sampleCount,
            0.0f);
        std::vector<float> input(2 * hopSamples, 0.0f);

        for (std::size_t hopIndex = 0; hopIndex < totalHops; ++hopIndex) {
            if (stopToken.stop_requested()) {
                worker.stop();
                workerPid_.store(0, std::memory_order_release);
                separationState_.store(SeparationState::cancelled, std::memory_order_release);
                setSeparationMessage("Separation cancelled");
                return;
            }
            std::fill(input.begin(), input.end(), 0.0f);
            if (hopIndex < realHops) {
                const std::size_t offset = hopIndex * hopSamples;
                const std::size_t available = (std::min)(hopSamples, sampleCount - offset);
                std::copy_n(left.data() + offset, available, input.data());
                std::copy_n(
                    right.data() + offset,
                    available,
                    input.data() + hopSamples);
            }

            const float* workerOutput = nullptr;
            double elapsedMilliseconds = 0.0;
            if (!worker.process(
                    1,
                    input.data(),
                    static_cast<std::uint32_t>(hopSamples),
                    workerOutput,
                    &elapsedMilliseconds)) {
                worker.stop();
                workerPid_.store(0, std::memory_order_release);
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(juce::String::fromUTF8(worker.lastError().c_str()));
                return;
            }

            for (std::size_t sample = 0; sample < hopSamples; ++sample) {
                const std::size_t streamSample = hopIndex * hopSamples + sample;
                if (streamSample < overlapSamples ||
                    streamSample >= overlapSamples + sampleCount) {
                    continue;
                }
                const std::size_t destinationSample = streamSample - overlapSamples;
                for (int source = 0; source < configuration.sourceCount; ++source) {
                    for (std::size_t channel = 0; channel < 2; ++channel) {
                        const std::size_t plane =
                            static_cast<std::size_t>(source) * 2 + channel;
                        result->stems[plane * sampleCount + destinationSample] =
                            workerOutput[
                                plane * htfx::GpuWorkerClient::kMaxFrames + sample];
                    }
                }
            }
            lastInferenceMilliseconds_.store(
                elapsedMilliseconds, std::memory_order_release);
            cudaAllocatedBytes_.store(
                worker.cudaAllocatedBytes(), std::memory_order_release);
            cudaReservedBytes_.store(
                worker.cudaReservedBytes(), std::memory_order_release);
            cudaMaxAllocatedBytes_.store(
                worker.cudaMaxAllocatedBytes(), std::memory_order_release);
            cudaMaxReservedBytes_.store(
                worker.cudaMaxReservedBytes(), std::memory_order_release);
            workerProcesses_.store(hopIndex + 1, std::memory_order_release);
            separationProgress_.store(
                0.05 + 0.95 *
                           (static_cast<double>(hopIndex + 1) /
                            static_cast<double>(totalHops)),
                std::memory_order_release);
            setSeparationMessage(
                "Separating · " + juce::String(resolved == 2 ? "CPU" : "GPU") +
                " · block " + juce::String(hopIndex + 1) + "/" +
                juce::String(totalHops));
        }

        worker.stop();
        workerPid_.store(0, std::memory_order_release);
        result->originalLeft = std::move(left);
        result->originalRight = std::move(right);
        previewResult_.store(
            std::shared_ptr<const SeparationResult>(std::move(result)),
            std::memory_order_release);
        previewCursor_.store(0, std::memory_order_release);
        previewPlaying_.store(false, std::memory_order_release);
        separationProgress_.store(1.0, std::memory_order_release);
        separationState_.store(SeparationState::previewReady, std::memory_order_release);
        setSeparationMessage(
            "Ready to preview · " + juce::String(configuration.modelName) + " · " +
            (resolved == 2 ? "CPU" : "GPU"));
    } catch (const std::exception& error) {
        workerPid_.store(0, std::memory_order_release);
        separationState_.store(SeparationState::error, std::memory_order_release);
        setSeparationMessage(juce::String::fromUTF8(error.what()));
    }
}

void HTDemucsGpuFXAudioProcessor::processRecordMode(
    juce::AudioBuffer<float>& buffer) {
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    auto result = previewResult_.load(std::memory_order_acquire);
    double cursor = previewCursor_.load(std::memory_order_relaxed);
    bool playing = previewPlaying_.load(std::memory_order_acquire) && result != nullptr;
    const bool capturing = recording_.load(std::memory_order_acquire);
    const double playbackSampleRate =
        playbackSampleRate_.load(std::memory_order_acquire);
    const double previewStep =
        playbackSampleRate > 0.0
            ? static_cast<double>(kSampleRate) / playbackSampleRate
            : 1.0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const float inputLeft = left[sample];
        const float inputRight = right[sample];
        if (capturing && !recordRing_.tryPush({inputLeft, inputRight})) {
            recordOverruns_.fetch_add(1, std::memory_order_relaxed);
        }

        std::array<float, kMaxSources> gains{};
        for (std::size_t source = 0; source < gains.size(); ++source) {
            gains[source] = stemGains_[source].getNextValue();
        }
        const float trim = outputTrim_.getNextValue();
        const float wet = wetMix_.getNextValue();
        if (playing && result->sampleCount > 0 &&
            cursor < static_cast<double>(result->sampleCount)) {
            const auto first = (std::min)(
                static_cast<std::size_t>(cursor),
                result->sampleCount - 1);
            const auto second = (std::min)(first + 1, result->sampleCount - 1);
            const float fraction =
                static_cast<float>(cursor - static_cast<double>(first));
            const auto interpolate =
                [first, second, fraction](
                    const std::vector<float>& samples,
                    std::size_t planeOffset) noexcept {
                    const float firstValue = samples[planeOffset + first];
                    const float secondValue = samples[planeOffset + second];
                    return firstValue +
                           fraction * (secondValue - firstValue);
                };
            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;
            for (int source = 0; source < result->sourceCount; ++source) {
                const auto leftPlane = static_cast<std::size_t>(source) * 2;
                const auto rightPlane = leftPlane + 1;
                mixedLeft += interpolate(
                                 result->stems,
                                 leftPlane * result->sampleCount) *
                             gains[static_cast<std::size_t>(source)];
                mixedRight += interpolate(
                                  result->stems,
                                  rightPlane * result->sampleCount) *
                              gains[static_cast<std::size_t>(source)];
            }
            const float dryLeft = interpolate(result->originalLeft, 0);
            const float dryRight = interpolate(result->originalRight, 0);
            left[sample] = (mixedLeft * wet + dryLeft * (1.0f - wet)) * trim;
            right[sample] = (mixedRight * wet + dryRight * (1.0f - wet)) * trim;
            cursor += previewStep;
            if (cursor + 1.0e-7 >=
                static_cast<double>(result->sampleCount)) {
                cursor = static_cast<double>(result->sampleCount);
                playing = false;
                previewPlaying_.store(false, std::memory_order_release);
            }
        } else {
            left[sample] = inputLeft * trim;
            right[sample] = inputRight * trim;
        }
    }
    previewCursor_.store(cursor, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::togglePreviewPlayback() noexcept {
    const auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        return;
    }
    if (previewCursor_.load(std::memory_order_acquire) >=
        static_cast<double>(result->sampleCount)) {
        previewCursor_.store(0.0, std::memory_order_release);
    }
    previewPlaying_.store(
        !previewPlaying_.load(std::memory_order_acquire),
        std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::stopPreview() noexcept {
    previewPlaying_.store(false, std::memory_order_release);
    previewCursor_.store(0.0, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::setPreviewPosition(
    double normalizedPosition) noexcept {
    const auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        return;
    }
    const auto clamped = std::clamp(normalizedPosition, 0.0, 1.0);
    previewCursor_.store(
        clamped * static_cast<double>(result->sampleCount),
        std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::setSeparationMessage(
    const juce::String& message) {
    const juce::ScopedLock lock(separationMessageLock_);
    separationMessage_ = message;
}

juce::String HTDemucsGpuFXAudioProcessor::sourceName(int sourceIndex) {
    static constexpr std::array<const char*, kMaxSources> names{
        "Drums", "Bass", "Other", "Vocals", "Guitar", "Piano"};
    return sourceIndex >= 0 && sourceIndex < kMaxSources
               ? juce::String(names[static_cast<std::size_t>(sourceIndex)])
               : "Stem " + juce::String(sourceIndex + 1);
}

juce::String HTDemucsGpuFXAudioProcessor::deriveRoformerStemLabel(
    const juce::File& outputFile, const juce::String& inputStem) {
    auto label = outputFile.getFileNameWithoutExtension();
    const auto prefix = inputStem + "_";
    if (label.startsWithIgnoreCase(prefix)) {
        label = label.substring(prefix.length());
    }
    return label.toLowerCase();
}

juce::String HTDemucsGpuFXAudioProcessor::getStemLabel(int sourceIndex) const {
    const auto result = previewResult_.load(std::memory_order_acquire);
    if (result != nullptr && sourceIndex >= 0 &&
        sourceIndex < static_cast<int>(result->stemLabels.size()) &&
        !result->stemLabels[static_cast<std::size_t>(sourceIndex)].empty()) {
        const auto label = juce::String::fromUTF8(
            result->stemLabels[static_cast<std::size_t>(sourceIndex)].c_str());
        return label.substring(0, 1).toUpperCase() + label.substring(1);
    }
    return sourceName(sourceIndex);
}

juce::String HTDemucsGpuFXAudioProcessor::getMediaStatusText() const {
    const juce::ScopedLock lock(mediaMessageLock_);
    return mediaMessage_;
}

juce::String HTDemucsGpuFXAudioProcessor::getImportedMediaName() const {
    const juce::ScopedLock lock(mediaMetadataLock_);
    return importedMediaFile_.existsAsFile() ? importedMediaFile_.getFileName()
                                             : juce::String{};
}

juce::File HTDemucsGpuFXAudioProcessor::getImportedMediaFile() const {
    const juce::ScopedLock lock(mediaMetadataLock_);
    return importedMediaFile_;
}

bool HTDemucsGpuFXAudioProcessor::previewUsesModel(
    const juce::String& modelName) const {
    const auto result = previewResult_.load(std::memory_order_acquire);
    return result != nullptr &&
           juce::String::fromUTF8(result->modelName.c_str()) == modelName;
}

void HTDemucsGpuFXAudioProcessor::setMediaMessage(const juce::String& message) {
    const juce::ScopedLock lock(mediaMessageLock_);
    mediaMessage_ = message;
}

void HTDemucsGpuFXAudioProcessor::stopMediaThread() {
    if (mediaThread_.joinable()) {
        mediaThread_.request_stop();
        mediaThread_.join();
    }
    mediaBusy_.store(false, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::cancelMediaOperation() {
    if (mediaThread_.joinable() && mediaBusy_.load(std::memory_order_acquire)) {
        mediaThread_.request_stop();
        setMediaMessage("Cancelling media operation");
    }
}

juce::String HTDemucsGpuFXAudioProcessor::getModelDownloadStatusText() const {
    const juce::ScopedLock lock(modelDownloadMessageLock_);
    return modelDownloadMessage_;
}

void HTDemucsGpuFXAudioProcessor::setModelDownloadMessage(
    const juce::String& message) {
    const juce::ScopedLock lock(modelDownloadMessageLock_);
    modelDownloadMessage_ = message;
}

bool HTDemucsGpuFXAudioProcessor::isModelInstalled(
    const juce::String& modelName) const {
    if (isRoformerModelName(modelName)) {
        const auto modelDirectory =
            configuredRoformerModelsDirectory().getChildFile(modelName);
        return modelDirectory.isDirectory() &&
               modelDirectory.getNumberOfChildFiles(
                   juce::File::findFiles, "*.ckpt") > 0;
    }
    const auto modelsPath = configuredModelsDirectory();
    if (modelsPath.empty()) {
        return false;
    }
    const auto modelsDirectory = juce::File(displayPath(modelsPath));
    const auto manifestFile = modelsDirectory.getChildFile("model-manifest.json");
    if (!manifestFile.existsAsFile()) {
        return false;
    }

    const auto registry = juce::JSON::parse(manifestFile.loadFileAsString());
    const auto* registryObject = registry.getDynamicObject();
    if (registryObject == nullptr) {
        return false;
    }
    const auto* modelsObject =
        registryObject->getProperty("models").getDynamicObject();
    const auto* artifactsObject =
        registryObject->getProperty("artifacts").getDynamicObject();
    if (modelsObject == nullptr || artifactsObject == nullptr) {
        return false;
    }
    const auto* modelObject =
        modelsObject->getProperty(juce::Identifier(modelName)).getDynamicObject();
    if (modelObject == nullptr) {
        return false;
    }
    const auto files = modelObject->getProperty("files");
    const auto* fileArray = files.getArray();
    if (fileArray == nullptr || fileArray->isEmpty()) {
        return false;
    }
    for (const auto& fileValue : *fileArray) {
        const auto fileName = fileValue.toString();
        if (fileName.isEmpty() || fileName.containsAnyOf("/\\")) {
            return false;
        }
        const auto* artifact =
            artifactsObject->getProperty(juce::Identifier(fileName)).getDynamicObject();
        if (artifact == nullptr) {
            return false;
        }
        const auto expectedBytes =
            artifact->getProperty("bytes").toString().getLargeIntValue();
        const auto modelFile = modelsDirectory.getChildFile(fileName);
        if (expectedBytes <= 0 || !modelFile.existsAsFile() ||
            modelFile.getSize() != expectedBytes) {
            return false;
        }
    }
    return true;
}

void HTDemucsGpuFXAudioProcessor::stopModelDownloadThread() {
    if (modelDownloadThread_.joinable()) {
        modelDownloadThread_.request_stop();
        modelDownloadThread_.join();
    }
    modelDownloadBusy_.store(false, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::cancelModelDownload() {
    if (modelDownloadThread_.joinable() &&
        modelDownloadBusy_.load(std::memory_order_acquire)) {
        modelDownloadThread_.request_stop();
        setModelDownloadMessage("Cancelling model download");
    }
}

bool HTDemucsGpuFXAudioProcessor::beginModelDownload(
    const juce::String& modelName) {
    if (modelDownloadBusy_.load(std::memory_order_acquire)) {
        return false;
    }
    if (isModelInstalled(modelName)) {
        modelDownloadProgress_.store(1.0, std::memory_order_release);
        setModelDownloadMessage(modelName + " is already installed");
        return true;
    }
    const auto worker = configuredWorkerExecutable();
    const auto models = configuredModelsDirectory();
    if (worker.empty() || !std::filesystem::is_regular_file(worker)) {
        setModelDownloadMessage(
            "The bundled model downloader is missing. Repair the installation.");
        return false;
    }
    if (models.empty() ||
        !juce::File(displayPath(models))
             .getChildFile("model-manifest.json")
             .existsAsFile()) {
        setModelDownloadMessage(
            "The model download manifest is missing. Repair the installation.");
        return false;
    }

    stopModelDownloadThread();
    bool expected = false;
    if (!modelDownloadBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    modelDownloadProgress_.store(0.0, std::memory_order_release);
    setModelDownloadMessage("Preparing to download " + modelName);
    modelDownloadThread_ = std::jthread(
        [this, modelName](std::stop_token stopToken) {
            try {
                modelDownloadLoop(stopToken, modelName);
            } catch (const std::exception& exception) {
                setModelDownloadMessage(
                    "Model download failed: " +
                    juce::String::fromUTF8(exception.what()));
                modelDownloadBusy_.store(false, std::memory_order_release);
            } catch (...) {
                setModelDownloadMessage("Model download failed unexpectedly");
                modelDownloadBusy_.store(false, std::memory_order_release);
            }
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::modelDownloadLoop(
    std::stop_token stopToken,
    juce::String modelName) {
    const auto worker = configuredWorkerExecutable();
    const auto models = configuredModelsDirectory();
    auto statusDirectory = installedDataDirectory().getChildFile("Downloads");
    if (!statusDirectory.createDirectory() && !statusDirectory.isDirectory()) {
        setModelDownloadMessage("Could not create the model download status directory");
        modelDownloadBusy_.store(false, std::memory_order_release);
        return;
    }
    const auto statusFile = statusDirectory.getChildFile(
        "model-" + modelName.retainCharacters(
                         "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") +
        ".json");
    statusFile.deleteFile();

    juce::StringArray command{
        displayPath(worker),
        "--install-model",
        modelName,
        "--models-dir",
        displayPath(models),
        "--status-json",
        statusFile.getFullPathName()};
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut |
                                    juce::ChildProcess::wantStdErr)) {
        setModelDownloadMessage("Could not start the bundled model downloader");
        modelDownloadBusy_.store(false, std::memory_order_release);
        return;
    }

    juce::MemoryOutputStream captured;
    std::array<char, 4096> outputBuffer{};
    while (process.isRunning()) {
        if (stopToken.stop_requested()) {
            process.kill();
            setModelDownloadMessage("Model download cancelled");
            modelDownloadBusy_.store(false, std::memory_order_release);
            return;
        }
        const int bytesRead = process.readProcessOutput(
            outputBuffer.data(), static_cast<int>(outputBuffer.size()));
        if (bytesRead > 0) {
            captured.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
        }
        if (statusFile.existsAsFile()) {
            const auto status = juce::JSON::parse(statusFile.loadFileAsString());
            if (const auto* statusObject = status.getDynamicObject()) {
                const auto completed = statusObject->getProperty("completed_bytes")
                                           .toString()
                                           .getLargeIntValue();
                const auto total = statusObject->getProperty("total_bytes")
                                       .toString()
                                       .getLargeIntValue();
                if (total > 0) {
                    const auto progress = std::clamp(
                        static_cast<double>(completed) / static_cast<double>(total),
                        0.0,
                        1.0);
                    modelDownloadProgress_.store(progress, std::memory_order_release);
                    const auto fileName = statusObject->getProperty("file").toString();
                    setModelDownloadMessage(
                        "Downloading " + modelName +
                        (fileName.isNotEmpty() ? " - " + fileName : juce::String{}) +
                        " (" + juce::String(juce::roundToInt(progress * 100.0)) + "%)");
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (;;) {
        const int bytesRead = process.readProcessOutput(
            outputBuffer.data(), static_cast<int>(outputBuffer.size()));
        if (bytesRead <= 0) {
            break;
        }
        captured.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
    }

    const int exitCode = process.getExitCode();
    if (exitCode == 0 && isModelInstalled(modelName)) {
        modelDownloadProgress_.store(1.0, std::memory_order_release);
        setModelDownloadMessage(modelName + " installed and ready");
    } else {
        auto diagnostics = juce::String::fromUTF8(
                               static_cast<const char*>(captured.getData()),
                               static_cast<int>(captured.getDataSize()))
                               .trim();
        if (diagnostics.length() > 1000) {
            diagnostics = diagnostics.substring(diagnostics.length() - 1000);
        }
        setModelDownloadMessage(
            "Model download failed (exit " + juce::String(exitCode) + ")" +
            (diagnostics.isNotEmpty() ? ": " + diagnostics : juce::String{}));
    }
    modelDownloadBusy_.store(false, std::memory_order_release);
}

bool HTDemucsGpuFXAudioProcessor::beginMediaImport(const juce::File& mediaFile) {
    if (getOperatingMode() != OperatingMode::record) {
        setMediaMessage("Switch to Record mode before importing media");
        return false;
    }
    if (!mediaFile.existsAsFile()) {
        setMediaMessage("The selected media file does not exist");
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        return false;
    }
    stopMediaThread();
    bool expected = false;
    if (!mediaBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    recording_.store(false, std::memory_order_release);
    stopRecordingThread();
    stopSeparationThread();
    recordRing_.clearWhenStopped();
    recordedLeft_.clear();
    recordedRight_.clear();
    recordedSamples_.store(0, std::memory_order_release);
    previewPlaying_.store(false, std::memory_order_release);
    previewCursor_.store(0, std::memory_order_release);
    previewResult_.store(
        std::shared_ptr<const SeparationResult>{}, std::memory_order_release);
    separationProgress_.store(0.0, std::memory_order_release);
    separationState_.store(SeparationState::loading, std::memory_order_release);
    mediaProgress_.store(0.01, std::memory_order_release);
    importedVideo_.store(hasVideoExtension(mediaFile), std::memory_order_release);
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        importedMediaFile_ = mediaFile;
        importedBaseName_ = legalMediaBaseName(mediaFile);
    }
    setMediaMessage("Importing " + mediaFile.getFileName());
    setSeparationMessage("Decoding imported media");
    mediaThread_ = std::jthread(
        [this, mediaFile](std::stop_token stopToken) {
            importMediaLoop(stopToken, mediaFile);
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::importMediaLoop(
    std::stop_token stopToken,
    juce::File mediaFile) {
    try {
        std::vector<float> left;
        std::vector<float> right;
        juce::String ffmpegError;
        const bool video = hasVideoExtension(mediaFile);
        mediaProgress_.store(0.08, std::memory_order_release);
        bool decoded = decodeMediaWithFfmpeg(
            mediaFile, stopToken, left, right, ffmpegError);
        if (!decoded && !video && !stopToken.stop_requested()) {
            juce::String fallbackError;
            decoded = readAudioFileAtProjectRate(
                mediaFile, left, right, fallbackError);
            if (!decoded) {
                ffmpegError += " | JUCE fallback: " + fallbackError;
            }
        }
        if (stopToken.stop_requested()) {
            separationState_.store(
                SeparationState::cancelled, std::memory_order_release);
            setSeparationMessage("Media import cancelled");
            setMediaMessage("Media import cancelled");
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        if (!decoded || left.empty() || left.size() != right.size()) {
            separationState_.store(SeparationState::error, std::memory_order_release);
            setSeparationMessage(ffmpegError);
            setMediaMessage(ffmpegError);
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }

        mediaProgress_.store(0.9, std::memory_order_release);
        recordedLeft_ = std::move(left);
        recordedRight_ = std::move(right);
        recordedSamples_.store(recordedLeft_.size(), std::memory_order_release);
        separationState_.store(SeparationState::recorded, std::memory_order_release);
        const auto duration = static_cast<double>(recordedLeft_.size()) / kSampleRate;
        const auto message =
            "Imported " + mediaFile.getFileName() + " (" +
            juce::String(duration, 1) + " s) - press Separate";
        setSeparationMessage(message);
        setMediaMessage(message);
        mediaProgress_.store(1.0, std::memory_order_release);
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        const auto message =
            "Media import failed: " + juce::String::fromUTF8(exception.what());
        separationState_.store(SeparationState::error, std::memory_order_release);
        setSeparationMessage(message);
        setMediaMessage(message);
        mediaBusy_.store(false, std::memory_order_release);
    }
}

HTDemucsGpuFXAudioProcessor::MixSettings
HTDemucsGpuFXAudioProcessor::currentMixSettings() const {
    MixSettings settings;
    for (std::size_t source = 0; source < settings.stemGains.size(); ++source) {
        settings.stemGains[source] = decibelsToGain(
            stemGainParameters_[source]->load(std::memory_order_relaxed));
    }
    settings.outputTrim = decibelsToGain(
        outputTrimParameter_->load(std::memory_order_relaxed));
    settings.bypass = bypassParameter_->load(std::memory_order_relaxed) >= 0.5f;
    return settings;
}

bool HTDemucsGpuFXAudioProcessor::beginStemExport(
    const juce::File& outputDirectory,
    std::vector<int> sourceIndices) {
    auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        setMediaMessage("Separate some audio before exporting stems");
        return false;
    }
    std::erase_if(sourceIndices, [result](int source) {
        return source < 0 || source >= result->sourceCount;
    });
    std::ranges::sort(sourceIndices);
    sourceIndices.erase(
        std::unique(sourceIndices.begin(), sourceIndices.end()),
        sourceIndices.end());
    if (sourceIndices.empty()) {
        setMediaMessage("Select at least one stem to export");
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        return false;
    }
    stopMediaThread();
    bool expected = false;
    if (!mediaBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    juce::String baseName;
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        baseName = importedBaseName_;
    }
    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage("Exporting original-volume stems");
    mediaThread_ = std::jthread(
        [this,
         outputDirectory,
         sourceIndices = std::move(sourceIndices),
         result = std::move(result),
         baseName](std::stop_token stopToken) mutable {
            stemExportLoop(
                stopToken,
                outputDirectory,
                std::move(sourceIndices),
                std::move(result),
                baseName);
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::stemExportLoop(
    std::stop_token stopToken,
    juce::File outputDirectory,
    std::vector<int> sourceIndices,
    std::shared_ptr<const SeparationResult> result,
    juce::String baseName) {
    try {
        juce::String error;
        for (std::size_t item = 0; item < sourceIndices.size(); ++item) {
            if (stopToken.stop_requested()) {
                setMediaMessage("Stem export cancelled");
                mediaBusy_.store(false, std::memory_order_release);
                return;
            }
            const int source = sourceIndices[item];
            const auto leftPlane = static_cast<std::size_t>(source) * 2;
            const auto rightPlane = leftPlane + 1;
            const auto label =
                source >= 0 &&
                        source < static_cast<int>(result->stemLabels.size()) &&
                        !result->stemLabels[static_cast<std::size_t>(source)].empty()
                    ? juce::String::fromUTF8(
                          result->stemLabels[static_cast<std::size_t>(source)].c_str())
                    : sourceName(source).toLowerCase();
            const auto output = outputDirectory.getChildFile(
                baseName + "_" + label + ".wav");
            if (!writeFloatWav(
                    output,
                    result->stems.data() + leftPlane * result->sampleCount,
                    result->stems.data() + rightPlane * result->sampleCount,
                    static_cast<std::size_t>(result->sampleCount),
                    error)) {
                setMediaMessage(error);
                mediaBusy_.store(false, std::memory_order_release);
                return;
            }
            mediaProgress_.store(
                static_cast<double>(item + 1) / sourceIndices.size(),
                std::memory_order_release);
        }
        setMediaMessage(
            "Exported " + juce::String(sourceIndices.size()) +
            " original-volume stem WAV file(s) to " +
            outputDirectory.getFullPathName());
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        setMediaMessage(
            "Stem export failed: " + juce::String::fromUTF8(exception.what()));
        mediaBusy_.store(false, std::memory_order_release);
    }
}

bool HTDemucsGpuFXAudioProcessor::beginQuickExport(
    const juce::File& requestedOutputFile,
    QuickExportKind kind) {
    auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        setMediaMessage("Separate some audio before using quick export");
        return false;
    }
    if (result->modelName != "htdemucs" || result->sourceCount < 4) {
        setMediaMessage("Quick export requires a result from the default htdemucs model");
        return false;
    }

    const auto outputFile = requestedOutputFile.withFileExtension(".wav");
    juce::File originalMediaFile;
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        originalMediaFile = importedMediaFile_;
    }
    if (originalMediaFile.existsAsFile() && outputFile == originalMediaFile) {
        setMediaMessage("Choose a different output name; the imported source is protected");
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        return false;
    }
    stopMediaThread();
    bool expected = false;
    if (!mediaBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage(
        kind == QuickExportKind::vocals
            ? "Exporting vocals at original Demucs level"
            : "Exporting accompaniment at original Demucs level");
    mediaThread_ = std::jthread(
        [this,
         outputFile,
         kind,
         result = std::move(result)](std::stop_token stopToken) mutable {
            quickExportLoop(
                stopToken,
                outputFile,
                kind,
                std::move(result));
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::quickExportLoop(
    std::stop_token stopToken,
    juce::File outputFile,
    QuickExportKind kind,
    std::shared_ptr<const SeparationResult> result) {
    try {
        const auto sampleCount = static_cast<std::size_t>(result->sampleCount);
        std::vector<float> outputLeft(sampleCount, 0.0f);
        std::vector<float> outputRight(sampleCount, 0.0f);
        constexpr int vocalsSource = 3;
        constexpr std::size_t cancellationBlock = 1u << 18;

        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            if (sample % cancellationBlock == 0) {
                if (stopToken.stop_requested()) {
                    setMediaMessage("Quick export cancelled");
                    mediaBusy_.store(false, std::memory_order_release);
                    return;
                }
                mediaProgress_.store(
                    sampleCount == 0
                        ? 0.0
                        : 0.85 * static_cast<double>(sample) / sampleCount,
                    std::memory_order_release);
            }

            if (kind == QuickExportKind::vocals) {
                const auto leftPlane = static_cast<std::size_t>(vocalsSource) * 2;
                const auto rightPlane = leftPlane + 1;
                outputLeft[sample] = result->stems[leftPlane * sampleCount + sample];
                outputRight[sample] = result->stems[rightPlane * sampleCount + sample];
                continue;
            }

            for (int source = 0; source < 4; ++source) {
                if (source == vocalsSource) {
                    continue;
                }
                const auto leftPlane = static_cast<std::size_t>(source) * 2;
                const auto rightPlane = leftPlane + 1;
                outputLeft[sample] +=
                    result->stems[leftPlane * sampleCount + sample];
                outputRight[sample] +=
                    result->stems[rightPlane * sampleCount + sample];
            }
        }

        juce::String error;
        if (!writeFloatWav(
                outputFile,
                outputLeft.data(),
                outputRight.data(),
                sampleCount,
                error)) {
            setMediaMessage(error);
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        mediaProgress_.store(1.0, std::memory_order_release);
        setMediaMessage(
            (kind == QuickExportKind::vocals ? "Exported vocals: "
                                             : "Exported accompaniment: ") +
            outputFile.getFullPathName());
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        setMediaMessage(
            "Quick export failed: " +
            juce::String::fromUTF8(exception.what()));
        mediaBusy_.store(false, std::memory_order_release);
    }
}

bool HTDemucsGpuFXAudioProcessor::beginMixExport(
    const juce::File& requestedOutputFile,
    bool replaceVideoAudio) {
    auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        setMediaMessage("Separate some audio before exporting a mix");
        return false;
    }
    if (replaceVideoAudio && !importedFromVideo()) {
        setMediaMessage("Video export is available only when a video was imported");
        return false;
    }
    juce::File originalMediaFile;
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        originalMediaFile = importedMediaFile_;
    }
    const auto outputFile = requestedOutputFile.withFileExtension(
        replaceVideoAudio ? ".mp4" : ".wav");
    if (originalMediaFile.existsAsFile() && outputFile == originalMediaFile) {
        setMediaMessage("Choose a different output name; the imported source is protected");
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        return false;
    }
    stopMediaThread();
    bool expected = false;
    if (!mediaBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    const auto settings = currentMixSettings();
    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage(
        replaceVideoAudio ? "Mixing and replacing the video audio track"
                          : "Exporting the current interface mix");
    mediaThread_ = std::jthread(
        [this,
         outputFile,
         replaceVideoAudio,
         result = std::move(result),
         settings,
         originalMediaFile](std::stop_token stopToken) mutable {
            mixExportLoop(
                stopToken,
                outputFile,
                replaceVideoAudio,
                std::move(result),
                settings,
                originalMediaFile);
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::mixExportLoop(
    std::stop_token stopToken,
    juce::File outputFile,
    bool replaceVideoAudio,
    std::shared_ptr<const SeparationResult> result,
    MixSettings settings,
    juce::File originalMediaFile) {
    try {
        const auto sampleCount = static_cast<std::size_t>(result->sampleCount);
        std::vector<float> mixedLeft(sampleCount, 0.0f);
        std::vector<float> mixedRight(sampleCount, 0.0f);
        constexpr std::size_t cancellationBlock = 1u << 18;
        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            if (sample % cancellationBlock == 0) {
                if (stopToken.stop_requested()) {
                    setMediaMessage("Mix export cancelled");
                    mediaBusy_.store(false, std::memory_order_release);
                    return;
                }
                mediaProgress_.store(
                    sampleCount == 0
                        ? 0.0
                        : 0.55 * static_cast<double>(sample) / sampleCount,
                    std::memory_order_release);
            }
            if (settings.bypass) {
                mixedLeft[sample] = result->originalLeft[sample] * settings.outputTrim;
                mixedRight[sample] = result->originalRight[sample] * settings.outputTrim;
                continue;
            }
            float left = 0.0f;
            float right = 0.0f;
            for (int source = 0; source < result->sourceCount; ++source) {
                const auto leftPlane = static_cast<std::size_t>(source) * 2;
                const auto rightPlane = leftPlane + 1;
                left += result->stems[leftPlane * sampleCount + sample] *
                        settings.stemGains[static_cast<std::size_t>(source)];
                right += result->stems[rightPlane * sampleCount + sample] *
                         settings.stemGains[static_cast<std::size_t>(source)];
            }
            mixedLeft[sample] = left * settings.outputTrim;
            mixedRight[sample] = right * settings.outputTrim;
        }

        juce::String error;
        if (!replaceVideoAudio) {
            if (!writeFloatWav(
                    outputFile,
                    mixedLeft.data(),
                    mixedRight.data(),
                    sampleCount,
                    error)) {
                setMediaMessage(error);
                mediaBusy_.store(false, std::memory_order_release);
                return;
            }
            mediaProgress_.store(1.0, std::memory_order_release);
            setMediaMessage(
                "Exported interface mix: " + outputFile.getFullPathName());
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }

        auto temporaryMix = juce::File::createTempFile(".wav");
        temporaryMix.deleteFile();
        if (!writeFloatWav(
                temporaryMix,
                mixedLeft.data(),
                mixedRight.data(),
                sampleCount,
                error)) {
            setMediaMessage(error);
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        mediaProgress_.store(0.65, std::memory_order_release);
        outputFile.getParentDirectory().createDirectory();
        const auto temporaryVideo =
            outputFile.getParentDirectory().getNonexistentChildFile(
                outputFile.getFileNameWithoutExtension() + ".htfx-part",
                ".mp4",
                false);
        const juce::StringArray arguments{
            "-hide_banner", "-loglevel", "error", "-y", "-i",
            originalMediaFile.getFullPathName(), "-i",
            temporaryMix.getFullPathName(), "-map", "0:v:0", "-map", "1:a:0",
            "-map_metadata", "0", "-c:v", "copy", "-c:a", "aac", "-b:a",
            "320k", "-af", "apad", "-shortest", "-movflags", "+faststart",
            temporaryVideo.getFullPathName()};
        const bool muxed = runFfmpeg(arguments, stopToken, error);
        temporaryMix.deleteFile();
        if (!muxed) {
            temporaryVideo.deleteFile();
            setMediaMessage(
                error +
                " (The source video codec may not be compatible with MP4 stream copy.)");
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        if ((outputFile.existsAsFile() && !outputFile.deleteFile()) ||
            !temporaryVideo.moveFileTo(outputFile)) {
            temporaryVideo.deleteFile();
            setMediaMessage(
                "Could not replace the selected MP4 output file: " +
                outputFile.getFullPathName());
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        mediaProgress_.store(1.0, std::memory_order_release);
        setMediaMessage(
            "Exported MP4 with the interface mix: " +
            outputFile.getFullPathName());
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        setMediaMessage(
            "Mix export failed: " + juce::String::fromUTF8(exception.what()));
        mediaBusy_.store(false, std::memory_order_release);
    }
}

void HTDemucsGpuFXAudioProcessor::startBridge() {
    stopBridge();
    setBridgeMessage({});
    bridgeStatus_.store(BridgeStatus::waitingForAudio, std::memory_order_release);
    bridgeThread_ = std::jthread(
        [this](std::stop_token stopToken) { bridgeLoop(stopToken); });
}

void HTDemucsGpuFXAudioProcessor::stopBridge() {
    if (bridgeThread_.joinable()) {
        bridgeThread_.request_stop();
        bridgeThread_.join();
    }
    bridgeStatus_.store(BridgeStatus::stopped, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::bridgeLoop(std::stop_token stopToken) {
    const bool fakeWorker = environmentFlag("HTFX_USE_FAKE_WORKER");
    RuntimeConfiguration configuration;
    {
        std::scoped_lock control(runtimeControlMutex_);
        configuration = activeRuntimeConfiguration_;
    }
    htfx::GpuWorkerClient worker;
    htfx::GpuWorkerConfig workerConfig;
    workerConfig.workerExecutable = configuredWorkerExecutable();
    workerConfig.pythonExecutable = configuredPythonPath();
    workerConfig.workerScript = configuredPath(
        "HTFX_GPU_WORKER", "worker/gpu_ipc_worker.py", HTFX_DEFAULT_GPU_WORKER_PATH);
    workerConfig.modelsDirectory = configuredModelsDirectory();
    workerConfig.modelName = configuration.modelName;
    workerConfig.sourceCount = static_cast<std::uint32_t>(configuration.sourceCount);
    workerConfig.segmentFrames = static_cast<std::uint32_t>(configuration.segmentSamples);
    workerConfig.hopFrames = static_cast<std::uint32_t>(configuration.hopSamples);
    workerConfig.gpuIndex = static_cast<std::uint32_t>(configuration.gpuIndex);
    workerConfig.backend = configuration.backend == 1
                               ? htfx::WorkerBackend::cuda
                           : configuration.backend == 2
                               ? htfx::WorkerBackend::cpu
                           : configuration.backend == 3
                               ? htfx::WorkerBackend::mps
                               : htfx::WorkerBackend::autoSelect;
    workerConfig.readyTimeout = std::chrono::minutes(10);
    workerConfig.processTimeout = std::chrono::minutes(10);

    const auto hopSamples = static_cast<std::size_t>(configuration.hopSamples);
    const auto overlapSamples = static_cast<std::size_t>(configuration.overlapSamples);
    std::vector<float> hop(2 * hopSamples, 0.0f);
    std::vector<float> zeroHop(2 * hopSamples, 0.0f);
    std::size_t fill = 0;
    std::uint32_t activeEpoch = streamEpoch_.load(std::memory_order_acquire);
    bool hopHasSignal = false;
    bool launchAttempted = false;
    bool workerStarted = false;
    bool emittedTimeline = false;
    bool workerHasStartedBefore = false;

    const auto pushZeroFrames = [&](std::size_t count) {
        StemFrame output;
        output.epoch = activeEpoch;
        for (std::size_t sample = 0; sample < count; ++sample) {
            while (!outputRing_.tryPush(output)) {
                if (stopToken.stop_requested() ||
                    streamEpoch_.load(std::memory_order_acquire) != activeEpoch) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        return true;
    };

    const auto pushWorkerOutput = [&](const float* planar, std::size_t start) {
        for (std::size_t sample = start; sample < hopSamples; ++sample) {
            if (stopToken.stop_requested() ||
                streamEpoch_.load(std::memory_order_acquire) != activeEpoch) {
                return false;
            }
            StemFrame output;
            output.epoch = activeEpoch;
            for (int source = 0; source < configuration.sourceCount; ++source) {
                for (std::size_t channel = 0; channel < 2; ++channel) {
                    const auto plane = static_cast<std::size_t>(source) * 2 + channel;
                    output.samples[plane] =
                        planar[plane * htfx::GpuWorkerClient::kMaxFrames + sample];
                }
            }
            while (!outputRing_.tryPush(output)) {
                if (stopToken.stop_requested() ||
                    streamEpoch_.load(std::memory_order_acquire) != activeEpoch) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        return true;
    };

    while (!stopToken.stop_requested()) {
        InputFrame frame;
        if (!inputRing_.tryPop(frame)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        const auto requestedEpoch = streamEpoch_.load(std::memory_order_acquire);
        if (frame.epoch != requestedEpoch) {
            continue;
        }
        if (frame.epoch != activeEpoch) {
            activeEpoch = frame.epoch;
            const bool forceGpuRestart = gpuRestartRequested_.exchange(
                false, std::memory_order_acq_rel);
            bool resetWorker = workerStarted && !forceGpuRestart;
            if (resetWorker) {
                bridgeStatus_.store(BridgeStatus::priming, std::memory_order_release);
                setBridgeMessage("Resetting OLA epoch " + juce::String(activeEpoch));
                resetWorker = worker.reset(activeEpoch);
            }
            if (!resetWorker && workerStarted) {
                worker.stop();
                workerPid_.store(0, std::memory_order_release);
                workerStarted = false;
            }
            workerConfig.gpuIndex = static_cast<std::uint32_t>(std::clamp(
                static_cast<int>(std::lround(
                    gpuIndexParameter_->load(std::memory_order_relaxed))),
                0,
                7));
            fill = 0;
            hopHasSignal = false;
            launchAttempted = resetWorker;
            workerStarted = resetWorker;
            emittedTimeline = false;
            bridgeStatus_.store(
                resetWorker ? BridgeStatus::priming : BridgeStatus::waitingForAudio,
                std::memory_order_release);
        }

        hop[fill] = frame.left;
        hop[hopSamples + fill] = frame.right;
        ++fill;
        const bool hasSignal = std::abs(frame.left) > 1.0e-12f ||
                               std::abs(frame.right) > 1.0e-12f;
        hopHasSignal = hopHasSignal || hasSignal;

        const bool shouldLaunchWorker =
            hasSignal || wrapperType == wrapperType_Standalone;
        if (shouldLaunchWorker && !fakeWorker && !launchAttempted) {
            launchAttempted = true;
            bridgeStatus_.store(BridgeStatus::loading, std::memory_order_release);
            const juce::String requestedDevice =
                workerConfig.backend == htfx::WorkerBackend::cpu
                    ? "CPU"
                : workerConfig.backend == htfx::WorkerBackend::cuda
                    ? "cuda:" + juce::String(workerConfig.gpuIndex)
                : workerConfig.backend == htfx::WorkerBackend::mps
                    ? "Apple Metal (MPS)"
                    : "Auto";
            setBridgeMessage(
                "Loading " + juce::String(configuration.modelName) + " on " +
                requestedDevice);
            workerStarted = worker.start(workerConfig, activeEpoch);
            if (!workerStarted) {
                workerPid_.store(0, std::memory_order_release);
                bridgeStatus_.store(BridgeStatus::error, std::memory_order_release);
                setBridgeMessage(juce::String::fromUTF8(worker.lastError().c_str()));
            } else {
                workerPid_.store(worker.workerPid(), std::memory_order_release);
                if (workerHasStartedBefore) {
                    workerRestarts_.fetch_add(1, std::memory_order_relaxed);
                }
                workerHasStartedBefore = true;
                resolvedBackend_.store(
                    worker.resolvedBackend() == htfx::WorkerBackend::cpu
                        ? 2
                    : worker.resolvedBackend() == htfx::WorkerBackend::mps
                        ? 3
                        : 1,
                    std::memory_order_release);
                activeSourceCount_.store(
                    static_cast<int>(worker.activeSourceCount()),
                    std::memory_order_release);
                bridgeStatus_.store(BridgeStatus::priming, std::memory_order_release);
                setBridgeMessage(
                    juce::String::fromUTF8(worker.gpuName().c_str()) +
                    " worker PID " + juce::String(worker.workerPid()));
                if (emittedTimeline) {
                    const float* ignored = nullptr;
                    if (!worker.process(
                            activeEpoch,
                            zeroHop.data(),
                            static_cast<std::uint32_t>(hopSamples),
                            ignored,
                            nullptr)) {
                        workerStarted = false;
                        bridgeStatus_.store(BridgeStatus::error, std::memory_order_release);
                        setBridgeMessage(juce::String::fromUTF8(worker.lastError().c_str()));
                        worker.stop();
                        workerPid_.store(0, std::memory_order_release);
                    }
                }
            }
        }

        if (fill != hopSamples) {
            continue;
        }

        bool abandoned = false;
        if (fakeWorker) {
            bridgeStatus_.store(BridgeStatus::processing, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            constexpr std::array<float, kMaxSources> fakeSourceGains{
                0.4f, 0.25f, 0.2f, 0.15f, 0.0f, 0.0f};
            for (std::size_t sample = 0; sample < hopSamples; ++sample) {
                if (stopToken.stop_requested() ||
                    streamEpoch_.load(std::memory_order_acquire) != activeEpoch) {
                    abandoned = true;
                    break;
                }
                StemFrame output;
                output.epoch = activeEpoch;
                for (int source = 0; source < configuration.sourceCount; ++source) {
                    output.samples[source * 2] = hop[sample] * fakeSourceGains[source];
                    output.samples[source * 2 + 1] =
                        hop[hopSamples + sample] *
                        fakeSourceGains[source];
                }
                while (!outputRing_.tryPush(output)) {
                    if (stopToken.stop_requested() ||
                        streamEpoch_.load(std::memory_order_acquire) != activeEpoch) {
                        abandoned = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                if (abandoned) {
                    break;
                }
            }
        } else if (!hopHasSignal && !workerStarted) {
            const std::size_t start = emittedTimeline
                                          ? 0
                                          : overlapSamples;
            abandoned = !pushZeroFrames(hopSamples - start);
            emittedTimeline = true;
            if (!abandoned) {
                bridgeStatus_.store(
                    BridgeStatus::waitingForAudio, std::memory_order_release);
            }
        } else if (workerStarted) {
            bridgeStatus_.store(BridgeStatus::processing, std::memory_order_release);
            const float* workerOutput = nullptr;
            double elapsedMilliseconds = 0.0;
            if (!worker.process(
                    activeEpoch,
                    hop.data(),
                    static_cast<std::uint32_t>(hopSamples),
                    workerOutput,
                    &elapsedMilliseconds)) {
                bridgeStatus_.store(BridgeStatus::recovering, std::memory_order_release);
                setBridgeMessage(juce::String::fromUTF8(worker.lastError().c_str()));
                worker.stop();
                workerPid_.store(0, std::memory_order_release);
                workerStarted = false;
                bridgeRecoveryRequested_.store(true, std::memory_order_release);
                abandoned = true;
            } else {
                const std::size_t start = emittedTimeline
                                              ? 0
                                              : overlapSamples;
                abandoned = !pushWorkerOutput(workerOutput, start);
                emittedTimeline = true;
                if (!abandoned) {
                    lastInferenceMilliseconds_.store(
                        elapsedMilliseconds, std::memory_order_release);
                    cudaAllocatedBytes_.store(
                        worker.cudaAllocatedBytes(), std::memory_order_release);
                    cudaReservedBytes_.store(
                        worker.cudaReservedBytes(), std::memory_order_release);
                    cudaMaxAllocatedBytes_.store(
                        worker.cudaMaxAllocatedBytes(), std::memory_order_release);
                    cudaMaxReservedBytes_.store(
                        worker.cudaMaxReservedBytes(), std::memory_order_release);
                    workerProcesses_.fetch_add(1, std::memory_order_release);
                    bridgeStatus_.store(BridgeStatus::running, std::memory_order_release);
                    setBridgeMessage(
                        juce::String::fromUTF8(worker.gpuName().c_str()) + " · " +
                        juce::String(configuration.modelName) + " · " +
                        juce::String(elapsedMilliseconds, 1) + " ms");
                }
            }
        }
        fill = 0;
        hopHasSignal = false;
        if (!abandoned && fakeWorker) {
            bridgeStatus_.store(BridgeStatus::running, std::memory_order_release);
        }
    }
    worker.stop();
    workerPid_.store(0, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::setBridgeMessage(const juce::String& message) {
    const juce::ScopedLock lock(bridgeMessageLock_);
    bridgeMessage_ = message;
}

juce::String HTDemucsGpuFXAudioProcessor::getBridgeStatusText() const {
    switch (bridgeStatus_.load(std::memory_order_acquire)) {
        case BridgeStatus::stopped: return "Stopped";
        case BridgeStatus::waitingForAudio: return "Waiting for audio";
        case BridgeStatus::loading: return "Loading Demucs model";
        case BridgeStatus::priming: return "Priming";
        case BridgeStatus::processing: return "Processing";
        case BridgeStatus::running:
            if (environmentFlag("HTFX_USE_FAKE_WORKER")) {
                return "Running (fake worker)";
            }
            {
                const juce::ScopedLock lock(bridgeMessageLock_);
                return bridgeMessage_.isEmpty() ? "Running (GPU)" : "Running · " + bridgeMessage_;
            }
        case BridgeStatus::recovering:
            {
                const juce::ScopedLock lock(bridgeMessageLock_);
                return "Recovering · " + bridgeMessage_;
            }
        case BridgeStatus::error:
            {
                const juce::ScopedLock lock(bridgeMessageLock_);
                return "Error · " + bridgeMessage_;
            }
        case BridgeStatus::unsupportedSampleRate: return "Unsupported sample rate";
    }
    return "Unknown";
}

juce::String HTDemucsGpuFXAudioProcessor::getRecordStatusText() const {
    const auto state = separationState_.load(std::memory_order_acquire);
    const juce::ScopedLock lock(separationMessageLock_);
    if (separationMessage_.isNotEmpty()) {
        return separationMessage_;
    }
    switch (state) {
        case SeparationState::idle: return "Ready to record";
        case SeparationState::recording: return "Recording";
        case SeparationState::recorded: return "Ready to separate";
        case SeparationState::loading: return "Loading model";
        case SeparationState::separating: return "Separating";
        case SeparationState::previewReady: return "Ready to preview";
        case SeparationState::error: return "Error";
        case SeparationState::cancelled: return "Cancelled";
    }
    return "Unknown";
}

juce::String HTDemucsGpuFXAudioProcessor::getResolvedDeviceName() const {
    switch (resolvedBackend_.load(std::memory_order_acquire)) {
        case 1: return "NVIDIA GPU";
        case 2: return "CPU";
        case 3: return "Apple Metal (MPS)";
        default: return "Auto detection pending";
    }
}

namespace {

class ExportDialogContent final : public juce::Component {
public:
    explicit ExportDialogContent(HTDemucsGpuFXAudioProcessor& processor)
        : processor_(processor) {
        title_.setText(
            "Choose original-volume stems, or export the mix currently heard in the interface.",
            juce::dontSendNotification);
        title_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(title_);

        const int sourceCount = processor_.getActiveSourceCount();
        for (int source = 0; source < HTDemucsGpuFXAudioProcessor::kMaxSources;
             ++source) {
            stemButtons_[static_cast<std::size_t>(source)].setButtonText(
                processor_.getStemLabel(source));
            stemButtons_[static_cast<std::size_t>(source)].setToggleState(
                source < sourceCount, juce::dontSendNotification);
            addAndMakeVisible(stemButtons_[static_cast<std::size_t>(source)]);
            stemButtons_[static_cast<std::size_t>(source)].setVisible(
                source < sourceCount);
        }

        selectedButton_.setButtonText("Export selected stems");
        selectedButton_.onClick = [this] { chooseStemFolder(selectedSources()); };
        allButton_.setButtonText("Export all stems");
        allButton_.onClick = [this] {
            std::vector<int> all;
            for (int source = 0; source < processor_.getActiveSourceCount(); ++source) {
                all.push_back(source);
            }
            chooseStemFolder(std::move(all));
        };
        mixButton_.setButtonText("Export mix");
        mixButton_.onClick = [this] { chooseMixFile(); };
        closeButton_.setButtonText("Close");
        closeButton_.onClick = [this] {
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
                dialog->exitModalState(0);
            }
        };
        for (auto* button : std::array<juce::Button*, 4>{
                 &selectedButton_, &allButton_, &mixButton_, &closeButton_}) {
            addAndMakeVisible(button);
        }

        audioOnlyButton_.setButtonText("Audio only (.wav)");
        videoButton_.setButtonText("Video with mixed audio (.mp4)");
        audioOnlyButton_.setRadioGroupId(0x48544658, juce::dontSendNotification);
        videoButton_.setRadioGroupId(0x48544658, juce::dontSendNotification);
        audioOnlyButton_.setToggleState(true, juce::dontSendNotification);
        const bool videoInput = processor_.importedFromVideo();
        addAndMakeVisible(audioOnlyButton_);
        addAndMakeVisible(videoButton_);
        audioOnlyButton_.setVisible(videoInput);
        videoButton_.setVisible(videoInput);

        note_.setText(
            videoInput
                ? "MP4 export copies the original video stream and replaces only its audio."
                : "Individual stems ignore the interface gain controls and preserve Demucs output level.",
            juce::dontSendNotification);
        note_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        note_.setFont(juce::FontOptions{12.0f});
        addAndMakeVisible(note_);
        setSize(520, videoInput ? 290 : 245);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title_.setBounds(area.removeFromTop(38));
        const int sourceCount = processor_.getActiveSourceCount();
        constexpr int stemColumns = 3;
        constexpr int stemRowHeight = 28;
        const int stemRows = juce::jmax(1, (sourceCount + stemColumns - 1) / stemColumns);
        auto stemArea = area.removeFromTop(stemRows * stemRowHeight);
        for (int source = 0; source < sourceCount; ++source) {
            auto& button = stemButtons_[static_cast<std::size_t>(source)];
            const int column = source % stemColumns;
            const int row = source / stemColumns;
            button.setBounds(
                stemArea.getX() + column * stemArea.getWidth() / stemColumns,
                stemArea.getY() + row * stemRowHeight,
                stemArea.getWidth() / stemColumns,
                26);
        }
        area.removeFromTop(6);
        auto stemButtons = area.removeFromTop(32);
        selectedButton_.setBounds(stemButtons.removeFromLeft(220));
        stemButtons.removeFromLeft(8);
        allButton_.setBounds(stemButtons.removeFromLeft(180));

        if (processor_.importedFromVideo()) {
            area.removeFromTop(8);
            auto formatRow = area.removeFromTop(28);
            audioOnlyButton_.setBounds(formatRow.removeFromLeft(190));
            videoButton_.setBounds(formatRow.removeFromLeft(270));
        }
        area.removeFromTop(8);
        auto mixRow = area.removeFromTop(32);
        mixButton_.setBounds(mixRow.removeFromLeft(220));
        closeButton_.setBounds(mixRow.removeFromRight(90));
        note_.setBounds(area.removeFromTop(34));
    }

private:
    std::vector<int> selectedSources() const {
        std::vector<int> selected;
        for (int source = 0; source < processor_.getActiveSourceCount(); ++source) {
            if (stemButtons_[static_cast<std::size_t>(source)].getToggleState()) {
                selected.push_back(source);
            }
        }
        return selected;
    }

    void chooseStemFolder(std::vector<int> sources) {
        if (sources.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "No stems selected",
                "Select at least one stem before exporting.");
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder for the stem WAV files",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));
        juce::Component::SafePointer<ExportDialogContent> safeThis(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectDirectories,
            [safeThis, sources = std::move(sources)](
                const juce::FileChooser& chooser) mutable {
                if (safeThis == nullptr) {
                    return;
                }
                const auto folder = chooser.getResult();
                if (folder != juce::File{}) {
                    safeThis->processor_.beginStemExport(folder, std::move(sources));
                }
                safeThis->chooser_.reset();
            });
    }

    void chooseMixFile() {
        const bool video =
            processor_.importedFromVideo() && videoButton_.getToggleState();
        auto base = juce::File::createLegalFileName(
            juce::File(processor_.getImportedMediaName())
                .getFileNameWithoutExtension());
        if (base.isEmpty()) {
            base = "htdemucs";
        }
        auto suggested = juce::File::getSpecialLocation(
                             juce::File::userDocumentsDirectory)
                             .getChildFile(
                                 base + "_mix" + (video ? ".mp4" : ".wav"));
        chooser_ = std::make_unique<juce::FileChooser>(
            video ? "Export video with the interface mix"
                  : "Export the interface mix",
            suggested,
            video ? "*.mp4" : "*.wav");
        juce::Component::SafePointer<ExportDialogContent> safeThis(this);
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis, video](const juce::FileChooser& chooser) {
                if (safeThis == nullptr) {
                    return;
                }
                const auto output = chooser.getResult();
                if (output != juce::File{}) {
                    safeThis->processor_.beginMixExport(output, video);
                }
                safeThis->chooser_.reset();
            });
    }

    HTDemucsGpuFXAudioProcessor& processor_;
    juce::Label title_;
    std::array<juce::ToggleButton, HTDemucsGpuFXAudioProcessor::kMaxSources>
        stemButtons_;
    juce::TextButton selectedButton_;
    juce::TextButton allButton_;
    juce::TextButton mixButton_;
    juce::TextButton closeButton_;
    juce::ToggleButton audioOnlyButton_;
    juce::ToggleButton videoButton_;
    juce::Label note_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

class HTDemucsGpuFXEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer {
public:
    explicit HTDemucsGpuFXEditor(HTDemucsGpuFXAudioProcessor& processor)
        : AudioProcessorEditor(processor),
          processor_(processor),
          state_(processor.parameters()),
          progressBar_(progressValue_) {
        panelSwitchButton_.setButtonText("Advanced panel");
        panelSwitchButton_.onClick = [this] { setAdvancedPanel(!advancedPanel_); };
        addAndMakeVisible(panelSwitchButton_);

        simpleTitle_.setText("HTDemucs Quick Separation", juce::dontSendNotification);
        simpleTitle_.setFont(juce::FontOptions{22.0f, juce::Font::bold});
        simpleTitle_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(simpleTitle_);
        simpleFile_.setText("No audio or video selected", juce::dontSendNotification);
        simpleFile_.setJustificationType(juce::Justification::centredLeft);
        simpleFile_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(simpleFile_);
        vocalsOnlyButton_.setButtonText("Export Vocals only");
        vocalsOnlyButton_.onClick = [this] {
            chooseQuickExportFile(
                HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals);
        };
        accompanyOnlyButton_.setButtonText("Export Accompany only");
        accompanyOnlyButton_.onClick = [this] {
            chooseQuickExportFile(
                HTDemucsGpuFXAudioProcessor::QuickExportKind::accompaniment);
        };
        addAndMakeVisible(vocalsOnlyButton_);
        addAndMakeVisible(accompanyOnlyButton_);

        separationModeLabel_.setText("Separation mode", juce::dontSendNotification);
        addAndMakeVisible(separationModeLabel_);
        separationModeBox_.setName("Separation mode");
        separationModeBox_.setTextWhenNothingSelected("Choose a separation mode...");
        separationModeBox_.addItem("4-stem separation", 1);
        separationModeBox_.addItem("6-stem separation", 2);
        juce::StringArray separationModeCategories;
        for (const auto& model : processor_.getRoformerModels()) {
            separationModeCategories.addIfNotAlreadyThere(model.category);
        }
        separationModeCategories.sort(true);
        for (const auto& category : separationModeCategories) {
            separationModeBox_.addItem(
                category.substring(0, 1).toUpperCase() + category.substring(1),
                separationModeBox_.getNumItems() + 1);
        }
        separationModeBox_.onChange = [this] { updateSixSourceControls(); };
        addAndMakeVisible(separationModeBox_);

        modeLabel_.setText("Mode", juce::dontSendNotification);
        addAndMakeVisible(modeLabel_);
        addAndMakeVisible(modeBox_);
        modeBox_.addItem("Record mode", 1);
        modeBox_.addItem("Realtime mode (Ultra high latency)", 2);
        modeBox_.setSelectedItemIndex(choiceIndex("operatingMode"), juce::dontSendNotification);
        modeBox_.onChange = [this] {
            setChoice("operatingMode", modeBox_.getSelectedItemIndex());
            if (modeBox_.getSelectedItemIndex() == 1) {
                processor_.endRecording();
            }
            processor_.applyUserConfiguration();
            updateVisibility();
        };

        fullScreenButton_.setButtonText("Full screen");
        fullScreenButton_.onClick = [this] { toggleFullScreen(); };
        addAndMakeVisible(fullScreenButton_);
        scaleButton_.setButtonText("Scale UI");
        scaleButton_.onClick = [this] { updateResizeMode(); };
        addAndMakeVisible(scaleButton_);

        constexpr std::array<const char*, HTDemucsGpuFXAudioProcessor::kMaxSources>
            stemNames{"Drums", "Bass", "Other", "Vocals", "Guitar", "Piano"};
        constexpr std::array<const char*, HTDemucsGpuFXAudioProcessor::kMaxSources>
            stemIds{"drumsGain", "bassGain", "otherGain", "vocalsGain", "guitarGain", "pianoGain"};
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            stemLabels_[index].setText(stemNames[index], juce::dontSendNotification);
            stemLabels_[index].setJustificationType(juce::Justification::centredRight);
            stemSliders_[index].setSliderStyle(juce::Slider::LinearHorizontal);
            stemSliders_[index].setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
            stemSliders_[index].setTextValueSuffix(" dB");
            stemSliders_[index].setName(stemIds[index]);
            addAndMakeVisible(stemLabels_[index]);
            addAndMakeVisible(stemSliders_[index]);
            stemAttachments_[index] = std::make_unique<SliderAttachment>(
                state_, stemIds[index], stemSliders_[index]);
        }

        outputLabel_.setText("Output Trim", juce::dontSendNotification);
        outputLabel_.setJustificationType(juce::Justification::centredRight);
        outputSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        outputSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
        outputSlider_.setTextValueSuffix(" dB");
        outputSlider_.setName("outputTrim");
        addAndMakeVisible(outputLabel_);
        addAndMakeVisible(outputSlider_);
        outputAttachment_ = std::make_unique<SliderAttachment>(
            state_, "outputTrim", outputSlider_);

        bypassButton_.setButtonText("Bypass (preview original recording)");
        addAndMakeVisible(bypassButton_);
        bypassAttachment_ = std::make_unique<ButtonAttachment>(
            state_, "bypass", bypassButton_);

        recordButton_.setButtonText("Record");
        recordButton_.setColour(
            juce::TextButton::buttonColourId, juce::Colour(0xffb3262e));
        recordButton_.onClick = [this] {
            if (processor_.getSeparationState() ==
                HTDemucsGpuFXAudioProcessor::SeparationState::recording) {
                processor_.endRecording();
            } else {
                processor_.beginRecording();
            }
        };
        addAndMakeVisible(recordButton_);

        importButton_.setButtonText("Import audio/video");
        importButton_.onClick = [this] { chooseMediaFile(); };
        addAndMakeVisible(importButton_);

        separateButton_.setButtonText("Separate");
        separateButton_.onClick = [this] { processor_.beginSeparation(); };
        addAndMakeVisible(separateButton_);
        exportButton_.setButtonText("Export");
        exportButton_.onClick = [this] { showExportDialog(); };
        addAndMakeVisible(exportButton_);
        cancelButton_.setButtonText("Cancel");
        cancelButton_.onClick = [this] {
            if (processor_.isModelDownloadBusy()) {
                processor_.cancelModelDownload();
            } else if (processor_.isMediaBusy()) {
                processor_.cancelMediaOperation();
            } else {
                processor_.cancelSeparation();
            }
        };
        addAndMakeVisible(cancelButton_);
        addAndMakeVisible(progressBar_);

        previewGroup_.setText("Preview");
        addAndMakeVisible(previewGroup_);
        previewPlayButton_.setButtonText("Play");
        previewPlayButton_.onClick = [this] { processor_.togglePreviewPlayback(); };
        previewStopButton_.setButtonText("Stop");
        previewStopButton_.onClick = [this] { processor_.stopPreview(); };
        previewPosition_.setSliderStyle(juce::Slider::LinearHorizontal);
        previewPosition_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        previewPosition_.setRange(0.0, 1.0, 0.0001);
        previewPosition_.onDragEnd = [this] {
            processor_.setPreviewPosition(previewPosition_.getValue());
        };
        previewTime_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(previewPlayButton_);
        addAndMakeVisible(previewStopButton_);
        addAndMakeVisible(previewPosition_);
        addAndMakeVisible(previewTime_);

        advancedButton_.setButtonText("Advanced options >");
        advancedButton_.onClick = [this] {
            advancedVisible_ = !advancedVisible_;
            advancedButton_.setButtonText(
                advancedVisible_ ? "Advanced options v" : "Advanced options >");
            updateAdvancedVisibility();
            updateSize();
        };
        addAndMakeVisible(advancedButton_);

        segmentLabel_.setText("Inference window", juce::dontSendNotification);
        segmentBox_.addItem("2 seconds", 1);
        segmentBox_.addItem("3 seconds", 2);
        segmentBox_.addItem("4 seconds", 3);
        segmentBox_.addItem("5 seconds", 4);
        segmentBox_.addItem("7.8 seconds", 5);
        segmentBox_.setSelectedItemIndex(choiceIndex("segmentLength"), juce::dontSendNotification);
        segmentBox_.onChange = [this] {
            setChoice("segmentLength", segmentBox_.getSelectedItemIndex());
            processor_.applyUserConfiguration();
        };

        modelLabel_.setText("Demucs model", juce::dontSendNotification);
        modelBox_.addItem("htdemucs", 1);
        modelBox_.addItem("htdemucs_ft", 2);
        modelBox_.addItem("htdemucs_6s", 3);
        modelBox_.addItem("hdemucs_mmi", 4);
        modelBox_.setSelectedItemIndex(choiceIndex("model"), juce::dontSendNotification);
        modelBox_.onChange = [this] {
            setChoice("model", modelBox_.getSelectedItemIndex());
            if (processor_.isModelInstalled(modelBox_.getText())) {
                processor_.applyUserConfiguration();
            }
            updateSixSourceControls();
        };
        modelDownloadButton_.setButtonText("Download selected model");
        modelDownloadButton_.onClick = [this] {
            processor_.beginModelDownload(modelBox_.getText());
        };
        addAndMakeVisible(modelDownloadButton_);

        roformerCategoryLabel_.setText("RoFormer category", juce::dontSendNotification);
        roformerCategoryBox_.setName("RoFormer category");
        roformerCategoryBox_.addItem("All categories", 1);
        juce::StringArray roformerCategories;
        for (const auto& model : processor_.getRoformerModels()) {
            roformerCategories.addIfNotAlreadyThere(model.category);
        }
        roformerCategories.sort(true);
        for (const auto& category : roformerCategories) {
            roformerCategoryBox_.addItem(category, roformerCategoryBox_.getNumItems() + 1);
        }
        roformerCategoryBox_.setSelectedItemIndex(0, juce::dontSendNotification);
        roformerCategoryBox_.onChange = [this] { refreshRoformerBrowser(); };

        roformerSearchLabel_.setText("Search models", juce::dontSendNotification);
        roformerSearch_.setName("RoFormer search");
        roformerSearch_.setTextToShowWhenEmpty(
            "Name or model ID", juce::Colours::grey);
        roformerSearch_.onTextChange = [this] { refreshRoformerBrowser(); };

        roformerModelLabel_.setText("RoFormer model", juce::dontSendNotification);
        roformerModelBox_.setName("RoFormer model");
        roformerModelBox_.onChange = [this] {
            const auto index = roformerModelBox_.getSelectedItemIndex();
            if (index >= 0 && index < static_cast<int>(visibleRoformerIds_.size())) {
                processor_.selectRoformerModel(
                    visibleRoformerIds_[static_cast<std::size_t>(index)]);
            }
            updateRoformerStatus();
        };

        roformerStatusLabel_.setText("Download status", juce::dontSendNotification);
        roformerStatus_.setName("RoFormer download status");
        roformerStatus_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        refreshRoformerBrowser();

        for (auto* component : std::array<juce::Component*, 8>{
                 &roformerCategoryLabel_, &roformerCategoryBox_,
                 &roformerSearchLabel_, &roformerSearch_,
                 &roformerModelLabel_, &roformerModelBox_,
                 &roformerStatusLabel_, &roformerStatus_}) {
            addAndMakeVisible(component);
        }

        computeLabel_.setText("Compute device", juce::dontSendNotification);
#if JUCE_MAC
        computeBox_.addItem("Auto (Apple MPS, otherwise CPU)", 1);
#else
        computeBox_.addItem("Auto (NVIDIA CUDA, otherwise CPU)", 1);
#endif
        computeBox_.addItem("NVIDIA CUDA", 2);
        computeBox_.addItem("CPU", 3);
        computeBox_.addItem("Apple Metal (MPS)", 4);
        computeBox_.setSelectedItemIndex(choiceIndex("computeBackend"), juce::dontSendNotification);
        computeBox_.onChange = [this] {
            setChoice("computeBackend", computeBox_.getSelectedItemIndex());
            processor_.applyUserConfiguration();
            updateCpuWarning();
        };

        gpuLabel_.setText("CUDA GPU index", juce::dontSendNotification);
        gpuSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        gpuSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 24);
        gpuSlider_.setRange(0, 7, 1);
        gpuAttachment_ = std::make_unique<SliderAttachment>(
            state_, "gpuIndex", gpuSlider_);
        gpuSlider_.onDragEnd = [this] { processor_.applyUserConfiguration(); };

        for (auto* component : std::array<juce::Component*, 8>{
                 &segmentLabel_, &segmentBox_, &modelLabel_, &modelBox_,
                 &computeLabel_, &computeBox_, &gpuLabel_, &gpuSlider_}) {
            addAndMakeVisible(component);
        }
        cpuWarning_.setColour(juce::Label::textColourId, juce::Colour(0xffffb74d));
        cpuWarning_.setFont(juce::FontOptions{13.0f, juce::Font::bold});
        addAndMakeVisible(cpuWarning_);

        addAndMakeVisible(status_);
        addAndMakeVisible(metrics_);
        addAndMakeVisible(resetWorker_);
        status_.setJustificationType(juce::Justification::centredLeft);
        metrics_.setJustificationType(juce::Justification::centredLeft);
        status_.setFont(juce::FontOptions{15.0f, juce::Font::bold});
        metrics_.setFont(juce::FontOptions{12.0f});
        resetWorker_.setButtonText("Reset worker");
        resetWorker_.onClick = [this] { processor_.requestWorkerRecovery(); };
        while (getNumChildComponents() > 0) {
            auto* child = getChildComponent(0);
            const bool wasVisible = child->isVisible();
            scaledContent_.addChildComponent(child);
            child->setVisible(wasVisible);
        }
        addAndMakeVisible(scaledContent_);
        setResizable(false, false);
        updateSixSourceControls();
        updateAdvancedVisibility();
        updatePanelVisibility();
        updateVisibility();
        updateSize();
        startTimerHz(10);
        timerCallback();
    }

    void resized() override {
        scaledContent_.setTransform({});
        scaledContent_.setBounds(0, 0, designWidth(), designHeight());
        auto area = scaledContent_.getLocalBounds().reduced(12);

        if (!advancedPanel_) {
            auto header = area.removeFromTop(34);
            simpleTitle_.setBounds(header.removeFromLeft(330));
            panelSwitchButton_.setBounds(header.removeFromRight(150));
            area.removeFromTop(10);
            simpleFile_.setBounds(area.removeFromTop(26));
            area.removeFromTop(8);
            importButton_.setBounds(area.removeFromTop(38));
            area.removeFromTop(10);
            auto exports = area.removeFromTop(42);
            vocalsOnlyButton_.setBounds(exports.removeFromLeft(256));
            exports.removeFromLeft(12);
            accompanyOnlyButton_.setBounds(exports);
            area.removeFromTop(10);
            progressBar_.setBounds(area.removeFromTop(18));
            area.removeFromTop(5);
            status_.setBounds(area.removeFromTop(38));

            const float scale = (std::min)(
                static_cast<float>(getWidth()) / designWidth(),
                static_cast<float>(getHeight()) / designHeight());
            const float offsetX =
                (static_cast<float>(getWidth()) - designWidth() * scale) * 0.5f;
            const float offsetY =
                (static_cast<float>(getHeight()) - designHeight() * scale) * 0.5f;
            scaledContent_.setTransform(
                juce::AffineTransform::scale(scale).translated(offsetX, offsetY));
            return;
        }

        auto sepModeRow = area.removeFromTop(30);
        separationModeLabel_.setBounds(sepModeRow.removeFromLeft(150));
        separationModeBox_.setBounds(sepModeRow.removeFromLeft(300));
        area.removeFromTop(6);

        auto modeRow = area.removeFromTop(30);
        modeLabel_.setBounds(modeRow.removeFromLeft(72));
        modeBox_.setBounds(modeRow.removeFromLeft(260));
        modeRow.removeFromLeft(6);
        fullScreenButton_.setBounds(modeRow.removeFromLeft(100));
        modeRow.removeFromLeft(6);
        scaleButton_.setBounds(modeRow.removeFromLeft(90));
        modeRow.removeFromLeft(6);
        panelSwitchButton_.setBounds(modeRow);

        auto transport = area.removeFromTop(38).reduced(0, 4);
        recordButton_.setBounds(transport.removeFromLeft(102));
        transport.removeFromLeft(5);
        importButton_.setBounds(transport.removeFromLeft(150));
        transport.removeFromLeft(5);
        separateButton_.setBounds(transport.removeFromLeft(94));
        transport.removeFromLeft(5);
        exportButton_.setBounds(transport.removeFromLeft(86));
        transport.removeFromLeft(5);
        cancelButton_.setBounds(transport.removeFromLeft(80));
        progressBar_.setBounds(area.removeFromTop(18).reduced(0, 1));
        area.removeFromTop(4);

        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            auto row = area.removeFromTop(28);
            stemLabels_[index].setBounds(row.removeFromLeft(100));
            stemSliders_[index].setBounds(row);
        }
        auto outputRow = area.removeFromTop(28);
        outputLabel_.setBounds(outputRow.removeFromLeft(100));
        outputSlider_.setBounds(outputRow);
        bypassButton_.setBounds(area.removeFromTop(26).removeFromLeft(300));

        auto preview = area.removeFromTop(82);
        previewGroup_.setBounds(preview);
        preview = preview.reduced(10, 20);
        auto previewButtons = preview.removeFromTop(24);
        previewPlayButton_.setBounds(previewButtons.removeFromLeft(92));
        previewButtons.removeFromLeft(6);
        previewStopButton_.setBounds(previewButtons.removeFromLeft(72));
        previewTime_.setBounds(previewButtons.removeFromRight(170));
        previewPosition_.setBounds(preview.removeFromTop(18));

        advancedButton_.setBounds(area.removeFromTop(28).removeFromLeft(210));
        if (advancedVisible_) {
            area.removeFromTop(3);
            auto layoutAdvancedRow = [&](juce::Label& label, juce::Component& control) {
                auto row = area.removeFromTop(28);
                label.setBounds(row.removeFromLeft(150));
                control.setBounds(row.removeFromLeft(430));
            };
            layoutAdvancedRow(segmentLabel_, segmentBox_);
            layoutAdvancedRow(modelLabel_, modelBox_);
            auto downloadRow = area.removeFromTop(28);
            downloadRow.removeFromLeft(150);
            modelDownloadButton_.setBounds(downloadRow.removeFromLeft(430));
            layoutAdvancedRow(roformerCategoryLabel_, roformerCategoryBox_);
            layoutAdvancedRow(roformerSearchLabel_, roformerSearch_);
            layoutAdvancedRow(roformerModelLabel_, roformerModelBox_);
            layoutAdvancedRow(roformerStatusLabel_, roformerStatus_);
            layoutAdvancedRow(computeLabel_, computeBox_);
            layoutAdvancedRow(gpuLabel_, gpuSlider_);
        }

        cpuWarning_.setBounds(area.removeFromTop(26));

        auto footer = area.removeFromTop(50);
        status_.setBounds(footer.removeFromTop(24));
        resetWorker_.setBounds(footer.removeFromRight(120).reduced(3));
        metrics_.setBounds(footer);

        const float scale = (std::min)(
            static_cast<float>(getWidth()) / designWidth(),
            static_cast<float>(getHeight()) / designHeight());
        const float offsetX =
            (static_cast<float>(getWidth()) - designWidth() * scale) * 0.5f;
        const float offsetY =
            (static_cast<float>(getHeight()) - designHeight() * scale) * 0.5f;
        scaledContent_.setTransform(
            juce::AffineTransform::scale(scale).translated(offsetX, offsetY));
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    [[nodiscard]] int designWidth() const noexcept {
        return advancedPanel_ ? 720 : 560;
    }

    [[nodiscard]] int designHeight() const noexcept {
        return advancedPanel_ ? (advancedVisible_ ? 826 : 576) : 260;
    }

    void chooseMediaFile() {
        mediaChooser_ = std::make_unique<juce::FileChooser>(
            "Import an audio or video file",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg;*.m4a;*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.wmv;*.mpeg");
        juce::Component::SafePointer<HTDemucsGpuFXEditor> safeThis(this);
        mediaChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser) {
                if (safeThis == nullptr) {
                    return;
                }
                const auto media = chooser.getResult();
                if (media != juce::File{}) {
                    safeThis->processor_.beginMediaImport(media);
                }
                safeThis->mediaChooser_.reset();
            });
    }

    void chooseQuickExportFile(
        HTDemucsGpuFXAudioProcessor::QuickExportKind kind) {
        const auto imported = processor_.getImportedMediaFile();
        if (!imported.existsAsFile() || processor_.getRecordedSeconds() <= 0.0) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Import media first",
                "Choose an audio or video file before exporting.");
            return;
        }
        if (!processor_.isModelInstalled("htdemucs")) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Default model is missing",
                "The general panel requires the htdemucs model. Re-run the installer or open the Advanced panel to install it.");
            return;
        }

        auto base = juce::File::createLegalFileName(
            imported.getFileNameWithoutExtension());
        if (base.isEmpty()) {
            base = "htdemucs";
        }
        const auto suffix =
            kind == HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals
                ? "_vocals.wav"
                : "_accompany.wav";
        const auto suggested = imported.getParentDirectory().getChildFile(base + suffix);
        mediaChooser_ = std::make_unique<juce::FileChooser>(
            kind == HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals
                ? "Export vocals"
                : "Export accompaniment",
            suggested,
            "*.wav");
        juce::Component::SafePointer<HTDemucsGpuFXEditor> safeThis(this);
        mediaChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis, kind](const juce::FileChooser& chooser) {
                if (safeThis == nullptr) {
                    return;
                }
                const auto output = chooser.getResult();
                if (output != juce::File{}) {
                    safeThis->pendingQuickExport_ = PendingQuickExport{output, kind};
                    safeThis->startPendingQuickExport();
                }
                safeThis->mediaChooser_.reset();
            });
    }

    void startPendingQuickExport() {
        if (!pendingQuickExport_.has_value()) {
            return;
        }

        modeBox_.setSelectedItemIndex(0, juce::dontSendNotification);
        modelBox_.setSelectedItemIndex(0, juce::dontSendNotification);
        setChoice("operatingMode", 0);
        setChoice("model", 0);
        processor_.applyUserConfiguration();

        if (processor_.previewUsesModel("htdemucs")) {
            const auto pending = *pendingQuickExport_;
            if (processor_.beginQuickExport(pending.outputFile, pending.kind)) {
                pendingQuickExport_.reset();
            }
            return;
        }

        if (!processor_.beginSeparation()) {
            pendingQuickExport_.reset();
        }
    }

    void showExportDialog() {
        if (!processor_.hasPreview()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Nothing to export",
                "Import or record audio, then run Separate before exporting.");
            return;
        }
        auto* content = new ExportDialogContent(processor_);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Export HTDemucs stems or mix";
        options.dialogBackgroundColour = getLookAndFeel().findColour(
            juce::ResizableWindow::backgroundColourId);
        options.content.setOwned(content);
        options.componentToCentreAround = this;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void setAdvancedPanel(bool advanced) {
        advancedPanel_ = advanced;
        panelSwitchButton_.setButtonText(
            advancedPanel_ ? "General panel" : "Advanced panel");
        updatePanelVisibility();
        updateVisibility();
        updateSize();
    }

    void updateResizeMode() {
        const bool enabled = scaleButton_.getToggleState();
        if (enabled) {
            const int minimumWidth = 480;
            const int maximumWidth = 2880;
            setResizable(true, true);
            setResizeLimits(
                minimumWidth,
                juce::roundToInt(
                    static_cast<double>(minimumWidth) * designHeight() /
                    designWidth()),
                maximumWidth,
                juce::roundToInt(
                    static_cast<double>(maximumWidth) * designHeight() /
                    designWidth()));
            if (auto* boundsConstraint = getConstrainer()) {
                boundsConstraint->setFixedAspectRatio(
                    static_cast<double>(designWidth()) / designHeight());
            }
        } else {
            setResizable(false, false);
            setSize(designWidth(), designHeight());
        }
        resized();
    }

    void toggleFullScreen() {
        if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
            if (auto* window = findParentComponentOfClass<juce::ResizableWindow>()) {
                const bool enter = !window->isFullScreen();
                window->setFullScreen(enter);
                fullScreenButton_.setButtonText(
                    enter ? "Exit full screen" : "Full screen");
                return;
            }
        }

        if (!editorFullScreen_) {
            previousEditorSize_ = {getWidth(), getHeight()};
            const auto* display = juce::Desktop::getInstance()
                                      .getDisplays()
                                      .getDisplayForRect(getScreenBounds());
            if (display != nullptr) {
                const auto available = display->userBounds.toNearestInt();
                setSize(available.getWidth(), available.getHeight());
            }
            editorFullScreen_ = true;
            fullScreenButton_.setButtonText("Exit full screen");
        } else {
            setSize(previousEditorSize_.x, previousEditorSize_.y);
            editorFullScreen_ = false;
            fullScreenButton_.setButtonText("Full screen");
        }
    }

    int choiceIndex(const juce::String& parameterId) const {
        if (const auto* value = state_.getRawParameterValue(parameterId)) {
            return static_cast<int>(std::lround(value->load(std::memory_order_relaxed)));
        }
        return 0;
    }

    void setChoice(const juce::String& parameterId, int index) {
        if (auto* parameter = state_.getParameter(parameterId)) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
            parameter->endChangeGesture();
        }
    }

    void updateSize() {
        if (scaleButton_.getToggleState()) {
            const int width = (std::max)(getWidth(), 480);
            updateResizeMode();
            setSize(
                width,
                juce::roundToInt(
                    static_cast<double>(width) * designHeight() / designWidth()));
        } else {
            setSize(designWidth(), designHeight());
        }
        resized();
    }

    void updateAdvancedVisibility() {
        for (auto* component : std::array<juce::Component*, 16>{
                 &segmentLabel_, &segmentBox_, &modelLabel_, &modelBox_,
                 &roformerCategoryLabel_, &roformerCategoryBox_,
                 &roformerSearchLabel_, &roformerSearch_,
                 &roformerModelLabel_, &roformerModelBox_,
                 &roformerStatusLabel_, &roformerStatus_,
                 &computeLabel_, &computeBox_, &gpuLabel_, &gpuSlider_}) {
            component->setVisible(advancedPanel_ && advancedVisible_);
        }
        modelDownloadButton_.setVisible(advancedPanel_ && advancedVisible_);
    }

    void refreshRoformerBrowser() {
        const auto selectedId = processor_.getSelectedRoformerModel();
        const auto category = roformerCategoryBox_.getSelectedItemIndex() <= 0
                                  ? juce::String{}
                                  : roformerCategoryBox_.getText();
        const auto search = roformerSearch_.getText().trim();

        roformerModelBox_.clear(juce::dontSendNotification);
        visibleRoformerIds_.clear();
        int selectedIndex = -1;
        for (const auto& model : processor_.getRoformerModels()) {
            if (category.isNotEmpty() && model.category != category) {
                continue;
            }
            if (search.isNotEmpty() &&
                !model.name.containsIgnoreCase(search) &&
                !model.id.containsIgnoreCase(search)) {
                continue;
            }
            const auto displayName = model.name +
                                     (model.experimental
                                          ? " [Experimental]"
                                          : " [Audited]");
            roformerModelBox_.addItem(
                displayName, roformerModelBox_.getNumItems() + 1);
            visibleRoformerIds_.push_back(model.id);
            if (model.id == selectedId) {
                selectedIndex = roformerModelBox_.getNumItems() - 1;
            }
        }
        if (selectedIndex < 0 && !visibleRoformerIds_.empty()) {
            selectedIndex = 0;
        }
        roformerModelBox_.setSelectedItemIndex(
            selectedIndex, juce::sendNotificationSync);
        updateRoformerStatus();
    }

    void updateRoformerStatus() {
        const auto index = roformerModelBox_.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(visibleRoformerIds_.size())) {
            roformerStatus_.setText("No matching models", juce::dontSendNotification);
            return;
        }
        const auto& id = visibleRoformerIds_[static_cast<std::size_t>(index)];
        const auto models = processor_.getRoformerModels();
        const auto found = std::find_if(
            models.begin(), models.end(),
            [&id](const auto& model) { return model.id == id; });
        if (found == models.end()) {
            roformerStatus_.setText("Unknown model", juce::dontSendNotification);
            return;
        }
        roformerStatus_.setText(
            juce::String(found->experimental ? "Experimental" : "Audited") +
                " · " +
                (processor_.isModelInstalled(id) ? "Downloaded" : "Not downloaded"),
            juce::dontSendNotification);
    }

    void updatePanelVisibility() {
        simpleTitle_.setVisible(!advancedPanel_);
        simpleFile_.setVisible(!advancedPanel_);
        vocalsOnlyButton_.setVisible(!advancedPanel_);
        accompanyOnlyButton_.setVisible(!advancedPanel_);

        for (auto* component : std::array<juce::Component*, 13>{
                 &separationModeLabel_, &separationModeBox_,
                 &modeLabel_, &modeBox_, &fullScreenButton_, &scaleButton_,
                 &outputLabel_, &outputSlider_, &bypassButton_, &advancedButton_,
                 &cpuWarning_, &metrics_, &resetWorker_}) {
            component->setVisible(advancedPanel_);
        }
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            stemLabels_[index].setVisible(advancedPanel_);
            stemSliders_[index].setVisible(advancedPanel_);
        }
        updateAdvancedVisibility();
    }

    void updateCpuWarning() {
        const bool shouldWarn =
            computeBox_.getSelectedItemIndex() == 2 || processor_.resolvedToCpu();
        cpuWarning_.setText(
            shouldWarn
                ? "CPU mode: separation is supported, but a full recording may take a long time."
                : "",
            juce::dontSendNotification);
    }

    void updateSixSourceControls() {
        const bool modeChosen = separationModeBox_.getSelectedItemIndex() >= 0;
        const bool sixSources = modelBox_.getSelectedItemIndex() == 2;
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            const bool enabled = modeChosen && (index < 4 || sixSources);
            stemSliders_[index].setEnabled(enabled);
            stemLabels_[index].setEnabled(enabled);
        }
        outputSlider_.setEnabled(modeChosen);
        outputLabel_.setEnabled(modeChosen);
    }

    void updateVisibility() {
        if (!advancedPanel_) {
            importButton_.setVisible(true);
            progressBar_.setVisible(true);
            status_.setVisible(true);
            recordButton_.setVisible(false);
            separateButton_.setVisible(false);
            exportButton_.setVisible(false);
            cancelButton_.setVisible(false);
            previewGroup_.setVisible(false);
            previewPlayButton_.setVisible(false);
            previewStopButton_.setVisible(false);
            previewPosition_.setVisible(false);
            previewTime_.setVisible(false);
            resetWorker_.setVisible(false);
            return;
        }
        const bool recordMode = modeBox_.getSelectedItemIndex() == 0;
        recordButton_.setVisible(recordMode);
        importButton_.setVisible(recordMode);
        separateButton_.setVisible(recordMode);
        exportButton_.setVisible(recordMode);
        cancelButton_.setVisible(recordMode);
        progressBar_.setVisible(recordMode);
        previewGroup_.setVisible(recordMode);
        previewPlayButton_.setVisible(recordMode);
        previewStopButton_.setVisible(recordMode);
        previewPosition_.setVisible(recordMode);
        previewTime_.setVisible(recordMode);
        resetWorker_.setVisible(!recordMode);
    }

    void timerCallback() override {
        const bool recordMode = modeBox_.getSelectedItemIndex() == 0;
        const auto separationState = processor_.getSeparationState();
        const bool separationBusy =
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::loading ||
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::separating;
        const bool mediaBusy = processor_.isMediaBusy();
        const bool modelBusy = processor_.isModelDownloadBusy();
        const bool busy = separationBusy || mediaBusy || modelBusy;
        const bool recording =
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::recording;
        progressValue_ = modelBusy
                             ? processor_.getModelDownloadProgress()
                             : (mediaBusy ? processor_.getMediaProgress()
                                          : processor_.getSeparationProgress());
        recordButton_.setButtonText(recording ? "Stop recording" : "Record");
        recordButton_.setEnabled(!mediaBusy && !separationBusy);
        importButton_.setEnabled(!recording && !busy);
        const bool quickExportReady =
            !recording && !busy && !pendingQuickExport_.has_value() &&
            processor_.getImportedMediaFile().existsAsFile() &&
            processor_.getRecordedSeconds() > 0.0 &&
            processor_.isModelInstalled("htdemucs");
        vocalsOnlyButton_.setEnabled(quickExportReady);
        accompanyOnlyButton_.setEnabled(quickExportReady);
        panelSwitchButton_.setEnabled(!busy && !pendingQuickExport_.has_value());
        separateButton_.setEnabled(
            !recording && !busy && processor_.getRecordedSeconds() > 0.0 &&
            processor_.isModelInstalled(modelBox_.getText()));
        exportButton_.setEnabled(!recording && !busy && processor_.hasPreview());
        cancelButton_.setVisible((recordMode && (separationBusy || mediaBusy)) || modelBusy);
        const bool configurationEnabled = !recording && !busy;
        const bool modeChosen = separationModeBox_.getSelectedItemIndex() >= 0;
        separationModeBox_.setEnabled(configurationEnabled);
        modeBox_.setEnabled(configurationEnabled);
        segmentBox_.setEnabled(configurationEnabled);
        modelBox_.setEnabled(configurationEnabled && modeChosen);
        const bool selectedModelInstalled =
            processor_.isModelInstalled(modelBox_.getText());
        modelDownloadButton_.setButtonText(
            selectedModelInstalled
                ? "Installed"
                : (modelBusy ? "Downloading..." : "Download selected model"));
        modelDownloadButton_.setEnabled(
            advancedVisible_ && configurationEnabled && modeChosen && !selectedModelInstalled);
        roformerCategoryBox_.setEnabled(modeChosen);
        roformerSearch_.setEnabled(modeChosen);
        roformerModelBox_.setEnabled(modeChosen);
        computeBox_.setEnabled(configurationEnabled);
        gpuSlider_.setEnabled(
            configurationEnabled && computeBox_.getSelectedItemIndex() == 1);

        const double previewDuration = processor_.getPreviewDurationSeconds();
        const double previewPosition = processor_.getPreviewPositionSeconds();
        if (!previewPosition_.isMouseButtonDown()) {
            previewPosition_.setValue(
                previewDuration > 0.0 ? previewPosition / previewDuration : 0.0,
                juce::dontSendNotification);
        }
        previewPlayButton_.setEnabled(processor_.hasPreview());
        previewStopButton_.setEnabled(processor_.hasPreview());
        previewPlayButton_.setButtonText(processor_.isPreviewPlaying() ? "Pause" : "Play");
        previewTime_.setText(
            juce::String(previewPosition, 1) + " / " +
                juce::String(previewDuration, 1) + " s",
            juce::dontSendNotification);

        const auto importedFile = processor_.getImportedMediaFile();
        simpleFile_.setText(
            importedFile.existsAsFile()
                ? importedFile.getFileName()
                : "No audio or video selected",
            juce::dontSendNotification);

        if (pendingQuickExport_.has_value()) {
            if (separationState ==
                    HTDemucsGpuFXAudioProcessor::SeparationState::previewReady &&
                processor_.previewUsesModel("htdemucs") && !mediaBusy) {
                const auto pending = *pendingQuickExport_;
                if (processor_.beginQuickExport(pending.outputFile, pending.kind)) {
                    pendingQuickExport_.reset();
                }
            } else if (
                separationState == HTDemucsGpuFXAudioProcessor::SeparationState::error ||
                separationState ==
                    HTDemucsGpuFXAudioProcessor::SeparationState::cancelled) {
                pendingQuickExport_.reset();
            }
        }

        const auto mediaStatus = processor_.getMediaStatusText();
        const auto modelStatus = processor_.getModelDownloadStatusText();
        updateRoformerStatus();
        status_.setText(
            modelBusy || (!selectedModelInstalled && modelStatus.isNotEmpty())
                ? modelStatus
                : recordMode
                ? (mediaStatus.isNotEmpty() ? mediaStatus
                                            : processor_.getRecordStatusText())
                : processor_.getBridgeStatusText(),
            juce::dontSendNotification);
        const int latency = processor_.getActiveLatencySamples();
        metrics_.setText(
            (recordMode ? "Record/preview latency 0 samples"
                        : "Realtime latency " + juce::String(latency) + " samples (" +
                              juce::String(static_cast<double>(latency) / 44100.0, 2) + " s)") +
                " · worker PID " +
                juce::String(processor_.getWorkerPid()) + " · restarts " +
                juce::String(processor_.getWorkerRestarts()) + " · over/under " +
                juce::String(processor_.getInputOverruns()) + "/" +
                juce::String(processor_.getOutputUnderruns()) + " · accelerator max " +
                juce::String(
                    static_cast<double>(processor_.getCudaMaxAllocatedBytes()) /
                        (1024.0 * 1024.0),
                    0) +
                " MiB",
            juce::dontSendNotification);
        updateCpuWarning();
        updateSixSourceControls();
    }

    HTDemucsGpuFXAudioProcessor& processor_;
    juce::AudioProcessorValueTreeState& state_;
    juce::Component scaledContent_;
    juce::TextButton panelSwitchButton_;
    juce::Label simpleTitle_;
    juce::Label simpleFile_;
    juce::TextButton vocalsOnlyButton_;
    juce::TextButton accompanyOnlyButton_;
    juce::Label separationModeLabel_;
    juce::ComboBox separationModeBox_;
    juce::Label modeLabel_;
    juce::ComboBox modeBox_;
    juce::TextButton fullScreenButton_;
    juce::ToggleButton scaleButton_;
    std::array<juce::Label, HTDemucsGpuFXAudioProcessor::kMaxSources> stemLabels_;
    std::array<juce::Slider, HTDemucsGpuFXAudioProcessor::kMaxSources> stemSliders_;
    std::array<std::unique_ptr<SliderAttachment>, HTDemucsGpuFXAudioProcessor::kMaxSources>
        stemAttachments_;
    juce::Label outputLabel_;
    juce::Slider outputSlider_;
    std::unique_ptr<SliderAttachment> outputAttachment_;
    juce::ToggleButton bypassButton_;
    std::unique_ptr<ButtonAttachment> bypassAttachment_;
    juce::TextButton recordButton_;
    juce::TextButton importButton_;
    juce::TextButton separateButton_;
    juce::TextButton exportButton_;
    juce::TextButton cancelButton_;
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_;
    juce::GroupComponent previewGroup_;
    juce::TextButton previewPlayButton_;
    juce::TextButton previewStopButton_;
    juce::Slider previewPosition_;
    juce::Label previewTime_;
    juce::TextButton advancedButton_;
    juce::Label segmentLabel_;
    juce::ComboBox segmentBox_;
    juce::Label modelLabel_;
    juce::ComboBox modelBox_;
    juce::TextButton modelDownloadButton_;
    juce::Label roformerCategoryLabel_;
    juce::ComboBox roformerCategoryBox_;
    juce::Label roformerSearchLabel_;
    juce::TextEditor roformerSearch_;
    juce::Label roformerModelLabel_;
    juce::ComboBox roformerModelBox_;
    juce::Label roformerStatusLabel_;
    juce::Label roformerStatus_;
    std::vector<juce::String> visibleRoformerIds_;
    juce::Label computeLabel_;
    juce::ComboBox computeBox_;
    juce::Label gpuLabel_;
    juce::Slider gpuSlider_;
    std::unique_ptr<SliderAttachment> gpuAttachment_;
    juce::Label cpuWarning_;
    juce::Label status_;
    juce::Label metrics_;
    juce::TextButton resetWorker_;
    struct PendingQuickExport {
        juce::File outputFile;
        HTDemucsGpuFXAudioProcessor::QuickExportKind kind;
    };
    std::optional<PendingQuickExport> pendingQuickExport_;
    std::unique_ptr<juce::FileChooser> mediaChooser_;
    juce::Point<int> previousEditorSize_{560, 260};
    bool editorFullScreen_ = false;
    bool advancedPanel_ = false;
    bool advancedVisible_ = false;
};

}  // namespace

juce::AudioProcessorEditor* HTDemucsGpuFXAudioProcessor::createEditor() {
    return new HTDemucsGpuFXEditor(*this);
}

void HTDemucsGpuFXAudioProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (const auto xml = parameters_.copyState().createXml()) {
        copyXmlToBinary(*xml, destination);
    }
}

void HTDemucsGpuFXAudioProcessor::setStateInformation(const void* data, int size) {
    if (const auto xml = getXmlFromBinary(data, size)) {
        if (xml->hasTagName(parameters_.state.getType())) {
            parameters_.replaceState(juce::ValueTree::fromXml(*xml));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HTDemucsGpuFXAudioProcessor();
}
