#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: vst3_scan_smoke <bundle.vst3>\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[1]));
    if (descriptions.isEmpty()) {
        std::cerr << "no VST3 types found\n";
        return 1;
    }

    juce::String error;
    auto instance = format.createInstanceFromDescription(
        *descriptions[0], 44'100.0, 256, error);
    if (instance == nullptr) {
        std::cerr << "instance creation failed: " << error << '\n';
        return 1;
    }

    bool modeFound = false;
    bool recordIsDefault = false;
    for (auto* parameter : instance->getParameters()) {
        if (parameter->getName(64) == "Mode") {
            modeFound = true;
            recordIsDefault = parameter->getValue() < 0.5f;
        }
    }

    instance->setPlayConfigDetails(2, 2, 44'100.0, 256);
    instance->prepareToPlay(44'100.0, 256);
    const int instanceInputs = instance->getTotalNumInputChannels();
    const int instanceOutputs = instance->getTotalNumOutputChannels();
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    buffer.clear();
    instance->processBlock(buffer, midi);
    const int latency = instance->getLatencySamples();
    instance->releaseResources();

    const bool passed = descriptions.size() == 1 &&
                        descriptions[0]->name == "HTDemucs GPU FX" &&
                        instanceInputs == 2 && instanceOutputs == 2 &&
                        modeFound && recordIsDefault && latency == 0;
    std::cout << "types=" << descriptions.size() << '\n'
              << "name=" << descriptions[0]->name << '\n'
              << "manufacturer=" << descriptions[0]->manufacturerName << '\n'
              << "scan_inputs=" << descriptions[0]->numInputChannels << '\n'
              << "scan_outputs=" << descriptions[0]->numOutputChannels << '\n'
              << "instance_inputs=" << instanceInputs << '\n'
              << "instance_outputs=" << instanceOutputs << '\n'
              << "latency_samples=" << latency << '\n'
              << "record_default=" << std::boolalpha << recordIsDefault << '\n'
              << "PASS=" << std::boolalpha << passed << '\n';
    return passed ? 0 : 1;
}
