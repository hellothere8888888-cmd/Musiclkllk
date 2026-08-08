#include "ChopInspectorPanel.h"

namespace pablo
{
using namespace theme;

ChopInspectorPanel::ChopInspectorPanel (SessionState& s, SamplerEngine& e) : session (s), engine (e)
{
    pitchSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    pitchSlider.setRange (-24.0, 24.0, 0.1);
    pitchSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    pitchSlider.setTextValueSuffix (" st");
    pitchSlider.setDoubleClickReturnValue (true, 0.0);
    pitchSlider.onValueChange = [this]
    {
        if (updating) return;
        auto track = session.getActiveTrack();
        if (track.isValid() && selectedChop < track.getNumChops())
            track.setChopPitch (selectedChop, (float) pitchSlider.getValue(), nullptr);
    };
    addAndMakeVisible (pitchSlider);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setRange (0.0, 200.0, 1.0);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    volumeSlider.setTextValueSuffix (" %");
    volumeSlider.setDoubleClickReturnValue (true, 100.0);
    volumeSlider.setTooltip ("Volume of this chop (100% = unity)");
    volumeSlider.onValueChange = [this]
    {
        if (updating) return;
        auto track = session.getActiveTrack();
        if (track.isValid() && selectedChop < track.getNumChops())
            track.setChopGain (selectedChop, (float) (volumeSlider.getValue() / 100.0), nullptr);
    };
    addAndMakeVisible (volumeSlider);

    velSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velSlider.setRange (0.0, 100.0, 1.0);
    velSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    velSlider.setTextValueSuffix (" %");
    velSlider.setDoubleClickReturnValue (true, 100.0);
    velSlider.setTooltip ("How strongly this chop follows MIDI velocity (0 = always full, 100 = full dynamics)");
    velSlider.onValueChange = [this]
    {
        if (updating) return;
        auto track = session.getActiveTrack();
        if (track.isValid() && selectedChop < track.getNumChops())
            track.setChopVelSens (selectedChop, (float) (velSlider.getValue() / 100.0), nullptr);
    };
    addAndMakeVisible (velSlider);

    stretchSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    stretchSlider.setRange (50.0, 200.0, 1.0);
    stretchSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    stretchSlider.setTextValueSuffix (" %");
    stretchSlider.setDoubleClickReturnValue (true, 100.0);
    stretchSlider.setTooltip ("Time-stretch this chop to fit a new tempo without changing pitch "
                              "(100% = off). Pitch stays independent while stretched.");
    stretchSlider.onValueChange = [this]
    {
        if (updating) return;
        auto track = session.getActiveTrack();
        if (track.isValid() && selectedChop < track.getNumChops())
            track.setChopStretch (selectedChop, (float) (stretchSlider.getValue() / 100.0), nullptr);
    };
    addAndMakeVisible (stretchSlider);

    reverseButton.setClickingTogglesState (true);
    reverseButton.setTooltip ("Play this chop backwards");
    reverseButton.onClick = [this]
    {
        if (updating) return;
        auto track = session.getActiveTrack();
        if (track.isValid() && selectedChop < track.getNumChops())
        {
            session.getUndoManager().beginNewTransaction ("Reverse chop");
            track.setChopReverse (selectedChop, reverseButton.getToggleState(), &session.getUndoManager());
        }
    };
    addAndMakeVisible (reverseButton);

    playButton.setTooltip ("Audition this chop");
    playButton.onClick = [this] { engine.triggerFromUI (-1, selectedChop, true); };
    addAndMakeVisible (playButton);

    applyAllButton.setTooltip ("Apply this chop's pitch to every chop on the track");
    applyAllButton.onClick = [this]
    {
        auto track = session.getActiveTrack();
        if (! track.isValid() || selectedChop >= track.getNumChops())
            return;
        const float pitch = track.getChopPitch (selectedChop);
        session.getUndoManager().beginNewTransaction ("Pitch all chops");
        for (int i = 0; i < track.getNumChops(); ++i)
            track.setChopPitch (i, pitch, &session.getUndoManager());
    };
    addAndMakeVisible (applyAllButton);

    refresh();
}

void ChopInspectorPanel::setSelectedChop (int index)
{
    selectedChop = index;
    refresh();
}

void ChopInspectorPanel::refresh()
{
    auto track = session.getActiveTrack();
    const bool valid = track.isValid() && selectedChop >= 0 && selectedChop < track.getNumChops();
    haveChop = valid;

    updating = true;
    pitchSlider.setValue (valid ? track.getChopPitch (selectedChop) : 0.0, juce::dontSendNotification);
    volumeSlider.setValue (valid ? track.getChopGain (selectedChop) * 100.0 : 100.0, juce::dontSendNotification);
    velSlider.setValue (valid ? track.getChopVelSens (selectedChop) * 100.0 : 100.0, juce::dontSendNotification);
    stretchSlider.setValue (valid ? track.getChopStretch (selectedChop) * 100.0 : 100.0, juce::dontSendNotification);
    reverseButton.setToggleState (valid && track.getChopReverse (selectedChop), juce::dontSendNotification);
    updating = false;

    // Slice length readout for the header.
    if (valid && track.getSampleRate() > 0.0)
    {
        const double secs = (double) (track.getChopEnd (selectedChop) - track.getChopStart (selectedChop))
                          / track.getSampleRate();
        sliceInfo = juce::String (secs, 2) + "s slice";
    }
    else
        sliceInfo = {};

    // Hide the controls entirely when there's nothing to edit, so the empty-
    // state hint reads cleanly instead of a row of greyed-out sliders.
    pitchSlider.setVisible (valid);
    volumeSlider.setVisible (valid);
    velSlider.setVisible (valid);
    stretchSlider.setVisible (valid);
    reverseButton.setVisible (valid);
    playButton.setVisible (valid);
    applyAllButton.setVisible (valid);
    repaint();
}

void ChopInspectorPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ink);
    g.fillRect (bounds.translated (2.5f, 2.5f));
    g.setColour (pink);
    g.fillRect (bounds);
    g.setColour (ink);
    g.drawRect (bounds, 1.4f);

    auto headerRow = getLocalBounds().reduced (10, 6).removeFromTop (20);
    g.setFont (markerFont (17.0f));
    g.drawText ("CHOP " + juce::String (selectedChop + 1), headerRow, juce::Justification::centredLeft);
    if (haveChop && sliceInfo.isNotEmpty())
    {
        g.setFont (monoFont (11.0f));
        g.drawText (sliceInfo, headerRow, juce::Justification::centredRight);
    }

    // Empty state: make it obvious why the controls are greyed out.
    if (! haveChop)
    {
        g.setColour (ink.withAlpha (0.75f));
        g.setFont (monoFont (12.0f));
        g.drawFittedText ("LOAD A SAMPLE, THEN PICK A CHOP (CLICK A PAD)",
                          getLocalBounds().reduced (14, 0).withTrimmedTop (24),
                          juce::Justification::centredTop, 2);
        return;
    }

    g.setColour (ink);
    g.setFont (monoFont (11.0f));
    g.drawText ("PITCH", pitchLabelArea,   juce::Justification::centredLeft);
    g.drawText ("VOL",   volLabelArea,     juce::Justification::centredLeft);
    g.drawText ("VEL",   velLabelArea,     juce::Justification::centredLeft);
    g.drawText ("STR",   stretchLabelArea, juce::Justification::centredLeft);
}

void ChopInspectorPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 6);
    area.removeFromTop (22);

    auto row1 = area.removeFromTop (22);
    pitchLabelArea = row1.removeFromLeft (38);
    pitchSlider.setBounds (row1);

    area.removeFromTop (3);
    auto rowVol = area.removeFromTop (22);
    volLabelArea = rowVol.removeFromLeft (38);
    volumeSlider.setBounds (rowVol);

    area.removeFromTop (3);
    auto rowVel = area.removeFromTop (22);
    velLabelArea = rowVel.removeFromLeft (38);
    velSlider.setBounds (rowVel);

    area.removeFromTop (3);
    auto rowStretch = area.removeFromTop (22);
    stretchLabelArea = rowStretch.removeFromLeft (38);
    stretchSlider.setBounds (rowStretch);

    area.removeFromTop (3);
    auto row2 = area.removeFromTop (28);
    const int bw = (row2.getWidth() - 12) / 3;
    playButton.setBounds (row2.removeFromLeft (bw));
    row2.removeFromLeft (6);
    reverseButton.setBounds (row2.removeFromLeft (bw));
    row2.removeFromLeft (6);
    applyAllButton.setBounds (row2);
}
} // namespace pablo
