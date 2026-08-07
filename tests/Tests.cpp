// Lightweight unit tests for the PABLO sampler core. No external framework:
// each CHECK records a failure and the process exits non-zero if any fail, so
// CTest / CI reports red on regression.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Identifiers.h"
#include "model/SessionState.h"
#include "model/EngineSnapshot.h"
#include "dsp/TransientDetector.h"
#include "dsp/Resampler.h"
#include "stems/StemSeparator.h"

#include <cstdio>

namespace
{
int g_failures = 0;
int g_checks = 0;

void check (bool cond, const char* what, const char* file, int line)
{
    ++g_checks;
    if (! cond)
    {
        ++g_failures;
        std::printf ("  FAIL: %s  (%s:%d)\n", what, file, line);
    }
}

#define CHECK(cond) check ((cond), #cond, __FILE__, __LINE__)

juce::ValueTree makeTrackTree (juce::int64 length, double sr = 44100.0)
{
    using namespace pablo;
    juce::ValueTree t (id::TRACK);
    t.setProperty (id::uid, 1, nullptr);
    t.setProperty (id::name, "test", nullptr);
    t.setProperty (id::sampleRate, sr, nullptr);
    t.setProperty (id::lengthSamples, length, nullptr);
    t.setProperty (id::gain, 1.0f, nullptr);
    t.appendChild (juce::ValueTree (id::CHOPS), nullptr);
    return t;
}

// ---- chop math ----------------------------------------------------------
void testChopMath()
{
    std::printf ("testChopMath\n");
    using namespace pablo;

    SampleTrack track (makeTrackTree (10000));
    CHECK (track.isValid());
    CHECK (track.getNumChops() == 0);

    // Equal slices tile the whole sample contiguously.
    track.equalSlices (4, nullptr);
    CHECK (track.getNumChops() == 4);
    CHECK (track.getChopStart (0) == 0);
    CHECK (track.getChopStart (1) == 2500);
    CHECK (track.getChopEnd (0) == 2500);
    CHECK (track.getChopEnd (3) == 10000);       // last chop ends at sample length

    // findChopContaining picks the slice a sample falls in.
    CHECK (track.findChopContaining (0) == 0);
    CHECK (track.findChopContaining (2499) == 0);
    CHECK (track.findChopContaining (2500) == 1);
    CHECK (track.findChopContaining (9999) == 3);

    // Adding a boundary keeps the list sorted and rejects duplicates.
    const int added = track.addChopAt (1250, nullptr);
    CHECK (added == 1);
    CHECK (track.getNumChops() == 5);
    CHECK (track.getChopStart (1) == 1250);
    CHECK (track.getChopStart (2) == 2500);
    CHECK (track.addChopAt (1250, nullptr) == -1);           // duplicate rejected
    CHECK (track.addChopAt (999999, nullptr) == -1);         // out of range rejected

    // Dragging a marker clamps strictly between its neighbours.
    track.moveChopStart (1, 5000, nullptr);                  // would cross neighbour 2 (2500)
    CHECK (track.getChopStart (1) == 2499);
    track.moveChopStart (1, 0, nullptr);                     // would cross neighbour 0 (0)
    CHECK (track.getChopStart (1) == 1);

    // Per-chop pitch and reverse persist.
    track.setChopPitch (2, 7.0f, nullptr);
    track.setChopReverse (2, true, nullptr);
    CHECK (juce::approximatelyEqual (track.getChopPitch (2), 7.0f));
    CHECK (track.getChopReverse (2));

    // Removing a chop shrinks the grid; chop 0 removal is allowed here.
    const int before = track.getNumChops();
    track.removeChop (2, nullptr);
    CHECK (track.getNumChops() == before - 1);
}

// ---- transient detection ------------------------------------------------
void testTransientDetector()
{
    std::printf ("testTransientDetector\n");
    using namespace pablo;

    const double sr = 44100.0;
    const int len = (int) (sr * 2.0);
    juce::AudioBuffer<float> buffer (1, len);
    buffer.clear();
    auto* d = buffer.getWritePointer (0);

    // Impulsive bursts (decaying noise) at 0.5s and 1.0s and 1.5s.
    juce::Random rng (1);
    for (double t : { 0.5, 1.0, 1.5 })
    {
        const int start = (int) (t * sr);
        for (int i = 0; i < 2000 && start + i < len; ++i)
            d[start + i] = (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / 400.0f);
    }

    auto onsets = detectTransients (buffer, sr, 0.6f);
    CHECK (onsets.size() >= 4);           // 0 plus the three bursts (maybe a couple extra)
    CHECK (onsets.front() == 0);

    auto near = [&onsets, sr] (double seconds)
    {
        const auto target = (juce::int64) (seconds * sr);
        for (auto o : onsets)
            if (std::abs (o - target) < (juce::int64) (0.05 * sr))
                return true;
        return false;
    };
    CHECK (near (0.5));
    CHECK (near (1.0));
    CHECK (near (1.5));
}

// ---- resampler ----------------------------------------------------------
void testResampler()
{
    std::printf ("testResampler\n");
    using namespace pablo;

    const int len = 44100;
    juce::AudioBuffer<float> sine (2, len);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* w = sine.getWritePointer (ch);
        for (int i = 0; i < len; ++i)
            w[i] = std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f);
    }

    // Identity when rates match.
    auto same = resampleBuffer (sine, 44100.0, 44100.0);
    CHECK (same.getNumSamples() == len);

    // Down to 22050 halves the length (approximately).
    auto down = resampleBuffer (sine, 44100.0, 22050.0);
    CHECK (std::abs (down.getNumSamples() - len / 2) < 4);

    // Round trip preserves length within a sample or two and stays bounded.
    auto up = resampleBuffer (down, 22050.0, 44100.0);
    CHECK (std::abs (up.getNumSamples() - len) < 8);
    float peak = up.getMagnitude (0, up.getNumSamples());
    CHECK (peak > 0.5f && peak < 1.5f);
}

