#include "TransportBar.h"
#include "../engine/ParamIDs.h"

namespace pablo
{
using namespace theme;

TransportBar::TransportBar (SessionState& s, Recorder& r, SamplerEngine& e,
                            juce::AudioProcessorValueTreeState& apvts)
    : session (s), recorder (r), engine (e)
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
    transients.addItem (201, "Very few (biggest hits)");
    transients.addItem (202, "Few");
    transients.addItem (203, "Normal");
    transients.addItem (204, "Many");
    transients.addItem (205, "Most (very sensitive)");

    const double bpm = engine.getHostBpm();
    juce::PopupMenu grid;
    grid.addItem (401, "Per bar");
    grid.addItem (402, "Per beat (1/4)");
    grid.addItem (403, "1/8");
    grid.addItem (404, "1/16");

    juce::PopupMenu menu;
    menu.addSubMenu ("Slice to grid  (@ " + juce::String (bpm, 1) + " BPM)", grid);
    menu.addSubMenu ("Detect transients", transients);
    menu.addSubMenu ("Equal slices", equal);
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
        else if (result >= 201 && result <= 205)
        {
            const float sensitivity = 0.15f + 0.2f * (float) (result - 201);   // 0.15 .. 0.95
            auto onsets = detectTransients (*buffer, track.getSampleRate(), sensitivity);
            track.setChopStarts (onsets, &um);
        }
        else if (result >= 401 && result <= 404)
        {
            const double beatsPerSlice = result == 401 ? 4.0     // bar
                                       : result == 402 ? 1.0     // beat
                                       : result == 403 ? 0.5     // 1/8
                                                       : 0.25;   // 1/16
            track.sliceByBeats (engine.getHostBpm(), track.getSampleRate(), beatsPerSlice, &um);
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
