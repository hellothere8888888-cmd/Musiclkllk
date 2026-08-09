// Lightweight unit tests for the PABLO sampler core. No external framework:
// each CHECK records a failure and the process exits non-zero if any fail, so
// CTest / CI reports red on regression.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Identifiers.h"
#include "model/SessionState.h"
#include "model/EngineSnapshot.h"
#include "engine/SamplerEngine.h"
#include "engine/GrooveTiming.h"
#include "dsp/CarveFilter.h"
#include "dsp/TimeStretch.h"
#include "dsp/TransientDetector.h"
#include "dsp/Resampler.h"
#include "stems/StemSeparator.h"

#include <cstdio>
#include <cmath>

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

// ---- overlapping / free regions ----------------------------------------
void testOverlappingChops()
{
    std::printf ("testOverlappingChops\n");
    using namespace pablo;

    SampleTrack track (makeTrackTree (10000));
    track.equalSlices (4, nullptr);                 // [0,2500)[2500,5000)[5000,7500)[7500,10000)
    CHECK (track.getNumChops() == 4);

    // A free region that overlaps chops 0 and 1 becomes its own pad without
    // disturbing the existing tiling.
    const int r = track.addChopRange (1000, 4000, nullptr);
    CHECK (r == 1);                                 // sorts after chop starting at 0
    CHECK (track.getNumChops() == 5);
    CHECK (track.getChopStart (r) == 1000);
    CHECK (track.getChopEnd (r) == 4000);           // explicit end -> may overlap
    CHECK (track.getChopStart (0) == 0);
    CHECK (track.getChopEnd (0) == 2500);           // neighbour untouched (frozen)
    CHECK (track.getChopEnd (2) == 5000);           // the old chop 1 is now index 2

    // Its edges are independent of the tiling neighbour: moving its start does
    // not drag chop 0's end (they aren't a shared boundary).
    track.moveChopStart (r, 500, nullptr);
    CHECK (track.getChopStart (r) == 500);
    CHECK (track.getChopEnd (0) == 2500);

    // Clamped to at least one sample inside its own end.
    track.moveChopStart (r, 999999, nullptr);
    CHECK (track.getChopStart (r) == track.getChopEnd (r) - 1);

    // Re-slicing wipes free regions back to a clean grid.
    track.equalSlices (2, nullptr);
    CHECK (track.getNumChops() == 2);
    CHECK (track.getChopEnd (0) == 5000);
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

// ---- engine playback end-to-end -----------------------------------------
// Trigger -> voice -> varispeed -> output. Exercises the real audio path.
void testEnginePlayback()
{
    std::printf ("testEnginePlayback\n");
    using namespace pablo;

    // A loud stereo tone we can detect at the output.
    const int len = 4096;
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (2, len);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* w = buffer->getWritePointer (ch);
        for (int i = 0; i < len; ++i)
            w[i] = 0.7f * std::sin (juce::MathConstants<float>::twoPi * 200.0f * (float) i / 44100.0f);
    }

    auto makeSnapshot = [&] (float pitch, bool reverse)
    {
        auto snap = std::make_unique<EngineSnapshot>();
        TrackPlayInfo t;
        t.buffer = buffer;
        t.sourceSampleRate = 44100.0;
        t.gain = 1.0f;
        t.chops.push_back ({ 0, len, pitch, reverse });
        snap->tracks.push_back (std::move (t));
        snap->activeTrack = 0;
        return snap;
    };

    auto renderMagnitude = [] (SamplerEngine& engine, SnapshotExchange& ex)
    {
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer midi;
        SamplerEngine::Params p;                 // 0 dB, unity, choke on
        float mag = 0.0f;
        for (int block = 0; block < 4; ++block)  // let fades/gain settle
        {
            out.clear();
            engine.process (out, midi, ex.acquire(), p);
            mag = juce::jmax (mag, out.getMagnitude (0, out.getNumSamples()));
        }
        return mag;
    };

    // Silence before any trigger.
    {
        SnapshotExchange ex;
        ex.publish (makeSnapshot (0.0f, false));
        SamplerEngine engine;
        engine.prepare (44100.0, 512);
        CHECK (renderMagnitude (engine, ex) < 1.0e-6f);
    }

    // Forward playback produces sound.
    {
        SnapshotExchange ex;
        ex.publish (makeSnapshot (0.0f, false));
        SamplerEngine engine;
        engine.prepare (44100.0, 512);
        engine.triggerFromUI (0, 0, true);
        CHECK (renderMagnitude (engine, ex) > 0.1f);
    }

    // Reverse playback also produces sound (same energy, different direction).
    {
        SnapshotExchange ex;
        ex.publish (makeSnapshot (0.0f, true));
        SamplerEngine engine;
        engine.prepare (44100.0, 512);
        engine.triggerFromUI (0, 0, true);
        CHECK (renderMagnitude (engine, ex) > 0.1f);
    }

    // Pitching up an octave (2x rate) still plays and stays bounded (no NaNs).
    {
        SnapshotExchange ex;
        ex.publish (makeSnapshot (12.0f, false));
        SamplerEngine engine;
        engine.prepare (44100.0, 512);
        engine.triggerFromUI (0, 0, true);
        const float mag = renderMagnitude (engine, ex);
        CHECK (mag > 0.1f && mag < 2.0f);
    }

    // A pad flash was recorded for the message thread.
    {
        SnapshotExchange ex;
        ex.publish (makeSnapshot (0.0f, false));
        SamplerEngine engine;
        engine.prepare (44100.0, 512);
        engine.triggerFromUI (0, 0, true);
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer midi;
        SamplerEngine::Params p;
        engine.process (out, midi, ex.acquire(), p);
        auto flashes = engine.drainFlashes();
        CHECK (! flashes.empty());
        if (! flashes.empty())
            CHECK (flashes[0].first == 0 && flashes[0].second == 0);
    }
}

