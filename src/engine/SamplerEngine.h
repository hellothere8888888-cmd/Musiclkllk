#pragma once
#include "Voice.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

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
        float swing = 0.0f;          // 0..1 (processor scales the 0..100 % param)
        bool  quantize = false;
        int   gridDivisions = 16;    // 4/8/16/32
    };

    // Host transport read on the audio thread each block. 'valid' is false when
    // the host provides no musical position (e.g. some offline renders), in
    // which case grooving is bypassed and notes fire at their raw sample offset.
    struct TransportInfo
    {
        double bpm = 120.0;
        double ppqPosition = 0.0;    // musical position (quarter notes) at block start
        bool   isPlaying = false;
        bool   valid = false;
    };

    struct TriggerEvent { int track = -1, chop = -1; bool on = true; float velocity = 1.0f; };

    SamplerEngine();

    void prepare (double sampleRate, int blockSize);

    // Message thread: trigger a chop (track < 0 means the active track).
    void triggerFromUI (int track, int chop, bool on);

    // Audio thread.
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                  const EngineSnapshot* snap, const Params& params,
                  TransportInfo transport);

    // Convenience overload for callers with no host transport (tests, offline).
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                  const EngineSnapshot* snap, const Params& params)
    {
        process (out, midi, snap, params, TransportInfo{});
    }

    // Message thread: last host tempo seen by the audio thread (for the UI).
    double getHostBpm() const { return hostBpm.load (std::memory_order_relaxed); }

    // Message thread: drains chops triggered since last call, for pad flashes.
    // Returns pairs of (track, chop).
    std::vector<std::pair<int, int>> drainFlashes();

    // Message thread: frees buffers retired by finished voices. Must be pumped
    // periodically (the processor runs a timer for this).
    void reclaimBuffers() { releasePool.reclaim(); }

private:
    void startChop (const EngineSnapshot& snap, int track, int chop,
                    const Params& params, float velocity);
    void stopChop (int track, int chop);
    void renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples);

    // A trigger scheduled to fire at an absolute sample time (sampleClock base).
    // Swing/quantize can push a trigger into a later block, so these persist.
    struct ScheduledTrigger { juce::uint64 time = 0; int track = -1, chop = -1; bool on = true; float velocity = 1.0f; };
    void schedule (juce::uint64 time, int track, int chop, bool on, float velocity);

    static constexpr int maxVoices = 32;
    std::vector<Voice> voices;
    juce::uint64 voiceAges[maxVoices] = {};
    juce::uint64 ageCounter = 0;

    static constexpr int maxScheduled = 256;
    ScheduledTrigger scheduled[maxScheduled];
    int scheduledCount = 0;
    juce::uint64 sampleClock = 0;
    std::atomic<double> hostBpm { 120.0 };

    juce::AbstractFifo uiFifo { 256 };
    TriggerEvent uiEvents[256];

    juce::AbstractFifo flashFifo { 256 };
    std::pair<int, int> flashEvents[256];

    juce::SmoothedValue<float> masterGain;
    double hostRate = 44100.0;
    BufferReleasePool releasePool;
};
} // namespace pablo
