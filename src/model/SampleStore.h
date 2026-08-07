#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <memory>

namespace pablo
{
// Owns the decoded audio for each track, keyed by the track's uid. Buffers are
// immutable and ref-counted so the audio thread (via EngineSnapshot / playing
// voices) can keep them alive past track deletion.
class SampleStore
{
public:
    using BufferPtr = std::shared_ptr<const juce::AudioBuffer<float>>;

    void set (int uid, BufferPtr buffer, double sampleRate)
    {
        entries[uid] = { std::move (buffer), sampleRate };
    }

    void remove (int uid) { entries.erase (uid); }

    void clear() { entries.clear(); }

    BufferPtr getBuffer (int uid) const
    {
        auto it = entries.find (uid);
        return it != entries.end() ? it->second.buffer : nullptr;
    }

    double getSampleRate (int uid) const
    {
        auto it = entries.find (uid);
        return it != entries.end() ? it->second.sampleRate : 44100.0;
    }

private:
    struct Entry { BufferPtr buffer; double sampleRate = 44100.0; };
    std::map<int, Entry> entries;
};
} // namespace pablo
