#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "SampleTrack.h"
#include "SampleStore.h"
#include "EngineSnapshot.h"

namespace pablo
{
// Source of truth for the session: owns the SESSION ValueTree, the decoded
// audio (SampleStore) and the undo manager. Any structural change triggers a
// debounced rebuild of the EngineSnapshot which is published to the audio
// thread through the SnapshotExchange handed in by the processor.
class SessionState : private juce::ValueTree::Listener,
                     private juce::AsyncUpdater,
                     private juce::Timer
{
public:
    explicit SessionState (SnapshotExchange& exchangeToUse);
    ~SessionState() override;

    juce::ValueTree getRoot() const { return root; }
    juce::UndoManager& getUndoManager() { return undo; }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // ---- tracks ---------------------------------------------------------
    int getNumTracks() const { return root.getNumChildren(); }
    SampleTrack getTrack (int index) const { return SampleTrack (root.getChild (index)); }
    SampleTrack getTrackByUid (int uid) const;
    int indexOfUid (int uid) const;

    int getActiveTrackIndex() const { return juce::jlimit (0, juce::jmax (0, getNumTracks() - 1),
                                                           (int) root.getProperty (id::activeTrack, 0)); }
    void setActiveTrackIndex (int index) { root.setProperty (id::activeTrack, index, nullptr); }
    SampleTrack getActiveTrack() const { return getTrack (getActiveTrackIndex()); }

    SampleStore::BufferPtr getTrackBuffer (const SampleTrack& t) const { return store.getBuffer (t.getUid()); }

    // Loads a file on a background thread; commits the new track (and selects
    // it) on the message thread. onDone (may be null) receives success.
    void addTrackFromFile (const juce::File& file, std::function<void (bool)> onDone = nullptr);

    // Used by the recorder and the stem splitter. Takes ownership of the buffer.
    SampleTrack addTrackFromBuffer (const juce::String& name,
                                    std::unique_ptr<juce::AudioBuffer<float>> buffer,
                                    double sampleRate);

    void removeTrack (int index);

    // ---- persistence ----------------------------------------------------
    // The returned tree contains the SESSION structure plus embedded FLAC
    // audio (size-capped) so FL Studio projects reload even if files moved.
    juce::ValueTree createSaveTree() const;
    void restoreFromSaveTree (const juce::ValueTree& saved);

    // Forces an immediate snapshot rebuild + publish.
    void publishNow();

    std::function<void()> onSessionChanged;   // UI refresh hook (message thread)

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override            { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override     { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override             { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    // Debounced background render of pitch-preserving time-stretch. Both the
    // cache and buildSnapshot are message-thread only, so no locking is needed;
    // the worker thread only produces a buffer and hands it back via callAsync.
    void timerCallback() override;
    struct StretchEntry
    {
        juce::int64 start = 0, end = 0;
        double ratio = 1.0;
        float  pitch = 0.0f;
        bool   rendering = false;
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    };
    std::map<juce::int64, StretchEntry> stretchCache;   // key = uid * 100000 + chopIndex
    static juce::int64 stretchKey (int uid, int chopIndex) { return (juce::int64) uid * 100000 + chopIndex; }
    void launchStretch (juce::int64 key, int uid, double sampleRate);

    std::unique_ptr<EngineSnapshot> buildSnapshot();
    juce::ValueTree createTrackTree (const juce::String& name, juce::int64 length, double sampleRate);

    SnapshotExchange& exchange;
    juce::ValueTree root { id::SESSION };
    juce::UndoManager undo;
    SampleStore store;
    juce::AudioFormatManager formatManager;
    int nextUid = 1;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SessionState)
};
} // namespace pablo
