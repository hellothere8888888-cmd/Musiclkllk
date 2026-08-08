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

    updating = true;
    pitchSlider.setValue (valid ? track.getChopPitch (selectedChop) : 0.0, juce::dontSendNotification);
    velSlider.setValue (valid ? track.getChopVelSens (selectedChop) * 100.0 : 100.0, juce::dontSendNotification);
    reverseButton.setToggleState (valid && track.getChopReverse (selectedChop), juce::dontSendNotification);
    updating = false;

    pitchSlider.setEnabled (valid);
    velSlider.setEnabled (valid);
    reverseButton.setEnabled (valid);
    playButton.setEnabled (valid);
    applyAllButton.setEnabled (valid);
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

    g.setFont (markerFont (17.0f));
    g.drawText ("CHOP " + juce::String (selectedChop + 1),
                getLocalBounds().reduced (10, 6).removeFromTop (20),
                juce::Justification::centredLeft);

    g.setColour (ink);
    g.setFont (monoFont (11.0f));
    g.drawText ("PITCH", pitchLabelArea, juce::Justification::centredLeft);
    g.drawText ("VEL",   velLabelArea,   juce::Justification::centredLeft);
}

void ChopInspectorPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 6);
    area.removeFromTop (22);

    auto row1 = area.removeFromTop (24);
    pitchLabelArea = row1.removeFromLeft (38);
    pitchSlider.setBounds (row1);

    area.removeFromTop (4);
    auto rowVel = area.removeFromTop (24);
    velLabelArea = rowVel.removeFromLeft (38);
    velSlider.setBounds (rowVel);

    area.removeFromTop (4);
    auto row2 = area.removeFromTop (30);
    const int bw = (row2.getWidth() - 12) / 3;
    playButton.setBounds (row2.removeFromLeft (bw));
    row2.removeFromLeft (6);
    reverseButton.setBounds (row2.removeFromLeft (bw));
    row2.removeFromLeft (6);
    applyAllButton.setBounds (row2);
}
} // namespace pablo
