#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace pablo
{
// Offline, pitch-preserving time-stretch of a source region.
//
// Renders source[start, end) into a NEW buffer whose length is approximately
// round(regionLength * timeRatio), transposed by 'semitones' (0 = no pitch
// change). timeRatio > 1 makes the chop longer/slower, < 1 shorter/faster,
// pitch unaffected in both cases.
//
// Allocates and is CPU-heavy: call it on a background thread, never on the
// audio thread. Returns nullptr if stretch support is disabled at build time
// or the region is empty.
std::shared_ptr<juce::AudioBuffer<float>> timeStretchRegion (
    const juce::AudioBuffer<float>& source,
    juce::int64 start, juce::int64 end,
    double timeRatio, float semitones, double sampleRate);
} // namespace pablo
