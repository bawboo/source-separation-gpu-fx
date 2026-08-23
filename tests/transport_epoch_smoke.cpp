#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <iostream>
#include <memory>

namespace {

class TestPlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return position_; }

    void set(std::int64_t sample, bool playing, bool looping = false) {
        position_.setTimeInSamples(sample);
        position_.setIsPlaying(playing);
        position_.setIsLooping(looping);
    }

private:
    PositionInfo position_;
};

}  // namespace

int main() {
    _wputenv_s(L"HTFX_USE_FAKE_WORKER", L"1");
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    auto processor = std::make_unique<HTDemucsGpuFXAudioProcessor>();
    if (auto* mode = processor->parameters().getParameter("operatingMode")) {
        mode->setValueNotifyingHost(mode->convertTo0to1(1.0f));
    }
    TestPlayHead playHead;
    processor->setPlayHead(&playHead);
    processor->prepareToPlay(44'100.0, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    buffer.clear();

    const auto initial = processor->getStreamEpoch();
    playHead.set(0, true);
    processor->processBlock(buffer, midi);
    const auto afterFirst = processor->getStreamEpoch();
    playHead.set(256, true);
    processor->processBlock(buffer, midi);
    const auto afterContinuous = processor->getStreamEpoch();

    playHead.set(10'000, true);
    processor->processBlock(buffer, midi);
    const auto afterSeek = processor->getStreamEpoch();
    playHead.set(10'256, true);
    processor->processBlock(buffer, midi);
    const auto afterSeekContinuous = processor->getStreamEpoch();

    playHead.set(200, true, true);
    processor->processBlock(buffer, midi);
    const auto afterLoopWrap = processor->getStreamEpoch();

    playHead.set(456, false, true);
    processor->processBlock(buffer, midi);
    const auto afterStop = processor->getStreamEpoch();
    playHead.set(0, false, true);
    processor->processBlock(buffer, midi);
    const auto afterStoppedSeek = processor->getStreamEpoch();
    playHead.set(0, true, true);
    processor->processBlock(buffer, midi);
    const auto afterResume = processor->getStreamEpoch();
    processor->releaseResources();

    const bool passed =
        afterFirst == initial && afterContinuous == initial &&
        afterSeek == initial + 1 && afterSeekContinuous == afterSeek &&
        afterLoopWrap == initial + 2 && afterStop == initial + 3 &&
        afterStoppedSeek == afterStop && afterResume == initial + 4;
    std::cout << "initial=" << initial
              << " first=" << afterFirst
              << " continuous=" << afterContinuous
              << " seek=" << afterSeek
              << " seek_continuous=" << afterSeekContinuous
              << " loop_wrap=" << afterLoopWrap
              << " stop=" << afterStop
              << " stopped_seek=" << afterStoppedSeek
              << " resume=" << afterResume
              << " PASS=" << std::boolalpha << passed << '\n';
    return passed ? 0 : 1;
}
