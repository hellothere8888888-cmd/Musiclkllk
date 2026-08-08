#include "TimeStretch.h"

#if PABLO_ENABLE_STRETCH
 #include "signalsmith-stretch.h"
#endif

#include <cmath>
#include <vector>

namespace pablo
{
std::shared_ptr<juce::AudioBuffer<float>> timeStretchRegion (
    const juce::AudioBuffer<float>& source,
    juce::int64 start, juce::int64 end,
    double timeRatio, float semitones, double sampleRate)
{
    const juce::int64 total = source.getNumSamples();
    start = juce::jlimit<juce::int64> (0, total, start);
    end   = juce::jlimit<juce::int64> (start, total, end);
    const int inputLength = (int) (end - start);
    const int channels = juce::jmax (1, source.getNumChannels());
    if (inputLength <= 0)
        return nullptr;

#if PABLO_ENABLE_STRETCH
    timeRatio = juce::jlimit (0.25, 4.0, timeRatio);
    const int outputLength = juce::jmax (1, (int) std::llround ((double) inputLength * timeRatio));

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault (channels, (float) sampleRate);
    stretch.setTransposeSemitones (semitones);

    const int inLatency  = stretch.inputLatency();
    const int outLatency = stretch.outputLatency();

    // Input padded with inputLatency of silence at the end so process() can
    // read 'inputLength' samples starting one latency in (see the library's
    // fixed-length offline recipe).
    const int paddedIn = inputLength + inLatency;
    std::vector<std::vector<float>> in ((size_t) channels, std::vector<float> ((size_t) paddedIn, 0.0f));
    for (int c = 0; c < channels; ++c)
    {
        const auto* src = source.getReadPointer (juce::jmin (c, source.getNumChannels() - 1));
        for (int i = 0; i < inputLength; ++i)
            in[(size_t) c][(size_t) i] = src[start + i];
    }

    // Output holds the stretched body plus one output-latency of pre-roll to
    // discard from the front.
    const int paddedOut = outputLength + outLatency;
    std::vector<std::vector<float>> out ((size_t) channels, std::vector<float> ((size_t) paddedOut, 0.0f));

    std::vector<const float*> inSeek ((size_t) channels), inProc ((size_t) channels);
    std::vector<float*> outProc ((size_t) channels), outFlush ((size_t) channels);
    for (int c = 0; c < channels; ++c)
    {
        inSeek[(size_t) c]  = in[(size_t) c].data();
        inProc[(size_t) c]  = in[(size_t) c].data() + inLatency;
        outProc[(size_t) c] = out[(size_t) c].data();
        outFlush[(size_t) c] = out[(size_t) c].data() + outputLength;
    }

    // Prime processing time to the input start, then render, then flush the tail.
    stretch.seek (inSeek, inLatency, 1.0 / timeRatio);
    stretch.process (inProc, inputLength, outProc, outputLength);
    stretch.flush (outFlush, outLatency);

    // Fold the pre-roll back over the start to cancel the leading edge, then the
    // real output begins at outLatency.
    for (int c = 0; c < channels; ++c)
    {
        auto& o = out[(size_t) c];
        for (int i = 0; i < outLatency && outLatency + i < paddedOut; ++i)
            o[(size_t) (outLatency + i)] -= o[(size_t) (outLatency - 1 - i)];
    }

    auto result = std::make_shared<juce::AudioBuffer<float>> (channels, outputLength);
    for (int c = 0; c < channels; ++c)
    {
        auto* dst = result->getWritePointer (c);
        const auto& o = out[(size_t) c];
        for (int i = 0; i < outputLength; ++i)
            dst[i] = (outLatency + i < paddedOut) ? o[(size_t) (outLatency + i)] : 0.0f;
    }
    return result;
#else
    juce::ignoreUnused (timeRatio, semitones, sampleRate, channels);
    return nullptr;
#endif
}
} // namespace pablo
