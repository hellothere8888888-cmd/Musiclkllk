#include "TransportBar.h"
#include "../engine/ParamIDs.h"

namespace pablo
{
using namespace theme;

TransportBar::TransportBar (SessionState& s, Recorder& r,
                            juce::AudioProcessorValueTreeState& apvts)
    : session (s), recorder (r)
{
    loadButton.setTooltip ("Load a sample into a new track (or drop a file anywhere)");
    loadButton.onClick = [this] { if (onLoadRequested) onLoadRequested(); };
    addAndMakeVisible (loadButton);

    recButton.setClickingTogglesState (true);
    recButton.setTooltip ("Record the plugin's audio input into a new sample track.\n"
                          "In FL Studio: route a mixer track into the plugin's sidechain input.");
    recButton.onClick = [this]
    {
        if (recButton.getToggleState()) recorder.start();
        else recorder.stop();
    };
    addAndMakeVisible (recButton);

    chopButton.setTooltip ("Auto-chop the active track");
    chopButton.onClick = [this] { showChopMenu(); };
    addAndMakeVisible (chopButton);

    splitButton.setTooltip ("Split the active track into stems (drums / bass / other / vocals)");
    splitButton.onClick = [this] { if (onSplitRequested) onSplitRequested(); };
    addAndMakeVisible (splitButton);

    chokeButton.setClickingTogglesState (true);
    chokeButton.setTooltip ("Chops on the same track cut each other off (classic MPC)");
    addAndMakeVisible (chokeButton);
    chokeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, params::choke, chokeButton);

    gateButton.setClickingTogglesState (true);
    gateButton.setTooltip ("Gate mode: chops stop when the key is released");
    addAndMakeVisible (gateButton);
    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, params::gateMode, gateButton);

    startTimerHz (20);
}

void TransportBar::setSplitEnabled (bool enabled, const juce::String& tooltip)
{
    splitButton.setEnabled (enabled);
    splitButton.setTooltip (tooltip);
}

void TransportBar::timerCallback()
{
    if (recorder.isRecording() || recorder.getInputPeak() > 0.001f)
        repaint (meterBounds);
}

void TransportBar::showChopMenu()
{
    juce::PopupMenu equal;
    for (int n : { 4, 8, 16, 32 })
        equal.addItem (100 + n, juce::String (n) + " slices");

    juce::PopupMenu transients;
    transients.addItem (201, "Few chops (big hits only)");
    transients.addItem (202, "Normal");
    transients.addItem (203, "Many chops (sensitive)");

    juce::PopupMenu menu;
    menu.addSubMenu ("Equal slices", equal);
    menu.addSubMenu ("Detect transients", transients);
    menu.addSeparator();
    menu.addItem (300, "Clear chops (one big slice)");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chopButton),
        [this, safeThis = juce::Component::SafePointer<TransportBar> (this)] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto track = session.getActiveTrack();
        auto buffer = track.isValid() ? session.getTrackBuffer (track) : nullptr;
        if (buffer == nullptr)
            return;

        auto& um = session.getUndoManager();
        um.beginNewTransaction ("Auto-chop");

        if (result >= 100 && result < 200)
        {
            track.equalSlices (result - 100, &um);
        }
        else if (result >= 201 && result <= 203)
        {
            const float sensitivity = result == 201 ? 0.2f : result == 202 ? 0.5f : 0.85f;
            auto onsets = detectTransients (*buffer, track.getSampleRate(), sensitivity);
            track.setChopStarts (onsets, &um);
        }
        else if (result == 300)
        {
            track.setChopStarts ({ 0 }, &um);
        }
    });
}

void TransportBar::paint (juce::Graphics& g)
{
    // Input meter beside REC.
    const float peak = juce::jlimit (0.0f, 1.0f, recorder.getInputPeak());
    auto meter = meterBounds.toFloat();
    g.setColour (cream);
    g.fillRect (meter);
    g.setColour (recorder.isRecording() ? juce::Colour (0xffc0301e) : ink);
    g.fillRect (meter.removeFromBottom (meter.getHeight() * peak));
    g.setColour (ink);
    g.drawRect (meterBounds.toFloat(), 1.0f);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (0, 2);

    auto place = [&area] (juce::Component& c, int width)
    {
        c.setBounds (area.removeFromLeft (width));
        area.removeFromLeft (6);
    };

    place (loadButton, 86);
    place (recButton, 70);
    meterBounds = area.removeFromLeft (10);
    area.removeFromLeft (10);
    place (chopButton, 80);
    place (splitButton, 130);

    auto right = area;
    gateButton.setBounds (right.removeFromRight (72));
    right.removeFromRight (6);
    chokeButton.setBounds (right.removeFromRight (78));
}
} // namespace pablo
