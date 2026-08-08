#pragma once
#include "PluginProcessor.h"
#include "ui/PabloLookAndFeel.h"
#include "ui/WaveformView.h"
#include "ui/PadGridComponent.h"
#include "ui/TrackTabsComponent.h"
#include "ui/ChopInspectorPanel.h"
#include "ui/TransportBar.h"
#include "ui/StemSplitOverlay.h"
#include "ui/KeyboardHandler.h"

namespace pablo
{
class PabloAudioEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit PabloAudioEditor (PabloAudioProcessor&);
    ~PabloAudioEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void refreshAll();
    void openFileChooser();
    void requestStemSplit();
    void startSeparation();
    void updateSplitAvailability();

    PabloAudioProcessor& processor;
    PabloLookAndFeel lookAndFeel;

    WaveformView waveform;
    PadGridComponent padGrid;
    TrackTabsComponent trackTabs;
    ChopInspectorPanel chopInspector;
    TransportBar transport;
    StemSplitOverlay stemOverlay;
    KeyboardHandler keyboardHandler;

    juce::Slider masterGainSlider, globalPitchSlider, swingSlider;
    juce::ComboBox gridBox;
    juce::TextButton quantButton { "QUANT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment,
                                                                          globalPitchAttachment,
                                                                          swingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   quantAttachment;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool downloading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PabloAudioEditor)
};
} // namespace pablo
