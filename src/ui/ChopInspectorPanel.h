#pragma once
#include "Theme.h"
#include "../model/SessionState.h"
#include "../engine/SamplerEngine.h"

namespace pablo
{
// Controls for the selected chop: pitch in semitones, reverse, audition, and
// "ALL" to copy this chop's pitch to every chop on the track.
class ChopInspectorPanel : public juce::Component
{
public:
    ChopInspectorPanel (SessionState& session, SamplerEngine& engine);

    void setSelectedChop (int index);
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SessionState& session;
    SamplerEngine& engine;
    int selectedChop = 0;
    bool updating = false;

    juce::Slider pitchSlider;
    juce::TextButton reverseButton { "REVERSE" }, playButton { "PLAY" },
                     applyAllButton { "PITCH ALL" };
};
} // namespace pablo
