#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace juce {

// JUCE 8 no longer provides an MME device type. The portable standalone adds
// this WinMM implementation explicitly so older USB/audio interfaces remain
// selectable alongside WASAPI and DirectSound.
std::unique_ptr<AudioIODeviceType> createHTFXMMEAudioIODeviceType();

}  // namespace juce
