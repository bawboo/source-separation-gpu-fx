#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
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

int run() {
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
    require(editor->getWidth() == 720 && editor->getHeight() == 540,
            "advanced panel size mismatch");
    require(record->isVisible() && exportMedia->isVisible() &&
                !exportVocals->isVisible() && !exportAccompany->isVisible(),
            "advanced panel control visibility mismatch");

    advanced->onClick();
    require(editor->getHeight() == 678, "expanded editor size mismatch");
    require(segment->isVisible() && model->isVisible() && compute->isVisible(),
            "Advanced options did not become visible");
    require(advanced->getButtonText() == "Advanced options v",
            "expanded disclosure label mismatch");

    compute->setSelectedItemIndex(2, juce::sendNotificationSync);
    juce::Thread::sleep(120);
    juce::Timer::callPendingTimersSynchronously();
    require(hasLabelText(components, "CPU mode: separation is supported"),
            "CPU slow-mode warning was not displayed");

    advanced->onClick();
    require(editor->getHeight() == 540, "recollapsed editor size mismatch");
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
                 " fullscreen_toggle=true PASS\n";
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
