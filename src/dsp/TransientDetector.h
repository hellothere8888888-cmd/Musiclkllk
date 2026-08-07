#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace pablo
{
// Energy-flux onset detection for auto-chop. Returns sorted sample positions
// of detected transients (always including 0). sensitivity in [0, 1]:
// higher = more chops.
std::vector<juce::int64> detectTransients (const juce::AudioBuffer<float>& buffer,
                                           double sampleRate,
                                           float sensitivity);
} // namespace pablo
