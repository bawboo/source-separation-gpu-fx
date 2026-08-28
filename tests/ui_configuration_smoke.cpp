#include "Localization.h"
#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void collectComponents(juce::Component& parent, std::vector<juce::Component*>& result) {
    for (int index = 0; index < parent.getNumChildComponents(); ++index) {
        auto* child = parent.getChildComponent(index);
        result.push_back(child);
        collectComponents(*child, result);
    }
}

juce::ComboBox* findCombo(
    const std::vector<juce::Component*>& components,
    const juce::String& firstItem,
    int itemCount) {
    for (auto* component : components) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(component);
            combo != nullptr && combo->getNumItems() == itemCount &&
            combo->getItemText(0) == firstItem) {
            return combo;
        }
    }
    return nullptr;
}

template <typename ComponentType>
ComponentType* findNamedComponent(
    const std::vector<juce::Component*>& components,
    const juce::String& name) {
    for (auto* component : components) {
        if (component->getName() == name) {
            return dynamic_cast<ComponentType*>(component);
        }
    }
    return nullptr;
}

juce::Button* findButton(
    const std::vector<juce::Component*>& components,
    const juce::String& buttonText) {
    for (auto* component : components) {
        if (auto* button = dynamic_cast<juce::Button*>(component);
            button != nullptr && button->getButtonText() == buttonText) {
            return button;
        }
    }
    return nullptr;
}

