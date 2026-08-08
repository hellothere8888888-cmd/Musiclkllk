#include "SessionState.h"

namespace pablo
{
static constexpr juce::int64 maxEmbeddedBytesTotal = 50 * 1024 * 1024;

SessionState::SessionState (SnapshotExchange& exchangeToUse) : exchange (exchangeToUse)
{
    formatManager.registerBasicFormats();
    root.setProperty (id::version, 1, nullptr);
    root.setProperty (id::activeTrack, 0, nullptr);
    root.addListener (this);
}

SessionState::~SessionState()
{
    root.removeListener (this);
}

SampleTrack SessionState::getTrackByUid (int uid) const
{
    return SampleTrack (root.getChildWithProperty (id::uid, uid));
}

int SessionState::indexOfUid (int uid) const
{
    for (int i = 0; i < root.getNumChildren(); ++i)
        if ((int) root.getChild (i)[id::uid] == uid)
            return i;
    return -1;
}

juce::ValueTree SessionState::createTrackTree (const juce::String& name, juce::int64 length, double sampleRate)
{
    juce::ValueTree t (id::TRACK);
    t.setProperty (id::uid, nextUid++, nullptr);
    t.setProperty (id::name, name, nullptr);
    t.setProperty (id::sampleRate, sampleRate, nullptr);
    t.setProperty (id::lengthSamples, length, nullptr);
    t.setProperty (id::gain, 1.0f, nullptr);
    t.setProperty (id::embedAudio, true, nullptr);
    t.appendChild (juce::ValueTree (id::CHOPS), nullptr);

    // One chop spanning the whole sample so it is playable immediately.
    SampleTrack track (t);
    track.addChopAt (0, nullptr);
    return t;
}

void SessionState::addTrackFromFile (const juce::File& file, std::function<void (bool)> onDone)
{
    juce::Thread::launch ([safeThis = juce::WeakReference<SessionState> (this), file, onDone]
    {
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
        double sr = 44100.0;

        // Use a local format manager so the background thread never touches
        // 'this' (which may be destroyed mid-read); the member is only used on
        // the message thread. Registration is cheap.
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (file)))
        {
            const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 60ll * 10 * (juce::int64) reader->sampleRate);
            const auto numCh = juce::jmin (2, (int) reader->numChannels);
            if (len > 0 && numCh > 0)
            {
                buffer = std::make_unique<juce::AudioBuffer<float>> (numCh, len);
                reader->read (buffer.get(), 0, len, 0, true, numCh > 1);
                sr = reader->sampleRate;
            }
        }

        juce::MessageManager::callAsync ([safeThis, file, onDone,
                                          sharedBuf = std::shared_ptr<juce::AudioBuffer<float>> (buffer.release()), sr]() mutable
        {
            auto* self = safeThis.get();
            if (self == nullptr)
                return;

            if (sharedBuf == nullptr)
            {
                if (onDone) onDone (false);
                return;
            }

            auto tree = self->createTrackTree (file.getFileNameWithoutExtension(),
                                               sharedBuf->getNumSamples(), sr);
            tree.setProperty (id::filePath, file.getFullPathName(), nullptr);

            self->store.set ((int) tree[id::uid],
                             std::shared_ptr<const juce::AudioBuffer<float>> (sharedBuf),
                             sr);
            self->root.appendChild (tree, nullptr);
            self->setActiveTrackIndex (self->getNumTracks() - 1);
            if (onDone) onDone (true);
        });
    });
}

SampleTrack SessionState::addTrackFromBuffer (const juce::String& name,
                                              std::unique_ptr<juce::AudioBuffer<float>> buffer,
                                              double sampleRate)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (buffer == nullptr || buffer->getNumSamples() == 0)
        return SampleTrack();

    auto tree = createTrackTree (name, buffer->getNumSamples(), sampleRate);
    store.set ((int) tree[id::uid],
               std::shared_ptr<const juce::AudioBuffer<float>> (buffer.release()),
               sampleRate);
    root.appendChild (tree, nullptr);
    setActiveTrackIndex (getNumTracks() - 1);
    return SampleTrack (tree);
}

void SessionState::removeTrack (int index)
{
    auto t = getTrack (index);
    if (! t.isValid())
        return;
    const int uid = t.getUid();
    const int active = getActiveTrackIndex();
    root.removeChild (index, nullptr);
    store.remove (uid);   // playing voices keep their own shared_ptr copies

    // Keep the same track selected: shift the active index down if a track at
    // or before it was removed, then clamp to the new range.
    int newActive = active;
    if (index < active)      --newActive;
    else if (index == active) newActive = active;   // next track slides into place
    setActiveTrackIndex (juce::jlimit (0, juce::jmax (0, getNumTracks() - 1), newActive));
}

// ---- snapshot -----------------------------------------------------------

