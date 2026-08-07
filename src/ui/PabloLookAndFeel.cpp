#include "PabloLookAndFeel.h"

namespace pablo
{
using namespace theme;

PabloLookAndFeel::PabloLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, orange);
    setColour (juce::DocumentWindow::backgroundColourId, orange);

    setColour (juce::TextButton::buttonColourId, pink);
    setColour (juce::TextButton::buttonOnColourId, ink);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, cream);

    setColour (juce::Label::textColourId, ink);

    setColour (juce::Slider::rotarySliderFillColourId, ink);
    setColour (juce::Slider::rotarySliderOutlineColourId, cream);
    setColour (juce::Slider::thumbColourId, ink);
    setColour (juce::Slider::trackColourId, ink);
    setColour (juce::Slider::backgroundColourId, cream);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::PopupMenu::backgroundColourId, cream);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
    setColour (juce::PopupMenu::highlightedTextColourId, cream);

    setColour (juce::AlertWindow::backgroundColourId, cream);
    setColour (juce::AlertWindow::textColourId, ink);
    setColour (juce::AlertWindow::outlineColourId, ink);

    setColour (juce::TextEditor::backgroundColourId, cream);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::highlightColourId, pink);
    setColour (juce::TextEditor::outlineColourId, ink);
    setColour (juce::TextEditor::focusedOutlineColourId, ink);

    setColour (juce::ScrollBar::thumbColourId, ink);

    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::textColourId, cream);

    setColour (juce::ProgressBar::backgroundColourId, cream);
    setColour (juce::ProgressBar::foregroundColourId, ink);
}

juce::Font PabloLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return markerFont (juce::jmin (18.0f, (float) buttonHeight * 0.62f));
}

void PabloLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                             bool isHighlighted, bool isDown)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);

    const bool on = b.getToggleState() || isDown;
    auto fill = on ? ink : (isHighlighted ? pink.brighter (0.06f) : pink);

    // Offset black "misprint" shadow.
    g.setColour (ink);
    g.fillRect (bounds.translated (2.0f, 2.0f));

    g.setColour (fill);
    g.fillRect (bounds);
    g.setColour (ink);
    g.drawRect (bounds, 1.2f);
}

void PabloLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                       bool, bool isDown)
{
    const bool on = b.getToggleState() || isDown;
    g.setColour (on ? cream : ink);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText().toUpperCase(),
                      b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
}

void PabloLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    g.setColour (ink);
    g.fillEllipse (centre.x - radius + 2.0f, centre.y - radius + 2.0f, radius * 2.0f, radius * 2.0f);
    g.setColour (cream);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (ink);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - 3.5f, radius - 3.5f, 0.0f,
                       startAngle, angle, true);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.8f, -radius + 4.0f, 3.6f, radius * 0.45f, 1.5f);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void PabloLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        const auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f,
                                                   (float) w, 6.0f);
        g.setColour (cream);
        g.fillRect (track);
        g.setColour (ink);
        g.drawRect (track, 1.0f);

        const auto thumbX = juce::jlimit ((float) x, (float) (x + w), sliderPos);
        g.setColour (ink);
        g.fillRect (juce::Rectangle<float> (thumbX - 4.0f, (float) y + 2.0f, 8.0f, (float) h - 4.0f));
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s);
    }
}

juce::Label* PabloLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (s);
    label->setFont (theme::monoFont (13.0f));
    label->setColour (juce::Label::textColourId, ink);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

void PabloLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (ink);
    g.setColour (cream);
    g.setFont (monoFont (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 4),
                      juce::Justification::centredLeft, 5);
}
} // namespace pablo