// ---- groove timing (swing + quantize) -----------------------------------
void testGrooveTiming()
{
    std::printf ("testGrooveTiming\n");
    using namespace pablo;

    const double sr = 44100.0, bpm = 120.0;
    const double spq = 60.0 / bpm * sr;               // 22050 samples / quarter
    const int cap = 1'000'000;

    // Grid index -> divisions.
    CHECK (groove::gridDivisionsForIndex (0) == 4);
    CHECK (groove::gridDivisionsForIndex (1) == 8);
    CHECK (groove::gridDivisionsForIndex (2) == 16);
    CHECK (groove::gridDivisionsForIndex (3) == 32);

    // Straight (no swing, no quantize) returns the arrival offset unchanged.
    CHECK (groove::applyGroove (100, 0.0, bpm, sr, 0.0f, false, 16, cap) == 100);

    // Invalid tempo bypasses grooving entirely.
    CHECK (groove::applyGroove (77, 0.0, 0.0, sr, 1.0f, true, 16, cap) == 77);

    // Quantize snaps a note near the downbeat back onto the grid line at 0.
    CHECK (groove::applyGroove (200, 0.0, bpm, sr, 0.0f, true, 16, cap) == 0);

    // Quantize snaps a note near the first 1/16 (5512.5 samples) onto it.
    {
        const int off = groove::applyGroove (5000, 0.0, bpm, sr, 0.0f, true, 16, cap);
        CHECK (std::abs (off - (int) std::llround (0.25 * spq)) <= 1);
    }

    // Swing delays the odd (off-beat) 1/16 cell; full swing adds 0.5*step.
    {
        const int straight = groove::applyGroove (5000, 0.0, bpm, sr, 0.0f, true, 16, cap);
        const int swung    = groove::applyGroove (5000, 0.0, bpm, sr, 1.0f, true, 16, cap);
        const int expected = (int) std::llround ((0.25 + 0.5 * 0.25) * spq);
        CHECK (swung > straight);
        CHECK (std::abs (swung - expected) <= 1);
    }

    // Even (on-beat) cells are left alone by swing.
    {
        const int even = groove::applyGroove (11000, 0.0, bpm, sr, 1.0f, true, 16, cap);
        CHECK (std::abs (even - (int) std::llround (0.5 * spq)) <= 1);
    }

    // The clamp ceiling is honoured.
    CHECK (groove::applyGroove (5000, 0.0, bpm, sr, 1.0f, true, 16, 512) == 512);
}

