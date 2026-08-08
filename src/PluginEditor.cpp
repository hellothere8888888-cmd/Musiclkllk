#include "PluginEditor.h"

namespace pablo
{
using namespace theme;

PabloAudioEditor::PabloAudioEditor (PabloAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      waveform (p.session, p.engine),
      padGrid (p.session, p.engine),
      trackTabs (p.session),
      chopInspector (p.session, p.engine),
      transport (p.session, p.recorder, p.engine, p.apvts),
      keyboardHandler (p.engine)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (trackTabs);
    addAndMakeVisible (waveform);
    addAndMakeVisible (padGrid);
    addAndMakeVisible (chopInspector);
    addAndMakeVisible (transport);
    addChildComponent (stemOverlay);

    auto setupRotary = [this] (juce::Slider& s, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
        s.setTextValueSuffix (suffix);
        s.setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (s);
    };
    setupRotary (masterGainSlider, " dB");
    setupRotary (globalPitchSlider, " st");
    setupRotary (swingSlider, " %");
    globalPitchSlider.setTooltip ("Global pitch: shifts every chop on every track");
    swingSlider.setTooltip ("Swing: lays the off-beat 16ths back off the grid — the MPC / Dilla groove");
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, params::masterGain, masterGainSlider);
    globalPitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, params::globalPitch, globalPitchSlider);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, params::swing, swingSlider);

    // Per-track "carve" filter: one bipolar knob. Left high-passes the lows out,
    // right low-passes the highs off, centre bypasses. Bound to the active track.
    setupRotary (filterSlider, "");
    filterSlider.setRange (-100.0, 100.0, 1.0);
    filterSlider.setDoubleClickReturnValue (true, 0.0);
    filterSlider.setTooltip ("Carve filter for this track: left cleans the low end (high-pass), "
                             "right tames the highs (low-pass), centre is off");
    filterSlider.onValueChange = [this]
    {
        auto track = processor.session.getActiveTrack();
        if (track.isValid())
            track.setFilterCarve ((float) (filterSlider.getValue() / 100.0), nullptr);
    };

    // Groove grid (feeds both swing and quantize) + quantize toggle.
    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
    gridBox.setTooltip ("Groove grid used by swing and quantize");
    addAndMakeVisible (gridBox);
    gridAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, params::grid, gridBox);

    quantButton.setClickingTogglesState (true);
    quantButton.setTooltip ("Quantize: snap incoming notes to the grid (off = keep your raw finger timing)");
    addAndMakeVisible (quantButton);
    quantAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::quantize, quantButton);

    processor.session.onSessionChanged = [this] { refreshAll(); };

    waveform.onChopSelected = [this] (int chop)
    {
        chopInspector.setSelectedChop (chop);
    };
    padGrid.onChopSelected = [this] (int chop)
    {
        waveform.setSelectedChop (chop);
        chopInspector.setSelectedChop (chop);
    };

    trackTabs.onAddTrackRequested = [this] { openFileChooser(); };
    transport.onLoadRequested = [this] { openFileChooser(); };
    transport.onSplitRequested = [this] { requestStemSplit(); };

    stemOverlay.onCancel = [this]
    {
        if (downloading)
            processor.modelManager.cancelDownload();
        processor.separator.cancel();
        stemOverlay.setVisible (false);
    };

    addKeyListener (&keyboardHandler);
    setWantsKeyboardFocus (true);

    updateSplitAvailability();
    startTimerHz (20);

    setResizable (true, true);
    setResizeLimits (860, 560, 2400, 1600);
    setSize (1020, 660);
}

PabloAudioEditor::~PabloAudioEditor()
{
    processor.session.onSessionChanged = nullptr;
    removeKeyListener (&keyboardHandler);
    setLookAndFeel (nullptr);
}

void PabloAudioEditor::refreshAll()
{
    waveform.refresh();
    padGrid.refresh();
    trackTabs.refresh();
    chopInspector.refresh();

    // Sync the per-track carve knob to the active track (no notification, so
    // this never writes back over the value the user is dragging).
    auto track = processor.session.getActiveTrack();
    filterSlider.setValue (track.isValid() ? track.getFilterCarve() * 100.0 : 0.0,
                           juce::dontSendNotification);
    filterSlider.setEnabled (processor.session.getNumTracks() > 0);

    updateSplitAvailability();
}

