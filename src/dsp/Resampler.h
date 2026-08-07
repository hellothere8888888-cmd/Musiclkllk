#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace pablo
{
// Offline high-quality resampling used by the stem pipeline (model expects
// 44.1 kHz). Returns the input unchanged if the rates already match.
juce::AudioBuffer<float> resampleBuffer (const juce::AudioBuffer<float>& input,
                                         double inputRate, double outputRate);
} // namespace pablo