// ---- velocity response + swing scheduler carry-over ---------------------
void testVelocityAndScheduler()
{
    std::printf ("testVelocityAndScheduler\n");
    using namespace pablo;

    const int len = 4096;
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (2, len);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* w = buffer->getWritePointer (ch);
        for (int i = 0; i < len; ++i)
            w[i] = 0.7f * std::sin (juce::MathConstants<float>::twoPi * 200.0f * (float) i / 44100.0f);
    }

    auto makeSnap = [&] (float velSens)
    {
        auto snap = std::make_unique<EngineSnapshot>();
        TrackPlayInfo t;
        t.buffer = buffer;
        t.sourceSampleRate = 44100.0;
        t.gain = 1.0f;
        ChopPlayInfo c;
        c.start = 0; c.end = len; c.velSens = velSens;
        t.chops.push_back (c);
        snap->tracks.push_back (std::move (t));
        snap->activeTrack = 0;
        return snap;
    };

    // Trigger chop 0 via a MIDI note (baseNote 60) at a given velocity; no host
    // transport, so it fires immediately.
    auto midiMag = [] (float velSens, float velocity, std::shared_ptr<juce::AudioBuffer<float>> buf, int len_)
    {
        SnapshotExchange ex;
        {
            auto snap = std::make_unique<EngineSnapshot>();
            TrackPlayInfo t; t.buffer = buf; t.sourceSampleRate = 44100.0; t.gain = 1.0f;
            ChopPlayInfo c; c.start = 0; c.end = len_; c.velSens = velSens; t.chops.push_back (c);
            snap->tracks.push_back (std::move (t)); snap->activeTrack = 0;
            ex.publish (std::move (snap));
        }
        SamplerEngine engine; engine.prepare (44100.0, 512);
        SamplerEngine::Params p; p.baseNote = 60;
        juce::AudioBuffer<float> out (2, 512);
        float mag = 0.0f;
        for (int block = 0; block < 4; ++block)
        {
            out.clear();
            juce::MidiBuffer midi;
            if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, velocity), 0);
            engine.process (out, midi, ex.acquire(), p);
            mag = juce::jmax (mag, out.getMagnitude (0, out.getNumSamples()));
        }
        return mag;
    };

    // Full sensitivity: a soft hit is clearly quieter than a hard hit.
    {
        const float hard = midiMag (1.0f, 1.0f, buffer, len);
        const float soft = midiMag (1.0f, 0.25f, buffer, len);
        CHECK (hard > 0.1f);
        CHECK (soft < hard * 0.6f);
    }

    // Zero sensitivity: velocity is ignored, soft == hard.
    {
        const float hard = midiMag (0.0f, 1.0f, buffer, len);
        const float soft = midiMag (0.0f, 0.25f, buffer, len);
        CHECK (std::abs (hard - soft) < 0.02f);
    }

    // Per-chop volume scales the output (half gain ~ half magnitude).
    {
        auto makeGainSnap = [&] (float g)
        {
            auto snap = std::make_unique<EngineSnapshot>();
            TrackPlayInfo t; t.buffer = buffer; t.sourceSampleRate = 44100.0; t.gain = 1.0f;
            ChopPlayInfo c; c.start = 0; c.end = len; c.gain = g; t.chops.push_back (c);
            snap->tracks.push_back (std::move (t)); snap->activeTrack = 0;
            return snap;
        };
        auto mag = [] (std::unique_ptr<EngineSnapshot> snap)
        {
            SnapshotExchange ex; ex.publish (std::move (snap));
            SamplerEngine engine; engine.prepare (44100.0, 512);
            engine.triggerFromUI (0, 0, true);
            juce::AudioBuffer<float> out (2, 512);
            float m = 0.0f;
            SamplerEngine::Params p;
            for (int b = 0; b < 4; ++b) { out.clear(); engine.process (out, {}, ex.acquire(), p); m = juce::jmax (m, out.getMagnitude (0, out.getNumSamples())); }
            return m;
        };
        const float full = mag (makeGainSnap (1.0f));
        const float half = mag (makeGainSnap (0.5f));
        CHECK (full > 0.1f);
        CHECK (std::abs (half - full * 0.5f) < full * 0.1f);
    }

    // Swing defers an off-beat note past the block boundary; the scheduler must
    // carry it and fire it in a later block.
    {
        SnapshotExchange ex; ex.publish (makeSnap (1.0f));
        SamplerEngine engine; engine.prepare (44100.0, 512);
        SamplerEngine::Params p; p.baseNote = 60; p.swing = 1.0f; p.gridDivisions = 16;
        SamplerEngine::TransportInfo tr; tr.bpm = 120.0; tr.valid = true; tr.isPlaying = true; tr.ppqPosition = 0.25;

        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer midi; midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        out.clear();
        engine.process (out, midi, ex.acquire(), p, tr);
        CHECK (engine.drainFlashes().empty());        // deferred, not fired in block 0

        const double blockPpq = 512.0 / (60.0 / 120.0 * 44100.0);
        bool fired = false;
        for (int block = 1; block < 16 && ! fired; ++block)
        {
            out.clear();
            juce::MidiBuffer empty;
            SamplerEngine::TransportInfo tr2 = tr;
            tr2.ppqPosition = 0.25 + block * blockPpq;
            engine.process (out, empty, ex.acquire(), p, tr2);
            if (! engine.drainFlashes().empty())
                fired = true;
        }
        CHECK (fired);
    }
}

