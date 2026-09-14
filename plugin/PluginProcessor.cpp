#include "PluginProcessor.h"

#include "GpuWorkerClient.h"
#include "Localization.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
// One representative per stem layout. htdemucs_ft and hdemucs_mmi are
// quality tiers of the same separations, not different effects, and are no
// longer offered.
constexpr std::array<const char*, 2> kModelNames{"htdemucs", "htdemucs_6s"};

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
        for (const auto* product : {"Music SSP FX", "HTDemucs GPU FX"}) {
            roots.push_back(
                juce::File(localAppData)
                    .getChildFile("Programs")
                    .getChildFile(product)
                    .getChildFile("Resources")
                    .getChildFile("sidecar"));
        }
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
        const auto current = juce::File(localAppData).getChildFile("Music SSP FX");
        if (current.isDirectory()) {
            return current;
        }
        // Renamed product: fall back to the pre-rename folder if it still holds
        // the installed data (models downloaded by the old installer).
        const auto legacy = juce::File(localAppData).getChildFile("HTDemucs GPU FX");
        return legacy.isDirectory() ? legacy : current;
    }
#endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Music SSP FX");
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
    // The catalog uses two id prefixes; matching only the first one made every
    // "roformer-model-*" checkpoint (the majority of the 99) look like an
    // HTDemucs model, so separation was refused with "model not installed".
    return modelName.startsWith("melband-roformer-") ||
           modelName.startsWith("roformer-model-");
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

// Downloaded RoFormer checkpoints and the worker's scratch space must live
// somewhere writable by the user. The development tree keeps them beside the
// checkout; an installed or portable copy would otherwise resolve them relative
// to the working directory, landing somewhere that does not exist (and, under
// Program Files, is not writable), which made every RoFormer mode fail.
// The curated catalog decides which manifest entries the app offers. It is
// looked up beside the manifest so a custom manifest can carry its own.
juce::File configuredRoformerCatalog() {
    const auto environment = juce::SystemStats::getEnvironmentVariable(
        "HTFX_ROFORMER_CATALOG", {}).trim();
    if (environment.isNotEmpty()) {
        return juce::File(environment);
    }
    return configuredRoformerManifest().getSiblingFile("roformer-catalog.json");
}

std::filesystem::path configuredWorkerExecutable();
juce::String displayPath(const std::filesystem::path& path);

// Every frozen runtime ships runtime-manifest.json beside its executable with
// the flavour it was built as. Without a frozen runtime (source tree) there is
// no flavour, and nothing is filtered.
juce::String installedRuntimeFlavor() {
    const auto worker = configuredWorkerExecutable();
    if (worker.empty()) {
        return {};
    }
    const auto manifest = juce::File(displayPath(worker))
                              .getSiblingFile("runtime-manifest.json");
    const auto parsed = juce::JSON::parse(manifest.loadFileAsString());
    if (const auto* object = parsed.getDynamicObject()) {
        return object->getProperty("flavor").toString().trim().toLowerCase();
    }
    return {};
}

juce::File configuredRoformerModelsDirectory() {
    const auto environment = juce::SystemStats::getEnvironmentVariable(
        "HTFX_ROFORMER_MODELS_DIR", {}).trim();
    if (environment.isNotEmpty()) {
        return juce::File(environment);
    }
    const auto developmentCache = juce::File::getCurrentWorkingDirectory()
                                      .getParentDirectory()
                                      .getChildFile("verify")
                                      .getChildFile("roformer-cache");
    if (developmentCache.isDirectory()) {
        return developmentCache;
    }
    return installedDataDirectory().getChildFile("RoformerModels");
}

juce::File configuredRoformerOutputDirectory() {
    const auto environment = juce::SystemStats::getEnvironmentVariable(
        "HTFX_ROFORMER_OUTPUT_DIR", {}).trim();
    if (environment.isNotEmpty()) {
        return juce::File(environment);
    }
    const auto developmentOutput = juce::File::getCurrentWorkingDirectory()
                                       .getParentDirectory()
                                       .getChildFile("verify")
                                       .getChildFile("output");
    if (developmentOutput.isDirectory()) {
        return developmentOutput.getChildFile("roformer-runtime");
    }
    return installedDataDirectory().getChildFile("RoformerWork");
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
        diagnostics = htfx::tr("error.ffmpegNotStarted");
        return false;
    }

    juce::MemoryOutputStream captured;
    std::array<char, 4096> outputBuffer{};
    while (process.isRunning()) {
        if (stopToken.stop_requested()) {
            process.kill();
            diagnostics = htfx::tr("error.mediaOperationCancelled");
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
        diagnostics = htfx::tr("error.ffmpegFailedPrefix") +
                      juce::String(exitCode) +
                      htfx::tr("error.ffmpegFailedSuffix") + diagnostics;
        return false;
    }
    return true;
}

}  // namespace

namespace htfx {

namespace {
bool endsWithAnyExtension(const juce::String& path, const char* const* first, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (path.endsWithIgnoreCase(first[i])) {
            return true;
        }
    }
    return false;
}
}  // namespace

bool isVideoExtension(const juce::String& path) {
    return endsWithAnyExtension(path, kVideoExtensions.data(), kVideoExtensions.size());
}

bool isAcceptedMediaPath(const juce::String& path) {
    return endsWithAnyExtension(path, kAudioExtensions.data(), kAudioExtensions.size()) ||
           isVideoExtension(path);
}

juce::String acceptedMediaWildcards() {
    juce::StringArray patterns;
    for (const auto* extension : kAudioExtensions) {
        patterns.add(juce::String("*") + extension);
    }
    for (const auto* extension : kVideoExtensions) {
        patterns.add(juce::String("*") + extension);
    }
    return patterns.joinIntoString(";");
}

}  // namespace htfx

namespace {

bool hasVideoExtension(const juce::File& file) {
    return htfx::isVideoExtension(file.getFullPathName());
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
        error = htfx::tr("error.unsupportedAudioFile") + file.getFileName();
        return false;
    }
    if (reader->lengthInSamples <= 0 ||
        reader->lengthInSamples > std::numeric_limits<int>::max()) {
        error = htfx::tr("error.mediaDurationInvalid");
        return false;
    }

    const int sourceSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> source(2, sourceSamples);
    if (!reader->read(&source, 0, sourceSamples, 0, true, true)) {
        error = htfx::tr("error.audioStreamDecodeFailed");
        return false;
    }

    const auto sourceRate = reader->sampleRate;
    if (!(sourceRate > 0.0)) {
        error = htfx::tr("error.audioStreamNoSampleRate");
        return false;
    }
    const auto targetSamples64 = static_cast<std::int64_t>(std::llround(
        static_cast<double>(sourceSamples) *
        HTDemucsGpuFXAudioProcessor::kSampleRate / sourceRate));
    if (targetSamples64 <= 0 ||
        targetSamples64 > std::numeric_limits<int>::max()) {
        error = htfx::tr("error.resampledAudioTooLarge");
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
        error = htfx::tr("error.noAudioToExport");
        return false;
    }
    if (!output.getParentDirectory().createDirectory()) {
        error = htfx::tr("error.couldNotCreateOutputFolder") +
                output.getParentDirectory().getFullPathName();
        return false;
    }
    const auto temporaryOutput = output.getParentDirectory().getNonexistentChildFile(
        output.getFileNameWithoutExtension() + ".htfx-part",
        output.getFileExtension(),
        false);

    std::unique_ptr<juce::OutputStream> stream = temporaryOutput.createOutputStream();
    if (stream == nullptr) {
        error = htfx::tr("error.couldNotOpenOutputFile") + output.getFullPathName();
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
        error = htfx::tr("error.couldNotCreateWavWriter");
        return false;
    }

    constexpr std::size_t blockSize = 1u << 20;
    for (std::size_t offset = 0; offset < sampleCount; offset += blockSize) {
        const auto count = static_cast<int>(
            (std::min)(blockSize, sampleCount - offset));
        const std::array<const float*, 2> channels{left + offset, right + offset};
        if (!writer->writeFromFloatArrays(channels.data(), 2, count)) {
            error = htfx::tr("error.wavWriteFailed");
            writer.reset();
            temporaryOutput.deleteFile();
            return false;
        }
    }
    writer.reset();
    if (output.existsAsFile() && !output.deleteFile()) {
        temporaryOutput.deleteFile();
        error = htfx::tr("error.couldNotReplaceExistingFile") +
                output.getFullPathName();
        return false;
    }
    if (!temporaryOutput.moveFileTo(output)) {
        temporaryOutput.deleteFile();
        error = htfx::tr("error.couldNotCommitOutputFile") +
                output.getFullPathName();
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
    purgeStaleRoformerWorkingDirectories();
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
        juce::StringArray{"htdemucs", "htdemucs_6s"},
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
    configuration.sourceCount = modelIndex == 1 ? 6 : 4;
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

    // Offer only the curated catalog: one representative per category, plus
    // the odd extra that is a different effect rather than a quality tier.
    // On the CPU runtime, also drop models measured too slow to be usable.
    // A missing catalog leaves the full manifest available, so a development
    // checkout without one keeps working.
    const auto catalogParsed =
        juce::JSON::parse(configuredRoformerCatalog().loadFileAsString());
    if (const auto* catalogRoot = catalogParsed.getDynamicObject()) {
        if (const auto* entries = catalogRoot->getProperty("models").getArray()) {
            const bool cpuRuntime = installedRuntimeFlavor() == "cpu";
            std::vector<RoformerModel> curated;
            for (const auto& entryValue : *entries) {
                const auto* entry = entryValue.getDynamicObject();
                if (entry == nullptr) {
                    continue;
                }
                const auto id = entry->getProperty("id").toString();
                const bool cpuCapable =
                    !entry->hasProperty("cpu") ||
                    static_cast<bool>(entry->getProperty("cpu"));
                if (cpuRuntime && !cpuCapable) {
                    continue;
                }
                for (const auto& model : loaded) {
                    if (model.id == id) {
                        auto kept = model;
                        kept.cpuCapable = cpuCapable;
                        curated.push_back(std::move(kept));
                        break;
                    }
                }
            }
            loaded = std::move(curated);
        }
    }

    const std::scoped_lock lock(roformerMutex_);
    roformerModels_ = std::move(loaded);
}

juce::String HTDemucsGpuFXAudioProcessor::getRuntimeFlavor() const {
    return installedRuntimeFlavor();
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

void HTDemucsGpuFXAudioProcessor::clearRoformerModel() {
    const std::scoped_lock lock(roformerMutex_);
    selectedRoformerModel_.clear();
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
            sampleRateSupported_ ? htfx::tr("status.switchToRecordModeFirst")
                                 : htfx::tr("status.recordingRequires44100Hz"));
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
        importedMediaFile_ = juce::File{};
        importedBaseName_ = "recording";
    }
    importedVideo_.store(false, std::memory_order_release);
    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage({});
    separationProgress_.store(0.0, std::memory_order_release);
    separationState_.store(SeparationState::recording, std::memory_order_release);
    setSeparationMessage(htfx::tr("status.recordingStereoInput"));
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
            ? htfx::tr("status.noInputRecorded")
            : htfx::tr("status.recordedPrefix") + juce::String(
                  static_cast<double>(samples) / kSampleRate, 1) +
                  htfx::tr("status.recordedSuffix"));
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
        setSeparationMessage(htfx::tr("status.waitForMediaOperation"));
        return false;
    }
    const auto configuration = currentRuntimeConfiguration();
    // RoFormer checkpoints are fetched on demand by the worker itself (it
    // downloads, verifies the SHA-256 and manages the rolling cache), so an
    // uninstalled RoFormer model must NOT block the run — only HTDemucs
    // checkpoints, which have no download path in this code path, do.
    if (!isRoformerModelName(juce::String::fromUTF8(configuration.modelName.c_str())) &&
        !isModelInstalled(configuration.modelName)) {
        // A missing RoFormer checkpoint never blocked the run - the worker
        // fetches it - so a missing HTDemucs one should not either. Kick off
        // the same downloader the advanced panel uses and let the run resume
        // on its own when the file lands.
        const auto modelName = juce::String(configuration.modelName);
        if (modelDownloadBusy_.load(std::memory_order_acquire)) {
            setSeparationMessage(
                htfx::tr("status.downloadingModelPrefix") + modelName +
                htfx::tr("status.downloadingModelSuffix"));
            return false;
        }
        if (beginModelDownload(modelName)) {
            {
                const juce::ScopedLock lock(pendingSeparationLock_);
                separationPendingModel_ = modelName;
            }
            setSeparationMessage(
                htfx::tr("status.downloadingModelPrefix") + modelName +
                htfx::tr("status.downloadingModelSuffix"));
            return false;
        }
        separationState_.store(SeparationState::error, std::memory_order_release);
        setSeparationMessage(
            htfx::tr("status.modelNotInstalledPrefix") + modelName +
            htfx::tr("status.modelNotInstalledSuffix"));
        return false;
    }
    endRecording();
    if (recordedLeft_.empty() || recordedLeft_.size() != recordedRight_.size()) {
        separationState_.store(SeparationState::error, std::memory_order_release);
        setSeparationMessage(htfx::tr("status.recordBeforeSeparating"));
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
    setSeparationMessage(htfx::tr("status.startingDemucsWorker"));
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
        setSeparationMessage(htfx::tr("status.cancellingAfterBlock"));
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
                    setSeparationMessage(htfx::tr("status.separationCancelled"));
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
            setSeparationMessage(htfx::tr("status.readyToPreviewFakeWorker"));
            return;
        }

        if (isRoformerModelName(juce::String::fromUTF8(configuration.modelName.c_str()))) {
            // The frozen runtime bundles the RoFormer back-end behind a
            // "roformer" subcommand, sharing its PyTorch with HTDemucs. When it
            // is installed the user needs no Python at all; otherwise fall back
            // to the interpreter + script route used during development.
            const auto frozenWorker = configuredWorkerExecutable();
            const bool useFrozen = !frozenWorker.empty();
            const auto python = configuredRoformerPython();
            const auto workerScript = configuredRoformerWorker();
            const auto modelsDirectory = configuredRoformerModelsDirectory();
            auto workingDirectory = configuredRoformerOutputDirectory()
                                        .getNonexistentChildFile(
                                            "cpp-route-" + juce::Uuid().toString(),
                                            {}, false);
            const auto inputFile = workingDirectory.getChildFile("input.wav");
            const auto outputDirectory = workingDirectory.getChildFile("stems");
            // This directory holds a full copy of the input plus every stem -
            // roughly 300 MB per run - and nothing reads it once the stems are
            // in memory; failures are reported from the captured diagnostics,
            // not from disk. Leaving them behind filled up a disk.
            struct ScratchGuard {
                juce::File directory;
                ~ScratchGuard() { directory.deleteRecursively(); }
            } scratchGuard{workingDirectory};
            if ((!useFrozen &&
                 (!python.existsAsFile() || !workerScript.existsAsFile())) ||
                !(modelsDirectory.isDirectory() ||
                  modelsDirectory.createDirectory().wasOk()) ||
                !workingDirectory.createDirectory() ||
                !outputDirectory.createDirectory()) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(
                    htfx::tr("status.roformerPathsUnavailable"));
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
            juce::StringArray command;
            if (useFrozen) {
                command.add(displayPath(frozenWorker));
                command.add("roformer");
            } else {
                command.add(python.getFullPathName());
                command.add(workerScript.getFullPathName());
            }
            command.addArray(juce::StringArray{
                "--input", inputFile.getFullPathName(),
                "--output-dir", outputDirectory.getFullPathName(),
                "--model", juce::String::fromUTF8(configuration.modelName.c_str()),
                "--models-dir", modelsDirectory.getFullPathName(),
                "--manifest", configuredRoformerManifest().getFullPathName(),
                "--device", device});
            juce::ChildProcess process;
            setSeparationMessage(
                htfx::tr("status.loadingRoformerPrefix") +
                juce::String::fromUTF8(configuration.modelName.c_str()) +
                " · " + device);
            if (!process.start(
                    command,
                    juce::ChildProcess::wantStdOut |
                        juce::ChildProcess::wantStdErr)) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(htfx::tr("status.couldNotStartRoformerWorker"));
                return;
            }

            const auto startedAt = juce::Time::getMillisecondCounterHiRes();
            juce::MemoryOutputStream diagnostics;
            // readProcessOutput busy-waits until it has filled the buffer it is
            // given, so the buffer size sets the progress latency: at ~40 bytes
            // per update, 4 KB would mean waiting for about a hundred of them -
            // more than an entire run emits - and every update landing at once
            // when the worker exits. This returns after a line or two.
            std::array<char, 64> outputBuffer{};
            separationState_.store(SeparationState::separating, std::memory_order_release);
            separationProgress_.store(-1.0, std::memory_order_release);
            roformerEstimatedSeconds_.store(0.0, std::memory_order_release);
            setSeparationMessage(htfx::tr("status.roformerStartingEngine"));
            // Split on raw bytes rather than decoded text: a read can end in
            // the middle of a multi-byte character, and decoding that fragment
            // would corrupt it. Line breaks are always single ASCII bytes, so
            // splitting first and decoding whole lines is safe. Upstream ends
            // its counter with a carriage return, so that counts as a break
            // too - otherwise the entire run is one unterminated line.
            std::string pendingLine;
            const auto drain = [this, &pendingLine](bool flushTail) {
                for (;;) {
                    const auto cut = pendingLine.find_first_of("\r\n");
                    if (cut == std::string::npos) {
                        break;
                    }
                    consumeRoformerProgressLine(
                        juce::String::fromUTF8(pendingLine.data(),
                                               static_cast<int>(cut)));
                    pendingLine.erase(0, cut + 1);
                }
                if (flushTail && !pendingLine.empty()) {
                    consumeRoformerProgressLine(
                        juce::String::fromUTF8(
                            pendingLine.data(),
                            static_cast<int>(pendingLine.size())));
                    pendingLine.clear();
                }
            };
            while (process.isRunning()) {
                if (stopToken.stop_requested()) {
                    process.kill();
                    separationState_.store(
                        SeparationState::cancelled, std::memory_order_release);
                    setSeparationMessage(htfx::tr("status.roformerSeparationCancelled"));
                    return;
                }
                const int bytesRead = process.readProcessOutput(
                    outputBuffer.data(), static_cast<int>(outputBuffer.size()));
                if (bytesRead > 0) {
                    diagnostics.write(outputBuffer.data(), static_cast<std::size_t>(bytesRead));
                    pendingLine.append(outputBuffer.data(),
                                       static_cast<std::size_t>(bytesRead));
                    drain(false);
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
                pendingLine.append(outputBuffer.data(),
                                   static_cast<std::size_t>(bytesRead));
                drain(false);
            }
            drain(true);
            if (process.getExitCode() != 0) {
                auto message = juce::String::fromUTF8(
                    static_cast<const char*>(diagnostics.getData()),
                    static_cast<int>(diagnostics.getDataSize())).trim();
                if (message.length() > 1800) {
                    message = message.substring(message.length() - 1800);
                }
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(
                    htfx::tr("status.roformerWorkerFailedPrefix") +
                    juce::String(process.getExitCode()) +
                    htfx::tr("status.roformerWorkerFailedSuffix") + message);
                return;
            }

            juce::Array<juce::File> outputFiles;
            outputDirectory.findChildFiles(
                outputFiles, juce::File::findFiles, false, "*.wav");
            if (outputFiles.size() != 2) {
                separationState_.store(SeparationState::error, std::memory_order_release);
                setSeparationMessage(
                    htfx::tr("status.roformerWrongStemCountPrefix") +
                    juce::String(outputFiles.size()) +
                    htfx::tr("status.roformerWrongStemCountSuffix"));
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
                        error.isNotEmpty()
                            ? error
                            : htfx::tr("status.roformerStemDurationMismatch"));
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
                htfx::tr("status.readyToPreviewPrefix") +
                juce::String::fromUTF8(configuration.modelName.c_str()) +
                htfx::tr("status.readyToPreviewRoformerSuffix"));
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

        // This names the device that was *asked* for, before the worker has
        // resolved one. Auto is its own answer and must not claim CUDA: on a
        // Mac auto resolves to Metal, so the old three-way mapping told every
        // Apple Silicon user their model was loading on a CUDA GPU. The
        // vocabulary matches getResolvedDeviceName().
        const auto requestedDevice =
            configuration.backend == 1   ? juce::String{"CUDA GPU"}
            : configuration.backend == 2 ? juce::String{"CPU"}
            : configuration.backend == 3 ? juce::String{"Apple Metal (MPS)"}
                                         : juce::String{"Auto"};
        setSeparationMessage(
            htfx::tr("status.loadingModelPrefix") +
            juce::String(configuration.modelName) +
            htfx::tr("status.loadingModelDeviceMiddle") + requestedDevice +
            htfx::tr("status.loadingModelDeviceSuffix"));
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
            (resolved == 2 ? htfx::tr("status.cpuInferenceSlowSuffix")
                           : juce::String()));

        const std::size_t sampleCount = left.size();
        const std::size_t hopSamples = static_cast<std::size_t>(configuration.hopSamples);
        const std::size_t overlapSamples =
            static_cast<std::size_t>(configuration.overlapSamples);
        const std::size_t realHops = (sampleCount + hopSamples - 1) / hopSamples;
        const std::size_t totalHops = realHops + 1;  // Flush the OLA tail.
        separationProgress_.store(0.05, std::memory_order_release);
        setSeparationMessage(
            htfx::tr("status.separatingPrefix") +
            juce::String(resolved == 2 ? "CPU" : "GPU") +
            htfx::tr("status.separatingBlockMiddle") + "0/" +
            juce::String(totalHops));
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
                setSeparationMessage(htfx::tr("status.separationCancelled"));
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
                htfx::tr("status.separatingPrefix") +
                juce::String(resolved == 2 ? "CPU" : "GPU") +
                htfx::tr("status.separatingBlockMiddle") +
                juce::String(hopIndex + 1) + "/" +
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
            htfx::tr("status.readyToPreviewPrefix") +
            juce::String(configuration.modelName) + " · " +
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
            const auto lastIndex = static_cast<std::size_t>(result->sampleCount - 1);
            const auto first = (std::min)(static_cast<std::size_t>(cursor), lastIndex);
            const auto second = (std::min)(first + 1, lastIndex);
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

