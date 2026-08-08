#include "SamplerEngine.h"
#include "GrooveTiming.h"

namespace pablo
{
SamplerEngine::SamplerEngine()
{
    voices.resize (maxVoices);
}

void SamplerEngine::prepare (double sampleRate, int)
{
    hostRate = sampleRate;
    for (auto& v : voices)
        v.prepare (sampleRate, &releasePool);
    masterGain.reset (sampleRate, 0.02);
    scheduledCount = 0;
    sampleClock = 0;
}

void SamplerEngine::triggerFromUI (int track, int chop, bool on)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0)
        uiEvents[scope.startIndex1] = { track, chop, on };
}

void SamplerEngine::schedule (juce::uint64 time, int track, int chop, bool on, float velocity)
{
    // Drop under pathological overload rather than allocate on the audio thread.
    if (scheduledCount >= maxScheduled)
        return;
    scheduled[scheduledCount++] = { time, track, chop, on, velocity };
}

void SamplerEngine::renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;
    for (auto& v : voices)
        v.render (out, startSample, numSamples);
}

std::vector<std::pair<int, int>> SamplerEngine::drainFlashes()
{
    std::vector<std::pair<int, int>> out;
    const auto scope = flashFifo.read (flashFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) out.push_back (flashEvents[scope.startIndex1 + i]);
    for (int i = 0; i < scope.blockSize2; ++i) out.push_back (flashEvents[scope.startIndex2 + i]);
    return out;
}

void SamplerEngine::startChop (const EngineSnapshot& snap, int track, int chop,
                               const Params& params, float velocity)
{
    if (track < 0 || track >= (int) snap.tracks.size())
        return;
    const auto& t = snap.tracks[(size_t) track];
    if (t.buffer == nullptr || chop < 0 || chop >= (int) t.chops.size())
        return;

    if (params.choke)
        for (auto& v : voices)
            if (v.isActive() && v.getTrackIndex() == track)
                v.steal();

    // Retriggering the same chop always self-steals for the classic MPC feel.
    for (auto& v : voices)
        if (v.isActive() && v.getTrackIndex() == track && v.getChopIndex() == chop)
            v.steal();

    Voice* free = nullptr;
    juce::uint64 oldest = ~ (juce::uint64) 0;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (! voices[(size_t) i].isActive()) { free = &voices[(size_t) i]; break; }
        if (voiceAges[i] < oldest) { oldest = voiceAges[i]; free = &voices[(size_t) i]; }
    }
    if (free == nullptr)
        return;

    const auto& c = t.chops[(size_t) chop];
    // Velocity attenuation scaled by the chop's sensitivity: at velSens 0 the
    // chop always plays full; at 1 it tracks velocity across the full range.
    const float vel = juce::jlimit (0.0f, 1.0f, velocity);
    const float velGain = 1.0f - c.velSens * (1.0f - vel);
    free->start (t.buffer, t.sourceSampleRate, c,
                 c.pitchSemis + params.globalPitch, t.gain * velGain, track, chop);
    voiceAges[free - voices.data()] = ++ageCounter;

    const auto scope = flashFifo.write (1);
    if (scope.blockSize1 > 0)
        flashEvents[scope.startIndex1] = { track, chop };
}

void SamplerEngine::stopChop (int track, int chop)
{
    for (auto& v : voices)
        if (v.isActive() && v.getTrackIndex() == track && v.getChopIndex() == chop)
            v.release();
}

void SamplerEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                             const EngineSnapshot* snap, const Params& params,
                             TransportInfo transport)
{
    if (snap == nullptr)
        return;

    if (transport.bpm > 0.0)
        hostBpm.store (transport.bpm, std::memory_order_relaxed);

    const int numSamples = out.getNumSamples();
    const juce::uint64 clockStart = sampleClock;
    const juce::uint64 clockEnd   = clockStart + (juce::uint64) numSamples;

    // Groove only while the transport is rolling: a frozen grid (host stopped)
    // would misplace live/auditioned notes. Stopped notes fire sample-accurately.
    const bool groovable = transport.valid && transport.isPlaying && transport.bpm > 0.0;
    // A note may be swung/quantized up to ~one beat past its arrival.
    const int maxOffset = numSamples + (groovable ? (int) (60.0 / transport.bpm * hostRate) : 0);

    // UI-originated triggers (pads, waveform auditions, laptop keyboard) fire
    // immediately — they carry no musical timing.
    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        auto handle = [&] (const TriggerEvent& e)
        {
            const int track = e.track < 0 ? snap->activeTrack : e.track;
            schedule (clockStart, track, e.chop, e.on, e.velocity);
        };
        for (int i = 0; i < scope.blockSize1; ++i) handle (uiEvents[scope.startIndex1 + i]);
        for (int i = 0; i < scope.blockSize2; ++i) handle (uiEvents[scope.startIndex2 + i]);
    }

    // MIDI: baseNote + n triggers chop n of the active track (this is how FL
    // Studio's typing-keyboard and piano roll reach us). Note-ons pass through
    // the groove engine (swing + quantize); velocity drives per-hit gain.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int samplePos = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);

        if (msg.isNoteOn())
        {
            const int chop = msg.getNoteNumber() - params.baseNote;
            const int offset = groovable
                ? groove::applyGroove (samplePos, transport.ppqPosition, transport.bpm, hostRate,
                                       params.swing, params.quantize, params.gridDivisions, maxOffset)
                : samplePos;
            schedule (clockStart + (juce::uint64) offset, snap->activeTrack, chop, true,
                      msg.getFloatVelocity());
        }
        else if (msg.isNoteOff() && params.gate)
        {
            // Note-offs keep raw timing so gated notes never hang.
            schedule (clockStart + (juce::uint64) samplePos, snap->activeTrack,
                      msg.getNoteNumber() - params.baseNote, false, 1.0f);
        }
    }

    // Split scheduled events into those due this block and those still pending.
    ScheduledTrigger due[maxScheduled];
    int dueCount = 0, keep = 0;
    for (int i = 0; i < scheduledCount; ++i)
    {
        if (scheduled[i].time < clockEnd) due[dueCount++] = scheduled[i];
        else                              scheduled[keep++] = scheduled[i];
    }
    scheduledCount = keep;

    // Insertion sort due events by fire time (N is tiny in practice).
    for (int i = 1; i < dueCount; ++i)
    {
        const auto key = due[i];
        int j = i - 1;
        while (j >= 0 && due[j].time > key.time) { due[j + 1] = due[j]; --j; }
        due[j + 1] = key;
    }

    // Sample-accurate render: advance all voices up to each event's offset,
    // apply the event, continue. Voices started mid-block therefore begin on
    // exactly the right sample.
    int cursor = 0;
    for (int i = 0; i < dueCount; ++i)
    {
        const int localOff = (int) juce::jlimit<juce::uint64> (0, (juce::uint64) numSamples,
                                                               due[i].time - clockStart);
        if (localOff > cursor)
        {
            renderVoices (out, cursor, localOff - cursor);
            cursor = localOff;
        }
        if (due[i].on) startChop (*snap, due[i].track, due[i].chop, params, due[i].velocity);
        else if (params.gate) stopChop (due[i].track, due[i].chop);
    }
    if (cursor < numSamples)
        renderVoices (out, cursor, numSamples - cursor);

    sampleClock = clockEnd;

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (params.masterGainDb));
    masterGain.applyGain (out, numSamples);
}
} // namespace pablo
