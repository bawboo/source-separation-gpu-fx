// full_feature_check — every user-facing feature, run against one real song.
//
// goal_check covers four separation modes and quick export. This drives the
// whole public surface the way the UI does, on the file the user actually
// tests with, and on a chosen compute backend so the CPU and GPU runtimes can
// each be signed off:
//
//   1. import + separate + quick export (vocals / accompaniment)
//   2. stem export for every source, and a full mix export
//   3. cancelling a separation part-way through
//   4. video import (an MP4 built from the song) and mix export back into MP4
//   5. all twelve separation modes: 4-stem, 6-stem, and each RoFormer
//      category's default model (the same audited-first rule the UI applies)
//   6. batch: import two clips, batch-separate, batch-export
//
// Every exported WAV must be 44.1 kHz stereo float and match the source
// length; a stem must not be silent. One line per check; the summary names
// what broke.
//
// Usage: htdemucs_full_feature_check.exe "<song>" [--backend auto|cpu|cuda]
//                                                 [--modes all|quick]

#include "Localization.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using Processor = HTDemucsGpuFXAudioProcessor;
using State = Processor::SeparationState;
using Kind = Processor::QuickExportKind;

int checksRun = 0;
int checksFailed = 0;
std::vector<std::string> failures;

void report(const std::string& label, bool ok, const std::string& detail = {}) {
    ++checksRun;
    std::cout << (ok ? "  ok   " : "  FAIL ") << label;
    if (!detail.empty()) {
        std::cout << "  -- " << detail;
    }
    std::cout << std::endl;
    if (!ok) {
        ++checksFailed;
        failures.push_back(label + (detail.empty() ? "" : " (" + detail + ")"));
    }
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        juce::Timer::callPendingTimersSynchronously();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return predicate();
}

void setChoice(Processor& processor, const juce::String& id, int index) {
    auto* parameter = processor.parameters().getParameter(id);
    if (parameter == nullptr) {
        report("parameter " + id.toStdString(), false, "missing");
        return;
    }
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(index)));
}

bool waitForMedia(Processor& processor, std::chrono::seconds timeout) {
    return waitUntil([&processor] { return !processor.isMediaBusy(); }, timeout);
}

// Echo status changes while separating: a run that goes quiet for minutes is
// the failure mode the progress work guards against, so it stays visible.
// A run is only "stuck" when nothing changes for a long time. A model
// download or a slow inference that keeps reporting progress is not a
// failure, however long it takes, so the deadline moves with each update.
bool waitForSeparation(Processor& processor, const std::string& label,
                       std::chrono::seconds stallTimeout) {
    juce::String last;
    auto lastChange = std::chrono::steady_clock::now();
    const auto absoluteDeadline = lastChange + std::chrono::hours(3);
    return waitUntil(
        [&] {
            if (std::chrono::steady_clock::now() - lastChange > stallTimeout ||
                std::chrono::steady_clock::now() > absoluteDeadline) {
                return true;  // let the caller see the unfinished state
            }
            const auto status = processor.getRecordStatusText();
            if (status != last) {
                last = status;
                lastChange = std::chrono::steady_clock::now();
                const auto fraction = processor.getSeparationProgress();
                std::cout << "       [" << label << "] " << status;
                if (fraction >= 0.0) {
                    std::cout << " (" << juce::roundToInt(fraction * 100.0) << "%)";
                }
                std::cout << std::endl;
            }
            const auto state = processor.getSeparationState();
            return state == State::previewReady || state == State::error ||
                   state == State::cancelled;
        },
        std::chrono::hours(3));
}

struct WavInfo {
    bool readable = false;
    double sampleRate = 0.0;
    int channels = 0;
    juce::int64 frames = 0;
    bool isFloat = false;
    double rms = 0.0;
};