juce::File HTDemucsGpuFXAudioProcessor::getLastExportedFile() const {
    const juce::ScopedLock lock(mediaMessageLock_);
    return lastExportedFile_;
}

void HTDemucsGpuFXAudioProcessor::setLastExportedFile(const juce::File& target) {
    const juce::ScopedLock lock(mediaMessageLock_);
    lastExportedFile_ = target;
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

void HTDemucsGpuFXAudioProcessor::purgeStaleRoformerWorkingDirectories() const {
    // Runs before this fix - and any run killed mid-flight - left their scratch
    // directories behind. Sweep them once at startup so the disk usage cannot
    // keep growing across updates.
    const auto root = configuredRoformerOutputDirectory();
    if (!root.isDirectory()) {
        return;
    }
    // Only directories nobody can still be using: a second instance of the
    // app (or a test running beside it) may own a fresh one, and deleting it
    // under that run would fail its separation.
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours(1);
    for (const auto& entry : juce::RangedDirectoryIterator(
             root, false, "cpp-route-*", juce::File::findDirectories)) {
        if (entry.getModificationTime() < cutoff) {
            entry.getFile().deleteRecursively();
        }
    }
}

juce::String formatDurationForStatus(double seconds) {
    const auto whole = juce::roundToInt(seconds);
    if (whole < 60) {
        return juce::String(whole) + htfx::tr("status.secondsSuffix");
    }
    return juce::String(whole / 60) + htfx::tr("status.minutesSuffix") +
           juce::String(whole % 60) + htfx::tr("status.secondsSuffix");
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
        setMediaMessage(htfx::tr("status.cancellingMediaOperation"));
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

void HTDemucsGpuFXAudioProcessor::consumeRoformerProgressLine(
    const juce::String& line) {
    const auto trimmed = line.trim();
    if (trimmed.isEmpty()) {
        return;
    }

    // Structured markers the worker emits for the phases it controls.
    if (trimmed.startsWith("HTFX_PROGRESS ")) {
        const auto payload =
            juce::JSON::parse(trimmed.fromFirstOccurrenceOf(" ", false, false));
        const auto stage = payload.getProperty("stage", {}).toString();
        const auto detail = payload.getProperty("message", {}).toString();
        const auto fractionVar = payload.getProperty("fraction", {});

        if (stage == "prepare") {
            setSeparationMessage(htfx::tr("status.roformerPreparing"));
            separationProgress_.store(-1.0, std::memory_order_release);
        } else if (stage == "download") {
            setSeparationMessage(
                htfx::tr("status.roformerDownloading") + detail);
            if (!fractionVar.isVoid()) {
                separationProgress_.store(
                    static_cast<double>(fractionVar), std::memory_order_release);
            }
        } else if (stage == "load") {
            setSeparationMessage(htfx::tr("status.roformerLoadingModel") + detail);
            separationProgress_.store(-1.0, std::memory_order_release);
        } else if (stage == "infer") {
            setSeparationMessage(htfx::tr("status.roformerSeparating"));
            separationProgress_.store(0.0, std::memory_order_release);
        } else if (stage == "verify") {
            setSeparationMessage(htfx::tr("status.roformerVerifying"));
            separationProgress_.store(1.0, std::memory_order_release);
        }
        return;
    }

    // The upstream separator prints its own estimate once per chunk. It is the
    // only progress signal available during inference, which is by far the
    // longest phase, so it drives the bar rather than being thrown away.
    const auto readSeconds = [&trimmed](const char* prefix) -> double {
        const auto index = trimmed.indexOf(prefix);
        if (index < 0) {
            return -1.0;
        }
        return trimmed.substring(index + static_cast<int>(std::strlen(prefix)))
            .trim()
            .upToFirstOccurrenceOf(" ", false, false)
            .getDoubleValue();
    };

    const auto total = readSeconds("Estimated total processing time for this track:");
    if (total > 0.0) {
        roformerEstimatedSeconds_.store(total, std::memory_order_release);
        setSeparationMessage(
            htfx::tr("status.roformerSeparating") + " · " +
            htfx::tr("status.roformerRemainingPrefix") +
            formatDurationForStatus(total));
        separationProgress_.store(0.0, std::memory_order_release);
        return;
    }

    const auto remaining = readSeconds("Estimated time remaining:");
    if (remaining >= 0.0) {
        const auto estimate = roformerEstimatedSeconds_.load(std::memory_order_acquire);
        if (estimate > 0.0) {
            separationProgress_.store(
                juce::jlimit(0.0, 1.0, 1.0 - (remaining / estimate)),
                std::memory_order_release);
        }
        setSeparationMessage(
            htfx::tr("status.roformerSeparating") + " · " +
            htfx::tr("status.roformerRemainingPrefix") +
            formatDurationForStatus(remaining));
    }
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
        resumeSeparationAfterModelDownload(modelName);
        modelDownloadBusy_.store(false, std::memory_order_release);
        return;
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
        {
            // Do not leave a separation waiting on a download that failed.
            const juce::ScopedLock lock(pendingSeparationLock_);
            if (separationPendingModel_ == modelName) {
                separationPendingModel_.clear();
                separationState_.store(
                    SeparationState::error, std::memory_order_release);
                setSeparationMessage(getModelDownloadStatusText());
            }
        }
    }
    modelDownloadBusy_.store(false, std::memory_order_release);
}

void HTDemucsGpuFXAudioProcessor::resumeSeparationAfterModelDownload(
    const juce::String& modelName) {
    {
        const juce::ScopedLock lock(pendingSeparationLock_);
        if (separationPendingModel_ != modelName) {
            // Either nothing was waiting, or the user has since picked a
            // different model - starting a run now would surprise them.
            return;
        }
        separationPendingModel_.clear();
    }
    // Resume on this thread rather than posting to the message thread: the
    // run does not touch any UI, and a host or tool without a running dispatch
    // loop would otherwise never start it.
    const auto configuration = currentRuntimeConfiguration();
    if (juce::String(configuration.modelName) != modelName) {
        return;
    }
    beginSeparation();
}

// == Multi-clip support ==================================================
// One Clip per imported file. The active clip owns the preview transport and
// the mixer parameters; switching clips swaps both, so every file keeps its
// own fader positions and its own separation result.

void HTDemucsGpuFXAudioProcessor::storeActiveClipMixerState() {
    const auto index = activeClipIndex_.load(std::memory_order_acquire);
    const juce::ScopedLock lock(clipsLock_);
    if (index < 0 || index >= static_cast<int>(clips_.size())) {
        return;
    }
    auto& clip = clips_[static_cast<std::size_t>(index)];
    for (std::size_t stem = 0; stem < kStemParameterIds.size(); ++stem) {
        if (auto* raw = parameters_.getRawParameterValue(kStemParameterIds[stem])) {
            clip.stemGains[stem] = raw->load(std::memory_order_relaxed);
        }
    }
    if (auto* trim = parameters_.getRawParameterValue("outputTrim")) {
        clip.outputTrim = trim->load(std::memory_order_relaxed);
    }
}

void HTDemucsGpuFXAudioProcessor::applyClipMixerState(const Clip& clip) {
    for (std::size_t stem = 0; stem < kStemParameterIds.size(); ++stem) {
        if (auto* parameter = parameters_.getParameter(kStemParameterIds[stem])) {
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(clip.stemGains[stem]));
        }
    }
    if (auto* trim = parameters_.getParameter("outputTrim")) {
        trim->setValueNotifyingHost(trim->convertTo0to1(clip.outputTrim));
    }
}

// Caller must hold clipsLock_.
void HTDemucsGpuFXAudioProcessor::activateClipLocked(int index) {
    if (index < 0 || index >= static_cast<int>(clips_.size())) {
        return;
    }
    const auto& clip = clips_[static_cast<std::size_t>(index)];
    activeClipIndex_.store(index, std::memory_order_release);
    recordedLeft_ = clip.left;
    recordedRight_ = clip.right;
    recordedSamples_.store(recordedLeft_.size(), std::memory_order_release);
    previewPlaying_.store(false, std::memory_order_release);
    previewCursor_.store(0, std::memory_order_release);
    previewResult_.store(clip.result, std::memory_order_release);
    if (clip.result != nullptr) {
        activeSourceCount_.store(clip.result->sourceCount, std::memory_order_release);
        separationState_.store(
            SeparationState::previewReady, std::memory_order_release);
        separationProgress_.store(1.0, std::memory_order_release);
    } else {
        separationState_.store(SeparationState::recorded, std::memory_order_release);
        separationProgress_.store(0.0, std::memory_order_release);
    }
    {
        const juce::ScopedLock metadata(mediaMetadataLock_);
        importedMediaFile_ = clip.sourceFile;
        importedBaseName_ = clip.sourceFile.getFileNameWithoutExtension();
    }
    importedVideo_.store(clip.fromVideo, std::memory_order_release);
}

int HTDemucsGpuFXAudioProcessor::getClipCount() const {
    const juce::ScopedLock lock(clipsLock_);
    return static_cast<int>(clips_.size());
}

HTDemucsGpuFXAudioProcessor::ClipInfo HTDemucsGpuFXAudioProcessor::getClipInfo(
    int index) const {
    const juce::ScopedLock lock(clipsLock_);
    ClipInfo info;
    if (index < 0 || index >= static_cast<int>(clips_.size())) {
        return info;
    }
    const auto& clip = clips_[static_cast<std::size_t>(index)];
    info.name = clip.name;
    info.selected = clip.selected;
    info.separated = clip.result != nullptr;
    // Stored as a string-table key so the list follows a language switch.
    info.status = htfx::tr(clip.status);
    info.seconds = static_cast<double>(clip.left.size()) / kSampleRate;
    return info;
}

void HTDemucsGpuFXAudioProcessor::setActiveClip(int index) {
    if (index == activeClipIndex_.load(std::memory_order_acquire)) {
        return;
    }
    storeActiveClipMixerState();
    std::array<float, kMaxSources> gains{};
    float trim = 0.0f;
    {
        const juce::ScopedLock lock(clipsLock_);
        if (index < 0 || index >= static_cast<int>(clips_.size())) {
            return;
        }
        activateClipLocked(index);
        gains = clips_[static_cast<std::size_t>(index)].stemGains;
        trim = clips_[static_cast<std::size_t>(index)].outputTrim;
    }
    Clip snapshot;
    snapshot.stemGains = gains;
    snapshot.outputTrim = trim;
    applyClipMixerState(snapshot);
}

void HTDemucsGpuFXAudioProcessor::setClipSelected(int index, bool selected) {
    const juce::ScopedLock lock(clipsLock_);
    if (index >= 0 && index < static_cast<int>(clips_.size())) {
        clips_[static_cast<std::size_t>(index)].selected = selected;
    }
}

int HTDemucsGpuFXAudioProcessor::getSelectedClipCount() const {
    const juce::ScopedLock lock(clipsLock_);
    return static_cast<int>(std::count_if(
        clips_.begin(), clips_.end(),
        [](const Clip& clip) { return clip.selected; }));
}