void PabloAudioEditor::updateSplitAvailability()
{
    const bool hasAudio = processor.session.getNumTracks() > 0
                       && processor.session.getTrackBuffer (processor.session.getActiveTrack()) != nullptr;
    const bool busy = processor.separator.isRunning() || downloading;

   #if PABLO_ENABLE_STEMS
    if (! OrtSession::isRuntimeAvailable())
        transport.setSplitEnabled (false, "STEM SPLITTING NOT AVAILABLE\n(onnxruntime library missing)\nEverything else still works");
    else
        transport.setSplitEnabled (hasAudio && ! busy,
                                   ModelManager::isModelPresent()
                                       ? "Split the active track into drums / bass / other / vocals"
                                       : "Split the active track into stems\n(first use downloads the AI model, ~170 MB)");
   #else
    transport.setSplitEnabled (false, "Stem splitting disabled in this build");
   #endif
}

// ---- look ---------------------------------------------------------------

void PabloAudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (orange);

    // Header: scrawled logo, TLOP style.
    auto header = getLocalBounds().removeFromTop (58);
    g.setColour (ink);
    g.setFont (markerFont (40.0f));
    g.drawText ("PABLO", header.withTrimmedLeft (16), juce::Justification::centredLeft);

    g.setFont (markerFont (14.0f));
    g.drawFittedText ("WHICH / ONE\nSAMPLER", header.withTrimmedLeft (160).withWidth (110),
                      juce::Justification::centredLeft, 2);

    g.setFont (monoFont (11.0f));
    g.drawText ("GAIN", masterGainSlider.getBounds().translated (0, -12).removeFromTop (12),
                juce::Justification::centred);
    g.drawText ("PITCH ALL", globalPitchSlider.getBounds().translated (0, -12).removeFromTop (12),
                juce::Justification::centred);
    g.drawText ("SWING", swingSlider.getBounds().translated (0, -12).removeFromTop (12),
                juce::Justification::centred);
    g.drawText ("FILTER", filterSlider.getBounds().translated (0, -12).removeFromTop (12),
                juce::Justification::centred);
    g.drawText ("GRID", juce::Rectangle<int> (gridBox.getX(), gridBox.getY() - 12, gridBox.getWidth(), 12),
                juce::Justification::centred);
}

void PabloAudioEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (58);
    auto knobs = header.removeFromRight (406).reduced (0, 2);
    knobs.removeFromTop (10);
    masterGainSlider.setBounds (knobs.removeFromLeft (76));
    globalPitchSlider.setBounds (knobs.removeFromLeft (76));
    swingSlider.setBounds (knobs.removeFromLeft (76));
    filterSlider.setBounds (knobs.removeFromLeft (76));
    auto grooveCol = knobs.reduced (4, 0);
    quantButton.setBounds (grooveCol.removeFromBottom (20));
    grooveCol.removeFromBottom (3);
    gridBox.setBounds (grooveCol.removeFromBottom (20));

    transport.setBounds (area.removeFromBottom (44).reduced (10, 3));

    trackTabs.setBounds (area.removeFromTop (36).reduced (10, 0));

    auto main = area.reduced (10, 4);
    auto right = main.removeFromRight (juce::jmin (300, main.getWidth() / 3));
    padGrid.setBounds (right.reduced (4, 0).withTrimmedLeft (6));

    chopInspector.setBounds (main.removeFromBottom (176).reduced (0, 4));
    waveform.setBounds (main);
    stemOverlay.setBounds (waveform.getBounds());
}

void PabloAudioEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool PabloAudioEditor::keyPressed (const juce::KeyPress& key)
{
    auto& um = processor.session.getUndoManager();

    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0))
        return um.undo();
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::ctrlModifier, 0))
        return um.redo();

    if (key == juce::KeyPress::createFromDescription ("delete")
        || key == juce::KeyPress::createFromDescription ("backspace"))
    {
        auto track = processor.session.getActiveTrack();
        const int chop = waveform.getSelectedChop();
        if (track.isValid() && chop >= 0 && chop < track.getNumChops()
            && (chop > 0 || track.getNumChops() > 1))
        {
            um.beginNewTransaction ("Delete chop");
            track.removeChop (chop, &um);
            waveform.setSelectedChop (juce::jmax (0, chop - 1));
            chopInspector.setSelectedChop (juce::jmax (0, chop - 1));
            return true;
        }
    }
    return false;
}

// ---- file loading -------------------------------------------------------

bool PabloAudioEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (processor.session.getFormatManager().findFormatForFileExtension (
                juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void PabloAudioEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        processor.session.addTrackFromFile (juce::File (f));
}

void PabloAudioEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load a sample",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        processor.session.getFormatManager().getWildcardForAllFormats());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& chooser)
        {
            for (const auto& file : chooser.getResults())
                processor.session.addTrackFromFile (file);
        });
}

// ---- stem splitting -----------------------------------------------------

void PabloAudioEditor::requestStemSplit()
{
    if (processor.separator.isRunning() || downloading)
        return;

    if (ModelManager::isModelPresent())
    {
        startSeparation();
        return;
    }

    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::NoIcon)
            .withTitle ("Download stem-split model?")
            .withMessage ("Splitting uses an AI model (HTDemucs) that is downloaded once.\n"
                          "It is about 170 MB. Continue?")
            .withButton ("Download")
            .withButton ("Cancel"),
        [this] (int result)
        {
            if (result != 1)
                return;

            downloading = true;
            stemOverlay.setVisible (true);
            stemOverlay.update ("Downloading model...", 0.0f);
            updateSplitAvailability();

            juce::Component::SafePointer<PabloAudioEditor> safe (this);
            processor.modelManager.startDownload (
                [safe] (float progress)
                {
                    if (auto* self = safe.getComponent())
                        self->stemOverlay.update ("Downloading model (" + juce::String ((int) (progress * 100)) + "%)",
                                                  progress * 0.98f);
                },
                [safe] (bool success, juce::String message)
                {
                    auto* self = safe.getComponent();
                    if (self == nullptr)
                        return;   // editor closed during download
                    self->downloading = false;
                    if (success)
                    {
                        self->startSeparation();
                    }
                    else
                    {
                        self->stemOverlay.setVisible (false);
                        self->updateSplitAvailability();
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                "Stem split", message);
                    }
                });
        });
}

void PabloAudioEditor::startSeparation()
{
    auto track = processor.session.getActiveTrack();
    auto buffer = track.isValid() ? processor.session.getTrackBuffer (track) : nullptr;
    if (buffer == nullptr)
        return;

    const auto baseName = track.getName();
    stemOverlay.setVisible (true);
    stemOverlay.update ("Starting...", 0.0f);
    updateSplitAvailability();

    // The separator outlives the editor; the editor is always destroyed before
    // the processor, so a SafePointer guard covers both closing mid-run.
    juce::Component::SafePointer<PabloAudioEditor> safe (this);
    processor.separator.separate (*buffer, track.getSampleRate(),
        [safe, baseName] (StemSeparator::Result result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;   // editor closed while separating

            self->stemOverlay.setVisible (false);

            if (! result.success)
            {
                self->updateSplitAvailability();
                if (result.message != "Cancelled")
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Stem split failed", result.message);
                return;
            }

            for (size_t i = 0; i < result.stems.size(); ++i)
                self->processor.session.addTrackFromBuffer (
                    baseName + " · " + (i < result.stemNames.size() ? result.stemNames[i]
                                                                    : "stem " + juce::String ((int) i + 1)),
                    std::move (result.stems[i]), result.sampleRate);
        });
}

void PabloAudioEditor::timerCallback()
{
    if (stemOverlay.isVisible() && ! downloading && processor.separator.isRunning())
        stemOverlay.update (processor.separator.getStage(), processor.separator.getProgress());
}
} // namespace pablo