bool hasLabelText(
    const std::vector<juce::Component*>& components,
    const juce::String& needle) {
    for (auto* component : components) {
        if (const auto* label = dynamic_cast<juce::Label*>(component);
            label != nullptr && label->getText().contains(needle)) {
            return true;
        }
    }
    return false;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// juce::Timer::callPendingTimersSynchronously() only fires callbacks whose
// countdown has already been decremented by JUCE's real background
// TimerThread, so a single sleep()+callPendingTimersSynchronously() call is
// inherently racy (the countdown may not have ticked down yet regardless of
// how long we slept). Pumping it on every poll iteration here makes waits on
// timer-driven UI state (e.g. Component::isEnabled() flags only refreshed in
// timerCallback()) deterministic instead of occasionally missing the window.
bool waitUntil(const std::function<bool()>& predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        juce::Timer::callPendingTimersSynchronously();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    juce::Timer::callPendingTimersSynchronously();
    return predicate();
}

void waitForMedia(HTDemucsGpuFXAudioProcessor& processor) {
    require(
        waitUntil([&processor] { return !processor.isMediaBusy(); },
                  std::chrono::seconds(30)),
        "RoFormer fixture import timed out");
}

void waitForRoformerPreview(HTDemucsGpuFXAudioProcessor& processor) {
    require(
        waitUntil(
            [&processor] {
                const auto state = processor.getSeparationState();
                return state ==
                           HTDemucsGpuFXAudioProcessor::SeparationState::previewReady ||
                       state == HTDemucsGpuFXAudioProcessor::SeparationState::error;
            },
            // Cold-start cost is dominated by the worker process: python +
            // torch import + a ~913 MB checkpoint load + CUDA init runs to
            // ~70 s on a cold file cache, while the separation itself takes
            // ~2.5 s. 60 s was tight enough to fail whenever the OS file
            // cache had been evicted.
            std::chrono::seconds(240)),
        "RoFormer C++ route timed out");
    require(processor.hasPreview(), "RoFormer C++ route produced no preview");
}

int run() {
    const auto repository = juce::File::getCurrentWorkingDirectory();
    const auto verifyRoot = repository.getParentDirectory().getChildFile("verify");
    const auto fixture = verifyRoot.getChildFile("fixtures").getChildFile("test_48k_2s.wav");
    const auto roformerPython = juce::File{
        R"(C:\Users\<user>\anaconda3\envs\htfx-roformer\python.exe)"};
    const auto roformerWorker = repository.getChildFile("worker").getChildFile("roformer_worker.py");
    const auto roformerCache = verifyRoot.getChildFile("roformer-cache");
    const auto roformerOutput = verifyRoot.getChildFile("output").getChildFile("roformer-cpp-integration");
    require(fixture.existsAsFile(), "RoFormer fixture is missing");
    require(roformerPython.existsAsFile(), "htfx-roformer Python is missing");
    require(roformerWorker.existsAsFile(), "RoFormer worker script is missing");
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"");
    _wputenv_s(L"HTFX_ROFORMER_PYTHON", roformerPython.getFullPathName().toWideCharPointer());
    _wputenv_s(L"HTFX_ROFORMER_WORKER", roformerWorker.getFullPathName().toWideCharPointer());
    _wputenv_s(L"HTFX_ROFORMER_MODELS_DIR", roformerCache.getFullPathName().toWideCharPointer());
    _wputenv_s(L"HTFX_ROFORMER_OUTPUT_DIR", roformerOutput.getFullPathName().toWideCharPointer());

    // Isolate the UI language preference from a real user's saved choice —
    // must be set before the first Localization::instance() access, which
    // happens inside the very first HTDemucsGpuFXEditor constructed below.
    const auto languageSettingsFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("htfx-ui-language-" + juce::Uuid().toString() + ".txt");
    _wputenv_s(L"HTFX_UI_LANGUAGE_FILE",
               languageSettingsFile.getFullPathName().toWideCharPointer());
    const auto startupSettingsFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("htfx-ui-startup-" + juce::Uuid().toString() + ".txt");
    _wputenv_s(L"HTFX_UI_STARTUP_FILE",
               startupSettingsFile.getFullPathName().toWideCharPointer());

    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);
    processor->prepareToPlay(44'100.0, 256);

    const auto roformerModels = processor->getRoformerModels();
    require(roformerModels.size() == 99, "RoFormer catalog count mismatch");
    require(
        std::count_if(
            roformerModels.begin(),
            roformerModels.end(),
            [](const auto& model) { return model.audited; }) == 57,
        "RoFormer audited count mismatch");
    require(
        !processor->selectRoformerModel("not-a-real-model"),
        "unknown RoFormer model was accepted");
    require(
        processor->selectRoformerModel("melband-roformer-kim-vocals") &&
            processor->getSelectedRoformerModel() ==
                "melband-roformer-kim-vocals",
        "RoFormer selection was not retained");
    require(processor->beginMediaImport(fixture), "RoFormer fixture import did not start");
    waitForMedia(*processor);
    require(processor->beginSeparation(), "RoFormer C++ route did not start");
    waitForRoformerPreview(*processor);
    require(processor->getActiveSourceCount() == 2, "RoFormer preview is not two-stem");
    require(processor->previewUsesModel("melband-roformer-kim-vocals"),
            "RoFormer preview lost its model identity");
    require(std::abs(processor->getPreviewDurationSeconds() - 2.0) < 0.02,
            "RoFormer preview duration mismatch");

    // Category-agnostic naming: the label deriver reads the upstream worker's
    // own "<input>_<output_id>.wav" filenames, so it must resolve every
    // 2-stem category's naming convention correctly, not just vocals.
    const juce::File syntheticDir =
        juce::File::getSpecialLocation(juce::File::tempDirectory);
    const struct {
        const char* filename;
        const char* expected;
    } categoryFixtures[] = {
        {"input_vocals.wav", "vocals"},          // vocals
        {"input_instrumental.wav", "instrumental"},  // instrumental/karaoke residual
        {"input_dry.wav", "dry"},                // dereverb/denoise target
        {"input_reverb.wav", "reverb"},          // dereverb residual
        {"input_noise.wav", "noise"},            // denoise residual
        {"input_other.wav", "other"},            // instvoc residual
        {"input_guitar.wav", "guitar"},          // guitar category
        {"input_aspiration.wav", "aspiration"},  // aspiration category
    };
    for (const auto& fixture_ : categoryFixtures) {
        const auto derived = HTDemucsGpuFXAudioProcessor::deriveRoformerStemLabel(
            syntheticDir.getChildFile(fixture_.filename), "input");
        require(derived == juce::String(fixture_.expected),
                "RoFormer stem label derivation mismatch for a category fixture");
    }

    require(processor->getStemLabel(0).isNotEmpty() &&
                processor->getStemLabel(1).isNotEmpty(),
            "RoFormer stem labels missing after a real separation");
    const auto stemLabel0 = processor->getStemLabel(0).toLowerCase();
    const auto stemLabel1 = processor->getStemLabel(1).toLowerCase();
    require((stemLabel0 == "vocals" || stemLabel1 == "vocals") &&
                (stemLabel0 == "instrumental" || stemLabel1 == "instrumental"),
            "RoFormer vocals-category preview did not label vocals/instrumental");
    require(stemLabel0 != "drums" && stemLabel1 != "drums" &&
                stemLabel0 != "bass" && stemLabel1 != "bass",
            "RoFormer stems incorrectly kept HTDemucs source names");

    const auto stemExportDir =
        roformerOutput.getChildFile("stem-export-" + juce::Uuid().toString());
    require(stemExportDir.createDirectory(), "could not create RoFormer stem export directory");
    require(processor->beginStemExport(stemExportDir, {0, 1}),
            "RoFormer stem export did not start");
    require(waitUntil([&processor] { return !processor->isMediaBusy(); },
                       std::chrono::seconds(30)),
            "RoFormer stem export timed out");
    juce::Array<juce::File> exportedStems;
    stemExportDir.findChildFiles(exportedStems, juce::File::findFiles, false, "*.wav");
    require(exportedStems.size() == 2,
            "RoFormer stem export did not produce two WAV files");
    const bool exportedVocals = std::any_of(
        exportedStems.begin(), exportedStems.end(),
        [](const juce::File& file) { return file.getFileName().containsIgnoreCase("vocals"); });
    const bool exportedInstrumental = std::any_of(
        exportedStems.begin(), exportedStems.end(), [](const juce::File& file) {
            return file.getFileName().containsIgnoreCase("instrumental");
        });
    require(exportedVocals && exportedInstrumental,
            "RoFormer Export flow used incorrect (non-category) stem filenames");

    require(
        processor->getOperatingMode() ==
            HTDemucsGpuFXAudioProcessor::OperatingMode::record,
        "standalone default mode is not Record");
    require(processor->getLatencySamples() == 0, "Record mode latency is not zero");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    require(editor != nullptr, "custom editor was not created");
    require(editor->getWidth() == 560 && editor->getHeight() == 260,
            "general panel size mismatch");

    std::vector<juce::Component*> components;
    collectComponents(*editor, components);
    auto* mode = findCombo(components, htfx::tr("combo.modeRecord"), 2);
    auto* segment = findCombo(components, "2 seconds", 5);
    auto* model = findCombo(components, "htdemucs", 4);
#if JUCE_MAC
    auto* compute = findCombo(components, "Auto (Apple MPS, otherwise CPU)", 4);
#else
    auto* compute = findCombo(components, "Auto (NVIDIA CUDA, otherwise CPU)", 4);
#endif
    auto* advanced = findButton(components, htfx::tr("button.advancedOptionsExpand"));
    auto* panelSwitch = findButton(components, htfx::tr("button.advancedPanel"));
    auto* record = findButton(components, htfx::tr("button.record"));
    auto* importMedia = findButton(components, htfx::tr("button.import"));
    auto* exportVocals = findButton(components, htfx::tr("button.exportVocalsOnly"));
    auto* exportAccompany = findButton(components, htfx::tr("button.exportAccompanyOnly"));
    auto* exportMedia = findButton(components, htfx::tr("button.export"));
    auto* fullScreen = findButton(components, htfx::tr("button.fullScreen"));
    auto* scaleUi = findButton(components, htfx::tr("button.scaleUi"));
    require(mode != nullptr && segment != nullptr && model != nullptr &&
                compute != nullptr && advanced != nullptr && panelSwitch != nullptr &&
                record != nullptr && importMedia != nullptr &&
                exportVocals != nullptr && exportAccompany != nullptr &&
                exportMedia != nullptr &&
                fullScreen != nullptr && scaleUi != nullptr,
            "required UI controls were not found");

    require(importMedia->isVisible() && exportVocals->isVisible() &&
                exportAccompany->isVisible() && !record->isVisible() &&
                !exportMedia->isVisible(),
            "general panel control visibility mismatch");

    require(mode->getSelectedItemIndex() == 0, "Record mode is not selected");
    require(mode->getItemText(1) == htfx::tr("combo.modeRealtime"),
            "Realtime mode label mismatch");
    require(segment->getSelectedItemIndex() == 4 &&
                segment->getItemText(4) == "7.8 seconds",
            "segment choices/default mismatch");
    require(model->getSelectedItemIndex() == 0 &&
                model->getItemText(1) == "htdemucs_ft" &&
                model->getItemText(2) == "htdemucs_6s" &&
                model->getItemText(3) == "hdemucs_mmi",
            "model choices/default mismatch");
    require(compute->getSelectedItemIndex() == 0 &&
                compute->getItemText(1) == "NVIDIA CUDA" &&
                compute->getItemText(2) == "CPU" &&
                compute->getItemText(3) == "Apple Metal (MPS)",
            "compute choices/default mismatch");
    require(!segment->isVisible() && !model->isVisible() && !compute->isVisible(),
            "Advanced options are visible while collapsed");
    require(record->findColour(juce::TextButton::buttonColourId) ==
                juce::Colour(0xffb3262e),
            "Record button is not red");

    panelSwitch->onClick();
    require(editor->getWidth() == 720 && editor->getHeight() == 576,
            "advanced panel size mismatch");
    require(record->isVisible() && exportMedia->isVisible() &&
                !exportVocals->isVisible() && !exportAccompany->isVisible(),
            "advanced panel control visibility mismatch");

    auto* separationMode = findNamedComponent<juce::ComboBox>(
        components, "Separation mode");
    require(separationMode != nullptr, "Separation mode selector was not found");
    require(separationMode->isVisible(),
            "Separation mode selector is hidden in the advanced panel");
    int expectedVocalsMode = -1;
    for (int index = 0; index < separationMode->getNumItems(); ++index) {
        if (separationMode->getItemText(index) == "Vocals") {
            expectedVocalsMode = index;
            break;
        }
    }
    require(expectedVocalsMode >= 2, "Vocals mode entry missing for startup default");
    require(separationMode->getSelectedItemIndex() == expectedVocalsMode,
            "L4: startup did not preselect the Vocals separation mode");
    require(separationMode->getItemText(0) == htfx::tr("combo.separationMode4Stem") &&
                separationMode->getItemText(1) == htfx::tr("combo.separationMode6Stem") &&
                separationMode->getNumItems() == 12,
            "Separation mode list mismatch (2 HTDemucs + 10 RoFormer categories)");

    advanced->onClick();
    require(editor->getHeight() == 826, "expanded editor size mismatch");
    // In the startup Vocals (RoFormer) mode the Demucs model combo stays
    // hidden per D2; its visibility is asserted later in the HTDemucs modes.
    require(segment->isVisible() && compute->isVisible(),
            "Advanced options did not become visible");
    require(advanced->getButtonText() == htfx::tr("button.advancedOptionsCollapse"),
            "expanded disclosure label mismatch");

    auto* roformerCategory = findNamedComponent<juce::ComboBox>(
        components, "RoFormer category");
    auto* roformerSearch = findNamedComponent<juce::TextEditor>(
        components, "RoFormer search");
    auto* roformerModel = findNamedComponent<juce::ComboBox>(
        components, "RoFormer model");
    auto* roformerStatus = findNamedComponent<juce::Label>(
        components, "RoFormer download status");
    require(roformerCategory != nullptr && roformerSearch != nullptr &&
                roformerModel != nullptr && roformerStatus != nullptr,
            "RoFormer model browser controls were not found");
    require(waitUntil(
                [&] {
                    return roformerCategory->isVisible() && roformerModel->isVisible();
                },
                std::chrono::seconds(3)),
            "L4: startup Vocals preselect should show the RoFormer browser "
            "in the expanded advanced panel");
    require(roformerCategory->getItemText(0) == htfx::tr("combo.roformerAllCategories") &&
                roformerCategory->getNumItems() == 11,
            "RoFormer category browser mismatch");
    // Startup preselects the Vocals mode, so the browser opens filtered to
    // that category (a subset); the full 99-model catalog is asserted at the
    // processor level above.
    require(roformerModel->getNumItems() >= 1 && roformerModel->getNumItems() < 99,
            "RoFormer browser should open filtered to the vocals category");

    auto* drumsSlider = findNamedComponent<juce::Slider>(components, "drumsGain");
    auto* bassSlider = findNamedComponent<juce::Slider>(components, "bassGain");
    auto* otherSlider = findNamedComponent<juce::Slider>(components, "otherGain");
    auto* vocalsSlider = findNamedComponent<juce::Slider>(components, "vocalsGain");
    auto* guitarSlider = findNamedComponent<juce::Slider>(components, "guitarGain");
    auto* pianoSlider = findNamedComponent<juce::Slider>(components, "pianoGain");
    auto* outputTrimSlider = findNamedComponent<juce::Slider>(components, "outputTrim");
    require(drumsSlider != nullptr && bassSlider != nullptr && otherSlider != nullptr &&
                vocalsSlider != nullptr && guitarSlider != nullptr &&
                pianoSlider != nullptr && outputTrimSlider != nullptr,
            "stem/output gain sliders were not found");
    require(waitUntil(
                [&] {
                    return !model->isEnabled() && roformerModel->isEnabled() &&
                           drumsSlider->isEnabled() && outputTrimSlider->isEnabled();
                },
                std::chrono::seconds(3)),
            "L4: startup Vocals preselect should enable the RoFormer browser "
            "and 2-stem sliders while keeping the Demucs combo inert");
    require(processor->getSelectedRoformerModel().isNotEmpty(),
            "L4: startup Vocals preselect did not select a default model");

    separationMode->setSelectedItemIndex(0, juce::sendNotificationSync);
    require(waitUntil(
                [&] {
                    return model->isEnabled() && drumsSlider->isEnabled() &&
                           outputTrimSlider->isEnabled();
                },
                std::chrono::seconds(3)),
            "model/slider controls did not enable after choosing a "
            "separation mode");
    require(model->isVisible(),
            "D2: the Demucs model combo should be visible in an HTDemucs mode");
    require(waitUntil(
                [&] {
                    return !roformerCategory->isEnabled() && !roformerSearch->isEnabled() &&
                           !roformerModel->isEnabled() && !roformerCategory->isVisible() &&
                           !roformerSearch->isVisible() && !roformerModel->isVisible() &&
                           !roformerStatus->isVisible();
                },
                std::chrono::seconds(3)),
            "D3: the RoFormer browser trio should stay disabled/hidden in an "
            "HTDemucs (4-stem) separation mode");
    require(model->getSelectedItemIndex() == 0,
            "B2: choosing 4-stem separation did not default the model box to "
            "htdemucs");
    require(drumsSlider->isVisible() && bassSlider->isVisible() &&
                otherSlider->isVisible() && vocalsSlider->isVisible() &&
                drumsSlider->isEnabled() && bassSlider->isEnabled() &&
                otherSlider->isEnabled() && vocalsSlider->isEnabled(),
            "B3: 4-stem separation did not show/enable its four stem sliders");
    require(!guitarSlider->isVisible() && !pianoSlider->isVisible(),
            "B3: 4-stem separation did not hide the 6-stem-only sliders");

    separationMode->setSelectedItemIndex(1, juce::sendNotificationSync);
    require(waitUntil([&] { return model->getSelectedItemIndex() == 2; },
                       std::chrono::seconds(3)),
            "B2: choosing 6-stem separation did not default the model box to "
            "htdemucs_6s");
    require(drumsSlider->isVisible() && bassSlider->isVisible() &&
                otherSlider->isVisible() && vocalsSlider->isVisible() &&
                guitarSlider->isVisible() && pianoSlider->isVisible() &&
                guitarSlider->isEnabled() && pianoSlider->isEnabled(),
            "B3: 6-stem separation did not show/enable all six stem sliders");
    require(!roformerCategory->isVisible() && !roformerCategory->isEnabled(),
            "D3: 6-stem HTDemucs separation should keep the RoFormer browser "
            "trio disabled/hidden too");

    int vocalsModeIndex = -1;
    for (int index = 0; index < separationMode->getNumItems(); ++index) {
        if (separationMode->getItemText(index) == "Vocals") {
            vocalsModeIndex = index;
            break;
        }
    }
    require(vocalsModeIndex >= 0, "Vocals separation mode entry is missing");
    separationMode->setSelectedItemIndex(vocalsModeIndex, juce::sendNotificationSync);
    require(waitUntil(
                [&] {
                    return roformerCategory->getText() == "vocals" &&
                           roformerCategory->isVisible() && roformerSearch->isVisible() &&
                           roformerModel->isVisible() && roformerStatus->isVisible();
                },
                std::chrono::seconds(3)),
            "B2: choosing the Vocals separation mode did not lock the RoFormer "
            "category filter, or D3: did not show the RoFormer browser trio");
    require(processor->getSelectedRoformerModel() == "melband-roformer-big-beta5e",
            "B2: choosing the Vocals separation mode did not default to the "
            "audited-first vocals model");
    require(roformerStatus->getText().contains(htfx::tr("roformer.statusAudited")),
            "B2: Vocals separation mode default model should be marked Audited");
    require(drumsSlider->isVisible() && drumsSlider->isEnabled() &&
                bassSlider->isVisible() && bassSlider->isEnabled(),
            "B3: RoFormer 2-stem mode did not show/enable its two stem sliders");
    require(!otherSlider->isVisible() && !otherSlider->isEnabled() &&
                !vocalsSlider->isVisible() && !vocalsSlider->isEnabled() &&
                !guitarSlider->isVisible() && !guitarSlider->isEnabled() &&
                !pianoSlider->isVisible() && !pianoSlider->isEnabled(),
            "B3: RoFormer 2-stem mode did not hide the remaining HTDemucs stem "
            "sliders");
    require(waitUntil(
                [&] { return !model->isEnabled() && !model->isVisible(); },
                std::chrono::seconds(3)),
            "D2: RoFormer Vocals mode did not disable/hide the inert Demucs "
            "model combo");
    auto* stemSliderLabel0 = findNamedComponent<juce::Label>(components, "stemLabel0");
    auto* stemSliderLabel1 = findNamedComponent<juce::Label>(components, "stemLabel1");
    require(stemSliderLabel0 != nullptr && stemSliderLabel1 != nullptr,
            "stem slider labels were not found by name");
    require(stemSliderLabel0->getText() == "Vocals" && stemSliderLabel1->getText() == "Instrumental",
            "RoFormer Vocals mode left the stem slider labels on the HTDemucs "
            "names instead of relabeling to Vocals/Instrumental");

    int guitarModeIndex = -1;
    for (int index = 0; index < separationMode->getNumItems(); ++index) {
        if (separationMode->getItemText(index) == "Guitar") {
            guitarModeIndex = index;
            break;
        }
    }
    require(guitarModeIndex >= 0, "Guitar separation mode entry is missing");
    separationMode->setSelectedItemIndex(guitarModeIndex, juce::sendNotificationSync);
    require(waitUntil(
                [&] {
                    return roformerCategory->getText() == "guitar" &&
                           processor->getSelectedRoformerModel() ==
                               "roformer-model-melband-roformer-guitar-by-becruily" &&
                           roformerCategory->isVisible() && roformerModel->isVisible();
                },
                std::chrono::seconds(3)),
            "B2: choosing the Guitar separation mode did not default to its "
            "audited model, or D3: did not show the RoFormer browser trio");
    require(drumsSlider->isVisible() && drumsSlider->isEnabled() &&
                bassSlider->isVisible() && bassSlider->isEnabled() &&
                !otherSlider->isVisible() && !vocalsSlider->isVisible() &&
                !guitarSlider->isVisible() && !pianoSlider->isVisible(),
            "B3: RoFormer Guitar mode did not keep the 2-stem slider gating");
    require(!model->isEnabled() && !model->isVisible(),
            "D2: RoFormer Guitar mode did not keep the Demucs model combo "
            "disabled/hidden");
    require(stemSliderLabel0->getText() == "Guitar" && stemSliderLabel1->getText() == "Residual",
            "RoFormer Guitar mode did not relabel the stem sliders to "
            "Guitar/Residual");

    separationMode->setSelectedItemIndex(0, juce::sendNotificationSync);
    require(waitUntil([&] { return processor->getSelectedRoformerModel().isEmpty(); },
                       std::chrono::seconds(3)),
            "B3: switching back to 4-stem separation did not clear the stale "
            "RoFormer model selection");
    require(drumsSlider->isVisible() && drumsSlider->isEnabled() &&
                bassSlider->isVisible() && bassSlider->isEnabled() &&
                otherSlider->isVisible() && otherSlider->isEnabled() &&
                vocalsSlider->isVisible() && vocalsSlider->isEnabled() &&
                !guitarSlider->isVisible() && !pianoSlider->isVisible(),
            "B3: returning to 4-stem separation did not restore its four stem "
            "sliders and re-hide the 6-stem-only pair");
    require(stemSliderLabel0->getText() == "Drums" && stemSliderLabel1->getText() == "Bass",
            "returning to 4-stem separation did not restore the Drums/Bass "
            "stem slider labels");
    require(waitUntil(
                [&] { return model->isEnabled() && model->isVisible(); },
                std::chrono::seconds(3)),
            "D2: returning to 4-stem separation did not re-enable/show the "
            "Demucs model combo");
    require(waitUntil(
                [&] {
                    return !roformerCategory->isEnabled() && !roformerCategory->isVisible() &&
                           !roformerSearch->isVisible() && !roformerModel->isVisible();
                },
                std::chrono::seconds(3)),
            "D3: returning to 4-stem separation did not re-hide/disable the "
            "RoFormer browser trio");

    separationMode->setSelectedItemIndex(guitarModeIndex, juce::sendNotificationSync);
    require(waitUntil([&] { return roformerCategory->getText() == "guitar"; },
                       std::chrono::seconds(3)),
            "re-selecting the Guitar separation mode did not restore its category");
    roformerCategory->setText("vocals", juce::sendNotificationSync);
    require(roformerModel->getNumItems() == 24,
            "RoFormer category filter did not show the 24 vocal models");
    roformerCategory->setSelectedItemIndex(0, juce::sendNotificationSync);
    roformerSearch->setText("melband-roformer-instv8b", true);
    roformerSearch->onTextChange();
    require(roformerModel->getNumItems() == 1 &&
                roformerModel->getItemText(0).contains(
                    htfx::tr("roformer.tagExperimental")),
            "RoFormer search/experimental marker mismatch");
    roformerModel->setSelectedItemIndex(0, juce::sendNotificationSync);
    require(processor->getSelectedRoformerModel() ==
                "melband-roformer-instv8b",
            "RoFormer browser selection did not reach the processor");
    require(roformerStatus->getText().contains(htfx::tr("roformer.statusExperimental")) &&
                roformerStatus->getText().contains(htfx::tr("roformer.statusNotDownloaded")),
            "RoFormer download/experimental status mismatch");
    roformerSearch->clear();

    // B4: the checks above only spot-check two of the ten RoFormer category
    // modes (Vocals, Guitar). Walk every remaining category mode entry so
    // B1's enable gate, B2's audited-first default selection, and B3's
    // uniform 2-stem slider gating are verified for all ten categories, not
    // just the two hand-picked ones.
    for (int modeIndex = 2; modeIndex < separationMode->getNumItems(); ++modeIndex) {
        const auto category = separationMode->getItemText(modeIndex);
        separationMode->setSelectedItemIndex(modeIndex, juce::sendNotificationSync);
        require(waitUntil(
                    [&] {
                        return !model->isEnabled() && !model->isVisible() &&
                               roformerCategory->isEnabled() && roformerCategory->isVisible() &&
                               roformerSearch->isEnabled() && roformerSearch->isVisible() &&
                               roformerModel->isEnabled() && roformerModel->isVisible() &&
                               roformerStatus->isVisible() &&
                               roformerCategory->getText().equalsIgnoreCase(category) &&
                               processor->getSelectedRoformerModel().isNotEmpty();
                    },
                    std::chrono::seconds(3)),
                "B4/D2/D3: a RoFormer category mode did not enable/show its own "
                "controls / disable-hide the inert Demucs model combo / lock its "
                "category filter / select a default model");

        const auto selectedId = processor->getSelectedRoformerModel();
        bool categoryHasAuditedModel = false;
        bool selectedIsInCategory = false;
        bool selectedIsAudited = false;
        for (const auto& roformerModelEntry : roformerModels) {
            if (roformerModelEntry.category.equalsIgnoreCase(category)) {
                if (roformerModelEntry.audited) {
                    categoryHasAuditedModel = true;
                }
                if (roformerModelEntry.id == selectedId) {
                    selectedIsInCategory = true;
                    selectedIsAudited = roformerModelEntry.audited;
                }
            }
        }
        require(selectedIsInCategory,
                "B4: a RoFormer category default model does not belong to its "
                "own category");
        require(!categoryHasAuditedModel || selectedIsAudited,
                "B4: a RoFormer category default did not prefer an audited "
                "model when one exists in that category");
        require(drumsSlider->isVisible() && drumsSlider->isEnabled() &&
                    bassSlider->isVisible() && bassSlider->isEnabled() &&
                    !otherSlider->isVisible() && !otherSlider->isEnabled() &&
                    !vocalsSlider->isVisible() && !vocalsSlider->isEnabled() &&
                    !guitarSlider->isVisible() && !guitarSlider->isEnabled() &&
                    !pianoSlider->isVisible() && !pianoSlider->isEnabled(),
                "B4: a RoFormer category mode did not keep the uniform 2-stem "
                "slider gating");
    }

    compute->setSelectedItemIndex(2, juce::sendNotificationSync);
    // Re-collect the component tree on every poll: the editor rebuilds parts
    // of its hierarchy as modes/languages change, so a list captured earlier
    // can hold dangling pointers by the time we get here (dynamic_cast on a
    // freed object surfaces as "Access violation - no RTTI data!").
    require(waitUntil(
                [&editor] {
                    std::vector<juce::Component*> fresh;
                    collectComponents(*editor, fresh);
                    return hasLabelText(fresh, htfx::tr("status.cpuModeWarning"));
                },
                std::chrono::seconds(3)),
            "CPU slow-mode warning was not displayed");

    advanced->onClick();
    require(editor->getHeight() == 576, "recollapsed editor size mismatch");
    require(!segment->isVisible() && !model->isVisible() && !compute->isVisible() &&
                !roformerCategory->isVisible(),
            "Advanced options remained visible after recollapse");

    panelSwitch->onClick();
    require(panelSwitch->getButtonText() == htfx::tr("button.advancedPanel") &&
                editor->getWidth() == 560 && editor->getHeight() == 260,
            "general panel did not restore");

    auto* scaleToggle = dynamic_cast<juce::ToggleButton*>(scaleUi);
    require(scaleToggle != nullptr, "Scale UI is not a toggle control");
    scaleToggle->setToggleState(true, juce::dontSendNotification);
    scaleToggle->onClick();
    require(editor->isResizable(), "Scale UI did not enable host/user resizing");
    require(
        editor->getConstrainer() != nullptr &&
            std::abs(editor->getConstrainer()->getFixedAspectRatio() - 28.0 / 13.0) <
                1.0e-6,
        "Scale UI did not enforce the general panel design aspect ratio");
    editor->setBoundsConstrained({0, 0, 1120, 520});
    require(editor->getWidth() == 1120 && editor->getHeight() == 520,
            "proportional editor resize mismatch");
    scaleToggle->setToggleState(false, juce::dontSendNotification);
    scaleToggle->onClick();
    require(!editor->isResizable(), "Scale UI did not disable resizing");
    require(editor->getWidth() == 560 && editor->getHeight() == 260,
            "Scale UI did not restore design size");

    fullScreen->onClick();
    require(fullScreen->getButtonText() == htfx::tr("button.exitFullScreen"),
            "full-screen fallback did not enter");
    fullScreen->onClick();
    require(fullScreen->getButtonText() == htfx::tr("button.fullScreen") &&
                editor->getWidth() == 560 && editor->getHeight() == 260,
            "full-screen fallback did not restore the editor");

    // L1: language infrastructure — string table default (zh-TW), the
    // in-editor toggle switching a wired label live, and the choice
    // persisting so a freshly (re)opened editor observes it. This is
    // additive coverage for the new Localization module. Every lookup above
    // that targets a now-localized control resolves via htfx::tr(...) (the
    // same table this block exercises) rather than an English literal, so it
    // stays correct regardless of the default UI language; controls not yet
    // wired into Localization (segment/model/compute choices, dynamic status
    // text, the RoFormer catalog's own category names) remain English
    // literals until a future L2/L6 iteration covers them.
    require(htfx::Localization::instance().getLanguage() == htfx::Language::zhTW,
            "language did not default to zh-TW");
    auto* languageToggle =
        findNamedComponent<juce::TextButton>(components, "Language toggle");
    auto* resetWorker = findNamedComponent<juce::TextButton>(components, "Reset worker");
    require(languageToggle != nullptr && resetWorker != nullptr,
            "language toggle / reset worker controls were not found");
    require(languageToggle->getButtonText() == "EN",
            "zh-TW language toggle should offer switching to EN");
    require(resetWorker->getButtonText() == htfx::tr("button.resetWorker") &&
                resetWorker->getButtonText() != "Reset worker",
            "zh-TW default did not localize a wired static label");

    languageToggle->onClick();
    require(htfx::Localization::instance().getLanguage() == htfx::Language::en,
            "language toggle did not switch to English");
    require(resetWorker->getButtonText() == "Reset worker",
            "English toggle did not re-localize the wired label live");
    require(languageToggle->getButtonText() != "EN",
            "English language toggle should offer switching back to zh-TW");
    require(languageSettingsFile.loadFileAsString().trim() == "en",
            "language choice was not persisted to disk");

    std::unique_ptr<juce::AudioProcessorEditor> reopenedEditor(processor->createEditor());
    require(reopenedEditor != nullptr, "editor could not be reopened");
    std::vector<juce::Component*> reopenedComponents;
    collectComponents(*reopenedEditor, reopenedComponents);
    auto* reopenedResetWorker = findButton(reopenedComponents, "Reset worker");
    require(reopenedResetWorker != nullptr,
            "a freshly (re)opened editor did not restore the persisted English choice");
    reopenedEditor.reset();

    processor->releaseResources();
    std::cout << "default_panel=general quick_exports=vocals/accompany "
                 "default_mode=Record latency=0 advanced=collapsed/expanded/recollapsed"
                 " segments=5 models=4 compute=Auto/CUDA/CPU/MPS cpu_warning=true"
                 " record_button=red media_buttons=true proportional_scale=true"
                 " fullscreen_toggle=true roformer_cpp_route=true"
                 " roformer_stems=2 roformer_seconds=2"
                 " roformer_browser=99 categories=10 search=true"
                 " experimental=true download_status=true"
                 " separation_mode_gate=true separation_modes=12"
                 " separation_mode_defaults=true separation_mode_stem_gating=true"
                 " separation_mode_all_categories_verified=true"
                 " startup_default_mode=vocals"
                 " stem_slider_relabels=true"
                 " roformer_stem_labels=vocals/instrumental"
                 " roformer_stem_label_categories=8 roformer_export_naming=true"
                 " language_default=zh-TW language_toggle=true"
                 " language_persist_reopen=true PASS\n";
    return 0;
}

}  // namespace

int main() {
    try {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        return run();
    } catch (const std::exception& error) {
        juce::AudioProcessor::setTypeOfNextNewPlugin(
            juce::AudioProcessor::wrapperType_Undefined);
        std::cerr << "ui_configuration_smoke fatal: " << error.what() << '\n';
        return 2;
    }
}
