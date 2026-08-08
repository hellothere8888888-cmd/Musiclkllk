#pragma once
#include "../model/EngineSnapshot.h"
#include "BufferReleasePool.h"
#include <juce_dsp/juce_dsp.h>

namespace pablo
{
// One playback voice. MPC-style varispeed: a double phase accumulator steps
// through the source at 2^(semis/12) * (srcSR/hostSR); reverse simply steps
// backwards through the same code path. 4-point Catmull-Rom interpolation,
// short equal-time fades to avoid clicks.
class Voice
{
public:
    // 'pool' (may be null in tests) receives finished buffers so they are never
    // freed on the audio thread.
    void prepare (double hostSampleRate, int blockSize, BufferReleasePool* pool = nullptr);

    // Live per-block update of this voice's per-track carve filter (RT-safe:
    // only recomputes coefficients). mode: 0 = off, 1 = low-pass, 2 = high-pass.
    void setFilter (int mode, float cutoffHz);

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
    double getSourcePosition() const { return pos; }   // current read head, in source samples

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
    BufferReleasePool* releasePool = nullptr;

    juce::dsp::StateVariableTPTFilter<float> filter;
    int filterMode = 0;    // 0 = off, 1 = LP, 2 = HP

    // Drops 'source' without freeing on the audio thread when a pool is set.
    void releaseSource();
    float interpolate (const float* data, juce::int64 numSamples, double position) const;
};
} // namespace pablo