bool HTDemucsGpuFXAudioProcessor::beginMultiMediaImport(
    const juce::Array<juce::File>& mediaFiles) {
    if (mediaFiles.isEmpty()) {
        return false;
    }
    if (getOperatingMode() != OperatingMode::record) {
        setMediaMessage(htfx::tr("status.switchToRecordModeBeforeImport"));
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        setMediaMessage(htfx::tr("status.mediaBusyRetryLater"));
        return false;
    }
    stopMediaThread();
    bool expected = false;
    if (!mediaBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        setMediaMessage(htfx::tr("status.mediaBusyRetryLater"));
        return false;
    }
    mediaTask_.store(MediaTask::import_, std::memory_order_release);
    recording_.store(false, std::memory_order_release);
    stopRecordingThread();
    stopSeparationThread();
    recordRing_.clearWhenStopped();
    setLastExportedFile({});  // the new clips have not been exported
    {
        const juce::ScopedLock lock(clipsLock_);
        clips_.clear();
        activeClipIndex_.store(-1, std::memory_order_release);
    }
    previewPlaying_.store(false, std::memory_order_release);
    previewCursor_.store(0, std::memory_order_release);
    previewResult_.store(
        std::shared_ptr<const SeparationResult>{}, std::memory_order_release);
    mediaProgress_.store(0.0, std::memory_order_release);
    separationProgress_.store(0.0, std::memory_order_release);
    separationState_.store(SeparationState::loading, std::memory_order_release);

    juce::Array<juce::File> files(mediaFiles);
    mediaThread_ = std::jthread([this, files](std::stop_token stopToken) {
        try {
            const int total = files.size();
            int imported = 0;
            juce::StringArray skipped;  // files that could not be decoded
            for (int index = 0; index < total; ++index) {
                if (stopToken.stop_requested()) {
                    break;
                }
                const auto file = files.getReference(index);
                setMediaMessage(
                    htfx::tr("status.mediaImportInProgress") + " " +
                    juce::String(index + 1) + "/" + juce::String(total) + " - " +
                    file.getFileName());
                std::vector<float> left;
                std::vector<float> right;
                juce::String decodeError;
                const bool video = hasVideoExtension(file);
                bool decoded = decodeMediaWithFfmpeg(
                    file, stopToken, left, right, decodeError);
                if (!decoded && !video && !stopToken.stop_requested()) {
                    juce::String fallbackError;
                    decoded = readAudioFileAtProjectRate(
                        file, left, right, fallbackError);
                }
                if (!decoded || left.empty() || left.size() != right.size()) {
                    skipped.add(file.getFileName());
                    continue;
                }
                Clip clip;
                clip.sourceFile = file;
                clip.name = file.getFileName();
                clip.left = std::move(left);
                clip.right = std::move(right);
                clip.fromVideo = video;
                clip.status = juce::String("clip.statusPending");
                {
                    const juce::ScopedLock lock(clipsLock_);
                    clips_.push_back(std::move(clip));
                    if (clips_.size() == 1) {
                        activateClipLocked(0);
                    }
                }
                ++imported;
                mediaProgress_.store(
                    static_cast<double>(index + 1) / juce::jmax(1, total),
                    std::memory_order_release);
            }
            if (stopToken.stop_requested()) {
                // Cancelled by the user: not an error, and not "imported N".
                separationState_.store(SeparationState::cancelled, std::memory_order_release);
                setSeparationMessage(htfx::tr("status.mediaImportCancelled"));
                setMediaMessage(htfx::tr("status.mediaImportCancelled"));
                mediaBusy_.store(false, std::memory_order_release);
                return;
            }
            auto message = htfx::tr("clip.importedCountPrefix") +
                           juce::String(imported) +
                           htfx::tr("clip.importedCountSuffix");
            if (!skipped.isEmpty()) {
                // Say which files were dropped rather than silently
                // importing fewer than were chosen (first few names only:
                // a folder of hundreds must not become a screen-wide line).
                juce::StringArray named;
                for (int i = 0; i < juce::jmin(5, skipped.size()); ++i) {
                    named.add(skipped[i]);
                }
                message += htfx::tr("clip.importSkippedPrefix") +
                           juce::String(skipped.size()) +
                           htfx::tr("clip.importSkippedMiddle") +
                           named.joinIntoString(", ") +
                           (skipped.size() > named.size() ? htfx::tr("clip.importSkippedMore")
                                                          : juce::String());
            }
            // P1: nothing decoded => the state must not stay at "loading",
            // which would keep every control disabled with no way out.
            if (imported == 0) {
                separationState_.store(SeparationState::error, std::memory_order_release);
            }
            setSeparationMessage(message);
            setMediaMessage(message);
            mediaProgress_.store(1.0, std::memory_order_release);
            mediaBusy_.store(false, std::memory_order_release);
        } catch (const std::exception& exception) {
            const auto message = htfx::tr("status.mediaImportFailedPrefix") +
                                 juce::String::fromUTF8(exception.what());
            separationState_.store(SeparationState::error, std::memory_order_release);
            setSeparationMessage(message);
            setMediaMessage(message);
            mediaBusy_.store(false, std::memory_order_release);
        }
    });
    return true;
}

// Separates every clip in turn. Each finished clip stores its own result and
// becomes previewable immediately, so the user can audition clip 1 while clip
// 2 is still running.
bool HTDemucsGpuFXAudioProcessor::beginBatchSeparation() {
    if (getClipCount() <= 1) {
        return beginSeparation();
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        setSeparationMessage(htfx::tr("status.waitForMediaOperation"));
        return false;
    }
    if (batchThread_.joinable()) {
        batchThread_.request_stop();
        batchThread_.join();
    }
    storeActiveClipMixerState();
    batchBusy_.store(true, std::memory_order_release);
    batchThread_ = std::jthread([this](std::stop_token stopToken) {
        batchSeparationLoop(stopToken);
    });
    return true;
}

