#pragma once
#include "../model/EngineSnapshot.h"

namespace pablo
{
// One playback voice. MPC-style varispeed: a double phase accumulator steps
// through the source at 2^(semis/12) * (srcSR/hostSR); reverse simply steps
// backwards through the same code path. 4-point Catmull-Rom interpolation,
// short equal-time fades to avoid clicks.
class Voice
{
public:
    void prepare (double hostSampleRate);

    void start (std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                double sourceSampleRate,
                const ChopPlayInfo& chop,
                float totalPitchSemis,
                float gain,
                int trackIndex, int chopIndex);

    void release();     // gate-mode note-off: begin fade-out
    void steal();       // fast fade-out for choke / voice stealing

    bool isActive() const { return active; }
    int  getTrackIndex() const { return trackIndex; }
    int  getChopIndex() const  { return chopIndex; }

    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples);

private:
    std::shared_ptr<const juce::AudioBuffer<float>> source;
    double pos = 0.0, step = 0.0;
    juce::int64 regionStart = 0, regionEnd = 0;
    bool reverse = false;
    bool active = false;
    float gain = 1.0f;
    int trackIndex = -1, chopIndex = -1;

    float fadeGain = 0.0f, fadeInc = 0.0f, fadeTarget = 1.0f;
    bool fadingOut = false;
    double hostRate = 44100.0;

    float interpolate (const float* data, juce::int64 numSamples, double position) const;
};
} // namespace pablo
