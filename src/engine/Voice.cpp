#include "Voice.h"

namespace pablo
{
static constexpr float fadeSeconds = 0.003f;

void Voice::prepare (double hostSampleRate, int blockSize, BufferReleasePool* pool)
{
    hostRate = hostSampleRate;
    releasePool = pool;
    active = false;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = hostSampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, blockSize);
    spec.numChannels = 2;
    filter.prepare (spec);
    filter.setResonance (0.707f);
    filterMode = 0;
}

void Voice::setFilter (int mode, float cutoffHz)
{
    filterMode = mode;
    if (mode != 0)
    {
        filter.setType (mode == 1 ? juce::dsp::StateVariableTPTFilterType::lowpass
                                  : juce::dsp::StateVariableTPTFilterType::highpass);
        // Keep the cutoff safely below Nyquist for whatever rate we run at.
        filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (hostRate * 0.49), cutoffHz));
    }
}

void Voice::releaseSource()
{
    if (source == nullptr)
        return;
    // Hand the buffer to the message thread; only free here (rare) if the pool
    // is absent or momentarily full.
    if (releasePool == nullptr || ! releasePool->retire (std::move (source)))
        source = nullptr;
    // On a successful retire, source was moved-from and is already null.
}

void Voice::start (std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                   double sourceSampleRate,
                   const ChopPlayInfo& chop,
                   float totalPitchSemis,
                   float gainToUse,
                   int track, int chopIdx)
{
    if (buffer == nullptr || chop.end <= chop.start)
        return;

    // If this voice was stolen while still holding a buffer, retire the old one
    // rather than letting the assignment below free it on the audio thread.
    if (source != nullptr)
        releaseSource();

    source = std::move (buffer);
    regionStart = chop.start;
    regionEnd = juce::jmin (chop.end, (juce::int64) source->getNumSamples());
    reverse = chop.reverse;
    step = std::pow (2.0, totalPitchSemis / 12.0) * (sourceSampleRate / hostRate);
    pos = reverse ? (double) regionEnd - 1.0 : (double) regionStart;
    gain = gainToUse;
    trackIndex = track;
    chopIndex = chopIdx;

    fadeGain = 0.0f;
    fadeTarget = 1.0f;
    fadeInc = 1.0f / juce::jmax (1.0f, fadeSeconds * (float) hostRate);
    fadingOut = false;
    active = true;

    filter.reset();   // clear filter state so a fresh hit never carries a click
}

void Voice::release()
{
    if (! active) return;
    fadingOut = true;
    fadeTarget = 0.0f;
    fadeInc = -1.0f / juce::jmax (1.0f, fadeSeconds * (float) hostRate);
}

void Voice::steal()
{
    if (! active) return;
    fadingOut = true;
    fadeTarget = 0.0f;
    fadeInc = -1.0f / juce::jmax (1.0f, 0.001f * (float) hostRate);
}

float Voice::interpolate (const float* data, juce::int64 numSamples, double position) const
{
    const auto i1 = (juce::int64) position;
    const auto frac = (float) (position - (double) i1);
    const auto i0 = juce::jmax ((juce::int64) 0, i1 - 1);
    const auto i2 = juce::jmin (numSamples - 1, i1 + 1);
    const auto i3 = juce::jmin (numSamples - 1, i1 + 2);

    const float y0 = data[i0], y1 = data[i1], y2 = data[i2], y3 = data[i3];

    // Catmull-Rom
    const float a = 0.5f * (3.0f * (y1 - y2) - y0 + y3);
    const float b = y2 + y2 + y0 - (5.0f * y1 + y3) * 0.5f;
    const float c = 0.5f * (y2 - y0);
    return ((a * frac + b) * frac + c) * frac + y1;
}

void Voice::render (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (! active || source == nullptr)
        return;

    const auto totalLen = (juce::int64) source->getNumSamples();
    const int srcChannels = source->getNumChannels();
    const int outChannels = out.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (pos < (double) regionStart || pos >= (double) regionEnd)
        {
            active = false;
            releaseSource();
            return;
        }

        fadeGain += fadeInc;
        if (fadeInc > 0.0f && fadeGain >= fadeTarget)      { fadeGain = fadeTarget; fadeInc = 0.0f; }
        else if (fadeInc < 0.0f && fadeGain <= 0.0f)
        {
            active = false;
            releaseSource();
            return;
        }

        // Begin an automatic fade-out just before the region ends so one-shot
        // playback never clicks.
        if (! fadingOut)
        {
            const double remaining = reverse ? (pos - (double) regionStart)
                                             : ((double) regionEnd - pos);
            if (remaining <= (double) fadeSeconds * hostRate * step)
                release();
        }

        const float g = gain * fadeGain;
        for (int ch = 0; ch < outChannels; ++ch)
        {
            const auto* src = source->getReadPointer (juce::jmin (ch, srcChannels - 1));
            float s = g * interpolate (src, totalLen, pos);
            if (filterMode != 0 && ch < 2)
                s = filter.processSample (ch, s);
            out.addSample (ch, startSample + i, s);
        }

        pos += reverse ? -step : step;
    }
}
} // namespace pablo
