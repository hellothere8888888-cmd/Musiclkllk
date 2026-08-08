#pragma once
#include "Theme.h"
#include "../model/SessionState.h"
#include "../engine/Recorder.h"
#include "../engine/SamplerEngine.h"
#include "../dsp/TransientDetector.h"

namespace pablo
{
// Bottom control strip: LOAD, REC (with input meter), AUTO-CHOP menu,
// SPLIT STEMS, plus choke/gate toggles wired to the APVTS.
class TransportBar : public juce::Component, private juce::Timer
{
public:
    TransportBar (SessionState& session, Recorder& recorder, SamplerEngine& engine,
                  juce::AudioProcessorValueTreeState& apvts);

    std::function<void()> onLoadRequested;
    std::function<void()> onSplitRequested;
    void setSplitEnabled (bool enabled, const juce::String& tooltip);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void showChopMenu();

    SessionState& session;
    Recorder& recorder;
    SamplerEngine& engine;

    juce::TextButton loadButton { "LOAD" }, recButton { "REC" },
                     chopButton { "CHOP" }, splitButton { "SPLIT STEMS" },
                     chokeButton { "CHOKE" }, gateButton { "GATE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chokeAttachment, gateAttachment;
    juce::Rectangle<int> meterBounds;
};
} // namespace pablo
