#pragma once
#include "Voice.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace pablo
{
// Renders chop triggers (MIDI + UI) against the current EngineSnapshot using
// a fixed, preallocated voice pool. Everything here is audio-thread safe:
// UI triggers arrive through a lock-free FIFO, pad-flash events leave through
// another one.
class SamplerEngine
{
public:
    struct Params
    {
        float masterGainDb = 0.0f;
        float globalPitch = 0.0f;
        bool  choke = true;
        bool  gate = false;
        int   baseNote = 60;
    };

    struct TriggerEvent { int track = -1, chop = -1; bool on = true; };

    SamplerEngine();

    void prepare (double sampleRate, int blockSize);

    // Message thread: trigger a chop (track < 0 means the active track).
    void triggerFromUI (int track, int chop, bool on);

    // Audio thread.
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                  const EngineSnapshot* snap, const Params& params);

    // Message thread: drains chops triggered since last call, for pad flashes.
    // Returns pairs of (track, chop).
    std::vector<std::pair<int, int>> drainFlashes();

private:
    void startChop (const EngineSnapshot& snap, int track, int chop, const Params& params);
    void stopChop (int track, int chop);

    static constexpr int maxVoices = 32;
    std::vector<Voice> voices;
    juce::uint64 voiceAges[maxVoices] = {};
    juce::uint64 ageCounter = 0;

    juce::AbstractFifo uiFifo { 256 };
    TriggerEvent uiEvents[256];

    juce::AbstractFifo flashFifo { 256 };
    std::pair<int, int> flashEvents[256];

    juce::SmoothedValue<float> masterGain;
    double hostRate = 44100.0;
};
} // namespace pablo