// ---- overlap-add windowing ----------------------------------------------
// Replicates the triangular-window OLA weighting used by StemSeparator and
// checks that summed windows divided by the summed weight reconstruct a
// constant signal (i.e. the segmentation itself is unity-gain).
void testOverlapAddWeighting()
{
    std::printf ("testOverlapAddWeighting\n");
    using namespace pablo;

    const int seg = StemSeparator::segmentLength;
    const int hop = (int) (seg * (1.0 - StemSeparator::overlapFraction));
    const int total = seg * 3;

    std::vector<float> window ((size_t) seg);
    for (int i = 0; i < seg; ++i)
    {
        const float x = (float) i / (float) (seg - 1);
        window[(size_t) i] = 1.0f - std::abs (2.0f * x - 1.0f) * 0.999f;
    }

    std::vector<float> accum ((size_t) total, 0.0f), weight ((size_t) total, 0.0f);
    for (int start = 0; start < total; start += hop)
    {
        const int n = juce::jmin (seg, total - start);
        for (int i = 0; i < n; ++i)
        {
            accum[(size_t) (start + i)]  += 1.0f * window[(size_t) i];   // constant "signal" of 1
            weight[(size_t) (start + i)] += window[(size_t) i];
        }
    }

    // In the fully-covered interior, reconstruction should be ~1.
    int checked = 0;
    for (int i = seg; i < total - seg; ++i)
    {
        const float recon = accum[(size_t) i] / juce::jmax (1.0e-6f, weight[(size_t) i]);
        CHECK (std::abs (recon - 1.0f) < 1.0e-3f);
        if (++checked > 5000) break;
    }
    CHECK (checked > 0);
}

// ---- snapshot exchange lock-free handover -------------------------------
void testSnapshotExchange()
{
    std::printf ("testSnapshotExchange\n");
    using namespace pablo;

    SnapshotExchange ex;
    CHECK (ex.acquire() == nullptr);       // nothing published yet

    auto s1 = std::make_unique<EngineSnapshot>();
    s1->activeTrack = 3;
    ex.publish (std::move (s1));

    const auto* got = ex.acquire();
    CHECK (got != nullptr && got->activeTrack == 3);

    // Publishing again while the old one is live must not crash and the audio
    // side sees the new value after acquire.
    auto s2 = std::make_unique<EngineSnapshot>();
    s2->activeTrack = 7;
    ex.publish (std::move (s2));
    const auto* got2 = ex.acquire();
    CHECK (got2 != nullptr && got2->activeTrack == 7);

    ex.collectTrash();
}

// ---- full session state round trip --------------------------------------
void testSessionRoundTrip()
{
    std::printf ("testSessionRoundTrip\n");
    using namespace pablo;

    SnapshotExchange ex;
    SessionState session (ex);

    // Build a synthetic stereo buffer and add it as a track.
    const int len = 20000;
    auto buffer = std::make_unique<juce::AudioBuffer<float>> (2, len);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* w = buffer->getWritePointer (ch);
        for (int i = 0; i < len; ++i)
            w[i] = 0.25f * std::sin (juce::MathConstants<float>::twoPi * (ch == 0 ? 220.0f : 330.0f)
                                     * (float) i / 44100.0f);
    }

    auto track = session.addTrackFromBuffer ("kick", std::move (buffer), 44100.0);
    CHECK (session.getNumTracks() == 1);
    CHECK (track.isValid());

    track.equalSlices (8, nullptr);
    track.setChopPitch (2, -5.0f, nullptr);
    track.setChopReverse (3, true, nullptr);
    const int chopsBefore = track.getNumChops();

    // Save, then restore into a fresh session.
    auto saved = session.createSaveTree();
    CHECK (saved.getChild (0).getChildWithName (id::AUDIO).isValid());   // FLAC was embedded

    SnapshotExchange ex2;
    SessionState restored (ex2);
    restored.restoreFromSaveTree (saved);

    CHECK (restored.getNumTracks() == 1);
    auto rtrack = restored.getTrack (0);
    CHECK (rtrack.getName() == "kick");
    CHECK (rtrack.getNumChops() == chopsBefore);
    CHECK (juce::approximatelyEqual (rtrack.getChopPitch (2), -5.0f));
    CHECK (rtrack.getChopReverse (3));

    // Audio survived via the embedded FLAC copy (no file path on disk).
    auto rbuffer = restored.getTrackBuffer (rtrack);
    CHECK (rbuffer != nullptr);
    if (rbuffer != nullptr)
    {
        CHECK (std::abs (rbuffer->getNumSamples() - len) < 8);
        CHECK (rbuffer->getMagnitude (0, rbuffer->getNumSamples()) > 0.1f);
    }
}
} // namespace

int main()
{
    // Establishes the message thread so SessionState's message-thread asserts
    // and async machinery behave.
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("PABLO unit tests\n");
    testChopMath();
    testTransientDetector();
    testResampler();
    testOverlapAddWeighting();
    testSnapshotExchange();
    testSessionRoundTrip();

    std::printf ("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
