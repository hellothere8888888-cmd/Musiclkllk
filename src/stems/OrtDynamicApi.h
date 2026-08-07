#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

namespace pablo
{
// Minimal wrapper around the ONNX Runtime C API, bound at *runtime* via
// OrtGetApiBase. We never link an import library: a VST3's implicit imports
// would be resolved against FL Studio's exe directory, not our bundle, so we
// LoadLibrary/dlopen the runtime from known locations instead — and if it is
// missing, stem splitting simply reports unavailable while the rest of the
// plugin works normally.
class OrtSession
{
public:
    OrtSession();
    ~OrtSession();

    // True if the ONNX Runtime shared library could be loaded.
    static bool isRuntimeAvailable();

    // Loads the model. Returns false and fills error on failure.
    bool load (const juce::File& modelFile, juce::String& error);

    // Runs one segment: input [numChannels x numSamples] (stereo expected by
    // HTDemucs), output one buffer per stem with the same channel/sample
    // counts. Returns false and fills error on failure.
    bool run (const juce::AudioBuffer<float>& segment,
              std::vector<juce::AudioBuffer<float>>& stemsOut,
              juce::String& error);

    int getNumStems() const;    // valid after the first successful run (else 4)

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace pablo