// ---- carve filter mapping + slice-to-grid -------------------------------
void testCarveAndSlice()
{
    std::printf ("testCarveAndSlice\n");
    using namespace pablo;

    // Carve knob -> filter setting.
    CHECK (carveToFilter (0.0f).mode == 0);              // centre = off
    CHECK (carveToFilter (0.02f).mode == 0);             // dead-zone = off
    {
        const auto hp = carveToFilter (-1.0f);
        CHECK (hp.mode == 2);                            // full left = high-pass
        CHECK (std::abs (hp.cutoff - 1200.0f) < 5.0f);
    }
    {
        const auto lp = carveToFilter (1.0f);
        CHECK (lp.mode == 1);                            // full right = low-pass
        CHECK (std::abs (lp.cutoff - 250.0f) < 5.0f);
    }
    // Monotonic: more left = higher HP cutoff; more right = lower LP cutoff.
    CHECK (carveToFilter (-1.0f).cutoff > carveToFilter (-0.5f).cutoff);
    CHECK (carveToFilter (1.0f).cutoff  < carveToFilter (0.5f).cutoff);

    // Slice-to-grid: 4 beats at 120 BPM / 44.1k = one chop per beat.
    {
        SnapshotExchange ex;
        SessionState session (ex);
        const int len = 88200;                           // 4 beats @ 120 BPM
        auto b = std::make_unique<juce::AudioBuffer<float>> (1, len);
        b->clear();
        auto track = session.addTrackFromBuffer ("grid", std::move (b), 44100.0);

        track.sliceByBeats (120.0, 44100.0, 1.0, nullptr);   // per beat
        CHECK (track.getNumChops() == 4);
        CHECK (track.getChopStart (0) == 0);
        CHECK (std::abs ((long) (track.getChopStart (1) - 22050)) <= 1);

        track.sliceByBeats (120.0, 44100.0, 0.5, nullptr);   // 1/8 = 8 chops
        CHECK (track.getNumChops() == 8);

        // Degenerate inputs are ignored (no throw, chops unchanged count-wise).
        const int before = track.getNumChops();
        track.sliceByBeats (0.0, 44100.0, 1.0, nullptr);
        CHECK (track.getNumChops() == before);
    }
}

// ---- time-stretch (pitch-preserving) ------------------------------------
void testTimeStretch()
{
    std::printf ("testTimeStretch\n");
    using namespace pablo;

    const double sr = 44100.0;
    const int len = 22050;                       // 0.5 s
    juce::AudioBuffer<float> in (1, len);
    {
        auto* w = in.getWritePointer (0);
        for (int i = 0; i < len; ++i)
            w[i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) sr);
    }

    auto freqHz = [sr] (const juce::AudioBuffer<float>& b)
    {
        int crossings = 0;
        const auto* d = b.getReadPointer (0);
        for (int i = 1; i < b.getNumSamples(); ++i)
            if ((d[i - 1] <= 0.0f) != (d[i] <= 0.0f)) ++crossings;
        return (double) crossings / 2.0 / ((double) b.getNumSamples() / sr);
    };

    auto s2 = timeStretchRegion (in, 0, len, 2.0, 0.0f, sr);