void HTDemucsGpuFXAudioProcessor::batchSeparationLoop(std::stop_token stopToken) {
    mediaTask_.store(MediaTask::batchSeparate, std::memory_order_release);
    batchIndex_.store(0, std::memory_order_release);
    batchTotal_.store(getClipCount(), std::memory_order_release);
    // Hold the batch flag for the whole loop, whichever way it exits.
    struct BatchGuard {
        std::atomic<bool>& flag;
        explicit BatchGuard(std::atomic<bool>& f) : flag(f) { flag.store(true, std::memory_order_release); }
        ~BatchGuard() { flag.store(false, std::memory_order_release); }
    } batchGuard{batchBusy_};
    const int total = getClipCount();
    for (int index = 0; index < total; ++index) {
        if (stopToken.stop_requested()) {
            break;
        }
        {
            const juce::ScopedLock lock(clipsLock_);
            if (index >= static_cast<int>(clips_.size()) ||
                clips_[static_cast<std::size_t>(index)].result != nullptr) {
                continue;
            }
        }
        // Route through the existing single-clip separation path by making the
        // target clip active, then waiting for its result.
        batchIndex_.store(index + 1, std::memory_order_release);
        setActiveClip(index);
        {
            const juce::ScopedLock lock(clipsLock_);
            if (index < static_cast<int>(clips_.size())) {
                clips_[static_cast<std::size_t>(index)].status =
                    juce::String("clip.statusSeparating");
            }
        }
        if (!beginSeparation()) {
            const juce::ScopedLock lock(clipsLock_);
            if (index < static_cast<int>(clips_.size())) {
                clips_[static_cast<std::size_t>(index)].status =
                    juce::String("clip.statusFailed");
            }
            continue;
        }
        while (!stopToken.stop_requested()) {
            const auto state = separationState_.load(std::memory_order_acquire);
            if (state == SeparationState::previewReady ||
                state == SeparationState::error ||
                state == SeparationState::cancelled) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        const auto result = previewResult_.load(std::memory_order_acquire);
        const juce::ScopedLock lock(clipsLock_);
        if (index < static_cast<int>(clips_.size())) {
            auto& clip = clips_[static_cast<std::size_t>(index)];
            clip.result = result;
            clip.status = result != nullptr ? juce::String("clip.statusDone")
                                            : juce::String("clip.statusFailed");
        }
    }
    setSeparationMessage(htfx::tr("clip.batchSeparationFinished"));
}

// Exports every ticked clip into `folder`. Clips with no separation result
// yet are separated first, so import -> export works without pressing
// Separate. Each clip is exported with its own mixer settings.
bool HTDemucsGpuFXAudioProcessor::beginBatchExport(
    const juce::File& folder, QuickExportKind kind) {
    if (getSelectedClipCount() == 0) {
        setMediaMessage(htfx::tr("clip.noneSelected"));
        return false;
    }
    if (batchThread_.joinable()) {
        batchThread_.request_stop();
        batchThread_.join();
    }
    storeActiveClipMixerState();
    // Raise the flag here, not only inside the loop: the thread may not have
    // started by the time this returns, and a caller polling isBatchBusy()
    // immediately would otherwise conclude the batch already finished.
    batchBusy_.store(true, std::memory_order_release);
    batchThread_ = std::jthread(
        [this, folder, kind](std::stop_token stopToken) {
            batchExportLoop(stopToken, folder, kind);
        });
    return true;
}

void HTDemucsGpuFXAudioProcessor::batchExportLoop(
    std::stop_token stopToken, juce::File folder, QuickExportKind kind) {
    mediaTask_.store(MediaTask::batchExport, std::memory_order_release);
    batchIndex_.store(0, std::memory_order_release);
    batchTotal_.store(getClipCount(), std::memory_order_release);
    // Hold the batch flag for the whole loop, whichever way it exits.
    struct BatchGuard {
        std::atomic<bool>& flag;
        explicit BatchGuard(std::atomic<bool>& f) : flag(f) { flag.store(true, std::memory_order_release); }
        ~BatchGuard() { flag.store(false, std::memory_order_release); }
    } batchGuard{batchBusy_};
    folder.createDirectory();
    const int total = getClipCount();
    int exported = 0;
    // Output names written by this batch. Two clips can share a base name
    // (song.mp3 and song.flac, or the same name from two folders); the
    // second one gets its source extension, then a counter, so nothing in
    // the batch overwrites another clip's export.
    juce::StringArray namesUsed;
    for (int index = 0; index < total; ++index) {
        if (stopToken.stop_requested()) {
            break;
        }
        juce::String baseName;
        juce::String sourceExtension;
        bool selected = false;
        bool hasResult = false;
        {
            const juce::ScopedLock lock(clipsLock_);
            if (index >= static_cast<int>(clips_.size())) {
                break;
            }
            const auto& clip = clips_[static_cast<std::size_t>(index)];
            selected = clip.selected;
            hasResult = clip.result != nullptr;
            baseName = clip.sourceFile.getFileNameWithoutExtension();
            sourceExtension = clip.sourceFile.getFileExtension().trimCharactersAtStart(".");
        }
        if (!selected) {
            continue;
        }
        setMediaMessage(
            htfx::tr("clip.exportingPrefix") + baseName + " (" +
            juce::String(index + 1) + "/" + juce::String(total) + ")");

        // Make this clip active so the existing single-clip export path, which
        // reads previewResult_ plus the live mixer parameters, operates on it.
        batchIndex_.store(index + 1, std::memory_order_release);
        setActiveClip(index);

        if (!hasResult) {
            if (!beginSeparation()) {
                continue;
            }
            while (!stopToken.stop_requested()) {
                const auto state = separationState_.load(std::memory_order_acquire);
                if (state == SeparationState::previewReady ||
                    state == SeparationState::error ||
                    state == SeparationState::cancelled) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            const auto produced = previewResult_.load(std::memory_order_acquire);
            {
                const juce::ScopedLock lock(clipsLock_);
                if (index < static_cast<int>(clips_.size())) {
                    auto& clip = clips_[static_cast<std::size_t>(index)];
                    clip.result = produced;
                    clip.status = produced != nullptr
                                      ? juce::String("clip.statusDone")
                                      : juce::String("clip.statusFailed");
                }
            }
            if (produced == nullptr) {
                continue;
            }
        }

        const juce::String suffix =
            kind == QuickExportKind::vocals ? "_vocals" : "_accompany";
        juce::String outputName = baseName + suffix + ".wav";
        if (namesUsed.contains(outputName, true)) {
            outputName = baseName + " (" + sourceExtension + ")" + suffix + ".wav";
            for (int counter = 2; namesUsed.contains(outputName, true); ++counter) {
                outputName = baseName + " (" + sourceExtension + " " +
                             juce::String(counter) + ")" + suffix + ".wav";
            }
        }
        const auto outputFile = folder.getChildFile(outputName);
        if (!beginQuickExport(outputFile, kind)) {
            continue;
        }
        namesUsed.add(outputName);
        // beginQuickExport runs on the media thread; wait for it so the files
        // are written one at a time and in order.
        while (!stopToken.stop_requested() &&
               mediaBusy_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (outputFile.existsAsFile()) {
            ++exported;
        }
    }
    if (exported > 0) {
        setLastExportedFile(folder);
    }
    setMediaMessage(
        htfx::tr("clip.exportFinishedPrefix") + juce::String(exported) +
        htfx::tr("clip.exportFinishedSuffix"));
}

bool HTDemucsGpuFXAudioProcessor::beginMediaImport(const juce::File& mediaFile) {
    if (getOperatingMode() != OperatingMode::record) {
        setMediaMessage(htfx::tr("status.switchToRecordModeBeforeImport"));
        return false;
    }
    if (!mediaFile.existsAsFile()) {
        // Windows file APIs stop at 260 characters unless the process is
        // long-path aware; say so instead of "not found".
#if JUCE_WINDOWS
        const bool tooLong = mediaFile.getFullPathName().length() > 259;
#else
        const bool tooLong = false;
#endif
        setMediaMessage(tooLong ? htfx::tr("status.mediaPathTooLong")
                                : htfx::tr("status.selectedMediaFileNotFound"));
        return false;
    }
    if (mediaBusy_.load(std::memory_order_acquire)) {
        setMediaMessage(htfx::tr("status.mediaBusyRetryLater"));
        return false;
    }
    stopMediaThread();
    bool expected = false;
    if (!mediaBusy_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        setMediaMessage(htfx::tr("status.mediaBusyRetryLater"));
        return false;
    }

    mediaTask_.store(MediaTask::import_, std::memory_order_release);
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
    setLastExportedFile({});  // the new clip has not been exported
    separationProgress_.store(0.0, std::memory_order_release);
    separationState_.store(SeparationState::loading, std::memory_order_release);
    mediaProgress_.store(0.01, std::memory_order_release);
    importedVideo_.store(hasVideoExtension(mediaFile), std::memory_order_release);
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        importedMediaFile_ = mediaFile;
        importedBaseName_ = legalMediaBaseName(mediaFile);
    }
    setMediaMessage(htfx::tr("status.importingPrefix") + mediaFile.getFileName());
    setSeparationMessage(htfx::tr("status.decodingImportedMedia"));
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
            setSeparationMessage(htfx::tr("status.mediaImportCancelled"));
            setMediaMessage(htfx::tr("status.mediaImportCancelled"));
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
            htfx::tr("status.importedPrefix") + mediaFile.getFileName() + " (" +
            juce::String(duration, 1) + htfx::tr("status.importedSuffix");
        setSeparationMessage(message);
        setMediaMessage(message);
        mediaProgress_.store(1.0, std::memory_order_release);
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        const auto message =
            htfx::tr("status.mediaImportFailedPrefix") +
            juce::String::fromUTF8(exception.what());
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
        setMediaMessage(htfx::tr("status.separateBeforeExportingStems"));
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
        setMediaMessage(htfx::tr("status.selectAtLeastOneStemToExport"));
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
    mediaTask_.store(MediaTask::stemExport, std::memory_order_release);
    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage(htfx::tr("status.exportingOriginalVolumeStems"));
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
                setMediaMessage(htfx::tr("status.stemExportCancelled"));
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
        setLastExportedFile(outputDirectory);
        setMediaMessage(
            htfx::tr("status.stemExportSuccessPrefix") +
            juce::String(sourceIndices.size()) +
            htfx::tr("status.stemExportSuccessMiddle") +
            outputDirectory.getFullPathName());
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        setMediaMessage(
            htfx::tr("status.stemExportFailedPrefix") +
            juce::String::fromUTF8(exception.what()));
        mediaBusy_.store(false, std::memory_order_release);
    }
}

bool HTDemucsGpuFXAudioProcessor::beginQuickExport(
    const juce::File& requestedOutputFile,
    QuickExportKind kind) {
    auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        setMediaMessage(htfx::tr("status.separateBeforeQuickExport"));
        return false;
    }
    // Quick export supports HTDemucs (4/6-stem: vocals = source 3, accompany =
    // the rest) and any RoFormer 2-stem result (target/residual pair).
    if (result->sourceCount == 2) {
        // ok: RoFormer two-stem
    } else if (result->sourceCount < 4) {
        setMediaMessage(htfx::tr("status.quickExportRequiresHtdemucs"));
        return false;
    }

    const auto outputFile = requestedOutputFile.withFileExtension(".wav");
    juce::File originalMediaFile;
    {
        const juce::ScopedLock lock(mediaMetadataLock_);
        originalMediaFile = importedMediaFile_;
    }
    if (originalMediaFile.existsAsFile() && outputFile == originalMediaFile) {
        setMediaMessage(htfx::tr("status.chooseDifferentOutputNameProtected"));
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
    mediaTask_.store(kind == QuickExportKind::vocals ? MediaTask::quickExportVocals
                                                     : MediaTask::quickExportAccompaniment,
                     std::memory_order_release);
    setMediaMessage(
        kind == QuickExportKind::vocals
            ? htfx::tr("status.exportingVocalsOriginalLevel")
            : htfx::tr("status.exportingAccompanyOriginalLevel"));
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
        constexpr std::size_t cancellationBlock = 1u << 18;
        // Two-stem (RoFormer) results carry a target/residual pair; prefer the
        // stem actually labelled "vocals", else treat stem 0 as the target.
        const bool twoStem = result->sourceCount == 2;
        int vocalsSource = 3;
        int residualSource = -1;
        if (twoStem) {
            vocalsSource = 0;
            residualSource = 1;
            for (std::size_t index = 0; index < result->stemLabels.size() && index < 2;
                 ++index) {
                if (juce::String(result->stemLabels[index].c_str())
                        .equalsIgnoreCase("vocals")) {
                    vocalsSource = static_cast<int>(index);
                    residualSource = vocalsSource == 0 ? 1 : 0;
                    break;
                }
            }
        }

        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            if (sample % cancellationBlock == 0) {
                if (stopToken.stop_requested()) {
                    setMediaMessage(htfx::tr("status.quickExportCancelled"));
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

            if (twoStem) {
                const auto leftPlane = static_cast<std::size_t>(residualSource) * 2;
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

        // Apply the output gain the preview is already playing through, so
        // what the user heard is what lands in the file. The stem faders stay
        // out of it: a vocals-only export is one stem, not the mix those
        // faders describe. At the default 0 dB this changes nothing.
        const float trim = currentMixSettings().outputTrim;
        if (trim != 1.0f) {
            for (std::size_t sample = 0; sample < sampleCount; ++sample) {
                outputLeft[sample] *= trim;
                outputRight[sample] *= trim;
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
        setLastExportedFile(outputFile);
        setMediaMessage(
            (kind == QuickExportKind::vocals
                 ? htfx::tr("status.quickExportedVocalsPrefix")
                 : htfx::tr("status.quickExportedAccompanyPrefix")) +
            outputFile.getFullPathName());
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        setMediaMessage(
            htfx::tr("status.quickExportFailedPrefix") +
            juce::String::fromUTF8(exception.what()));
        mediaBusy_.store(false, std::memory_order_release);
    }
}

bool HTDemucsGpuFXAudioProcessor::beginMixExport(
    const juce::File& requestedOutputFile,
    bool replaceVideoAudio) {
    auto result = previewResult_.load(std::memory_order_acquire);
    if (result == nullptr) {
        setMediaMessage(htfx::tr("status.separateBeforeExportingMix"));
        return false;
    }
    if (replaceVideoAudio && !importedFromVideo()) {
        setMediaMessage(htfx::tr("status.videoExportRequiresImportedVideo"));
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
        setMediaMessage(htfx::tr("status.chooseDifferentOutputNameProtected"));
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
    mediaTask_.store(replaceVideoAudio ? MediaTask::mixExportVideo : MediaTask::mixExport,
                     std::memory_order_release);
    mediaProgress_.store(0.0, std::memory_order_release);
    setMediaMessage(
        replaceVideoAudio ? htfx::tr("status.mixingReplacingVideoAudio")
                          : htfx::tr("status.exportingCurrentInterfaceMix"));
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
                    setMediaMessage(htfx::tr("status.mixExportCancelled"));
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
            setLastExportedFile(outputFile);
            setMediaMessage(
                htfx::tr("status.mixExportedPrefix") +
                outputFile.getFullPathName());
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
        // Stream-copy the video when MP4 can carry it (H.264, HEVC, MPEG-4,
        // AV1...). Sources MP4 cannot hold as-is -- VP8/VP9 from WebM, WMV,
        // MPEG-1 -- are re-encoded instead: H.264 through OpenH264 first, the
        // native MPEG-4 encoder as the last resort. Both are in the LGPL
        // FFmpeg build the app ships with.
        const std::array<juce::StringArray, 3> videoCodecs{
            juce::StringArray{"-c:v", "copy"},
            juce::StringArray{"-c:v", "libopenh264", "-pix_fmt", "yuv420p", "-b:v", "6M",
                              "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2"},
            juce::StringArray{"-c:v", "mpeg4", "-q:v", "3", "-pix_fmt", "yuv420p",
                              "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2"}};
        bool muxed = false;
        bool containerProblem = false;
        for (std::size_t attempt = 0; attempt < videoCodecs.size() && !muxed; ++attempt) {
            if (stopToken.stop_requested()) {
                break;
            }
            if (attempt > 0) {
                // Only a "this codec cannot go into MP4" failure is worth a
                // re-encode; a full disk or a locked file is not.
                if (!containerProblem) {
                    break;
                }
                setMediaMessage(htfx::tr("status.mixReencodingVideo"));
                temporaryVideo.deleteFile();
            }
            juce::StringArray arguments{
                "-hide_banner", "-loglevel", "error", "-y", "-i",
                originalMediaFile.getFullPathName(), "-i",
                temporaryMix.getFullPathName(), "-map", "0:v:0", "-map", "1:a:0",
                "-map_metadata", "0"};
            arguments.addArray(videoCodecs[attempt]);
            arguments.addArray({"-c:a", "aac", "-b:a", "320k", "-af", "apad", "-shortest",
                                "-movflags", "+faststart", temporaryVideo.getFullPathName()});
            juce::String attemptError;
            muxed = runFfmpeg(arguments, stopToken, attemptError);
            if (!muxed) {
                // Decided by the stream-copy attempt only; a later encoder
                // failing for its own reason must not cancel the fallback.
                if (attempt == 0) {
                    containerProblem =
                        attemptError.containsIgnoreCase("not currently supported in container") ||
                        attemptError.containsIgnoreCase("Could not find tag for codec");
                }
                // keep the first (most telling) message; later ones are appended
                error = error.isEmpty() ? attemptError : error + " | " + attemptError;
            }
        }
        temporaryMix.deleteFile();
        if (!muxed) {
            temporaryVideo.deleteFile();
            if (stopToken.stop_requested()) {
                setMediaMessage(htfx::tr("status.mixExportCancelledVideo"));
            } else {
                setMediaMessage(
                    error + (containerProblem ? htfx::tr("status.mp4StreamCopyIncompatibleSuffix")
                                              : juce::String()));
            }
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        if ((outputFile.existsAsFile() && !outputFile.deleteFile()) ||
            !temporaryVideo.moveFileTo(outputFile)) {
            temporaryVideo.deleteFile();
            setMediaMessage(
                htfx::tr("status.couldNotReplaceMp4OutputPrefix") +
                outputFile.getFullPathName());
            mediaBusy_.store(false, std::memory_order_release);
            return;
        }
        mediaProgress_.store(1.0, std::memory_order_release);
        setLastExportedFile(outputFile);
        setMediaMessage(
            htfx::tr("status.mixExportedMp4Prefix") +
            outputFile.getFullPathName());
        mediaBusy_.store(false, std::memory_order_release);
    } catch (const std::exception& exception) {
        setMediaMessage(
            htfx::tr("status.mixExportFailedPrefix") +
            juce::String::fromUTF8(exception.what()));
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
        case SeparationState::idle: return htfx::tr("status.readyToRecord");
        case SeparationState::recording: return htfx::tr("status.recording");
        case SeparationState::recorded: return htfx::tr("status.readyToSeparate");
        case SeparationState::loading: return htfx::tr("status.loadingModel");
        case SeparationState::separating: return htfx::tr("status.separating");
        case SeparationState::previewReady: return htfx::tr("status.readyToPreview");
        case SeparationState::error: return htfx::tr("status.error");
        case SeparationState::cancelled: return htfx::tr("status.cancelled");
    }
    return htfx::tr("status.unknown");
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
            htfx::tr("dialog.exportChooseTitle"),
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

        selectedButton_.setButtonText(htfx::tr("dialog.exportSelectedStems"));
        selectedButton_.onClick = [this] { chooseStemFolder(selectedSources()); };
        allButton_.setButtonText(htfx::tr("dialog.exportAllStems"));
        allButton_.onClick = [this] {
            std::vector<int> all;
            for (int source = 0; source < processor_.getActiveSourceCount(); ++source) {
                all.push_back(source);
            }
            chooseStemFolder(std::move(all));
        };
        mixButton_.setButtonText(htfx::tr("dialog.exportMix"));
        mixButton_.onClick = [this] { chooseMixFile(); };
        closeButton_.setButtonText(htfx::tr("dialog.close"));
        closeButton_.onClick = [this] {
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
                dialog->exitModalState(0);
            }
        };
        for (auto* button : std::array<juce::Button*, 4>{
                 &selectedButton_, &allButton_, &mixButton_, &closeButton_}) {
            addAndMakeVisible(button);
        }

        audioOnlyButton_.setButtonText(htfx::tr("dialog.audioOnlyWav"));
        videoButton_.setButtonText(htfx::tr("dialog.videoWithMixedAudio"));
        audioOnlyButton_.setRadioGroupId(0x48544658, juce::dontSendNotification);
        videoButton_.setRadioGroupId(0x48544658, juce::dontSendNotification);
        audioOnlyButton_.setToggleState(true, juce::dontSendNotification);
        const bool videoInput = processor_.importedFromVideo();
        addAndMakeVisible(audioOnlyButton_);
        addAndMakeVisible(videoButton_);
        audioOnlyButton_.setVisible(videoInput);
        videoButton_.setVisible(videoInput);

        note_.setText(
            videoInput ? htfx::tr("dialog.noteVideoExport")
                       : htfx::tr("dialog.noteStemExport"),
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
                htfx::tr("alert.noStemsSelectedTitle"),
                htfx::tr("alert.noStemsSelectedMessage"));
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            htfx::tr("filechooser.stemFolderTitle"),
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
            video ? htfx::tr("filechooser.exportVideoWithMix")
                  : htfx::tr("filechooser.exportMix"),
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

// One row of the clip list: a tick box (multi-file only), the file name and
// a short status. Clicking the row makes that clip active, which swaps the
// preview transport and the mixer faders over to it.
class ClipRow : public juce::Component {
public:
    ClipRow() {
        selectBox_.setName("clipSelect");
        addAndMakeVisible(selectBox_);
        nameLabel_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(nameLabel_);
        statusLabel_.setInterceptsMouseClicks(false, false);
        statusLabel_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(statusLabel_);
    }

    void update(const juce::String& name, const juce::String& status,
                bool selected, bool active, bool showTickBox) {
        nameLabel_.setText(name, juce::dontSendNotification);
        statusLabel_.setText(status, juce::dontSendNotification);
        selectBox_.setToggleState(selected, juce::dontSendNotification);
        selectBox_.setVisible(showTickBox);
        active_ = active;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        if (active_) {
            g.setColour(juce::Colour(0xff2f4f6f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(2);
        selectBox_.setBounds(area.removeFromLeft(24));
        statusLabel_.setBounds(area.removeFromRight(90));
        nameLabel_.setBounds(area);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onRowClicked != nullptr) {
            onRowClicked();
        }
    }

    std::function<void()> onRowClicked;
    juce::ToggleButton& tickBox() { return selectBox_; }

private:
    juce::ToggleButton selectBox_;
    juce::Label nameLabel_;
    juce::Label statusLabel_;
    bool active_ = false;
};

// The visual identity every control shares: one dark ground, flat rounded
// surfaces, and colour used only to say what a control does -- blue for the
// primary action, amber for vocals, teal for accompaniment, red for record
// and cancel. Disabled controls fade instead of changing shape, so the layout
// reads the same whether or not a step is available yet.
class HtfxLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    static constexpr juce::uint32 kBackground = 0xff171a20;
    static constexpr juce::uint32 kSurface = 0xff21252d;
    static constexpr juce::uint32 kSurfaceRaised = 0xff2c313b;
    static constexpr juce::uint32 kOutline = 0xff3b4352;
    static constexpr juce::uint32 kText = 0xffe9ecf1;
    static constexpr juce::uint32 kTextMuted = 0xff97a1b1;
    static constexpr juce::uint32 kAccent = 0xff3d8fe0;
    static constexpr juce::uint32 kVocals = 0xffd9962f;
    static constexpr juce::uint32 kAccompany = 0xff2aa88f;
    static constexpr juce::uint32 kDanger = 0xffb3262e;
    static constexpr juce::uint32 kSuccess = 0xff43b96a;

    HtfxLookAndFeel() {
        setColourScheme({juce::Colour(kBackground), juce::Colour(kSurface),
                         juce::Colour(kSurfaceRaised), juce::Colour(kOutline),
                         juce::Colour(kText), juce::Colour(kAccent), juce::Colour(kText),
                         juce::Colour(kAccent), juce::Colour(kText)});
        setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceRaised));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(kAccent));
        setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
        setColour(juce::TextButton::textColourOnId, juce::Colour(kText));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(kSurface));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(kOutline));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(kTextMuted));
        setColour(juce::ComboBox::textColourId, juce::Colour(kText));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(kSurfaceRaised));
        setColour(juce::PopupMenu::textColourId, juce::Colour(kText));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kAccent));
        setColour(juce::Label::textColourId, juce::Colour(kText));
        setColour(juce::Slider::backgroundColourId, juce::Colour(kOutline));
        setColour(juce::Slider::trackColourId, juce::Colour(kAccent));
        setColour(juce::Slider::thumbColourId, juce::Colour(kText));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(kSurface));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(kOutline));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(kText));
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(kSurface));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(kOutline));
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccent));
        setColour(juce::TextEditor::textColourId, juce::Colour(kText));
        setColour(juce::ToggleButton::textColourId, juce::Colour(kText));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccent));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(kTextMuted));
        setColour(juce::GroupComponent::outlineColourId, juce::Colour(kOutline));
        setColour(juce::GroupComponent::textColourId, juce::Colour(kTextMuted));
        setColour(juce::ProgressBar::backgroundColourId, juce::Colour(kSurface));
        setColour(juce::ProgressBar::foregroundColourId, juce::Colour(kAccent));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        constexpr float radius = 6.0f;
        auto fill = backgroundColour;
        if (!button.isEnabled()) {
            fill = fill.withMultipliedAlpha(0.35f);
        } else if (down) {
            fill = fill.darker(0.25f);
        } else if (highlighted) {
            fill = fill.brighter(0.15f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(juce::Colour(kOutline).withAlpha(button.isEnabled() ? 0.9f : 0.35f));
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }

    // Determinate: a rounded track, the accent fill, and the percentage in
    // light text with a soft shadow so it reads on both the track and the
    // fill (the stock renderer picks one contrast colour and loses the label
    // once the bar is full). Indeterminate keeps the stock stripes.
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override {
        if (progress < 0.0 || progress > 1.0) {
            // Indeterminate: the stock stripes run behind the text and swallow
            // it, so dim them under a scrim before writing the phase name.
            LookAndFeel_V4::drawProgressBar(g, bar, width, height, progress, {});
            if (textToShow.isNotEmpty()) {
                const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
                g.setColour(juce::Colour(kBackground).withAlpha(0.55f));
                g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
                g.setFont(juce::FontOptions{static_cast<float>(height) * 0.7f, juce::Font::bold});
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.drawText(textToShow, bounds.translated(0.0f, 1.0f).toNearestInt(),
                           juce::Justification::centred, false);
                g.setColour(juce::Colour(kText));
                g.drawText(textToShow, bounds.toNearestInt(), juce::Justification::centred, false);
            }
            return;
        }
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        const float radius = bounds.getHeight() * 0.5f;
        g.setColour(bar.findColour(juce::ProgressBar::backgroundColourId));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(juce::Colour(kOutline));
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
        if (progress > 0.0) {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addRoundedRectangle(bounds, radius);
            g.reduceClipRegion(clip);
            g.setColour(bar.findColour(juce::ProgressBar::foregroundColourId));
            g.fillRect(bounds.withWidth(static_cast<float>(width) * static_cast<float>(progress)));
        }
        if (textToShow.isNotEmpty()) {
            g.setFont(juce::FontOptions{static_cast<float>(height) * 0.7f, juce::Font::bold});
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.drawText(textToShow, bounds.translated(0.0f, 1.0f).toNearestInt(),
                       juce::Justification::centred, false);
            g.setColour(juce::Colour(kText));
            g.drawText(textToShow, bounds.toNearestInt(), juce::Justification::centred, false);
        }
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override {
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        const float alpha = box.isEnabled() ? 1.0f : 0.4f;
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
        const auto centre = juce::Point<float>(static_cast<float>(width) - 14.0f,
                                               static_cast<float>(height) * 0.5f);
        juce::Path chevron;
        chevron.startNewSubPath(centre.x - 4.0f, centre.y - 2.0f);
        chevron.lineTo(centre.x, centre.y + 2.5f);
        chevron.lineTo(centre.x + 4.0f, centre.y - 2.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(alpha));
        g.strokePath(chevron, juce::PathStrokeType(1.6f));
    }
};

// The transformed content layer of the editor; the editor paints the frame
// (cards, the step strip, the status dot) into it through onPaint so the
// decoration scales with the controls.
class PaintCanvas final : public juce::Component {
public:
    std::function<void(juce::Graphics&)> onPaint;
    std::function<void(juce::Graphics&)> onPaintOver;
    void paint(juce::Graphics& g) override {
        if (onPaint != nullptr) {
            onPaint(g);
        }
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (onPaintOver != nullptr) {
            onPaintOver(g);
        }
    }
};

bool isAcceptedMediaName(const juce::String& path) {
    return htfx::isAcceptedMediaPath(path);
}

class HTDemucsGpuFXEditor final : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer {
public:
    explicit HTDemucsGpuFXEditor(HTDemucsGpuFXAudioProcessor& processor)
        : AudioProcessorEditor(processor),
          processor_(processor),
          state_(processor.parameters()),
          progressBar_(progressValue_) {
        htfx::Localization::instance().reload();
        setLookAndFeel(&lookAndFeel_);
        setWantsKeyboardFocus(true);
        scaledContent_.onPaint = [this](juce::Graphics& g) { paintFrame(g); };
        scaledContent_.onPaintOver = [this](juce::Graphics& g) { paintDropOverlay(g); };
        importButton_.setColour(juce::TextButton::buttonColourId,
                                juce::Colour(HtfxLookAndFeel::kAccent));
        vocalsOnlyButton_.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(HtfxLookAndFeel::kVocals));
        accompanyOnlyButton_.setColour(juce::TextButton::buttonColourId,
                                       juce::Colour(HtfxLookAndFeel::kAccompany));
        separateButton_.setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(HtfxLookAndFeel::kAccent));
        cancelButton_.setColour(juce::TextButton::buttonColourId,
                                juce::Colour(HtfxLookAndFeel::kDanger).darker(0.35f));
        previewPlayButton_.setColour(juce::TextButton::buttonColourId,
                                     juce::Colour(HtfxLookAndFeel::kAccent).darker(0.2f));
        simpleFile_.setBorderSize(juce::BorderSize<int>(0, 10, 0, 10));
        status_.setBorderSize(juce::BorderSize<int>(0, 18, 0, 0));
        simpleTitle_.setColour(juce::Label::textColourId, juce::Colour(HtfxLookAndFeel::kText));

        updatePanelSwitchButtonText();
        panelSwitchButton_.onClick = [this] { setAdvancedPanel(!advancedPanel_); };
        addAndMakeVisible(panelSwitchButton_);

        languageButton_.setName("Language toggle");
        languageButton_.onClick = [this] {
            const auto next =
                htfx::Localization::instance().getLanguage() == htfx::Language::zhTW
                    ? htfx::Language::en
                    : htfx::Language::zhTW;
            htfx::Localization::instance().setLanguage(next);
            applyLocalizedStrings();
        };
        addAndMakeVisible(languageButton_);

        simpleTitle_.setFont(juce::FontOptions{22.0f, juce::Font::bold});
        simpleTitle_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(simpleTitle_);
        simpleFile_.setText(htfx::tr("label.noMediaSelected"), juce::dontSendNotification);
        simpleFile_.setJustificationType(juce::Justification::centredLeft);
        simpleFile_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(simpleFile_);
        vocalsOnlyButton_.setButtonText(htfx::tr("button.exportVocalsOnly"));
        vocalsOnlyButton_.onClick = [this] {
            // Multi-file: pick a destination folder and export every ticked
            // clip into it; single file keeps the familiar save dialog.
            if (processor_.getClipCount() > 1) {
                chooseBatchExportFolder(
                    HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals);
                return;
            }
            chooseQuickExportFile(
                HTDemucsGpuFXAudioProcessor::QuickExportKind::vocals);
        };
        accompanyOnlyButton_.setButtonText(htfx::tr("button.exportAccompanyOnly"));
        accompanyOnlyButton_.onClick = [this] {
            if (processor_.getClipCount() > 1) {
                chooseBatchExportFolder(
                    HTDemucsGpuFXAudioProcessor::QuickExportKind::accompaniment);
                return;
            }
            chooseQuickExportFile(
                HTDemucsGpuFXAudioProcessor::QuickExportKind::accompaniment);
        };
        addAndMakeVisible(vocalsOnlyButton_);
        addAndMakeVisible(accompanyOnlyButton_);

        addAndMakeVisible(separationModeLabel_);
        separationModeBox_.setName("Separation mode");
        separationModeBox_.setTextWhenNothingSelected(
            htfx::tr("combo.separationModePlaceholder"));
        separationModeBox_.addItem(htfx::tr("combo.separationMode4Stem"), 1);
        separationModeBox_.addItem(htfx::tr("combo.separationMode6Stem"), 2);
        juce::StringArray separationModeCategories;
        for (const auto& model : processor_.getRoformerModels()) {
            separationModeCategories.addIfNotAlreadyThere(model.category);
        }
        separationModeCategories.sort(true);
        separationModeCategories_ = separationModeCategories;
        for (const auto& category : separationModeCategories) {
            separationModeBox_.addItem(
                htfx::glossed(category.substring(0, 1).toUpperCase() +
                              category.substring(1)),
                separationModeBox_.getNumItems() + 1);
        }
        separationModeBox_.onChange = [this] { onSeparationModeChanged(); };
        addAndMakeVisible(separationModeBox_);

        addAndMakeVisible(modeLabel_);
        addAndMakeVisible(modeBox_);
        modeBox_.addItem(htfx::tr("combo.modeRecord"), 1);
        modeBox_.addItem(htfx::tr("combo.modeRealtime"), 2);
        modeBox_.setSelectedItemIndex(choiceIndex("operatingMode"), juce::dontSendNotification);
        modeBox_.onChange = [this] {
            setChoice("operatingMode", modeBox_.getSelectedItemIndex());
            if (modeBox_.getSelectedItemIndex() == 1) {
                processor_.endRecording();
            }
            processor_.applyUserConfiguration();
            updateVisibility();
        };

        updateFullScreenButtonText();
        fullScreenButton_.onClick = [this] { toggleFullScreen(); };
        addAndMakeVisible(fullScreenButton_);
        scaleButton_.setButtonText(htfx::tr("button.scaleUi"));
        scaleButton_.onClick = [this] { updateResizeMode(); };
        addAndMakeVisible(scaleButton_);

        constexpr std::array<const char*, HTDemucsGpuFXAudioProcessor::kMaxSources>
            stemNames{"Drums", "Bass", "Other", "Vocals", "Guitar", "Piano"};
        constexpr std::array<const char*, HTDemucsGpuFXAudioProcessor::kMaxSources>
            stemIds{"drumsGain", "bassGain", "otherGain", "vocalsGain", "guitarGain", "pianoGain"};
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            stemLabels_[index].setText(
                htfx::glossed(stemNames[index]), juce::dontSendNotification);
            stemLabels_[index].setName("stemLabel" + juce::String(static_cast<int>(index)));
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

        outputLabel_.setJustificationType(juce::Justification::centredRight);
        outputSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        outputSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
        outputSlider_.setTextValueSuffix(" dB");
        outputSlider_.setName("outputTrim");
        addAndMakeVisible(outputLabel_);
        addAndMakeVisible(outputSlider_);
        outputAttachment_ = std::make_unique<SliderAttachment>(
            state_, "outputTrim", outputSlider_);

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

        importButton_.setButtonText(htfx::tr("button.import"));
        importButton_.onClick = [this] { chooseMediaFile(); };
        addAndMakeVisible(importButton_);

        separateButton_.onClick = [this] { processor_.beginBatchSeparation(); };
        addAndMakeVisible(separateButton_);
        exportButton_.setButtonText(htfx::tr("button.export"));
        exportButton_.onClick = [this] { showExportDialog(); };
        addAndMakeVisible(exportButton_);
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

        addAndMakeVisible(previewGroup_);
        previewPlayButton_.setButtonText(htfx::tr("button.play"));
        previewPlayButton_.onClick = [this] { processor_.togglePreviewPlayback(); };
        previewStopButton_.onClick = [this] { processor_.stopPreview(); };
        previewPosition_.setSliderStyle(juce::Slider::LinearHorizontal);
        previewPosition_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        // The waveform overview is painted underneath; the slider only shows
        // its thumb over it.
        previewPosition_.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
        previewPosition_.setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
        previewPosition_.setRange(0.0, 1.0, 0.0001);
        previewPosition_.onDragEnd = [this] {
            processor_.setPreviewPosition(previewPosition_.getValue());
        };
        previewTime_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(previewPlayButton_);
        addAndMakeVisible(previewStopButton_);
        addAndMakeVisible(previewPosition_);
        addAndMakeVisible(previewTime_);

        updateAdvancedButtonText();
        advancedButton_.onClick = [this] {
            advancedVisible_ = !advancedVisible_;
            updateAdvancedButtonText();
            updateAdvancedVisibility();
            // Re-apply the RoFormer-mode-aware refinement (D2) so the model
            // combo doesn't flash visible for one frame when the disclosure
            // is expanded while a RoFormer mode is already active.
            updateSixSourceControls();
            updateSize();
        };
        addAndMakeVisible(advancedButton_);

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

        modelBox_.addItem("htdemucs", 1);
        modelBox_.addItem("htdemucs_6s", 2);
        modelBox_.setSelectedItemIndex(choiceIndex("model"), juce::dontSendNotification);
        modelBox_.onChange = [this] {
            setChoice("model", modelBox_.getSelectedItemIndex());
            if (processor_.isModelInstalled(modelBox_.getText())) {
                processor_.applyUserConfiguration();
            }
            updateSixSourceControls();
        };
        modelDownloadButton_.setButtonText(htfx::tr("button.downloadModel"));
        modelDownloadButton_.onClick = [this] {
            processor_.beginModelDownload(modelBox_.getText());
        };
        addAndMakeVisible(modelDownloadButton_);

        roformerCategoryBox_.setName("RoFormer category");
        roformerCategoryBox_.addItem(htfx::tr("combo.roformerAllCategories"), 1);
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

        roformerSearch_.setName("RoFormer search");
        roformerSearch_.setTextToShowWhenEmpty(
            htfx::tr("placeholder.roformerSearch"), juce::Colours::grey);
        roformerSearch_.onTextChange = [this] { refreshRoformerBrowser(); };

        roformerModelBox_.setName("RoFormer model");
        roformerModelBox_.onChange = [this] {
            const auto index = roformerModelBox_.getSelectedItemIndex();
            if (index >= 0 && index < static_cast<int>(visibleRoformerIds_.size())) {
                processor_.selectRoformerModel(
                    visibleRoformerIds_[static_cast<std::size_t>(index)]);
            }
            updateRoformerStatus();
            persistStartupSelection();
        };

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
        openOutputButton_.setName("Open output");
        openOutputButton_.onClick = [this] {
            const auto target = processor_.getLastExportedFile();
            if (target.exists()) {
                target.revealToUser();
            }
        };
        addAndMakeVisible(openOutputButton_);
        openOutputButton_.setVisible(false);
        clipViewport_.setViewedComponent(&clipList_, false);
        clipViewport_.setScrollBarsShown(true, false);
        clipViewport_.setScrollBarThickness(10);
        addAndMakeVisible(clipViewport_);
        clipViewport_.setVisible(false);
        status_.setJustificationType(juce::Justification::centredLeft);
        metrics_.setJustificationType(juce::Justification::centredLeft);
        status_.setFont(juce::FontOptions{15.0f, juce::Font::bold});
        metrics_.setFont(juce::FontOptions{12.0f});
        resetWorker_.setName("Reset worker");
        resetWorker_.onClick = [this] { processor_.requestWorkerRecovery(); };
        while (getNumChildComponents() > 0) {
            auto* child = getChildComponent(0);
            const bool wasVisible = child->isVisible();
            scaledContent_.addChildComponent(child);
            child->setVisible(wasVisible);
        }
        addAndMakeVisible(scaledContent_);
        tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 500);
        setResizable(false, false);
        applyLocalizedStrings();
        restoreStartupSelection();
        updateSixSourceControls();
        updateAdvancedVisibility();
        updatePanelVisibility();
        updateVisibility();
        updateSize();
        startTimerHz(10);
        timerCallback();
    }

    ~HTDemucsGpuFXEditor() override {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(HtfxLookAndFeel::kBackground));
    }

    // Dropping media onto the window imports it, the same as the Import
    // button: one file replaces the current clip, several become a batch.
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        for (const auto& file : files) {
            if (isAcceptedMediaName(file)) {
                return true;
            }
        }
        return false;
    }

    void fileDragEnter(const juce::StringArray&, int, int) override {
        dragOver_ = true;
        scaledContent_.repaint();
    }

    void fileDragExit(const juce::StringArray&) override {
        dragOver_ = false;
        scaledContent_.repaint();
    }

    // Keyboard: Space plays/pauses the preview, Esc cancels the running job,
    // Ctrl+O imports, Ctrl+E exports. Each key does exactly what the
    // corresponding button would, and only when that button is enabled.
    bool keyPressed(const juce::KeyPress& key) override {
        const auto ctrl = juce::ModifierKeys::ctrlModifier;
        // isVisible() rather than isShowing(): the latter is false for an
        // editor without a window peer (the smoke tests), while visibility
        // in the tree is what the panels toggle.
        if (key == juce::KeyPress::spaceKey && previewPlayButton_.isVisible() &&
            previewPlayButton_.isEnabled()) {
            processor_.togglePreviewPlayback();
            return true;
        }
        if (key == juce::KeyPress::escapeKey) {
            // Cancel whatever is running, on either panel (the simple panel
            // has no Cancel button of its own).
            const auto state = processor_.getSeparationState();
            const bool running =
                processor_.isMediaBusy() || processor_.isModelDownloadBusy() ||
                state == HTDemucsGpuFXAudioProcessor::SeparationState::loading ||
                state == HTDemucsGpuFXAudioProcessor::SeparationState::separating;
            if (running) {
                cancelButton_.onClick();
                return true;
            }
            return false;
        }
        if (key == juce::KeyPress('o', ctrl, 0) && importButton_.isVisible() &&
            importButton_.isEnabled()) {
            chooseMediaFile();
            return true;
        }
        if (key == juce::KeyPress('e', ctrl, 0) && exportButton_.isVisible() &&
            exportButton_.isEnabled()) {
            showExportDialog();
            return true;
        }
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override {
        dragOver_ = false;
        scaledContent_.repaint();
        juce::Array<juce::File> media;
        bool sawAcceptedName = false;
        for (const auto& path : files) {
            if (!isAcceptedMediaName(path)) {
                continue;
            }
            sawAcceptedName = true;
            const juce::File file(path);
            if (file.existsAsFile()) {
                media.add(file);
            }
        }
        if (media.isEmpty()) {
            if (sawAcceptedName) {
                // e.g. dragged straight out of a zip or a phone: the shell
                // hands over names that are not real files.
                showNotice(htfx::tr("status.dropNothingUsable"));
            }
            return;
        }
        // Same gate as the Import button, plus what the button does not
        // see: a batch in flight and a queued quick export.
        if (!importButton_.isEnabled() || processor_.isBatchBusy() ||
            pendingQuickExport_.has_value()) {
            showNotice(htfx::tr("status.dropIgnoredBusy"));
            return;
        }
        // Leave the OS drop callback before touching parameters or threads.
        juce::Component::SafePointer<HTDemucsGpuFXEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis, media] {
            if (safeThis == nullptr) {
                return;
            }
            if (safeThis->modeBox_.getSelectedItemIndex() != 0) {
                // Import only exists in record mode; switch the same way
                // the combo would.
                safeThis->modeBox_.setSelectedItemIndex(0, juce::sendNotificationSync);
            }
            const bool started = media.size() > 1
                                     ? safeThis->processor_.beginMultiMediaImport(media)
                                     : safeThis->processor_.beginMediaImport(media.getReference(0));
            if (!started) {
                const auto why = safeThis->processor_.getMediaStatusText();
                safeThis->showNotice(why.isNotEmpty() ? why : htfx::tr("status.dropIgnoredBusy"));
            }
        });
    }

    void resized() override {
        scaledContent_.setTransform({});
        scaledContent_.setBounds(0, 0, designWidth(), designHeight());
        auto area = scaledContent_.getLocalBounds().reduced(12);

        if (!advancedPanel_) {
            auto header = area.removeFromTop(30);
            simpleTitle_.setBounds(header.removeFromLeft(330));
            languageButton_.setBounds(header.removeFromRight(64));
            header.removeFromRight(6);
            panelSwitchButton_.setBounds(header.removeFromRight(130));
            area.removeFromTop(6);
            stepStrip_ = area.removeFromTop(18);
            area.removeFromTop(6);
            fileChip_ = area.removeFromTop(26);
            simpleFile_.setBounds(fileChip_);
            area.removeFromTop(6);
            importButton_.setBounds(area.removeFromTop(34));
            area.removeFromTop(6);
            auto exports = area.removeFromTop(38);
            vocalsOnlyButton_.setBounds(exports.removeFromLeft(256));
            exports.removeFromLeft(12);
            accompanyOnlyButton_.setBounds(exports);
            area.removeFromTop(6);
            layoutClipList(area.removeFromTop(clipListHeight()));
            progressBar_.setBounds(area.removeFromTop(16));
            area.removeFromTop(4);
            auto statusRow = area.removeFromTop(30);
            if (openOutputButton_.isVisible()) {
                openOutputButton_.setBounds(statusRow.removeFromRight(124).reduced(0, 3));
                statusRow.removeFromRight(6);
            }
            status_.setBounds(statusRow);

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
        fullScreenButton_.setBounds(modeRow.removeFromLeft(90));
        modeRow.removeFromLeft(6);
        scaleButton_.setBounds(modeRow.removeFromLeft(80));
        modeRow.removeFromLeft(6);
        languageButton_.setBounds(modeRow.removeFromLeft(64));
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
        layoutClipList(area.removeFromTop(clipListHeight()));
        progressBar_.setBounds(area.removeFromTop(18).reduced(0, 1));
        area.removeFromTop(4);

        // Only visible stems take a row: a 2-stem RoFormer mode or a 4-stem
        // HTDemucs mode leaves no blank rows where the other sliders would be.
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            if (!stemSliders_[index].isVisible()) {
                continue;
            }
            auto row = area.removeFromTop(28);
            stemLabels_[index].setBounds(row.removeFromLeft(160));
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
            // Rows belonging to the other model family are hidden by
            // updateSixSourceControls(); they take no space here.
            auto layoutAdvancedRow = [&](juce::Label& label, juce::Component& control) {
                if (!control.isVisible()) {
                    return;
                }
                auto row = area.removeFromTop(28);
                label.setBounds(row.removeFromLeft(150));
                control.setBounds(row.removeFromLeft(430));
            };
            layoutAdvancedRow(segmentLabel_, segmentBox_);
            layoutAdvancedRow(modelLabel_, modelBox_);
            if (modelDownloadButton_.isVisible()) {
                auto downloadRow = area.removeFromTop(28);
                downloadRow.removeFromLeft(150);
                modelDownloadButton_.setBounds(downloadRow.removeFromLeft(430));
            }
            layoutAdvancedRow(roformerCategoryLabel_, roformerCategoryBox_);
            layoutAdvancedRow(roformerSearchLabel_, roformerSearch_);
            layoutAdvancedRow(roformerModelLabel_, roformerModelBox_);
            layoutAdvancedRow(roformerStatusLabel_, roformerStatus_);
            layoutAdvancedRow(computeLabel_, computeBox_);
            layoutAdvancedRow(gpuLabel_, gpuSlider_);
        }

        cpuWarning_.setBounds(area.removeFromTop(26));

        // The footer sits on the panel's bottom edge, so a mode that hides
        // rows leaves its spare space between content and footer rather
        // than below a floating status line.
        auto footer = area.removeFromBottom(50);
        footerDivider_ = footer.getY() - 2;
        auto statusRow = footer.removeFromTop(24);
        if (openOutputButton_.isVisible()) {
            openOutputButton_.setBounds(statusRow.removeFromRight(124).reduced(0, 1));
            statusRow.removeFromRight(6);
        }
        status_.setBounds(statusRow);
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

    static constexpr int kClipRowHeight = 22;
    static constexpr int kMaxVisibleClipRows = 8;

    [[nodiscard]] int clipListHeight() const noexcept {
        return kClipRowHeight * (std::min)(static_cast<int>(clipRows_.size()), kMaxVisibleClipRows);
    }

    [[nodiscard]] int designHeight() const noexcept {
        // Multi-file imports add one row per clip, up to eight; beyond that
        // the list scrolls, so the panel never outgrows the screen.
        return (advancedPanel_ ? (advancedVisible_ ? 826 : 576) : 260) + clipListHeight();
    }

    void layoutClipList(juce::Rectangle<int> listArea) {
        clipViewport_.setBounds(listArea);
        clipViewport_.setVisible(!clipRows_.empty());
        const int rows = static_cast<int>(clipRows_.size());
        const bool scrolls = rows > kMaxVisibleClipRows;
        const int width = listArea.getWidth() - (scrolls ? clipViewport_.getScrollBarThickness() : 0);
        clipList_.setSize((std::max)(1, width), kClipRowHeight * rows);
        for (int i = 0; i < rows; ++i) {
            clipRows_[static_cast<std::size_t>(i)]->setBounds(
                juce::Rectangle<int>(0, kClipRowHeight * i, clipList_.getWidth(), kClipRowHeight)
                    .reduced(0, 1));
        }
    }

    enum class StepState { pending, active, done };

    // A short-lived line in the status label (e.g. why a drop was ignored);
    // the processor's own status returns once it expires.
    void showNotice(const juce::String& text) {
        notice_ = text;
        noticeUntil_ = juce::Time::getMillisecondCounter() + 4000;
        noticeBaseline_ = processor_.getMediaStatusText();
        status_.setText(text, juce::dontSendNotification);
        status_.setTooltip(text);
    }

    // True while a notice should still be shown: not expired (wrap-safe
    // difference), and the processor has not said anything new since --
    // an error must never sit behind a "busy, try again" line.
    bool noticeActive() const {
        if (notice_.isEmpty()) {
            return false;
        }
        const auto remaining = static_cast<juce::int32>(noticeUntil_ - juce::Time::getMillisecondCounter());
        return remaining > 0 && processor_.getMediaStatusText() == noticeBaseline_;
    }

    // Paints the decoration around the controls: the "1 import -> 2 separate
    // -> 3 export" strip and the file chip on the simple panel, a divider
    // above the footer on the advanced panel, and the coloured status dot on
    // both. Everything here is derived from processor state cached by
    // timerCallback(), never queried mid-paint.
    void paintFrame(juce::Graphics& g) {
        const auto text = juce::Colour(HtfxLookAndFeel::kText);
        const auto muted = juce::Colour(HtfxLookAndFeel::kTextMuted);
        const auto outline = juce::Colour(HtfxLookAndFeel::kOutline);
        if (!advancedPanel_) {
            const std::array<juce::String, 3> labels{
                htfx::tr("step.import"), htfx::tr("step.separate"), htfx::tr("step.export")};
            const std::array<StepState, 3> states{stepImport_, stepSeparate_, stepExport_};
            auto strip = stepStrip_.toFloat();
            const float gap = 10.0f;
            const float pillWidth = (strip.getWidth() - 2.0f * gap) / 3.0f;
            g.setFont(juce::FontOptions{12.5f, juce::Font::bold});
            for (std::size_t i = 0; i < labels.size(); ++i) {
                auto pill = strip.removeFromLeft(pillWidth);
                const auto fill = states[i] == StepState::done
                                      ? juce::Colour(HtfxLookAndFeel::kSuccess).withAlpha(0.22f)
                                  : states[i] == StepState::active
                                      ? juce::Colour(HtfxLookAndFeel::kAccent).withAlpha(0.28f)
                                      : juce::Colour(HtfxLookAndFeel::kSurface);
                const auto edge = states[i] == StepState::done
                                      ? juce::Colour(HtfxLookAndFeel::kSuccess)
                                  : states[i] == StepState::active
                                      ? juce::Colour(HtfxLookAndFeel::kAccent)
                                      : outline;
                g.setColour(fill);
                g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
                g.setColour(edge);
                g.drawRoundedRectangle(pill.reduced(0.5f), pill.getHeight() * 0.5f, 1.0f);
                g.setColour(states[i] == StepState::pending ? muted : text);
                g.drawText(labels[i], pill.toNearestInt(), juce::Justification::centred, true);
                if (i + 1 < labels.size()) {
                    g.setColour(outline);
                    g.fillRect(juce::Rectangle<float>(pill.getRight() + 2.0f,
                                                      pill.getCentreY() - 0.5f, gap - 4.0f, 1.0f));
                    strip.removeFromLeft(gap);
                }
            }
            g.setColour(juce::Colour(HtfxLookAndFeel::kSurface));
            g.fillRoundedRectangle(fileChip_.toFloat(), 6.0f);
            g.setColour(outline);
            g.drawRoundedRectangle(fileChip_.toFloat().reduced(0.5f), 6.0f, 1.0f);
        } else {
            if (footerDivider_ > 0) {
                g.setColour(outline);
                g.fillRect(12, footerDivider_, designWidth() - 24, 1);
            }
            paintWaveformOverview(g);
        }
        const auto dotColour = statusTone_ == 1 ? juce::Colour(HtfxLookAndFeel::kAccent)
                             : statusTone_ == 2 ? juce::Colour(HtfxLookAndFeel::kSuccess)
                             : statusTone_ == 3 ? juce::Colour(HtfxLookAndFeel::kDanger)
                                                : muted;
        const auto statusBounds = status_.getBounds().toFloat();
        const juce::Rectangle<float> dot(statusBounds.getX() + 2.0f,
                                         statusBounds.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour(dotColour);
        g.fillEllipse(dot);
    }

    // The separated clip's envelope behind the preview position slider: the
    // played part in the accent colour, the rest muted. Peaks are rebuilt by
    // timerCallback() whenever the preview result changes.
    void paintWaveformOverview(juce::Graphics& g) {
        const auto area = previewPosition_.getBounds().toFloat().reduced(6.0f, 1.0f);
        if (area.getWidth() < 8.0f || !previewPosition_.isVisible()) {
            return;
        }
        if (overviewPeaks_.empty()) {
            // no separation yet: a plain groove so the thumb has a track
            g.setColour(juce::Colour(HtfxLookAndFeel::kOutline));
            g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth(), 3.0f), 1.5f);
            return;
        }
        const auto accent = juce::Colour(HtfxLookAndFeel::kAccent);
        const auto rest = juce::Colour(HtfxLookAndFeel::kTextMuted).withAlpha(0.55f);
        const float playedX = area.getX() + area.getWidth() *
                                               static_cast<float>(previewPosition_.getValue());
        const float mid = area.getCentreY();
        const float half = area.getHeight() * 0.5f;
        const float columnWidth = area.getWidth() / static_cast<float>(overviewPeaks_.size());
        for (std::size_t i = 0; i < overviewPeaks_.size(); ++i) {
            const float x = area.getX() + columnWidth * static_cast<float>(i);
            const float h = (std::max)(1.0f, half * overviewPeaks_[i]);
            g.setColour(x < playedX ? accent : rest);
            g.fillRect(x, mid - h, (std::max)(1.0f, columnWidth - 0.5f), 2.0f * h);
        }
    }

    // Column peaks of the original mix, normalised so the loudest column
    // fills the slider height.
    void rebuildOverviewPeaks(const auto& result) {  // SeparationResult (private type)
        overviewPeaks_.clear();
        // A fixed column count: the result may arrive while the simple panel
        // is showing (the slider has no bounds yet), and the painter maps
        // columns onto whatever width the slider has when it is visible.
        constexpr std::size_t columns = 480;
        const auto samples = static_cast<std::size_t>(result.sampleCount);
        if (columns == 0 || samples == 0 || result.originalLeft.size() < samples ||
            result.originalRight.size() < samples) {
            return;
        }
        overviewPeaks_.resize(columns, 0.0f);
        float loudest = 0.0f;
        for (std::size_t c = 0; c < columns; ++c) {
            const auto from = samples * c / columns;
            const auto to = (std::max)(from + 1, samples * (c + 1) / columns);
            float peak = 0.0f;
            // sparse scan: enough for a picture, cheap for a 20-minute clip
            const std::size_t step = (to - from) / 64 > 0 ? (to - from) / 64 : 1;
            for (auto i = from; i < to; i += step) {
                peak = (std::max)(peak, (std::max)(std::abs(result.originalLeft[i]),
                                                   std::abs(result.originalRight[i])));
            }
            overviewPeaks_[c] = peak;
            loudest = (std::max)(loudest, peak);
        }
        if (loudest > 0.0f) {
            for (auto& v : overviewPeaks_) {
                v /= loudest;
            }
        }
    }

    // While files are dragged over the window: dim everything, frame it in
    // the accent colour, and say what letting go will do. Painted above the
    // controls so no button can cover the hint.
    void paintDropOverlay(juce::Graphics& g) {
        if (!dragOver_) {
            return;
        }
        const auto accent = juce::Colour(HtfxLookAndFeel::kAccent);
        const auto frame = scaledContent_.getLocalBounds().toFloat().reduced(3.0f);
        g.setColour(juce::Colour(HtfxLookAndFeel::kBackground).withAlpha(0.72f));
        g.fillRoundedRectangle(frame, 10.0f);
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(frame, 10.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(frame, 10.0f, 2.5f);
        const auto hint = htfx::tr("hint.dropToImport");
        g.setFont(juce::FontOptions{22.0f, juce::Font::bold});
        const auto textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), hint);
        const auto pill = juce::Rectangle<float>(textWidth + 48.0f, 44.0f).withCentre(frame.getCentre());
        g.setColour(juce::Colour(HtfxLookAndFeel::kSurfaceRaised));
        g.fillRoundedRectangle(pill, 22.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(pill, 22.0f, 1.5f);
        g.setColour(juce::Colour(HtfxLookAndFeel::kText));
        g.drawText(hint, pill.toNearestInt(), juce::Justification::centred, false);
    }

    // What the shared progress bar is currently measuring. One bar serves the
    // model download, the import, the separation and every export, so without
    // a name on it a run that moves on to the next stage looks like the same
    // job starting over.
    juce::String currentProgressPhase(bool modelBusy, bool mediaBusy,
                                      bool separationBusy, bool recording) const {
        using Task = HTDemucsGpuFXAudioProcessor::MediaTask;
        using State = HTDemucsGpuFXAudioProcessor::SeparationState;
        if (recording) {
            return htfx::tr("phase.recording");
        }
        if (modelBusy) {
            return htfx::tr("phase.downloadModel");
        }
        const auto task = processor_.getMediaTask();
        if (processor_.isBatchBusy()) {
            const auto name = task == Task::batchExport ? htfx::tr("phase.batchExport")
                                                        : htfx::tr("phase.batchSeparate");
            const int index = processor_.getBatchClipIndex();
            const int total = processor_.getBatchClipTotal();
            if (index > 0 && total > 0) {
                return name + " " + juce::String(index) + "/" + juce::String(total);
            }
            return name;
        }
        if (mediaBusy) {
            switch (task) {
                case Task::import_: return htfx::tr("phase.import");
                case Task::quickExportVocals: return htfx::tr("phase.exportVocals");
                case Task::quickExportAccompaniment: return htfx::tr("phase.exportAccompaniment");
                case Task::stemExport: return htfx::tr("phase.exportStems");
                case Task::mixExport: return htfx::tr("phase.exportMix");
                case Task::mixExportVideo: return htfx::tr("phase.exportVideo");
                default: return htfx::tr("phase.import");
            }
        }
        if (separationBusy) {
            return processor_.getSeparationState() == State::loading
                       ? htfx::tr("phase.loadingModel")
                       : htfx::tr("phase.separating");
        }
        return {};
    }

    // Re-applies every localized static string from the current language
    // (see Localization.h). Called once at construction and again whenever
    // languageButton_ toggles the language, so the switch takes effect
    // immediately without reopening the editor. Dynamic status/error text
    // (status_, metrics_, simpleFile_, cpuWarning_, modelDownloadButton_) is
    // computed elsewhere from live processor state and is out of scope here.
    void applyLocalizedStrings() {
        simpleTitle_.setText(htfx::tr("editor.title"), juce::dontSendNotification);
        separationModeLabel_.setText(
            htfx::tr("label.separationMode"), juce::dontSendNotification);
        modeLabel_.setText(htfx::tr("label.mode"), juce::dontSendNotification);
        outputLabel_.setText(htfx::tr("label.outputTrim"), juce::dontSendNotification);
        bypassButton_.setButtonText(htfx::tr("button.bypass"));
        separateButton_.setButtonText(htfx::tr("button.separate"));
        cancelButton_.setButtonText(htfx::tr("button.cancel"));
        previewGroup_.setText(htfx::tr("group.preview"));
        previewStopButton_.setButtonText(htfx::tr("button.previewStop"));
        segmentLabel_.setText(
            htfx::tr("label.inferenceWindow"), juce::dontSendNotification);
        modelLabel_.setText(htfx::tr("label.demucsModel"), juce::dontSendNotification);
        roformerCategoryLabel_.setText(
            htfx::tr("label.roformerCategory"), juce::dontSendNotification);
        roformerSearchLabel_.setText(
            htfx::tr("label.roformerSearch"), juce::dontSendNotification);
        roformerModelLabel_.setText(
            htfx::tr("label.roformerModel"), juce::dontSendNotification);
        roformerStatusLabel_.setText(
            htfx::tr("label.roformerStatus"), juce::dontSendNotification);
        computeLabel_.setText(htfx::tr("label.computeDevice"), juce::dontSendNotification);
        gpuLabel_.setText(htfx::tr("label.gpuIndex"), juce::dontSendNotification);
        resetWorker_.setButtonText(htfx::tr("button.resetWorker"));
        languageButton_.setButtonText(htfx::tr("button.languageToggle"));
        vocalsOnlyButton_.setButtonText(htfx::tr("button.exportVocalsOnly"));
        accompanyOnlyButton_.setButtonText(htfx::tr("button.exportAccompanyOnly"));
        importButton_.setButtonText(htfx::tr("button.import"));
        exportButton_.setButtonText(htfx::tr("button.export"));
        scaleButton_.setButtonText(htfx::tr("button.scaleUi"));
        separationModeBox_.setTextWhenNothingSelected(
            htfx::tr("combo.separationModePlaceholder"));
        // changeItemText() only rewrites the menu item; the box keeps showing
        // the old text, and because JUCE's getSelectedItemIndex() compares
        // the shown text with the item's, every "is a mode chosen" check
        // would then see -1 until the user reselects. Reselect by id.
        auto relabel = [](juce::ComboBox& box, std::initializer_list<std::pair<int, juce::String>> items) {
            const int selectedId = box.getSelectedId();
            for (const auto& [id, text] : items) {
                box.changeItemText(id, text);
            }
            if (selectedId != 0) {
                box.setSelectedId(0, juce::dontSendNotification);
                box.setSelectedId(selectedId, juce::dontSendNotification);
            }
        };
        relabel(separationModeBox_, {{1, htfx::tr("combo.separationMode4Stem")},
                                     {2, htfx::tr("combo.separationMode6Stem")}});
        // The RoFormer categories carry a Chinese gloss in the Chinese UI, so
        // they have to be rewritten on a language change like everything else.
        {
            const int selectedId = separationModeBox_.getSelectedId();
            for (int index = 0; index < separationModeCategories_.size(); ++index) {
                const auto& category = separationModeCategories_[index];
                separationModeBox_.changeItemText(
                    index + 3,
                    htfx::glossed(category.substring(0, 1).toUpperCase() +
                                  category.substring(1)));
            }
            if (selectedId != 0) {
                separationModeBox_.setSelectedId(0, juce::dontSendNotification);
                separationModeBox_.setSelectedId(selectedId, juce::dontSendNotification);
            }
        }
        relabel(modeBox_, {{1, htfx::tr("combo.modeRecord")},
                           {2, htfx::tr("combo.modeRealtime")}});
        relabel(roformerCategoryBox_, {{1, htfx::tr("combo.roformerAllCategories")}});
        roformerSearch_.setTextToShowWhenEmpty(
            htfx::tr("placeholder.roformerSearch"), juce::Colours::grey);
        updatePanelSwitchButtonText();
        updateAdvancedButtonText();
        updateFullScreenButtonText();
        openOutputButton_.setButtonText(htfx::tr("button.openOutput"));
        openOutputButton_.setTooltip(htfx::tr("tip.openOutput"));
        importButton_.setTooltip(htfx::tr("tip.import"));
        vocalsOnlyButton_.setTooltip(htfx::tr("tip.exportVocals"));
        accompanyOnlyButton_.setTooltip(htfx::tr("tip.exportAccompany"));
        separateButton_.setTooltip(htfx::tr("tip.separate"));
        exportButton_.setTooltip(htfx::tr("tip.export"));
        recordButton_.setTooltip(htfx::tr("tip.record"));
        cancelButton_.setTooltip(htfx::tr("tip.cancel"));
        panelSwitchButton_.setTooltip(htfx::tr("tip.panelSwitch"));
        languageButton_.setTooltip(htfx::tr("tip.language"));
        separationModeBox_.setTooltip(htfx::tr("tip.separationMode"));
        previewPlayButton_.setTooltip(htfx::tr("tip.preview"));
        bypassButton_.setTooltip(htfx::tr("tip.bypass"));
    }

    // Multi-file quick export: one destination folder, every ticked clip is
    // separated (if needed) and written there.
    void chooseBatchExportFolder(
        HTDemucsGpuFXAudioProcessor::QuickExportKind kind) {
        mediaChooser_ = std::make_unique<juce::FileChooser>(
            htfx::tr("clip.chooseExportFolder"),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory));
        juce::Component::SafePointer<HTDemucsGpuFXEditor> safeThis(this);
        mediaChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectDirectories,
            [safeThis, kind](const juce::FileChooser& chooser) {
                if (safeThis == nullptr) {
                    return;
                }
                const auto folder = chooser.getResult();
                if (folder != juce::File{}) {
                    safeThis->processor_.beginBatchExport(folder, kind);
                }
                safeThis->mediaChooser_.reset();
            });
    }

    void chooseMediaFile() {
        mediaChooser_ = std::make_unique<juce::FileChooser>(
            htfx::tr("filechooser.importMediaTitle"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            htfx::acceptedMediaWildcards());
        juce::Component::SafePointer<HTDemucsGpuFXEditor> safeThis(this);
        mediaChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::canSelectMultipleItems,
            [safeThis](const juce::FileChooser& chooser) {
                if (safeThis == nullptr) {
                    return;
                }
                const auto results = chooser.getResults();
                if (results.size() > 1) {
                    safeThis->processor_.beginMultiMediaImport(results);
                } else if (results.size() == 1) {
                    safeThis->processor_.beginMediaImport(results.getReference(0));
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
                htfx::tr("alert.importMediaFirstTitle"),
                htfx::tr("alert.importMediaFirstMessage"));
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
                ? htfx::tr("filechooser.exportVocalsTitle")
                : htfx::tr("filechooser.exportAccompanyTitle"),
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

        // Quick export honours whatever separation mode/model the user picked
        // (this used to force the model back to htdemucs, which silently
        // fought the mode-first UI and made the general panel unusable for
        // every RoFormer mode). Only the operating mode has to be Record.
        modeBox_.setSelectedItemIndex(0, juce::dontSendNotification);
        setChoice("operatingMode", 0);
        processor_.applyUserConfiguration();

        // Already separated with the current model? Export straight away.
        if (processor_.hasPreview()) {
            const auto pending = *pendingQuickExport_;
            if (processor_.beginQuickExport(pending.outputFile, pending.kind)) {
                pendingQuickExport_.reset();
            }
            return;
        }

        // Otherwise separate first; the timer picks the export back up when
        // the preview becomes ready.
        if (!processor_.beginSeparation() && !processor_.isModelDownloadBusy()) {
            // beginSeparation() also returns false when it has started
            // downloading a missing checkpoint and will resume by itself, so
            // only a real refusal drops the request.
            pendingQuickExport_.reset();
        }
    }

    void showExportDialog() {
        if (!processor_.hasPreview()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                htfx::tr("alert.nothingToExportTitle"),
                htfx::tr("alert.nothingToExportMessage"));
            return;
        }
        auto* content = new ExportDialogContent(processor_);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = htfx::tr("dialog.exportStemsOrMixTitle");
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
        updatePanelSwitchButtonText();
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
                updateFullScreenButtonText();
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
        } else {
            setSize(previousEditorSize_.x, previousEditorSize_.y);
            editorFullScreen_ = false;
        }
        updateFullScreenButtonText();
    }

    // Mirrors the two independent "are we full screen" representations used
    // above: a standalone host window queried live, or the in-editor flag
    // for the plugin/no-parent-window case. Needed so applyLocalizedStrings()
    // can re-derive the correct label on a language toggle without duplicating
    // toggleFullScreen()'s branching.
    bool isEffectivelyFullScreen() {
        if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
            if (auto* window = findParentComponentOfClass<juce::ResizableWindow>()) {
                return window->isFullScreen();
            }
        }
        return editorFullScreen_;
    }

    void updateFullScreenButtonText() {
        fullScreenButton_.setButtonText(
            isEffectivelyFullScreen() ? htfx::tr("button.exitFullScreen")
                                       : htfx::tr("button.fullScreen"));
    }

    void updatePanelSwitchButtonText() {
        panelSwitchButton_.setButtonText(
            advancedPanel_ ? htfx::tr("button.generalPanel")
                           : htfx::tr("button.advancedPanel"));
    }

    void updateAdvancedButtonText() {
        advancedButton_.setButtonText(
            advancedVisible_ ? htfx::tr("button.advancedOptionsCollapse")
                              : htfx::tr("button.advancedOptionsExpand"));
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
                                          ? htfx::tr("roformer.tagExperimental")
                                          : htfx::tr("roformer.tagAudited"));
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
            roformerStatus_.setText(htfx::tr("roformer.noMatchingModels"),
                                     juce::dontSendNotification);
            return;
        }
        const auto& id = visibleRoformerIds_[static_cast<std::size_t>(index)];
        const auto models = processor_.getRoformerModels();
        const auto found = std::find_if(
            models.begin(), models.end(),
            [&id](const auto& model) { return model.id == id; });
        if (found == models.end()) {
            roformerStatus_.setText(htfx::tr("roformer.unknownModel"),
                                     juce::dontSendNotification);
            return;
        }
        roformerStatus_.setText(
            (found->experimental ? htfx::tr("roformer.statusExperimental")
                                  : htfx::tr("roformer.statusAudited")) +
                " · " +
                (processor_.isModelInstalled(id)
                     ? htfx::tr("roformer.statusDownloaded")
                     : htfx::tr("roformer.statusNotDownloaded")),
            juce::dontSendNotification);
    }

    void selectHtdemucsModel(int comboIndex) {
        modelBox_.setSelectedItemIndex(comboIndex, juce::dontSendNotification);
        setChoice("model", comboIndex);
        processor_.clearRoformerModel();
        if (processor_.isModelInstalled(modelBox_.getText())) {
            processor_.applyUserConfiguration();
        }
    }

    // A RoFormer separation mode is any entry after the two fixed HTDemucs
    // entries (index 0 = 4-stem, index 1 = 6-stem); the remaining entries are
    // the RoFormer manifest categories appended in the constructor.
    bool roformerModeSelected() const {
        return separationModeBox_.getSelectedItemIndex() >= 2;
    }

    // L4: startup selection persistence. Two lines: a stable mode key
    // (htdemucs4 / htdemucs6 / category:<name>) and the RoFormer model id
    // (may be empty). Stable keys survive UI-language changes.
    static juce::File startupSelectionFile() {
        const auto overridePath = juce::SystemStats::getEnvironmentVariable(
            "HTFX_UI_STARTUP_FILE", {}).trim();
        if (overridePath.isNotEmpty()) {
            return juce::File(overridePath);
        }
        return htfx::Localization::settingsFile().getSiblingFile("ui-startup.txt");
    }

    void persistStartupSelection() const {
        const auto index = separationModeBox_.getSelectedItemIndex();
        juce::String modeKey;
        if (index == 0) {
            modeKey = "htdemucs4";
        } else if (index == 1) {
            modeKey = "htdemucs6";
        } else if (index >= 2 && index - 2 < separationModeCategories_.size()) {
            modeKey = "category:" + separationModeCategories_[index - 2];
        } else {
            return;
        }
        const auto file = startupSelectionFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(
            modeKey + "\n" + processor_.getSelectedRoformerModel() + "\n");
    }

    void restoreStartupSelection() {
        juce::String modeKey;
        juce::String modelId;
        const auto file = startupSelectionFile();
        if (file.existsAsFile()) {
            juce::StringArray lines;
            file.readLines(lines);
            if (lines.size() > 0) modeKey = lines[0].trim();
            if (lines.size() > 1) modelId = lines[1].trim();
        }
        int target = -1;
        if (modeKey == "htdemucs4") {
            target = 0;
        } else if (modeKey == "htdemucs6") {
            target = 1;
        } else if (modeKey.startsWith("category:")) {
            const auto category = modeKey.fromFirstOccurrenceOf(":", false, false);
            const auto categoryIndex = separationModeCategories_.indexOf(category);
            if (categoryIndex >= 0) target = 2 + categoryIndex;
        }
        if (target < 0) {
            // Default startup mode: HTDemucs 4-stem. It is the mode the simple
            // panel's quick exports were built around, it needs no download
            // beyond the checkpoint the installer already fetched, and it is
            // the same on both runtimes -- the CPU build filters the RoFormer
            // categories, so a RoFormer default silently meant one thing on a
            // GPU machine and another on a CPU one.
            target = 0;
            modelId.clear();
        }
        separationModeBox_.setSelectedItemIndex(target, juce::sendNotificationSync);
        if (target >= 2 && modelId.isNotEmpty()) {
            for (std::size_t index = 0; index < visibleRoformerIds_.size(); ++index) {
                if (visibleRoformerIds_[index] == modelId) {
                    roformerModelBox_.setSelectedItemIndex(
                        static_cast<int>(index), juce::sendNotificationSync);
                    break;
                }
            }
        }
    }

    void selectRoformerCategoryDefault(const juce::String& category) {
        for (int index = 0; index < roformerCategoryBox_.getNumItems(); ++index) {
            if (roformerCategoryBox_.getItemText(index).equalsIgnoreCase(category)) {
                roformerCategoryBox_.setSelectedItemIndex(index, juce::dontSendNotification);
                break;
            }
        }
        roformerSearch_.clear();
        refreshRoformerBrowser();

        // audited-first default: prefer an audited model in this category,
        // falling back to the first model of the category if none is audited.
        juce::String defaultId;
        for (const auto& model : processor_.getRoformerModels()) {
            if (model.category == category && model.audited) {
                defaultId = model.id;
                break;
            }
        }
        if (defaultId.isEmpty()) {
            for (const auto& model : processor_.getRoformerModels()) {
                if (model.category == category) {
                    defaultId = model.id;
                    break;
                }
            }
        }
        const auto found = std::find(
            visibleRoformerIds_.begin(), visibleRoformerIds_.end(), defaultId);
        if (found != visibleRoformerIds_.end()) {
            const auto index =
                static_cast<int>(std::distance(visibleRoformerIds_.begin(), found));
            roformerModelBox_.setSelectedItemIndex(index, juce::dontSendNotification);
            processor_.selectRoformerModel(defaultId);
        }
        updateRoformerStatus();
    }

    void onSeparationModeChanged() {
        const auto index = separationModeBox_.getSelectedItemIndex();
        if (index == 0) {
            selectHtdemucsModel(0);  // 4-stem separation -> htdemucs
        } else if (index == 1) {
            selectHtdemucsModel(1);  // 6-stem separation -> htdemucs_6s
        } else if (index >= 2) {
            const auto categoryIndex = index - 2;
            if (categoryIndex >= 0 && categoryIndex < separationModeCategories_.size()) {
                selectRoformerCategoryDefault(separationModeCategories_[categoryIndex]);
            }
        }
        updateSixSourceControls();
        persistStartupSelection();
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
        // Refine the coarse advancedPanel_ visibility above down to only the
        // stem sliders relevant to the currently chosen separation mode.
        updateSixSourceControls();
    }

    void updateCpuWarning() {
        const bool shouldWarn =
            computeBox_.getSelectedItemIndex() == 2 || processor_.resolvedToCpu();
        cpuWarning_.setText(
            shouldWarn
                ? htfx::tr("status.cpuModeWarning")
                : "",
            juce::dontSendNotification);
    }

    // Display names for the two stems of a RoFormer category (target,
    // residual). Pre-separation the exact worker output names are unknown, so
    // these are the category-level display pairs; the preview stem buttons
    // still switch to the worker's real labels once a separation completes.
    static std::pair<const char*, const char*> roformerStemDisplayNames(
        const juce::String& category) {
        const auto c = category.toLowerCase();
        if (c == "vocals" || c == "instvoc") return {"Vocals", "Instrumental"};
        if (c == "instrumental") return {"Instrumental", "Vocals"};
        if (c == "karaoke") return {"Karaoke", "Vocals"};
        if (c == "guitar") return {"Guitar", "Residual"};
        if (c == "denoise") return {"Clean", "Noise"};
        if (c == "dereverb") return {"Dry", "Reverb"};
        if (c == "aspiration") return {"Aspiration", "Residual"};
        if (c == "crowd") return {"Crowd", "Residual"};
        return {"Target", "Residual"};
    }

    // The display name for one stem id as the worker reported it
    // ("vocals", "instrumental", "dry", "noise", ...).
    static juce::String roformerStemDisplayName(const juce::String& stemId) {
        const auto id = stemId.toLowerCase();
        if (id == "vocals") return "Vocals";
        if (id == "instrumental" || id == "inst" || id == "music") return "Instrumental";
        if (id == "karaoke") return "Karaoke";
        if (id == "guitar") return "Guitar";
        if (id == "other" || id == "residual" || id == "rest") return "Residual";
        if (id == "clean" || id == "denoised") return "Clean";
        if (id == "noise") return "Noise";
        if (id == "dry" || id == "noreverb") return "Dry";
        if (id == "reverb" || id == "echo") return "Reverb";
        if (id == "aspiration" || id == "breath") return "Aspiration";
        if (id == "crowd") return "Crowd";
        if (id == "bleed") return "Bleed";
        if (id.isEmpty()) return {};
        return stemId.substring(0, 1).toUpperCase() + stemId.substring(1);
    }

    // Rebuilds the clip rows when the number of imported files changes, and
    // refreshes their text/state on every timer tick.
    void refreshClipRows() {
        const int count = processor_.getClipCount();
        if (count != shownClipCount_) {
            clipRows_.clear();
            for (int index = 0; index < count; ++index) {
                auto row = std::make_unique<ClipRow>();
                row->onRowClicked = [this, index] {
                    processor_.setActiveClip(index);
                    updateSixSourceControls();
                };
                row->tickBox().onClick = [this, index] {
                    processor_.setClipSelected(
                        index,
                        clipRows_[static_cast<std::size_t>(index)]
                            ->tickBox()
                            .getToggleState());
                };
                clipList_.addAndMakeVisible(*row);
                clipRows_.push_back(std::move(row));
            }
            shownClipCount_ = count;
            updateSize();  // the design height depends on the row count
        }
        const int active = processor_.getActiveClipIndex();
        for (int index = 0; index < static_cast<int>(clipRows_.size()); ++index) {
            const auto info = processor_.getClipInfo(index);
            clipRows_[static_cast<std::size_t>(index)]->update(
                info.name, info.status, info.selected, index == active, count > 1);
        }
    }

    void updateSixSourceControls() {
        const bool modeChosen = separationModeBox_.getSelectedItemIndex() >= 0;
        const bool roformerMode = roformerModeSelected();
        const bool sixSources = !roformerMode && modelBox_.getSelectedItemIndex() == 1;
        constexpr std::array<const char*, HTDemucsGpuFXAudioProcessor::kMaxSources>
            defaultStemNames{"Drums", "Bass", "Other", "Vocals", "Guitar", "Piano"};
        if (roformerMode) {
            // The combo shows a glossed name ("Vocals（人聲）"); the category
            // is the untranslated one this row was built from.
            const int categoryIndex = separationModeBox_.getSelectedItemIndex() - 2;
            const auto category =
                categoryIndex >= 0 && categoryIndex < separationModeCategories_.size()
                    ? separationModeCategories_[categoryIndex]
                    : juce::String{};
            const auto names = roformerStemDisplayNames(category);
            juce::String first(names.first);
            juce::String second(names.second);
            // Prefer the separated result's own stem ids: they are the only
            // record of which stem is which, and their order is whatever the
            // worker's output files enumerated as.
            const auto selectedModel = processor_.getSelectedRoformerModel();
            if (const auto result = processor_.getPreviewResult();
                result != nullptr && result->stemLabels.size() >= 2 &&
                selectedModel.isNotEmpty() &&
                juce::String(result->modelName.c_str()) == selectedModel) {
                const auto fromResult0 =
                    roformerStemDisplayName(juce::String(result->stemLabels[0].c_str()));
                const auto fromResult1 =
                    roformerStemDisplayName(juce::String(result->stemLabels[1].c_str()));
                if (fromResult0.isNotEmpty() && fromResult1.isNotEmpty()) {
                    first = fromResult0;
                    second = fromResult1;
                }
            }
            stemLabels_[0].setText(htfx::glossed(first), juce::dontSendNotification);
            stemLabels_[1].setText(htfx::glossed(second), juce::dontSendNotification);
        } else {
            for (std::size_t index = 0; index < stemLabels_.size(); ++index) {
                stemLabels_[index].setText(
                    htfx::glossed(defaultStemNames[index]), juce::dontSendNotification);
            }
        }
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            // RoFormer modes are always a 2-stem split (target + residual), so
            // only the first two sliders are relevant; HTDemucs modes show the
            // 4 or 6 sliders matching the chosen model's stem count.
            const bool relevant =
                roformerMode ? index < 2 : (index < 4 || sixSources);
            const bool enabled = modeChosen && relevant;
            stemSliders_[index].setEnabled(enabled);
            stemLabels_[index].setEnabled(enabled);
            stemSliders_[index].setVisible(advancedPanel_ && relevant);
            stemLabels_[index].setVisible(advancedPanel_ && relevant);
        }
        outputSlider_.setEnabled(modeChosen);
        outputLabel_.setEnabled(modeChosen);

        // D2: hide the Demucs model combo/label/download button while a
        // RoFormer mode is active, mirroring the stem-slider gating above --
        // the control has no effect on the active route once a RoFormer
        // model is selected (see currentRuntimeConfiguration()).
        const bool modelControlsVisible =
            advancedPanel_ && advancedVisible_ && !roformerMode;
        modelLabel_.setVisible(modelControlsVisible);
        modelBox_.setVisible(modelControlsVisible);
        modelDownloadButton_.setVisible(modelControlsVisible);

        // D3: mirror D2 in the opposite direction -- hide the RoFormer
        // browser trio (category/search/model) plus the download-status
        // readout and every label above them while an HTDemucs mode is
        // active. Before this fix these stayed visible and enabled for any
        // chosen mode, so interacting with roformerModelBox_ while an
        // HTDemucs mode was displayed would silently hijack
        // currentRuntimeConfiguration() over to the RoFormer route.
        const bool roformerControlsVisible =
            advancedPanel_ && advancedVisible_ && roformerMode;
        for (auto* component : std::array<juce::Component*, 8>{
                 &roformerCategoryLabel_, &roformerCategoryBox_,
                 &roformerSearchLabel_, &roformerSearch_,
                 &roformerModelLabel_, &roformerModelBox_,
                 &roformerStatusLabel_, &roformerStatus_}) {
            component->setVisible(roformerControlsVisible);
        }

        // The layout skips hidden rows, so it must run again whenever the
        // set of visible rows changes (this runs from the 10 Hz timer).
        int visibleRows = 0;
        for (std::size_t index = 0; index < stemSliders_.size(); ++index) {
            if (stemSliders_[index].isVisible()) {
                visibleRows |= 1 << index;
            }
        }
        if (modelControlsVisible) {
            visibleRows |= 1 << 8;
        }
        if (roformerControlsVisible) {
            visibleRows |= 1 << 9;
        }
        if (visibleRows != layoutSignature_) {
            layoutSignature_ = visibleRows;
            resized();
        }
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
        refreshClipRows();
        const bool recordMode = modeBox_.getSelectedItemIndex() == 0;
        const auto separationState = processor_.getSeparationState();
        const bool separationBusy =
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::loading ||
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::separating;
        const bool mediaBusy = processor_.isMediaBusy();
        const bool modelBusy = processor_.isModelDownloadBusy();
        const bool busy = separationBusy || mediaBusy || modelBusy || processor_.isBatchBusy();
        const bool recording =
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::recording;
        progressValue_ = modelBusy
                             ? processor_.getModelDownloadProgress()
                             : (mediaBusy ? processor_.getMediaProgress()
                                          : processor_.getSeparationProgress());
        {
            const auto phase =
                currentProgressPhase(modelBusy, mediaBusy, separationBusy, recording);
            juce::String barText;
            if (phase.isNotEmpty()) {
                barText = progressValue_ >= 0.0 && progressValue_ <= 1.0
                              ? phase + "  " +
                                    juce::String(juce::roundToInt(progressValue_ * 100.0)) + "%"
                              : phase + "...";
            }
            if (barText != progressBarText_) {
                progressBarText_ = barText;
                // setTextToDisplay() turns the percentage off for good, so the
                // idle bar has to ask for it back explicitly.
                if (barText.isEmpty()) {
                    progressBar_.setPercentageDisplay(true);
                } else {
                    progressBar_.setTextToDisplay(barText);
                }
            }
        }
        recordButton_.setButtonText(
            recording ? htfx::tr("button.stopRecording") : htfx::tr("button.record"));
        recordButton_.setEnabled(!mediaBusy && !separationBusy);
        importButton_.setEnabled(!recording && !busy);
        const bool quickExportReady =
            !recording && !busy && !pendingQuickExport_.has_value() &&
            processor_.getImportedMediaFile().existsAsFile() &&
            processor_.getRecordedSeconds() > 0.0;
        vocalsOnlyButton_.setEnabled(quickExportReady);
        accompanyOnlyButton_.setEnabled(quickExportReady);
        panelSwitchButton_.setEnabled(!busy && !pendingQuickExport_.has_value());
        // Not gated on an installed checkpoint, for the same reason the quick
        // exports are not: beginSeparation() fetches a missing HTDemucs one and
        // resumes by itself, and a RoFormer mode does not use this combo's
        // model at all -- so the check disabled the button over a file the run
        // would never have opened. Pressing it either starts or says why.
        separateButton_.setEnabled(
            !recording && !busy && processor_.getRecordedSeconds() > 0.0);
        exportButton_.setEnabled(!recording && !busy && processor_.hasPreview());
        // Only the advanced panel lays the Cancel button out; the simple
        // panel cancels with Esc.
        cancelButton_.setVisible(advancedPanel_ &&
                                 ((recordMode && (separationBusy || mediaBusy)) || modelBusy));
        const bool configurationEnabled = !recording && !busy;
        const bool modeChosen = separationModeBox_.getSelectedItemIndex() >= 0;
        const bool roformerModeActive = roformerModeSelected();
        separationModeBox_.setEnabled(configurationEnabled);
        modeBox_.setEnabled(configurationEnabled);
        segmentBox_.setEnabled(configurationEnabled);
        // D2: the Demucs model combo only affects the active separation route
        // while an HTDemucs mode is chosen -- currentRuntimeConfiguration()
        // always prefers the RoFormer selection once one is active -- so
        // disable it (and its download button) during RoFormer modes instead
        // of leaving it interactive but inert.
        modelBox_.setEnabled(configurationEnabled && modeChosen && !roformerModeActive);
        modelLabel_.setEnabled(configurationEnabled && modeChosen && !roformerModeActive);
        const bool selectedModelInstalled =
            processor_.isModelInstalled(modelBox_.getText());
        modelDownloadButton_.setButtonText(
            selectedModelInstalled
                ? htfx::tr("button.modelInstalled")
                : (modelBusy ? htfx::tr("button.modelDownloading")
                             : htfx::tr("button.downloadModel")));
        modelDownloadButton_.setEnabled(
            advancedVisible_ && configurationEnabled && modeChosen &&
            !roformerModeActive && !selectedModelInstalled);
        // D3: only interactive while a RoFormer mode is actually the active
        // route -- selecting a model here otherwise silently overrides
        // currentRuntimeConfiguration() even while an HTDemucs mode is
        // displayed as chosen. Mirrors modelBox_'s D2 gating in reverse.
        roformerCategoryBox_.setEnabled(modeChosen && roformerModeActive);
        roformerSearch_.setEnabled(modeChosen && roformerModeActive);
        roformerModelBox_.setEnabled(modeChosen && roformerModeActive);
        computeBox_.setEnabled(configurationEnabled);
        gpuSlider_.setEnabled(
            configurationEnabled && computeBox_.getSelectedItemIndex() == 1);

        {
            const auto result = processor_.getPreviewResult();
            if (result != overviewSource_) {
                overviewSource_ = result;  // held, so the address cannot be recycled
                if (result != nullptr) {
                    rebuildOverviewPeaks(*result);
                } else {
                    overviewPeaks_.clear();
                }
                scaledContent_.repaint();
            } else if (advancedPanel_ && !overviewPeaks_.empty() && processor_.isPreviewPlaying()) {
                scaledContent_.repaint(previewPosition_.getBounds());
            }
        }
        const double previewDuration = processor_.getPreviewDurationSeconds();
        const double previewPosition = processor_.getPreviewPositionSeconds();
        if (!previewPosition_.isMouseButtonDown()) {
            previewPosition_.setValue(
                previewDuration > 0.0 ? previewPosition / previewDuration : 0.0,
                juce::dontSendNotification);
        }
        previewPlayButton_.setEnabled(processor_.hasPreview());
        previewStopButton_.setEnabled(processor_.hasPreview());
        previewPlayButton_.setButtonText(
            processor_.isPreviewPlaying() ? htfx::tr("button.pause") : htfx::tr("button.play"));
        previewTime_.setText(
            juce::String(previewPosition, 1) + " / " +
                juce::String(previewDuration, 1) + " s",
            juce::dontSendNotification);

        const auto importedFile = processor_.getImportedMediaFile();
        simpleFile_.setText(
            importedFile.existsAsFile()
                ? importedFile.getFileName()
                : htfx::tr("label.noMediaSelected"),
            juce::dontSendNotification);

        if (pendingQuickExport_.has_value()) {
            if (separationState ==
                    HTDemucsGpuFXAudioProcessor::SeparationState::previewReady &&
                processor_.hasPreview() && !mediaBusy) {
                const auto pending = *pendingQuickExport_;
                // Either it starts now or it never will: every refusal left in
                // beginQuickExport is permanent (a result that is neither a
                // 2-stem pair nor a 4/6-stem one, an output path that would
                // overwrite the imported file), and it reports its own reason.
                processor_.beginQuickExport(pending.outputFile, pending.kind);
                pendingQuickExport_.reset();
            } else if (
                separationState == HTDemucsGpuFXAudioProcessor::SeparationState::error ||
                separationState ==
                    HTDemucsGpuFXAudioProcessor::SeparationState::cancelled) {
                pendingQuickExport_.reset();
            } else if (!busy) {
                // Nothing is running and no preview is on its way -- the
                // separation never started, or a new import replaced the clip
                // it was waiting for. Import is not gated on this flag, so a
                // request left pending here disabled both quick exports and
                // the panel switch for the rest of the session, which looked
                // exactly like "the buttons stopped working after I imported".
                pendingQuickExport_.reset();
            }
        }

        const auto mediaStatus = processor_.getMediaStatusText();
        const auto modelStatus = processor_.getModelDownloadStatusText();
        updateRoformerStatus();
        // The path is only stat'ed when the button is clicked; a dead
        // network share must not stall the message thread ten times a second.
        const bool haveExport = processor_.getLastExportedFile() != juce::File{};
        if (haveExport != openOutputButton_.isVisible()) {
            openOutputButton_.setVisible(haveExport);
            resized();  // the status line takes the whole row while hidden
        }
        openOutputButton_.setEnabled(!mediaBusy);
        const auto statusText =
            noticeActive() ? notice_
            : modelBusy || (!selectedModelInstalled && modelStatus.isNotEmpty())
                ? modelStatus
            : recordMode ? (mediaStatus.isNotEmpty() ? mediaStatus
                                                     : processor_.getRecordStatusText())
                         : processor_.getBridgeStatusText();
        auto statusLine = statusText;
        if (!noticeActive() && recordMode && !busy && !recording &&
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::recorded) {
            statusLine += htfx::tr(advancedPanel_ ? "hint.pressSeparate"
                                                  : "hint.pressQuickExport");
            // Say so rather than letting the first press look like a hang:
            // the run starts by fetching the checkpoint.
            if (!roformerModeActive && !selectedModelInstalled) {
                statusLine += htfx::tr("hint.downloadsModelFirst");
            }
        }
        if (statusLine != status_.getText()) {
            status_.setText(statusLine, juce::dontSendNotification);
            // Long status lines (an export path, an FFmpeg error) get cut
            // off in the label; hovering shows the whole text.
            status_.setTooltip(statusLine);
        }
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

        // Frame decoration: which step the user is on, and the tone of the
        // status line (0 idle, 1 busy, 2 ready, 3 error or recording).
        const bool hasMedia =
            importedFile.existsAsFile() && processor_.getRecordedSeconds() > 0.0;
        const bool separated =
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::previewReady &&
            processor_.hasPreview();
        const auto stepImport = hasMedia ? StepState::done : StepState::active;
        const auto stepSeparate = separated ? StepState::done
                                : (separationBusy || modelBusy) ? StepState::active
                                                                : StepState::pending;
        const auto stepExport = separated ? StepState::active : StepState::pending;
        const int tone =
            separationState == HTDemucsGpuFXAudioProcessor::SeparationState::error || recording
                ? 3
            : busy      ? 1
            : separated ? 2
                        : 0;
        if (stepImport != stepImport_ || stepSeparate != stepSeparate_ ||
            stepExport != stepExport_ || tone != statusTone_) {
            stepImport_ = stepImport;
            stepSeparate_ = stepSeparate;
            stepExport_ = stepExport;
            statusTone_ = tone;
            scaledContent_.repaint();
        }
    }

    HTDemucsGpuFXAudioProcessor& processor_;
    juce::AudioProcessorValueTreeState& state_;
    HtfxLookAndFeel lookAndFeel_;  // outlives every child that draws with it
    PaintCanvas scaledContent_;
    juce::Rectangle<int> stepStrip_;
    juce::Rectangle<int> fileChip_;
    int footerDivider_ = 0;
    int layoutSignature_ = -1;
    bool dragOver_ = false;
    juce::String notice_;
    juce::uint32 noticeUntil_ = 0;
    juce::String noticeBaseline_;
    std::vector<float> overviewPeaks_;
    std::shared_ptr<const void> overviewSource_;
    StepState stepImport_ = StepState::active;
    StepState stepSeparate_ = StepState::pending;
    StepState stepExport_ = StepState::pending;
    int statusTone_ = 0;
    juce::TextButton panelSwitchButton_;
    juce::TextButton languageButton_;
    juce::Label simpleTitle_;
    juce::Label simpleFile_;
    juce::TextButton vocalsOnlyButton_;
    juce::TextButton accompanyOnlyButton_;
    juce::Label separationModeLabel_;
    juce::ComboBox separationModeBox_;
    juce::StringArray separationModeCategories_;
    // Clip list (multi-file): one row per imported file, rebuilt whenever the
    // processor reports a different clip count.
    std::vector<std::unique_ptr<ClipRow>> clipRows_;
    juce::Viewport clipViewport_;  // the rows live in clipList_ inside it
    juce::Component clipList_;
    int shownClipCount_ = -1;
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
    juce::String progressBarText_;
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
    juce::TextButton openOutputButton_;
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
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;  // last: destroyed first
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