WavInfo inspect(const juce::File& file) {
    WavInfo info;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0) {
        return info;
    }
    info.readable = true;
    info.sampleRate = reader->sampleRate;
    info.channels = static_cast<int>(reader->numChannels);
    info.frames = reader->lengthInSamples;
    info.isFloat = reader->usesFloatingPointData;
    // RMS over a sparse sample of the file is enough to tell silence apart.
    const int block = 1 << 16;
    juce::AudioBuffer<float> buffer(info.channels, block);
    double sum = 0.0;
    juce::int64 counted = 0;
    for (juce::int64 start = 0; start < info.frames; start += block * 8) {
        const int n = static_cast<int>(std::min<juce::int64>(block, info.frames - start));
        if (!reader->read(&buffer, 0, n, start, true, true)) {
            break;
        }
        for (int c = 0; c < info.channels; ++c) {
            const auto* d = buffer.getReadPointer(c);
            for (int i = 0; i < n; ++i) {
                sum += static_cast<double>(d[i]) * d[i];
            }
        }
        counted += static_cast<juce::int64>(n) * info.channels;
    }
    info.rms = counted > 0 ? std::sqrt(sum / static_cast<double>(counted)) : 0.0;
    return info;
}

// Every export must be the project format and the source's length.
bool checkExport(const std::string& label, const juce::File& file,
                 juce::int64 expectedFrames, bool mustHaveSignal) {
    const auto info = inspect(file);
    if (!info.readable) {
        report(label, false, "not a readable WAV: " + file.getFileName().toStdString());
        return false;
    }
    std::string detail;
    bool ok = true;
    if (std::abs(info.sampleRate - 44'100.0) > 0.5) {
        ok = false; detail += "rate=" + std::to_string(static_cast<int>(info.sampleRate)) + " ";
    }
    if (info.channels != 2) {
        ok = false; detail += "channels=" + std::to_string(info.channels) + " ";
    }
    if (!info.isFloat) {
        ok = false; detail += "not-float ";
    }
    if (info.frames != expectedFrames) {
        ok = false;
        detail += "frames=" + std::to_string(info.frames) + " expected=" +
                  std::to_string(expectedFrames) + " ";
    }
    if (mustHaveSignal && info.rms < 1.0e-4) {
        ok = false; detail += "silent(rms=" + std::to_string(info.rms) + ") ";
    }
    report(label, ok, detail);
    return ok;
}

bool importFile(Processor& processor, const juce::File& file, const std::string& label) {
    if (!processor.beginMediaImport(file)) {
        report(label + " import", false, "did not start: " +
               processor.getMediaStatusText().toStdString());
        return false;
    }
    if (!waitForMedia(processor, std::chrono::seconds(300))) {
        report(label + " import", false, "timed out");
        return false;
    }
    report(label + " import", true);
    return true;
}

bool separate(Processor& processor, const std::string& label,
              std::chrono::seconds timeout) {
    // Wall-clock time from the user's point of view: includes a model
    // download and engine start-up, which is what they actually wait for.
    const auto startedAt = std::chrono::steady_clock::now();
    if (!processor.beginSeparation()) {
        if (!processor.isModelDownloadBusy()) {
            report(label + " separate", false, "did not start: " +
                   processor.getRecordStatusText().toStdString());
            return false;
        }
        // A missing checkpoint starts its own download and resumes by itself.
        std::cout << "       [" << label << "] downloading the model first" << std::endl;
        if (!waitUntil([&] { return !processor.isModelDownloadBusy(); },
                       std::chrono::seconds(1800))) {
            report(label + " separate", false, "model download timed out");
            return false;
        }
        if (!waitUntil([&] {
                const auto s = processor.getSeparationState();
                return s == State::loading || s == State::separating ||
                       s == State::previewReady || s == State::error;
            }, std::chrono::seconds(30))) {
            report(label + " separate", false, "did not resume after download: " +
                   processor.getRecordStatusText().toStdString());
            return false;
        }
    }
    waitForSeparation(processor, label, timeout);
    if (processor.getSeparationState() != State::previewReady) {
        report(label + " separate", false,
               processor.getSeparationState() == State::error
                   ? processor.getRecordStatusText().toStdString()
                   : "stalled: no status change for " + std::to_string(timeout.count()) +
                         "s (" + processor.getRecordStatusText().toStdString() + ")");
        return false;
    }
    if (!processor.hasPreview()) {
        report(label + " separate", false, processor.getRecordStatusText().toStdString());
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    report(label + " separate", true, std::to_string(elapsed) + "s wall");
    return true;
}

bool quickExports(Processor& processor, const std::string& label,
                  const juce::File& dir, juce::int64 frames) {
    bool all = true;
    for (const auto kind : {Kind::vocals, Kind::accompaniment}) {
        const bool vocals = kind == Kind::vocals;
        const auto out = dir.getChildFile(
            juce::String(label).replaceCharacter(' ', '_') +
            (vocals ? "_vocals.wav" : "_accompany.wav"));
        out.deleteFile();
        const std::string name = label + (vocals ? " export vocals" : " export accompaniment");
        if (!processor.beginQuickExport(out, kind)) {
            report(name, false, "refused: " + processor.getMediaStatusText().toStdString());
            all = false;
            continue;
        }
        if (!waitForMedia(processor, std::chrono::seconds(600))) {
            report(name, false, "timed out");
            all = false;
            continue;
        }
        all = checkExport(name, out, frames, true) && all;
    }
    return all;
}

struct Mode {
    std::string label;
    juce::String roformerId;   // empty => HTDemucs
    int htdemucsIndex = 0;     // kModelNames: htdemucs, htdemucs_6s
};

// The UI's rule: per category, the first audited model, else the first model.
std::vector<Mode> allModes(const Processor& processor) {
    std::vector<Mode> modes{{"4-stem", "", 0}, {"6-stem", "", 1}};
    juce::StringArray categories;
    for (const auto& model : processor.getRoformerModels()) {
        categories.addIfNotAlreadyThere(model.category);
    }
    categories.sort(true);
    for (const auto& category : categories) {
        juce::String pick;
        for (const auto& model : processor.getRoformerModels()) {
            if (model.category == category && model.audited) { pick = model.id; break; }
        }
        if (pick.isEmpty()) {
            for (const auto& model : processor.getRoformerModels()) {
                if (model.category == category) { pick = model.id; break; }
            }
        }
        modes.push_back({category.toStdString(), pick, 0});
    }
    return modes;
}

void applyMode(Processor& processor, const Mode& mode) {
    if (mode.roformerId.isNotEmpty()) {
        if (!processor.selectRoformerModel(mode.roformerId)) {
            report(mode.label + " select model", false,
                   "not in catalog: " + mode.roformerId.toStdString());
        }
    } else {
        processor.clearRoformerModel();
        setChoice(processor, "model", mode.htdemucsIndex);
    }
    processor.applyUserConfiguration();
}

bool runProcess(const juce::StringArray& command, juce::String& output) {
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        return false;
    }
    output = process.readAllProcessOutput();
    return process.waitForProcessToFinish(120'000) && process.getExitCode() == 0;
}

juce::File bundledFfmpeg() {
    const auto env = juce::SystemStats::getEnvironmentVariable("HTFX_FFMPEG", {}).trim();
    if (env.isNotEmpty()) {
        return juce::File(env);
    }
    // The build tree keeps the release FFmpeg beside the runtimes.
    auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (int up = 0; up < 5; ++up) {
        dir = dir.getParentDirectory();
        const auto candidate = dir.getChildFile("build/ffmpeg-lgpl/bin/ffmpeg.exe");
        if (candidate.existsAsFile()) {
            return candidate;
        }
        const auto atRoot = dir.getChildFile("ffmpeg-lgpl/bin/ffmpeg.exe");
        if (atRoot.existsAsFile()) {
            return atRoot;
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    // The UI language defaults to zh-TW, which is what the user tests with.

    // Read arguments as wide strings so a non-ASCII path survives.
    std::vector<juce::String> args;
    {
        int wideCount = 0;
        if (auto** wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideCount);
            wideArgv != nullptr) {
            for (int i = 1; i < wideCount; ++i) {
                args.emplace_back(wideArgv[i]);
            }
            LocalFree(wideArgv);
        }
    }
    if (args.empty()) {
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(juce::String::fromUTF8(argv[i]));
        }
    }
    juce::String inputPath;
    juce::String backend = "auto";
    juce::String modeSet = "all";
    // --phases 1,4,6 runs only those phases; the batch phase, for one, is
    // worth re-checking without re-downloading every model for phase 5.
    juce::StringArray phases;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--backend" && i + 1 < args.size()) {
            backend = args[++i];
        } else if (args[i] == "--modes" && i + 1 < args.size()) {
            modeSet = args[++i];
        } else if (args[i] == "--phases" && i + 1 < args.size()) {
            phases.addTokens(args[++i], ",", "");
            phases.trim();
        } else if (inputPath.isEmpty()) {
            inputPath = args[i];
        }
    }
    const juce::File input{inputPath};
    if (!input.existsAsFile()) {
        std::cerr << "input file not found: " << input.getFullPathName() << std::endl;
        return 2;
    }

    const auto outputDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("htfx-full-check-" + backend);
    outputDir.deleteRecursively();
    outputDir.createDirectory();

    const auto source = inspect(input);
    // The project runs at 44.1 kHz; a 48 kHz source is resampled on import,
    // so the expected export length is the source's duration at 44.1 kHz.
    const auto expectedFrames = static_cast<juce::int64>(
        std::llround(static_cast<double>(source.frames) * 44'100.0 / source.sampleRate));
    std::cout << "song: " << input.getFileName() << "  " << source.sampleRate << " Hz, "
              << source.channels << " ch, " << source.frames << " frames ("
              << juce::roundToInt(source.frames / source.sampleRate) << " s)\n"
              << "backend: " << backend << "   modes: " << modeSet << "\n"
              << "outputs: " << outputDir.getFullPathName() << "\n" << std::endl;

    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<Processor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);
    setChoice(*processor, "operatingMode", 0);
    setChoice(*processor, "computeBackend", backend == "cpu" ? 2 : backend == "cuda" ? 1 : 0);

    // Stall limit between status updates, not a cap on the whole run.
    const auto separationTimeout = std::chrono::seconds(backend == "cpu" ? 1800 : 900);
    const auto phaseEnabled = [&phases](int n) {
        return phases.isEmpty() || phases.contains(juce::String(n));
    };

    // ── 1. import, separate (4-stem), quick export ─────────────────────────
    std::cout << "[1] import / separate / quick export" << std::endl;
    applyMode(*processor, {"4-stem", "", 0});
    if (phaseEnabled(1) && importFile(*processor, input, "wav") &&
        separate(*processor, "4-stem", separationTimeout)) {
        report("resolved backend",
               backend != "cpu" || processor->resolvedToCpu(),
               processor->resolvedToCpu() ? "cpu" : "gpu");
        quickExports(*processor, "4-stem", outputDir, expectedFrames);

        // ── 2. stem export + mix export ───────────────────────────────────
        std::cout << "[2] stem export / mix export" << std::endl;
        const int sources = processor->getActiveSourceCount();
        report("active source count", sources == 4, std::to_string(sources));
        std::vector<int> indices;
        for (int i = 0; i < sources; ++i) indices.push_back(i);
        const auto stemDir = outputDir.getChildFile("stems-4");
        stemDir.deleteRecursively();
        stemDir.createDirectory();
        if (!processor->beginStemExport(stemDir, indices)) {
            report("stem export", false, "refused: " + processor->getMediaStatusText().toStdString());
        } else if (!waitForMedia(*processor, std::chrono::seconds(600))) {
            report("stem export", false, "timed out");
        } else {
            juce::Array<juce::File> stems;
            stemDir.findChildFiles(stems, juce::File::findFiles, false, "*.wav");
            report("stem export count", stems.size() == sources,
                   std::to_string(stems.size()) + " files");
            for (const auto& stem : stems) {
                checkExport("stem " + stem.getFileNameWithoutExtension().toStdString(),
                            stem, expectedFrames, true);
            }
        }
        const auto mix = outputDir.getChildFile("mix-4.wav");
        mix.deleteFile();
        if (!processor->beginMixExport(mix, false)) {
            report("mix export", false, "refused: " + processor->getMediaStatusText().toStdString());
        } else if (!waitForMedia(*processor, std::chrono::seconds(600))) {
            report("mix export", false, "timed out");
        } else {
            checkExport("mix export", mix, expectedFrames, true);
        }
    }

    // ── 3. cancel a separation part-way ────────────────────────────────────
    std::cout << "[3] cancel" << std::endl;
    applyMode(*processor, {"6-stem", "", 1});
    if (phaseEnabled(3) && importFile(*processor, input, "wav (cancel)")) {
        if (!processor->beginSeparation()) {
            report("cancel: start", false, processor->getRecordStatusText().toStdString());
        } else {
            waitUntil([&] { return processor->getSeparationState() == State::separating; },
                      std::chrono::seconds(240));
            std::this_thread::sleep_for(std::chrono::seconds(3));
            processor->cancelSeparation();
            const bool settled = waitUntil(
                [&] {
                    const auto s = processor->getSeparationState();
                    return s == State::cancelled || s == State::error || s == State::previewReady;
                },
                std::chrono::seconds(120));
            const auto s = processor->getSeparationState();
            report("cancel settles", settled && s != State::error,
                   s == State::cancelled ? "cancelled"
                   : s == State::previewReady ? "finished before cancel took effect"
                                              : processor->getRecordStatusText().toStdString());
        }
    }

    // ── 4. video round trip ────────────────────────────────────────────────
    std::cout << "[4] video import / mix export to MP4" << std::endl;
    const auto ffmpeg = bundledFfmpeg();
    if (!phaseEnabled(4)) {
        // skipped
    } else if (!ffmpeg.existsAsFile()) {
        report("video: ffmpeg available", false, "set HTFX_FFMPEG");
    } else {
        const auto video = outputDir.getChildFile("song.mp4");
        juce::String log;
        const bool made = runProcess(
            {ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-y",
             "-f", "lavfi", "-i", "color=c=black:s=320x180:r=25",
             "-i", input.getFullPathName(), "-shortest",
             "-c:v", "mpeg4", "-q:v", "8", "-c:a", "aac", video.getFullPathName()},
            log);
        report("video: build MP4 from song", made && video.getSize() > 1024, log.trim().toStdString());
        if (made) {
            applyMode(*processor, {"4-stem", "", 0});
            if (importFile(*processor, video, "mp4")) {
                report("mp4 classified as video", processor->importedFromVideo());
                if (separate(*processor, "mp4 4-stem", separationTimeout)) {
                    const auto outVideo = outputDir.getChildFile("song-mix.mp4");
                    outVideo.deleteFile();
                    if (!processor->beginMixExport(outVideo, true)) {
                        report("mix export into MP4", false,
                               "refused: " + processor->getMediaStatusText().toStdString());
                    } else if (!waitForMedia(*processor, std::chrono::seconds(900))) {
                        report("mix export into MP4", false, "timed out");
                    } else {
                        juce::String probe;
                        const bool has = runProcess(
                            {ffmpeg.getParentDirectory().getChildFile("ffprobe.exe").getFullPathName(),
                             "-v", "error", "-select_streams", "a:0", "-show_entries",
                             "stream=codec_name", "-of", "csv=p=0", outVideo.getFullPathName()},
                            probe);
                        report("mix export into MP4", outVideo.getSize() > 1024 && has,
                               "audio=" + probe.trim().toStdString() + " " +
                               std::to_string(outVideo.getSize() / 1024) + " KB");
                    }
                }
            }
        }
    }

    // ── 5. every separation mode ───────────────────────────────────────────
    std::cout << "[5] all separation modes" << std::endl;
    auto modes = allModes(*processor);
    if (modeSet == "quick") {
        modes.erase(std::remove_if(modes.begin(), modes.end(), [](const Mode& m) {
            return m.label != "6-stem" && m.label != "vocals" && m.label != "guitar";
        }), modes.end());
    }
    for (const auto& mode : modes) {
        if (!phaseEnabled(5) || mode.label == "4-stem") {
            continue;  // 4-stem is already exercised above
        }
        applyMode(*processor, mode);
        if (!importFile(*processor, input, mode.label)) {
            continue;
        }
        if (!separate(*processor, mode.label, separationTimeout)) {
            continue;
        }
        quickExports(*processor, mode.label, outputDir, expectedFrames);
    }

    // ── 6. batch: two clips ────────────────────────────────────────────────
    std::cout << "[6] batch import / separate / export" << std::endl;
    const auto second = outputDir.getChildFile("song-copy.wav");
    input.copyFileTo(second);
    applyMode(*processor, {"4-stem", "", 0});
    if (!phaseEnabled(6)) {
        // skipped
    } else if (!processor->beginMultiMediaImport({input, second})) {
        report("batch import", false, "did not start");
    } else if (!waitForMedia(*processor, std::chrono::seconds(600))) {
        report("batch import", false, "timed out");
    } else {
        report("batch import", processor->getClipCount() == 2,
               std::to_string(processor->getClipCount()) + " clips");
        if (!processor->beginBatchSeparation()) {
            report("batch separate", false, processor->getRecordStatusText().toStdString());
        } else {
            const bool done = waitUntil(
                [&] { return !processor->isBatchBusy() && !processor->isMediaBusy(); },
                std::chrono::hours(3));
            bool allSeparated = done;
            for (int i = 0; i < processor->getClipCount(); ++i) {
                allSeparated = allSeparated && processor->getClipInfo(i).separated;
            }
            report("batch separate", allSeparated,
                   allSeparated ? "" : processor->getRecordStatusText().toStdString());
            const auto batchDir = outputDir.getChildFile("batch");
            batchDir.deleteRecursively();
            batchDir.createDirectory();
            if (!processor->beginBatchExport(batchDir, Kind::vocals)) {
                report("batch export", false, "refused: " + processor->getMediaStatusText().toStdString());
            } else if (!waitUntil([&] { return !processor->isBatchBusy() && !processor->isMediaBusy(); },
                                  std::chrono::hours(1))) {
                report("batch export", false, "timed out");
            } else {
                juce::Array<juce::File> files;
                batchDir.findChildFiles(files, juce::File::findFiles, false, "*.wav");
                report("batch export count", files.size() == 2,
                       std::to_string(files.size()) + " files");
                for (const auto& f : files) {
                    checkExport("batch " + f.getFileNameWithoutExtension().toStdString(),
                                f, expectedFrames, true);
                }
            }
        }
    }

    processor->releaseResources();
    std::cout << "\n" << (checksFailed == 0 ? "FULL CHECK PASS" : "FULL CHECK FAIL")
              << " (" << (checksRun - checksFailed) << "/" << checksRun
              << " checks ok, backend=" << backend << ")" << std::endl;
    for (const auto& f : failures) {
        std::cout << "  - " << f << std::endl;
    }
    return checksFailed == 0 ? 0 : 1;
}
