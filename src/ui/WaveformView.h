#pragma once
#include "Theme.h"
#include "../model/SessionState.h"
#include "../engine/SamplerEngine.h"

namespace pablo
{
// Zoomable waveform editor for the active track with integrated chop-marker
// editing:
//   - wheel = zoom at cursor, shift+wheel = scroll, drag background = pan
//   - drag a marker to move it, double-click to add a chop
//   - click a slice to select + audition it, right-click for chop actions
//   - bottom strip = overview / scroll thumb
class WaveformView : public juce::Component,
                     private juce::Timer
{
public:
    WaveformView (SessionState& session, SamplerEngine& engine);

    void refresh();                       // session changed: re-sync to active track
    void setSelectedChop (int index);
    int getSelectedChop() const { return selectedChop; }

    std::function<void (int)> onChopSelected;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct PeakLevel
    {
        int binSize = 0;
        std::vector<float> minV, maxV;    // mono-aggregated
    };

    void timerCallback() override;
    void rebuildPeaks();
    void frameAll();
    void clampView();

    juce::Rectangle<int> waveArea() const;
    juce::Rectangle<int> overviewArea() const;

    double xToSample (float x) const;
    float sampleToX (double sample) const;
    int hitTestMarker (juce::Point<float> pos) const;
    int chopIndexAt (double sample) const;
    void showChopMenu (int chopIndex, juce::Point<int> screenPos);

    void drawWave (juce::Graphics&, juce::Rectangle<int> area,
                   double startSample, double samplesPerPixel, juce::Colour colour) const;

    SessionState& session;
    SamplerEngine& engine;

    SampleTrack track;
    SampleStore::BufferPtr buffer;
    int trackUid = -1;

    std::vector<PeakLevel> levels;
    std::vector<float> playheads;         // source-sample read positions, updated by the timer

    double viewStart = 0.0;               // first visible sample
    double spp = 1.0;                     // samples per pixel
    int selectedChop = 0;

    enum class Drag { none, marker, pan, overview, region };
    Drag dragMode = Drag::none;
    int dragMarkerIndex = -1;
    double dragAnchorViewStart = 0.0;
    juce::Point<float> dragAnchorPos;
    double regionAnchor = 0.0, regionCurrent = 0.0;   // shift-drag selection, in samples

    static constexpr int overviewHeight = 26;
    static constexpr int markerHitDistance = 6;
};
} // namespace pablo
