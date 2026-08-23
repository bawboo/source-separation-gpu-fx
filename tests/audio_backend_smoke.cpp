#include "MmeAudioIODevice.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <iostream>
#include <memory>

namespace {

class SilenceCallback final : public juce::AudioIODeviceCallback {
public:
    void audioDeviceIOCallbackWithContext(
        const float* const*,
        int,
        float* const* outputs,
        int numOutputs,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override {
        for (int channel = 0; channel < numOutputs; ++channel) {
            if (outputs[channel] != nullptr) {
                juce::FloatVectorOperations::clear(outputs[channel], numSamples);
            }
        }
        callbacks.fetch_add(1, std::memory_order_relaxed);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    std::atomic<int> callbacks{0};
};

juce::BigInteger stereoMask() {
    juce::BigInteger result;
    result.setRange(0, 2, true);
    return result;
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioDeviceManager manager;

    // Materialise the JUCE-native backends before appending the custom one.
    manager.getAvailableDeviceTypes();
    manager.addAudioDeviceType(juce::createHTFXMMEAudioIODeviceType());

    const std::array<juce::String, 5> required{
        "Windows Audio",
        "Windows Audio (Exclusive Mode)",
        "Windows Audio (Low Latency Mode)",
        "DirectSound",
        "MME (WinMM)",
    };

    juce::StringArray discovered;
    juce::AudioIODeviceType* mmeType = nullptr;
    for (auto* type : manager.getAvailableDeviceTypes()) {
        discovered.add(type->getTypeName());
        type->scanForDevices();
        std::cout << "backend=" << type->getTypeName().toStdString()
                  << " inputs=" << type->getDeviceNames(true).size()
                  << " outputs=" << type->getDeviceNames(false).size() << '\n';
        if (type->getTypeName() == "MME (WinMM)") {
            mmeType = type;
        }
    }

    for (const auto& expected : required) {
        if (!discovered.contains(expected)) {
            std::cerr << "missing backend: " << expected.toStdString() << '\n';
            return 2;
        }
    }
    if (mmeType == nullptr) {
        return 3;
    }

    const auto outputs = mmeType->getDeviceNames(false);
    const auto inputs = mmeType->getDeviceNames(true);
    std::cout << "mme_input_names=" << inputs.joinIntoString(" | ").toStdString()
              << '\n';
    std::cout << "mme_output_names=" << outputs.joinIntoString(" | ").toStdString()
              << '\n';

    if (outputs.isEmpty()) {
        std::cout << "PASS (all backends registered; no MME output device present)\n";
        return 0;
    }

    const std::array<double, 4> sampleRates{48'000.0, 44'100.0, 96'000.0, 88'200.0};
    juce::StringArray openErrors;
    for (const auto& outputName : outputs) {
        for (const auto sampleRate : sampleRates) {
            std::unique_ptr<juce::AudioIODevice> device(
                mmeType->createDevice(outputName, {}));
            if (device == nullptr) {
                continue;
            }
            const auto error = device->open({}, stereoMask(), sampleRate, 1024);
            if (error.isNotEmpty()) {
                openErrors.add(outputName + " @ " + juce::String(sampleRate) + ": " + error);
                continue;
            }

            SilenceCallback callback;
            device->start(&callback);
            for (int attempt = 0;
                 attempt < 20 && callback.callbacks.load(std::memory_order_relaxed) == 0;
                 ++attempt) {
                juce::Thread::sleep(25);
            }
            device->stop();
            device->close();
            if (callback.callbacks.load(std::memory_order_relaxed) > 0) {
                std::cout << "mme_stream=" << outputName.toStdString()
                          << " sample_rate=" << sampleRate
                          << " callbacks=" << callback.callbacks.load() << '\n';
                std::cout << "PASS\n";
                return 0;
            }
            openErrors.add(outputName + " opened but produced no callback");
        }
    }

    std::cerr << "MME devices were enumerated but none could stream:\n"
              << openErrors.joinIntoString("\n").toStdString() << '\n';
    return 4;
}
