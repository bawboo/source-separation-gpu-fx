// format_matrix_check — every media format the file chooser accepts, through
// every feature, on short synthetic clips.
//
// full_feature_check proves the features on one real song. This proves the
// import path on every container/codec the UI offers (*.wav *.flac *.aif
// *.aiff *.mp3 *.ogg *.m4a *.mp4 *.mov *.mkv *.avi *.webm *.m4v *.wmv *.mpeg),
// on unusual sample formats (8-bit, 24-bit, 32-bit float, mono, 22.05/48/96
// kHz, 5.1), and on files that must be rejected cleanly (not media, zero
// samples, a video without an audio track). For each importable file:
//
//   import → length matches the source → 4-stem separation → preview length
//   → vocals / accompaniment quick export → stem export → mix export
//   → (video) mix export back into MP4 → ffprobe confirms the streams
//
// then one batch over three different formats, and cancelling an import.
//
// Expectations are encoded in file names so the media set can be regenerated
// by a script:
//   bogus*, *_empty*, *_no_audio*   must be rejected with a message
//   *_silent*                       imports; exports may be silent
//   *_half_second*                  is 0.5 s long
//   *_truncated*                    imports; length is whatever decoded
//   everything else                 is 30 s long (±0.2 s for lossy codecs)
//
// Usage: htdemucs_format_matrix_check.exe <file-or-dir>... [--backend auto|cpu|cuda]

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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

