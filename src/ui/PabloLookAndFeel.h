#pragma once
#include "Theme.h"

namespace pablo
{
// TLOP visual language: flat pink blocks with a 1-2 px black "print
// misregistration" offset, scrawled marker text, black-on-cream rotaries.
class PabloLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PabloLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isHighlighted, bool isDown) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
};
} // namespace pablo
