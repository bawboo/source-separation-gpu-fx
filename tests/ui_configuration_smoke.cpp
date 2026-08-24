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

bool waitUntil(const std::function<bool()>& predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
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
            std::chrono::seconds(60)),
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
    auto* mode = findCombo(components, "Record mode", 2);
    auto* segment = findCombo(components, "2 seconds", 5);
    auto* model = findCombo(components, "htdemucs", 4);
#if JUCE_MAC
    auto* compute = findCombo(components, "Auto (Apple MPS, otherwise CPU)", 4);
#else
    auto* compute = findCombo(components, "Auto (NVIDIA CUDA, otherwise CPU)", 4);
#endif
    auto* advanced = findButton(components, "Advanced options >");
    auto* panelSwitch = findButton(components, "Advanced panel");
    auto* record = findButton(components, "Record");
    auto* importMedia = findButton(components, "Import audio/video");
    auto* exportVocals = findButton(components, "Export Vocals only");
    auto* exportAccompany = findButton(components, "Export Accompany only");
    auto* exportMedia = findButton(components, "Export");
    auto* fullScreen = findButton(components, "Full screen");
    auto* scaleUi = findButton(components, "Scale UI");
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
    require(mode->getItemText(1) == "Realtime mode (Ultra high latency)",
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
    require(separationMode->getSelectedItemIndex() == -1,
            "Separation mode should start unselected");
    require(separationMode->getItemText(0) == "4-stem separation" &&
                separationMode->getItemText(1) == "6-stem separation" &&
                separationMode->getNumItems() == 12,
            "Separation mode list mismatch (2 HTDemucs + 10 RoFormer categories)");

    advanced->onClick();
    require(editor->getHeight() == 826, "expanded editor size mismatch");
    require(segment->isVisible() && model->isVisible() && compute->isVisible(),
            "Advanced options did not become visible");
    require(advanced->getButtonText() == "Advanced options v",
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
    require(roformerCategory->isVisible() && roformerSearch->isVisible() &&
                roformerModel->isVisible() && roformerStatus->isVisible(),
            "RoFormer model browser controls are hidden while expanded");
    require(roformerCategory->getItemText(0) == "All categories" &&
                roformerCategory->getNumItems() == 11,
            "RoFormer category browser mismatch");
    require(roformerModel->getNumItems() == 99,
            "RoFormer browser did not expose all 99 models");

    auto* drumsSlider = findNamedComponent<juce::Slider>(components, "drumsGain");
    auto* outputTrimSlider = findNamedComponent<juce::Slider>(components, "outputTrim");
    require(drumsSlider != nullptr && outputTrimSlider != nullptr,
            "stem/output gain sliders were not found");
    require(!model->isEnabled() && !roformerCategory->isEnabled() &&
                !roformerSearch->isEnabled() && !roformerModel->isEnabled() &&
                !drumsSlider->isEnabled() && !outputTrimSlider->isEnabled(),
            "model/RoFormer/slider controls were not disabled before a separation "
            "mode was chosen");

    separationMode->setSelectedItemIndex(0, juce::sendNotificationSync);
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    require(model->isEnabled() && roformerCategory->isEnabled() &&
                roformerSearch->isEnabled() && roformerModel->isEnabled() &&
                drumsSlider->isEnabled() && outputTrimSlider->isEnabled(),
            "model/RoFormer/slider controls did not enable after choosing a "
            "separation mode");
    require(model->getSelectedItemIndex() == 0,
            "B2: choosing 4-stem separation did not default the model box to "
            "htdemucs");

    separationMode->setSelectedItemIndex(1, juce::sendNotificationSync);
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    require(model->getSelectedItemIndex() == 2,
            "B2: choosing 6-stem separation did not default the model box to "
            "htdemucs_6s");

    int vocalsModeIndex = -1;
    for (int index = 0; index < separationMode->getNumItems(); ++index) {
        if (separationMode->getItemText(index) == "Vocals") {
            vocalsModeIndex = index;
            break;
        }
    }
    require(vocalsModeIndex >= 0, "Vocals separation mode entry is missing");
    separationMode->setSelectedItemIndex(vocalsModeIndex, juce::sendNotificationSync);
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    require(roformerCategory->getText() == "vocals",
            "B2: choosing the Vocals separation mode did not lock the RoFormer "
            "category filter");
    require(processor->getSelectedRoformerModel() == "melband-roformer-big-beta5e",
            "B2: choosing the Vocals separation mode did not default to the "
            "audited-first vocals model");
    require(roformerStatus->getText().contains("Audited"),
            "B2: Vocals separation mode default model should be marked Audited");

    int guitarModeIndex = -1;
    for (int index = 0; index < separationMode->getNumItems(); ++index) {
        if (separationMode->getItemText(index) == "Guitar") {
            guitarModeIndex = index;
            break;
        }
    }
    require(guitarModeIndex >= 0, "Guitar separation mode entry is missing");
    separationMode->setSelectedItemIndex(guitarModeIndex, juce::sendNotificationSync);
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    require(roformerCategory->getText() == "guitar" &&
                processor->getSelectedRoformerModel() ==
                    "roformer-model-melband-roformer-guitar-by-becruily",
            "B2: choosing the Guitar separation mode did not default to its "
            "audited model");

    roformerCategory->setText("vocals", juce::sendNotificationSync);
    require(roformerModel->getNumItems() == 24,
            "RoFormer category filter did not show the 24 vocal models");
    roformerCategory->setSelectedItemIndex(0, juce::sendNotificationSync);
    roformerSearch->setText("melband-roformer-instv8b", true);
    roformerSearch->onTextChange();
    require(roformerModel->getNumItems() == 1 &&
                roformerModel->getItemText(0).contains("[Experimental]"),
            "RoFormer search/experimental marker mismatch");
    roformerModel->setSelectedItemIndex(0, juce::sendNotificationSync);
    require(processor->getSelectedRoformerModel() ==
                "melband-roformer-instv8b",
            "RoFormer browser selection did not reach the processor");
    require(roformerStatus->getText().contains("Experimental") &&
                roformerStatus->getText().contains("Not downloaded"),
            "RoFormer download/experimental status mismatch");
    roformerSearch->clear();

    compute->setSelectedItemIndex(2, juce::sendNotificationSync);
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    require(hasLabelText(components, "CPU mode: separation is supported"),
            "CPU slow-mode warning was not displayed");

    advanced->onClick();
    require(editor->getHeight() == 576, "recollapsed editor size mismatch");
    require(!segment->isVisible() && !model->isVisible() && !compute->isVisible(),
            "Advanced options remained visible after recollapse");

    panelSwitch->onClick();
    require(panelSwitch->getButtonText() == "Advanced panel" &&
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
    require(fullScreen->getButtonText() == "Exit full screen",
            "full-screen fallback did not enter");
    fullScreen->onClick();
    require(fullScreen->getButtonText() == "Full screen" &&
                editor->getWidth() == 560 && editor->getHeight() == 260,
            "full-screen fallback did not restore the editor");

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
                 " separation_mode_defaults=true"
                 " roformer_stem_labels=vocals/instrumental"
                 " roformer_stem_label_categories=8 roformer_export_naming=true PASS\n";
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
