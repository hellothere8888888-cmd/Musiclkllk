#pragma once
#include "Theme.h"
#include "KeyboardHandler.h"
#include "../model/SessionState.h"

namespace pablo
{
// MPC-style 4x4 pad bank for the active track's chops (two banks of 16, page
// buttons appear when needed). Pads flash when triggered from any source.
class PadGridComponent : public juce::Component, private juce::Timer
{
public:
    PadGridComponent (SessionState& session, SamplerEngine& engine);

    void refresh();
    std::function<void (int)> onChopSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> padBounds (int padOnPage) const;
    int padAt (juce::Point<float> pos) const;

    SessionState& session;
    SamplerEngine& engine;

    int page = 0;                       // 16 pads per page
    int heldPad = -1;
    float flash[32] = {};               // decaying per-chop flash level
};
} // namespace pablo
