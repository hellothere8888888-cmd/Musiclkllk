#include "Voice.h"

namespace pablo
{
static constexpr float fadeSeconds = 0.003f;

void Voice::prepare (double hostSampleRate)
{
    hostRate = hostSampleRate;
    active = false;
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
            source = nullptr;
            return;
        }

        fadeGain += fadeInc;
        if (fadeInc > 0.0f && fadeGain >= fadeTarget)      { fadeGain = fadeTarget; fadeInc = 0.0f; }
        else if (fadeInc < 0.0f && fadeGain <= 0.0f)
        {
            active = false;
            source = nullptr;
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
            out.addSample (ch, startSample + i, g * interpolate (src, totalLen, pos));
        }

        pos += reverse ? -step : step;
    }
}
} // namespace pablo
