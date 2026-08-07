#include "Resampler.h"

namespace pablo
{
juce::AudioBuffer<float> resampleBuffer (const juce::AudioBuffer<float>& input,
                                         double inputRate, double outputRate)
{
    if (juce::approximatelyEqual (inputRate, outputRate) || input.getNumSamples() == 0)
    {
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf (input);
        return copy;
    }

    const double ratio = inputRate / outputRate;
    const int outLength = (int) std::ceil ((double) input.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (input.getNumChannels(), outLength);

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, input.getReadPointer (ch), out.getWritePointer (ch),
                        outLength, input.getNumSamples(), 0);
    }
    return out;
}
} // namespace pablo
