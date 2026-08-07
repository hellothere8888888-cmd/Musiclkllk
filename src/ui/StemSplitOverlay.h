#pragma once
#include "Theme.h"

namespace pablo
{
// Progress card shown over the waveform while the model downloads or the
// separation runs. The owner polls and feeds it progress.
class StemSplitOverlay : public juce::Component
{
public:
    StemSplitOverlay()
    {
        cancelButton.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible (cancelButton);
        setInterceptsMouseClicks (true, true);
    }

    void update (const juce::String& stageText, float progressValue)
    {
        stage = stageText;
        progress = juce::jlimit (0.0f, 1.0f, progressValue);
        repaint();
    }

    std::function<void()> onCancel;

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (ink.withAlpha (0.55f));

        auto card = getCardBounds().toFloat();
        g.setColour (ink);
        g.fillRect (card.translated (3.0f, 3.0f));
        g.setColour (cream);
        g.fillRect (card);
        g.setColour (ink);
        g.drawRect (card, 1.6f);

        auto inner = getCardBounds().reduced (18);
        g.setFont (markerFont (24.0f));
        g.drawText ("SPLITTING STEMS", inner.removeFromTop (32), juce::Justification::centred);
        g.setFont (monoFont (14.0f));
        g.drawText (stage, inner.removeFromTop (22), juce::Justification::centred);

        inner.removeFromTop (8);
        auto bar = inner.removeFromTop (18);
        g.setColour (pink);
        g.fillRect (bar.toFloat());
        g.setColour (ink);
        g.fillRect (bar.toFloat().withWidth (bar.toFloat().getWidth() * progress));
        g.drawRect (bar.toFloat(), 1.2f);
    }

    void resized() override
    {
        auto card = getCardBounds();
        cancelButton.setBounds (card.removeFromBottom (46).reduced (card.getWidth() / 3, 8));
    }

private:
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (380, getWidth() - 30), 190);
    }

    juce::TextButton cancelButton { "CANCEL" };
    juce::String stage { "Preparing" };
    float progress = 0.0f;
};
} // namespace pablo
