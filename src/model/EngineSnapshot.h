#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace pablo
{
// Immutable snapshot of everything the audio thread needs to render. Rebuilt
// on the message thread whenever tracks/chops change and handed over through
// SnapshotExchange. The audio thread never touches the ValueTree.
struct ChopPlayInfo
{
    juce::int64 start = 0;      // inclusive, in source samples
    juce::int64 end   = 0;      // exclusive
    float pitchSemis  = 0.0f;
    bool  reverse     = false;
    float velSens     = 1.0f;   // MIDI-velocity sensitivity (1 = full, 0 = ignore)
};

struct TrackPlayInfo
{
    // shared_ptr keeps the audio alive even if the track is deleted while a
    // voice is still sounding.
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    double sourceSampleRate = 44100.0;
    float  gain = 1.0f;
    int    filterMode = 0;          // 0 = off, 1 = low-pass, 2 = high-pass
    float  filterCutoff = 20000.0f; // Hz
    std::vector<ChopPlayInfo> chops;
};

struct EngineSnapshot
{
    std::vector<TrackPlayInfo> tracks;
    int activeTrack = 0;
};

// Single-producer (message thread) / single-consumer (audio thread) handover.
//   - publish(): stores the new snapshot in a one-slot mailbox; an unclaimed
//     previous pending snapshot is deleted immediately (the audio thread never
//     saw it, so that is safe).
//   - acquire(): audio thread claims the pending snapshot, retiring the old
//     current one into a trash queue that the message thread empties.
// The audio thread therefore never deletes and never blocks.
class SnapshotExchange
{
public:
    SnapshotExchange() = default;

    ~SnapshotExchange()
    {
        delete pending.exchange (nullptr);
        delete current;
        collectTrash();
    }

    // Message thread only.
    void publish (std::unique_ptr<EngineSnapshot> snap)
    {
        delete pending.exchange (snap.release());
        collectTrash();
    }

    // Audio thread only. Returns the snapshot to render with (may be null
    // before the first publish).
    const EngineSnapshot* acquire()
    {
        if (auto* fresh = pending.exchange (nullptr))
        {
            if (current != nullptr)
            {
                const auto slot = trashWrite.load (std::memory_order_relaxed);
                const auto next = (slot + 1) % trashCapacity;
                if (next != trashRead.load (std::memory_order_acquire))
                {
                    trash[(size_t) slot] = current;
                    trashWrite.store (next, std::memory_order_release);
                }
                // If the trash queue is somehow full we intentionally leak
                // rather than free on the audio thread; collectTrash() keeps
                // this from ever happening in practice.
            }
            current = fresh;
        }
        return current;
    }

    // Message thread only.
    void collectTrash()
    {
        auto r = trashRead.load (std::memory_order_relaxed);
        const auto w = trashWrite.load (std::memory_order_acquire);
        while (r != w)
        {
            delete trash[(size_t) r];
            trash[(size_t) r] = nullptr;
            r = (r + 1) % trashCapacity;
        }
        trashRead.store (r, std::memory_order_release);
    }

private:
    static constexpr int trashCapacity = 64;

    std::atomic<EngineSnapshot*> pending { nullptr };
    EngineSnapshot* current = nullptr;                  // audio thread's view
    EngineSnapshot* trash[trashCapacity] = {};
    std::atomic<int> trashRead { 0 }, trashWrite { 0 };

    JUCE_DECLARE_NON_COPYABLE (SnapshotExchange)
};
} // namespace pablo
