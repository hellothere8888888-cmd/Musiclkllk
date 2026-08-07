#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace pablo
{
// Lets the audio thread hand a finished/replaced sample buffer's shared_ptr to
// the message thread for destruction, so the (potentially expensive) heap free
// of the audio data never runs on the audio thread. Single audio-thread
// producer, single message-thread consumer.
//
// This is what makes "playing voices keep the buffer alive" real-time safe:
// the voice can be the last owner of a buffer (e.g. after its track is
// deleted), but instead of destructing it in processBlock it retires the
// pointer here and the message thread frees it.
class BufferReleasePool
{
public:
    using Ptr = std::shared_ptr<const juce::AudioBuffer<float>>;

    // Audio thread. Moves 'p' into a free slot for later reclamation. Returns
    // false (leaving 'p' untouched) only if the pool is momentarily full; the
    // caller then drops its own reference, accepting a rare audio-thread free.
    bool retire (Ptr&& p)
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 < 1)
            return false;
        // The message thread reset this slot before releasing it back to us,
        // so this move-assign into an empty shared_ptr frees nothing.
        slots[(size_t) scope.startIndex1] = std::move (p);
        return true;
    }

    // Message thread. Frees everything retired since the last call.
    void reclaim()
    {
        const auto scope = fifo.read (fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) slots[(size_t) (scope.startIndex1 + i)].reset();
        for (int i = 0; i < scope.blockSize2; ++i) slots[(size_t) (scope.startIndex2 + i)].reset();
    }

private:
    static constexpr int capacity = 256;
    juce::AbstractFifo fifo { capacity };
    Ptr slots[capacity];
};
} // namespace pablo
