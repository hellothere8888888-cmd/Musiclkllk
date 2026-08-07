#include "SamplerEngine.h"

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
}

void SamplerEngine::triggerFromUI (int track, int chop, bool on)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0)
        uiEvents[scope.startIndex1] = { track, chop, on };
}

std::vector<std::pair<int, int>> SamplerEngine::drainFlashes()
{
    std::vector<std::pair<int, int>> out;
    const auto scope = flashFifo.read (flashFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) out.push_back (flashEvents[scope.startIndex1 + i]);
    for (int i = 0; i < scope.blockSize2; ++i) out.push_back (flashEvents[scope.startIndex2 + i]);
    return out;
}

void SamplerEngine::startChop (const EngineSnapshot& snap, int track, int chop, const Params& params)
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
    free->start (t.buffer, t.sourceSampleRate, c,
                 c.pitchSemis + params.globalPitch, t.gain, track, chop);
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
                             const EngineSnapshot* snap, const Params& params)
{
    if (snap == nullptr)
        return;

    // UI-originated triggers (pads, waveform auditions, laptop keyboard).
    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        auto handle = [&] (const TriggerEvent& e)
        {
            const int track = e.track < 0 ? snap->activeTrack : e.track;
            if (e.on) startChop (*snap, track, e.chop, params);
            else if (params.gate) stopChop (track, e.chop);
        };
        for (int i = 0; i < scope.blockSize1; ++i) handle (uiEvents[scope.startIndex1 + i]);
        for (int i = 0; i < scope.blockSize2; ++i) handle (uiEvents[scope.startIndex2 + i]);
    }

    // MIDI: baseNote + n triggers chop n of the active track. This is how FL
    // Studio's typing-keyboard reaches us.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            const int chop = msg.getNoteNumber() - params.baseNote;
            startChop (*snap, snap->activeTrack, chop, params);
        }
        else if (msg.isNoteOff() && params.gate)
        {
            stopChop (snap->activeTrack, msg.getNoteNumber() - params.baseNote);
        }
    }

    const int numSamples = out.getNumSamples();
    for (auto& v : voices)
        v.render (out, 0, numSamples);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (params.masterGainDb));
    masterGain.applyGain (out, numSamples);
}
} // namespace pablo
