#include "TransientDetector.h"
#include <algorithm>
#include <cmath>

namespace pablo
{
std::vector<juce::int64> detectTransients (const juce::AudioBuffer<float>& buffer,
                                           double sampleRate,
                                           float sensitivity)
{
    std::vector<juce::int64> onsets { 0 };
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return onsets;

    constexpr int hop = 256;
    const int numFrames = numSamples / hop;
    if (numFrames < 4)
        return onsets;

    // Per-frame RMS energy of the mono mix.
    std::vector<float> energy ((size_t) numFrames, 0.0f);
    const int numCh = buffer.getNumChannels();
    for (int f = 0; f < numFrames; ++f)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* d = buffer.getReadPointer (ch) + (juce::int64) f * hop;
            for (int i = 0; i < hop; ++i)
                sum += d[i] * d[i];
        }
        energy[(size_t) f] = std::sqrt (sum / (float) (hop * numCh));
    }

    // Positive energy flux.
    std::vector<float> flux ((size_t) numFrames, 0.0f);
    for (int f = 1; f < numFrames; ++f)
        flux[(size_t) f] = juce::jmax (0.0f, energy[(size_t) f] - energy[(size_t) f - 1]);

    // Adaptive threshold: local mean over ~0.4 s plus a sensitivity-scaled
    // multiple of the global spread.
    float globalMean = 0.0f;
    for (auto v : flux) globalMean += v;
    globalMean /= (float) numFrames;

    const float thresholdScale = juce::jmap (juce::jlimit (0.0f, 1.0f, sensitivity),
                                             4.0f, 1.2f);   // low sensitivity = high bar
    const int window = juce::jmax (1, (int) (0.4 * sampleRate / hop));
    const int minGapFrames = juce::jmax (1, (int) (0.05 * sampleRate / hop));   // 50 ms

    juce::int64 lastOnsetFrame = -minGapFrames;
    for (int f = 2; f < numFrames - 1; ++f)
    {
        float localMean = 0.0f;
        int count = 0;
        for (int k = juce::jmax (0, f - window); k < juce::jmin (numFrames, f + window); ++k, ++count)
            localMean += flux[(size_t) k];
        localMean /= (float) juce::jmax (1, count);

        const float threshold = juce::jmax (localMean, globalMean) * thresholdScale + 1.0e-5f;

        const bool isPeak = flux[(size_t) f] > flux[(size_t) f - 1]
                         && flux[(size_t) f] >= flux[(size_t) f + 1];

        if (isPeak && flux[(size_t) f] > threshold && f - lastOnsetFrame >= minGapFrames)
        {
            // Refine to the start of the rise for a tighter chop point.
            int startFrame = f;
            while (startFrame > 0 && flux[(size_t) startFrame - 1] > 0.0f
                   && energy[(size_t) startFrame - 1] < energy[(size_t) startFrame])
                --startFrame;

            const auto pos = (juce::int64) startFrame * hop;
            if (pos > 0)
                onsets.push_back (pos);
            lastOnsetFrame = f;
        }
    }

    std::sort (onsets.begin(), onsets.end());
    onsets.erase (std::unique (onsets.begin(), onsets.end()), onsets.end());
    return onsets;
}
} // namespace pablo