std::unique_ptr<EngineSnapshot> SessionState::buildSnapshot() const
{
    auto snap = std::make_unique<EngineSnapshot>();
    snap->activeTrack = getActiveTrackIndex();

    for (int i = 0; i < getNumTracks(); ++i)
    {
        auto track = getTrack (i);
        TrackPlayInfo info;
        info.buffer = store.getBuffer (track.getUid());
        info.sourceSampleRate = track.getSampleRate();
        info.gain = track.getGain();

        const auto len = info.buffer != nullptr ? (juce::int64) info.buffer->getNumSamples() : 0;
        for (int c = 0; c < track.getNumChops(); ++c)
        {
            ChopPlayInfo chop;
            chop.start = juce::jlimit<juce::int64> (0, len, track.getChopStart (c));
            chop.end   = juce::jlimit<juce::int64> (chop.start, len, track.getChopEnd (c));
            chop.pitchSemis = track.getChopPitch (c);
            chop.reverse = track.getChopReverse (c);
            chop.velSens = track.getChopVelSens (c);
            info.chops.push_back (chop);
        }
        snap->tracks.push_back (std::move (info));
    }
    return snap;
}

void SessionState::handleAsyncUpdate()
{
    publishNow();
    if (onSessionChanged)
        onSessionChanged();
}

void SessionState::publishNow()
{
    exchange.publish (buildSnapshot());
}

// ---- persistence --------------------------------------------------------

juce::ValueTree SessionState::createSaveTree() const
{
    auto saved = root.createCopy();
    juce::int64 embeddedTotal = 0;

    for (int i = 0; i < saved.getNumChildren(); ++i)
    {
        auto trackTree = saved.getChild (i);
        SampleTrack track (trackTree);
        auto buffer = store.getBuffer (track.getUid());
        if (buffer == nullptr || ! (bool) trackTree.getProperty (id::embedAudio, true))
            continue;

        juce::MemoryBlock flacData;
        {
            juce::FlacAudioFormat flac;
            // FLAC is 16/24-bit; 16-bit keeps project files small and is
            // plenty for recall purposes.
            std::unique_ptr<juce::AudioFormatWriter> writer (
                flac.createWriterFor (new juce::MemoryOutputStream (flacData, false),
                                      track.getSampleRate() > 0 ? track.getSampleRate() : 44100.0,
                                      (unsigned) buffer->getNumChannels(), 16, {}, 5));
            if (writer == nullptr)
                continue;
            writer->writeFromAudioSampleBuffer (*buffer, 0, buffer->getNumSamples());
        }

        if (embeddedTotal + (juce::int64) flacData.getSize() > maxEmbeddedBytesTotal)
            continue;
        embeddedTotal += (juce::int64) flacData.getSize();

        juce::ValueTree audio (id::AUDIO);
        audio.setProperty (id::flacBase64,
                           juce::Base64::toBase64 (flacData.getData(), flacData.getSize()),
                           nullptr);
        trackTree.appendChild (audio, nullptr);
    }
    return saved;
}

void SessionState::restoreFromSaveTree (const juce::ValueTree& saved)
{
    if (! saved.hasType (id::SESSION))
        return;

    // Drop any audio from a previous session so a reused uid can never serve
    // stale samples; it is fully rebuilt from the restored tree below.
    store.clear();
    nextUid = 1;

    root.removeListener (this);
    root.copyPropertiesAndChildrenFrom (saved, nullptr);

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        auto trackTree = root.getChild (i);
        SampleTrack track (trackTree);
        nextUid = juce::jmax (nextUid, track.getUid() + 1);

        SampleStore::BufferPtr buffer;
        double sr = track.getSampleRate();

        // Prefer the original file on disk; fall back to the embedded copy.
        const juce::File file (track.getFilePath());
        if (file.existsAsFile())
        {
            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (file)))
            {
                const auto numCh = juce::jmin (2, (int) reader->numChannels);
                auto b = std::make_shared<juce::AudioBuffer<float>> (numCh, (int) reader->lengthInSamples);
                reader->read (b.get(), 0, b->getNumSamples(), 0, true, numCh > 1);
                sr = reader->sampleRate;
                buffer = std::move (b);
            }
        }

        auto audioTree = trackTree.getChildWithName (id::AUDIO);
        if (buffer == nullptr && audioTree.isValid())
        {
            juce::MemoryOutputStream decoded;
            if (juce::Base64::convertFromBase64 (decoded, audioTree[id::flacBase64].toString()))
            {
                juce::FlacAudioFormat flac;
                if (auto reader = std::unique_ptr<juce::AudioFormatReader> (
                        flac.createReaderFor (new juce::MemoryInputStream (decoded.getData(),
                                                                           decoded.getDataSize(), false), true)))
                {
                    const auto numCh = juce::jmin (2, (int) reader->numChannels);
                    auto b = std::make_shared<juce::AudioBuffer<float>> (numCh, (int) reader->lengthInSamples);
                    reader->read (b.get(), 0, b->getNumSamples(), 0, true, numCh > 1);
                    sr = reader->sampleRate;
                    buffer = std::move (b);
                }
            }
        }

        // Strip the embedded blob from the working tree (re-created on save).
        if (audioTree.isValid())
            trackTree.removeChild (audioTree, nullptr);

        if (buffer != nullptr)
        {
            trackTree.setProperty (id::sampleRate, sr, nullptr);
            trackTree.setProperty (id::lengthSamples, (juce::int64) buffer->getNumSamples(), nullptr);
            store.set (track.getUid(), buffer, sr);
        }
        // else: track stays visible as "sample missing", chops preserved.
    }

    root.addListener (this);
    publishNow();
    if (onSessionChanged)
        onSessionChanged();
}
} // namespace pablo