bool waitForSeparation(Processor& processor, std::chrono::seconds stallTimeout) {
    juce::String last;
    auto lastChange = std::chrono::steady_clock::now();
    return waitUntil(
        [&] {
            if (std::chrono::steady_clock::now() - lastChange > stallTimeout) {
                return true;
            }
            const auto status = processor.getRecordStatusText();
            if (status != last) {
                last = status;
                lastChange = std::chrono::steady_clock::now();
            }
            const auto state = processor.getSeparationState();
            return state == State::previewReady || state == State::error ||
                   state == State::cancelled;
        },
        std::chrono::hours(2));
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
    const int block = 1 << 15;
    juce::AudioBuffer<float> buffer(info.channels, block);
    double sum = 0.0;
    juce::int64 counted = 0;
    for (juce::int64 start = 0; start < info.frames; start += block * 4) {
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
    // Synthetic clips have no real vocals, so a "vocals" stem can be very
    // quiet; what must never happen is an all-zero export, whose RMS is
    // exactly 0. Anything a model actually produced sits well above 1e-7.
    if (mustHaveSignal && info.rms < 1.0e-7) {
        ok = false; detail += "silent(rms=" + std::to_string(info.rms) + ") ";
    }
    report(label, ok, detail);
    return ok;
}

bool runProcess(const juce::StringArray& command, juce::String& output) {
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        return false;
    }
    output = process.readAllProcessOutput();
    return process.waitForProcessToFinish(120'000) && process.getExitCode() == 0;
}

juce::File bundledFfprobe() {
    const auto env = juce::SystemStats::getEnvironmentVariable("HTFX_FFMPEG", {}).trim();
    if (env.isNotEmpty()) {
        return juce::File(env).getSiblingFile("ffprobe.exe");
    }
    auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (int up = 0; up < 5; ++up) {
        dir = dir.getParentDirectory();
        for (const auto* relative : {"build/ffmpeg-lgpl/bin/ffprobe.exe",
                                     "ffmpeg-lgpl/bin/ffprobe.exe"}) {
            const auto candidate = dir.getChildFile(relative);
            if (candidate.existsAsFile()) {
                return candidate;
            }
        }
    }
    return {};
}

// codec names of the first video and first audio stream, e.g. "h264/aac".
juce::String probeStreams(const juce::File& ffprobe, const juce::File& media) {
    juce::String video, audio;
    runProcess({ffprobe.getFullPathName(), "-v", "error", "-select_streams", "v:0",
                "-show_entries", "stream=codec_name", "-of", "csv=p=0",
                media.getFullPathName()}, video);
    runProcess({ffprobe.getFullPathName(), "-v", "error", "-select_streams", "a:0",
                "-show_entries", "stream=codec_name", "-of", "csv=p=0",
                media.getFullPathName()}, audio);
    return video.trim() + "/" + audio.trim();
}

bool isVideoName(const juce::File& file) {
    const auto ext = file.getFileExtension().toLowerCase();
    for (const auto* v : {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".wmv", ".mpeg"}) {
        if (ext == v) return true;
    }
    return false;
}

bool isAcceptedName(const juce::File& file) {
    const auto ext = file.getFileExtension().toLowerCase();
    for (const auto* a : {".wav", ".flac", ".aif", ".aiff", ".mp3", ".ogg", ".m4a"}) {
        if (ext == a) return true;
    }
    return isVideoName(file);
}

struct Expectation {
    bool mustReject = false;
    bool maySilent = false;
    double seconds = 30.0;       // <= 0: any positive length
    double tolerance = 0.2;
};

Expectation expectationFor(const juce::File& file) {
    const auto name = file.getFileNameWithoutExtension().toLowerCase();
    Expectation e;
    if (name.startsWith("bogus") || name.contains("_empty") || name.contains("_no_audio")) {
        e.mustReject = true;
    } else if (name.contains("_silent")) {
        e.maySilent = true;
    } else if (name.contains("_half_second")) {
        e.seconds = 0.5;
        e.tolerance = 0.05;
    } else if (name.contains("_truncated")) {
        e.seconds = 0.0;
    } else if (name.contains("_20min")) {
        e.seconds = 1200.0;
    }
    return e;
}

std::string safeLabel(const juce::File& file) {
    // Exported file names come from the label; keep them ASCII and unique.
    auto s = file.getFileName().toStdString();
    for (auto& c : s) {
        if (static_cast<unsigned char>(c) > 126 || c == ' ' || c == '#') c = '_';
    }
    return s;
}

// One file through every feature. Returns false when a later phase could not
// run at all (the failures are already reported).
void exercise(Processor& processor, const juce::File& file, const juce::File& outputDir,
              const juce::File& ffprobe, std::chrono::seconds stallTimeout) {
    const auto label = file.getFileName().toStdString();
    const auto e = expectationFor(file);
    std::cout << "\n[" << label << "]" << (e.mustReject ? " (must be rejected)" : "") << std::endl;

    const bool started = processor.beginMediaImport(file);
    if (!started) {
        // A path beyond MAX_PATH may be refused, but never silently.
        const bool longPath = file.getFullPathName().length() > 259;
        const auto status = processor.getMediaStatusText();
        report(label + ": import starts",
               e.mustReject || (longPath && status.isNotEmpty()),
               "refused: '" + status.toStdString() + "' mediaBusy=" +
                   (processor.isMediaBusy() ? "1" : "0") + " exists=" +
                   (file.existsAsFile() ? "1" : "0") + " pathLength=" +
                   std::to_string(file.getFullPathName().length()));
        return;
    }
    if (!waitForMedia(processor, std::chrono::seconds(300))) {
        report(label + ": import completes", false, "timed out (still busy)");
        processor.cancelMediaOperation();
        waitForMedia(processor, std::chrono::seconds(60));
        return;
    }
    const auto seconds = processor.getRecordedSeconds();
    const bool errored = processor.getSeparationState() == State::error || seconds <= 0.0;
    if (e.mustReject) {
        report(label + ": rejected with a message",
               errored && processor.getMediaStatusText().isNotEmpty(),
               errored ? processor.getMediaStatusText().toStdString()
                       : "accepted " + std::to_string(seconds) + " s");
        return;
    }
    if (errored) {
        report(label + ": import", false, processor.getMediaStatusText().toStdString());
        return;
    }
    {
        std::string detail = std::to_string(seconds) + " s";
        bool lengthOk = seconds > 0.0;
        if (e.seconds > 0.0) {
            lengthOk = std::abs(seconds - e.seconds) <= e.tolerance;
            detail += " (expected " + std::to_string(e.seconds) + " ±" +
                      std::to_string(e.tolerance) + ")";
        }
        report(label + ": import length", lengthOk, detail);
    }
    report(label + ": classified as " + (isVideoName(file) ? "video" : "audio"),
           processor.importedFromVideo() == isVideoName(file));
    const auto frames = static_cast<juce::int64>(std::llround(seconds * 44'100.0));

    // separate (HTDemucs 4-stem)
    const auto startedAt = std::chrono::steady_clock::now();
    if (!processor.beginSeparation()) {
        if (!processor.isModelDownloadBusy()) {
            report(label + ": separate", false, "did not start: " +
                   processor.getRecordStatusText().toStdString());
            return;
        }
        waitUntil([&] { return !processor.isModelDownloadBusy(); }, std::chrono::seconds(1800));
        waitUntil([&] {
            const auto s = processor.getSeparationState();
            return s == State::loading || s == State::separating ||
                   s == State::previewReady || s == State::error;
        }, std::chrono::seconds(30));
    }
    waitForSeparation(processor, stallTimeout);
    if (processor.getSeparationState() != State::previewReady || !processor.hasPreview()) {
        report(label + ": separate", false, processor.getRecordStatusText().toStdString());
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    report(label + ": separate", true, std::to_string(elapsed) + " ms");
    report(label + ": preview length",
           std::abs(processor.getPreviewDurationSeconds() - seconds) < 0.01,
           std::to_string(processor.getPreviewDurationSeconds()) + " s");

    const auto base = safeLabel(file);
    // quick exports
    for (const auto kind : {Kind::vocals, Kind::accompaniment}) {
        const bool vocals = kind == Kind::vocals;
        const auto out = outputDir.getChildFile(base + (vocals ? "_vocals.wav" : "_accompany.wav"));
        out.deleteFile();
        const std::string name = label + (vocals ? ": export vocals" : ": export accompaniment");
        if (!processor.beginQuickExport(out, kind)) {
            report(name, false, "refused: " + processor.getMediaStatusText().toStdString());
        } else if (!waitForMedia(processor, std::chrono::seconds(600))) {
            report(name, false, "timed out");
        } else {
            checkExport(name, out, frames, !e.maySilent);
        }
    }
    // stem export
    {
        const int sources = processor.getActiveSourceCount();
        std::vector<int> indices;
        for (int i = 0; i < sources; ++i) indices.push_back(i);
        const auto stemDir = outputDir.getChildFile(base + "-stems");
        stemDir.deleteRecursively();
        stemDir.createDirectory();
        if (!processor.beginStemExport(stemDir, indices)) {
            report(label + ": stem export", false, "refused: " + processor.getMediaStatusText().toStdString());
        } else if (!waitForMedia(processor, std::chrono::seconds(600))) {
            report(label + ": stem export", false, "timed out");
        } else {
            juce::Array<juce::File> stems;
            stemDir.findChildFiles(stems, juce::File::findFiles, false, "*.wav");
            bool ok = stems.size() == sources;
            std::string detail = std::to_string(stems.size()) + " files";
            for (const auto& stem : stems) {
                const auto info = inspect(stem);
                if (!info.readable || info.frames != frames || info.channels != 2 || !info.isFloat) {
                    ok = false;
                    detail += " bad:" + stem.getFileName().toStdString();
                }
            }
            report(label + ": stem export", ok, detail);
        }
    }
    // mix export (wav)
    {
        const auto mix = outputDir.getChildFile(base + "_mix.wav");
        mix.deleteFile();
        if (!processor.beginMixExport(mix, false)) {
            report(label + ": mix export", false, "refused: " + processor.getMediaStatusText().toStdString());
        } else if (!waitForMedia(processor, std::chrono::seconds(600))) {
            report(label + ": mix export", false, "timed out");
        } else {
            checkExport(label + ": mix export", mix, frames, !e.maySilent);
        }
    }
    // mix export back into the video
    if (isVideoName(file)) {
        const auto outVideo = outputDir.getChildFile(base + "_mix.mp4");
        outVideo.deleteFile();
        const auto sourceStreams = probeStreams(ffprobe, file);
        if (!processor.beginMixExport(outVideo, true)) {
            report(label + ": mix into MP4", false, "refused: " + processor.getMediaStatusText().toStdString());
        } else if (!waitForMedia(processor, std::chrono::seconds(900))) {
            report(label + ": mix into MP4", false, "timed out");
        } else if (!outVideo.existsAsFile() || outVideo.getSize() < 1024) {
            report(label + ": mix into MP4", false,
                   "no output; status: " + processor.getMediaStatusText().toStdString() +
                   " [source " + sourceStreams.toStdString() + "]");
        } else {
            const auto streams = probeStreams(ffprobe, outVideo);
            const auto sourceVideo = sourceStreams.upToFirstOccurrenceOf("/", false, false);
            const auto outVideoCodec = streams.upToFirstOccurrenceOf("/", false, false);
            // Either the source video was stream-copied, or a codec MP4
            // cannot carry (VP8, WMV, MPEG-1) was re-encoded to H.264/MPEG-4.
            const bool videoOk = outVideoCodec == sourceVideo || outVideoCodec == "h264" ||
                                 outVideoCodec == "mpeg4";
            const bool ok = videoOk && streams.endsWith("/aac");
            juce::String durationText, sourceDurationText;
            runProcess({ffprobe.getFullPathName(), "-v", "error", "-show_entries",
                        "format=duration", "-of", "csv=p=0", outVideo.getFullPathName()},
                       durationText);
            runProcess({ffprobe.getFullPathName(), "-v", "error", "-show_entries",
                        "format=duration", "-of", "csv=p=0", file.getFullPathName()},
                       sourceDurationText);
            const auto duration = durationText.trim().getDoubleValue();
            const auto sourceDuration = sourceDurationText.trim().getDoubleValue();
            // The picture is kept whole; audio shorter than it is padded, so
            // the result is as long as the source video, not the audio.
            report(label + ": mix into MP4", ok && std::abs(duration - sourceDuration) < 0.5,
                   "streams " + streams.toStdString() + " (source " + sourceStreams.toStdString() +
                   ") " + std::to_string(duration) + " s (source " + std::to_string(sourceDuration) +
                   " s), " + std::to_string(outVideo.getSize() / 1024) + " KB");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

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
    juce::String backend = "auto";
    juce::Array<juce::File> inputs;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--backend" && i + 1 < args.size()) {
            backend = args[++i];
            continue;
        }
        const juce::File path{args[i]};
        if (path.isDirectory()) {
            juce::Array<juce::File> found;
            path.findChildFiles(found, juce::File::findFiles, true);
            for (const auto& f : found) {
                if (isAcceptedName(f) && !f.getFileName().startsWith("src_")) {
                    inputs.add(f);
                }
            }
        } else if (path.existsAsFile()) {
            inputs.add(path);
        } else {
            std::cerr << "not found: " << path.getFullPathName() << std::endl;
            return 2;
        }
    }
    if (inputs.isEmpty()) {
        std::cerr << "usage: htdemucs_format_matrix_check.exe <file-or-dir>... [--backend auto|cpu|cuda]"
                  << std::endl;
        return 2;
    }
    std::sort(inputs.begin(), inputs.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });

    const auto outputDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("htfx-format-check-" + backend);
    outputDir.deleteRecursively();
    outputDir.createDirectory();
    const auto ffprobe = bundledFfprobe();
    std::cout << "files: " << inputs.size() << "   backend: " << backend
              << "\noutputs: " << outputDir.getFullPathName()
              << "\nffprobe: " << ffprobe.getFullPathName() << "\n" << std::endl;
    if (!ffprobe.existsAsFile()) {
        std::cerr << "ffprobe not found (set HTFX_FFMPEG)" << std::endl;
        return 2;
    }

    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<Processor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);
    setChoice(*processor, "operatingMode", 0);
    setChoice(*processor, "computeBackend", backend == "cpu" ? 2 : backend == "cuda" ? 1 : 0);
    processor->clearRoformerModel();
    setChoice(*processor, "model", 0);
    processor->applyUserConfiguration();
    const auto stallTimeout = std::chrono::seconds(backend == "cpu" ? 900 : 600);

    const auto wall = std::chrono::steady_clock::now();
    for (const auto& file : inputs) {
        exercise(*processor, file, outputDir, ffprobe, stallTimeout);
    }

    // ── batch over three different formats ─────────────────────────────────
    {
        juce::Array<juce::File> batch;
        for (const auto* wanted : {".mp3", ".mkv", ".flac", ".ogg", ".wav"}) {
            for (const auto& f : inputs) {
                if (f.getFileExtension().equalsIgnoreCase(wanted) &&
                    !expectationFor(f).mustReject && expectationFor(f).seconds == 30.0 &&
                    !expectationFor(f).maySilent && f.getFullPathName().length() <= 259) {
                    batch.add(f);
                    break;
                }
            }
            if (batch.size() == 3) break;
        }
        std::cout << "\n[batch over " << batch.size() << " formats]" << std::endl;
        if (batch.size() < 2) {
            // A single-file run (a stress clip, say) has nothing to batch.
            report("batch: enough inputs", inputs.size() < 2, std::to_string(batch.size()));
        } else if (!processor->beginMultiMediaImport(batch)) {
            report("batch: import starts", false);
        } else if (!waitForMedia(*processor, std::chrono::seconds(600))) {
            report("batch: import completes", false, "timed out");
        } else {
            report("batch: clip count", processor->getClipCount() == batch.size(),
                   std::to_string(processor->getClipCount()));
            for (int i = 0; i < processor->getClipCount(); ++i) {
                const auto info = processor->getClipInfo(i);
                report("batch: clip " + info.name.toStdString() + " length",
                       std::abs(info.seconds - 30.0) <= 0.2, std::to_string(info.seconds) + " s");
            }
            if (!processor->beginBatchSeparation()) {
                report("batch: separate starts", false, processor->getRecordStatusText().toStdString());
            } else {
                const bool done = waitUntil(
                    [&] { return !processor->isBatchBusy() && !processor->isMediaBusy(); },
                    std::chrono::hours(1));
                bool all = done;
                for (int i = 0; i < processor->getClipCount(); ++i) {
                    all = all && processor->getClipInfo(i).separated;
                }
                report("batch: separate", all, all ? "" : processor->getRecordStatusText().toStdString());
                const auto batchDir = outputDir.getChildFile("batch");
                batchDir.deleteRecursively();
                batchDir.createDirectory();
                if (!processor->beginBatchExport(batchDir, Kind::accompaniment)) {
                    report("batch: export starts", false, processor->getMediaStatusText().toStdString());
                } else if (!waitUntil([&] { return !processor->isBatchBusy() && !processor->isMediaBusy(); },
                                      std::chrono::minutes(30))) {
                    report("batch: export", false, "timed out");
                } else {
                    juce::Array<juce::File> files;
                    batchDir.findChildFiles(files, juce::File::findFiles, false, "*.wav");
                    report("batch: export count", files.size() == batch.size(),
                           std::to_string(files.size()) + " files");
                    for (const auto& f : files) {
                        const auto info = inspect(f);
                        report("batch: " + f.getFileName().toStdString(),
                               info.readable && info.channels == 2 && info.isFloat &&
                                   std::abs(info.frames / 44'100.0 - 30.0) <= 0.2 && info.rms > 1e-7,
                               std::to_string(info.frames) + " frames");
                    }
                }
            }
        }
    }

    // ── cancel an import part-way ──────────────────────────────────────────
    {
        juce::File big;
        for (const auto& f : inputs) {
            if (f.getFileName().containsIgnoreCase("1080p")) { big = f; break; }
        }
        if (big == juce::File{}) {
            for (const auto& f : inputs) {
                if (isVideoName(f) && !expectationFor(f).mustReject) { big = f; break; }
            }
        }
        std::cout << "\n[cancel import: " << big.getFileName() << "]" << std::endl;
        if (!big.existsAsFile()) {
            std::cout << "  (no video input; skipped)" << std::endl;
        } else if (!processor->beginMediaImport(big)) {
            report("cancel import: starts", false, processor->getMediaStatusText().toStdString());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            processor->cancelMediaOperation();
            const bool settled = waitForMedia(*processor, std::chrono::seconds(60));
            report("cancel import: settles", settled,
                   "state=" + std::to_string(static_cast<int>(processor->getSeparationState())) +
                   " status=" + processor->getMediaStatusText().toStdString());
            // The processor must be usable again afterwards.
            juce::File again;
            for (const auto& f : inputs) {
                if (f.getFileExtension() == ".flac") { again = f; break; }
            }
            if (again.existsAsFile()) {
                const bool ok = processor->beginMediaImport(again) &&
                                waitForMedia(*processor, std::chrono::seconds(300)) &&
                                processor->getRecordedSeconds() > 1.0;
                report("cancel import: import works afterwards", ok,
                       std::to_string(processor->getRecordedSeconds()) + " s");
            }
        }
    }

    // ── exports to places that cannot be written ───────────────────────────
    {
        std::cout << "\n[unwritable export targets]" << std::endl;
        juce::File source;
        for (const auto& f : inputs) {
            if (f.getFileExtension() == ".flac" || f.getFileExtension() == ".wav") {
                if (!expectationFor(f).mustReject && expectationFor(f).seconds == 30.0 &&
                    f.getFullPathName().length() <= 259) {
                    source = f;
                    break;
                }
            }
        }
        if (!source.existsAsFile()) {
            std::cout << "  (no 30 s WAV/FLAC input; skipped)" << std::endl;
        } else if (processor->beginMediaImport(source) &&
                   waitForMedia(*processor, std::chrono::seconds(300)) &&
                   (processor->beginSeparation() || processor->isModelDownloadBusy())) {
            waitUntil([&] { return !processor->isModelDownloadBusy(); }, std::chrono::seconds(1800));
            waitForSeparation(*processor, stallTimeout);
            if (processor->getSeparationState() == State::previewReady) {
                // A drive letter that does not exist: the folder cannot be
                // created, so the export must fail with a message, leave the
                // processor idle, and not hang.
                const juce::File bad("Q:\\htfx-no-such-drive\\out.wav");
                const bool started = processor->beginQuickExport(bad, Kind::vocals);
                const bool settled = !started || waitForMedia(*processor, std::chrono::seconds(120));
                report("unwritable quick export: settles with a message",
                       settled && !bad.existsAsFile() && processor->getMediaStatusText().isNotEmpty(),
                       processor->getMediaStatusText().toStdString().substr(0, 120));
                const juce::File badDir("Q:\\htfx-no-such-drive\\stems");
                const bool stemStarted = processor->beginStemExport(badDir, {0, 1, 2, 3});
                const bool stemSettled = !stemStarted || waitForMedia(*processor, std::chrono::seconds(120));
                report("unwritable stem export: settles with a message",
                       stemSettled && !badDir.isDirectory() && processor->getMediaStatusText().isNotEmpty(),
                       processor->getMediaStatusText().toStdString().substr(0, 120));
                const juce::File badVideo("Q:\\htfx-no-such-drive\\mix.mp4");
                const bool mixStarted = processor->beginMixExport(badVideo, false);
                const bool mixSettled = !mixStarted || waitForMedia(*processor, std::chrono::seconds(120));
                report("unwritable mix export: settles with a message",
                       mixSettled && !badVideo.existsAsFile() && processor->getMediaStatusText().isNotEmpty(),
                       processor->getMediaStatusText().toStdString().substr(0, 120));
                // ...and a good export still works afterwards.
                const auto good = outputDir.getChildFile("after-unwritable.wav");
                good.deleteFile();
                const bool ok = processor->beginQuickExport(good, Kind::accompaniment) &&
                                waitForMedia(*processor, std::chrono::seconds(600)) && good.existsAsFile();
                report("export works after an unwritable one", ok);
            } else {
                report("unwritable export: separation for the check", false,
                       processor->getRecordStatusText().toStdString());
            }
        } else {
            report("unwritable export: setup", false, "no 30 s WAV/FLAC input or import failed");
        }
    }

    processor->releaseResources();
    const auto total = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - wall).count();
    std::cout << "\n" << (checksFailed == 0 ? "FORMAT MATRIX PASS" : "FORMAT MATRIX FAIL")
              << " (" << (checksRun - checksFailed) << "/" << checksRun
              << " checks ok, backend=" << backend << ", " << total << " s)" << std::endl;
    for (const auto& f : failures) {
        std::cout << "  - " << f << std::endl;
    }
    return checksFailed == 0 ? 0 : 1;
}