#if PABLO_ENABLE_STRETCH
    CHECK (s2 != nullptr);
    if (s2 != nullptr)
    {
        CHECK (std::abs (s2->getNumSamples() - 2 * len) < len / 8);      // ~2x length
        const float mag = s2->getMagnitude (0, s2->getNumSamples());
        CHECK (mag > 0.05f && std::isfinite (mag));                      // non-silent, bounded
        CHECK (std::abs (freqHz (*s2) - 220.0) < 40.0);                  // pitch preserved
    }

    auto sUp = timeStretchRegion (in, 0, len, 1.0, 12.0f, sr);           // +1 octave, same length
    if (sUp != nullptr)
        CHECK (freqHz (*sUp) > freqHz (in) * 1.5);                       // clearly higher
#else
    CHECK (s2 == nullptr);
#endif
}

// ---- track removal keeps the right track active -------------------------
void testRemoveTrackActiveIndex()
{
    std::printf ("testRemoveTrackActiveIndex\n");
    using namespace pablo;

    SnapshotExchange ex;
    SessionState session (ex);

    auto add = [&] (const char* name)
    {
        auto b = std::make_unique<juce::AudioBuffer<float>> (1, 1000);
        b->clear();
        session.addTrackFromBuffer (name, std::move (b), 44100.0);
    };
    add ("A"); add ("B"); add ("C");
    CHECK (session.getNumTracks() == 3);

    session.setActiveTrackIndex (2);                 // C
    session.removeTrack (0);                          // remove A
    CHECK (session.getNumTracks() == 2);
    CHECK (session.getActiveTrack().getName() == "C");// still C, now at index 1

    session.removeTrack (1);                          // remove active (C) from [B,C]
    CHECK (session.getNumTracks() == 1);
    CHECK (session.getActiveTrack().getName() == "B");// only B remains, now active
}

// ---- restore clears audio from a prior session --------------------------
void testRestoreClearsStore()
{
    std::printf ("testRestoreClearsStore\n");
    using namespace pablo;

    SnapshotExchange ex;
    SessionState session (ex);

    auto b = std::make_unique<juce::AudioBuffer<float>> (1, 2000);
    for (int i = 0; i < 2000; ++i) b->setSample (0, i, 0.5f);
    auto trackA = session.addTrackFromBuffer ("A", std::move (b), 44100.0);
    const int uidA = trackA.getUid();
    CHECK (session.getTrackBuffer (trackA) != nullptr);

    // A saved tree whose single track reuses the same uid but has no
    // recoverable audio (no file, no embedded FLAC).
    juce::ValueTree saved (id::SESSION);
    saved.setProperty (id::activeTrack, 0, nullptr);
    juce::ValueTree tr (id::TRACK);
    tr.setProperty (id::uid, uidA, nullptr);
    tr.setProperty (id::name, "B", nullptr);
    tr.setProperty (id::sampleRate, 44100.0, nullptr);
    tr.setProperty (id::lengthSamples, (juce::int64) 2000, nullptr);
    tr.setProperty (id::filePath, "/no/such/file.wav", nullptr);
    tr.appendChild (juce::ValueTree (id::CHOPS), nullptr);
    saved.appendChild (tr, nullptr);

    session.restoreFromSaveTree (saved);
    CHECK (session.getNumTracks() == 1);
    auto restored = session.getTrack (0);
    CHECK (restored.getName() == "B");
    // Must be "missing", NOT serving track A's stale audio for the reused uid.
    CHECK (session.getTrackBuffer (restored) == nullptr);
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
    testOverlappingChops();
    testTransientDetector();
    testResampler();
    testOverlapAddWeighting();
    testSnapshotExchange();
    testEnginePlayback();
    testGrooveTiming();
    testVelocityAndScheduler();
    testCarveAndSlice();
    testTimeStretch();
    testRemoveTrackActiveIndex();
    testRestoreClearsStore();
    testSessionRoundTrip();

    std::printf ("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
