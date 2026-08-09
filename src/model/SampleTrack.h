#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "../Identifiers.h"

namespace pablo
{
// Thin typed facade over a TRACK ValueTree. Chops are kept sorted by
// startSample; a chop's end is the next chop's start (or the sample length),
// so slices always tile the sample like an MPC slice grid.
class SampleTrack
{
public:
    SampleTrack() = default;
    explicit SampleTrack (juce::ValueTree t) : tree (std::move (t)) {}

    bool isValid() const { return tree.hasType (id::TRACK); }
    juce::ValueTree getTree() const { return tree; }

    int getUid() const                  { return tree[id::uid]; }
    juce::String getName() const        { return tree[id::name].toString(); }
    juce::String getFilePath() const    { return tree[id::filePath].toString(); }
    double getSampleRate() const        { return (double) tree.getProperty (id::sampleRate, 44100.0); }
    juce::int64 getLengthSamples() const{ return (juce::int64) tree.getProperty (id::lengthSamples, 0); }
    float getGain() const               { return (float) tree.getProperty (id::gain, 1.0f); }
    // One-knob tone control: -1 = high-pass (carve lows) .. 0 = off .. +1 = low-pass.
    float getFilterCarve() const        { return (float) tree.getProperty (id::filterCarve, 0.0f); }

    void setName (const juce::String& n, juce::UndoManager* um) { tree.setProperty (id::name, n, um); }
    void setFilterCarve (float c, juce::UndoManager* um) { tree.setProperty (id::filterCarve, juce::jlimit (-1.0f, 1.0f, c), um); }

    // ---- chops ----------------------------------------------------------
    juce::ValueTree getChopsTree() const { return tree.getChildWithName (id::CHOPS); }
    int getNumChops() const              { return getChopsTree().getNumChildren(); }
    juce::ValueTree getChop (int i) const{ return getChopsTree().getChild (i); }

    juce::int64 getChopStart (int i) const { return (juce::int64) getChop (i).getProperty (id::startSample, 0); }

    // A chop's end is explicit when it carries an endSample property (free /
    // possibly overlapping region). Otherwise it tiles: end = next chop's start.
    juce::int64 getChopEnd (int i) const
    {
        auto c = getChop (i);
        if (c.hasProperty (id::endSample))
            return juce::jlimit<juce::int64> (getChopStart (i) + 1, getLengthSamples(),
                                              (juce::int64) c.getProperty (id::endSample));
        if (i + 1 < getNumChops())
            return getChopStart (i + 1);
        return getLengthSamples();
    }

    float getChopPitch (int i) const   { return (float) getChop (i).getProperty (id::pitchSemis, 0.0f); }
    bool  getChopReverse (int i) const { return (bool) getChop (i).getProperty (id::reverse, false); }
    // How strongly this chop responds to MIDI velocity (1 = full, 0 = ignore).
    float getChopVelSens (int i) const { return (float) getChop (i).getProperty (id::velSens, 1.0f); }
    // Pitch-preserving time-stretch length multiplier (1 = off, >1 slower/longer).
    float getChopStretch (int i) const { return (float) getChop (i).getProperty (id::stretchRatio, 1.0f); }
    // Per-chop volume, linear (1 = 100%).
    float getChopGain (int i) const    { return (float) getChop (i).getProperty (id::chopGain, 1.0f); }

    void setChopPitch (int i, float semis, juce::UndoManager* um)  { getChop (i).setProperty (id::pitchSemis, semis, um); }
    void setChopReverse (int i, bool rev, juce::UndoManager* um)   { getChop (i).setProperty (id::reverse, rev, um); }
    void setChopVelSens (int i, float s, juce::UndoManager* um)    { getChop (i).setProperty (id::velSens, juce::jlimit (0.0f, 1.0f, s), um); }
    void setChopStretch (int i, float r, juce::UndoManager* um)    { getChop (i).setProperty (id::stretchRatio, juce::jlimit (0.5f, 2.0f, r), um); }
    void setChopGain (int i, float g, juce::UndoManager* um)       { getChop (i).setProperty (id::chopGain, juce::jlimit (0.0f, 2.0f, g), um); }

    int findChopContaining (juce::int64 sample) const
    {
        for (int i = getNumChops(); --i >= 0;)
            if (getChopStart (i) <= sample)
                return i;
        return -1;
    }

    // Freezes any tiling chop's implicit end into an explicit endSample, so that
    // inserting a free / overlapping region can't shift a neighbour's end. Ends
    // depend only on starts (unchanged here), so a single pass is safe.
    void freezeImplicitEnds (juce::UndoManager* um)
    {
        const int n = getNumChops();
        std::vector<juce::int64> ends ((size_t) n);
        for (int i = 0; i < n; ++i)
            ends[(size_t) i] = getChopEnd (i);
        for (int i = 0; i < n; ++i)
            if (! getChop (i).hasProperty (id::endSample))
                getChop (i).setProperty (id::endSample, ends[(size_t) i], um);
    }

    // Adds a free chop covering [start, end). Regions may overlap existing chops
    // (an MPC-style pad over a slice you've already made). Returns its index.
    int addChopRange (juce::int64 start, juce::int64 end, juce::UndoManager* um)
    {
        const auto len = getLengthSamples();
        if (len <= 0) return -1;
        start = juce::jlimit<juce::int64> (0, len - 1, start);
        end   = juce::jlimit<juce::int64> (start + 1, len, end);

        freezeImplicitEnds (um);

        auto chops = getChopsTree();
        int insertAt = chops.getNumChildren();
        for (int i = 0; i < chops.getNumChildren(); ++i)
            if (getChopStart (i) > start) { insertAt = i; break; }

        juce::ValueTree chop (id::CHOP);
        chop.setProperty (id::startSample, start, nullptr);
        chop.setProperty (id::endSample, end, nullptr);
        chop.setProperty (id::pitchSemis, 0.0f, nullptr);
        chop.setProperty (id::reverse, false, nullptr);
        chops.addChild (chop, insertAt, um);
        return insertAt;
    }

    // Double-click: add a boundary at 'sample'. Splits the chop under it into
    // two explicit ranges (or fills a gap). Returns the new chop's index.
    int addChopAt (juce::int64 sample, juce::UndoManager* um)
    {
        const auto len = getLengthSamples();
        if (sample <= 0 || sample >= len)
            return -1;

        freezeImplicitEnds (um);

        int container = -1;
        for (int i = 0; i < getNumChops(); ++i)
        {
            if (getChopStart (i) == sample) return -1;   // boundary already here
            if (getChopStart (i) <= sample && sample < getChopEnd (i))
                container = i;                            // topmost containing chop
        }

        if (container >= 0)
        {
            const auto cEnd = getChopEnd (container);
            getChop (container).setProperty (id::endSample, sample, um);
            return addChopRange (sample, cEnd, um);
        }

        juce::int64 e = len;
        for (int i = 0; i < getNumChops(); ++i)
            if (getChopStart (i) > sample) { e = getChopStart (i); break; }
        return addChopRange (sample, e, um);
    }

    void removeChop (int i, juce::UndoManager* um)
    {
        auto chops = getChopsTree();
        if (i < 0 || i >= chops.getNumChildren())
            return;
        // If the previous chop tiled straight into this one, heal the hole so a
        // deleted slice doesn't leave a silent gap.
        if (i > 0 && getChopEnd (i - 1) == getChopStart (i))
            getChop (i - 1).setProperty (id::endSample, getChopEnd (i), um);
        chops.removeChild (i, um);
    }

    // Drags chop i's start marker. A shared (tiling) boundary drags both sides;
    // a free region's start moves on its own.
    void moveChopStart (int i, juce::int64 newStart, juce::UndoManager* um)
    {
        if (i < 0 || i >= getNumChops())
            return;
        const juce::int64 lo = (i > 0 ? getChopStart (i - 1) + 1 : 0);
        const juce::int64 hi = getChopEnd (i) - 1;
        newStart = juce::jlimit (lo, hi, newStart);

        const bool sharedWithPrev = (i > 0 && getChopEnd (i - 1) == getChopStart (i));
        getChop (i).setProperty (id::startSample, newStart, um);
        if (sharedWithPrev)
            getChop (i - 1).setProperty (id::endSample, newStart, um);
    }

    void clearChops (juce::UndoManager* um) { getChopsTree().removeAllChildren (um); }

    // Lays out contiguous tiling slices from a list of starts, each with an
    // explicit end (= next start, or the sample length for the last).
    void setChopStarts (const std::vector<juce::int64>& starts, juce::UndoManager* um)
    {
        clearChops (um);
        auto chops = getChopsTree();
        const auto len = getLengthSamples();
        for (size_t k = 0; k < starts.size(); ++k)
        {
            const juce::int64 s = starts[k];
            const juce::int64 e = (k + 1 < starts.size()) ? starts[k + 1] : len;
            if (e <= s) continue;
            juce::ValueTree chop (id::CHOP);
            chop.setProperty (id::startSample, s, nullptr);
            chop.setProperty (id::endSample, e, nullptr);
            chop.setProperty (id::pitchSemis, 0.0f, nullptr);
            chop.setProperty (id::reverse, false, nullptr);
            chops.addChild (chop, -1, um);
        }
    }

    void equalSlices (int n, juce::UndoManager* um)
    {
        const auto len = getLengthSamples();
        if (len <= 0 || n <= 0) return;
        std::vector<juce::int64> starts;
        for (int i = 0; i < n; ++i)
            starts.push_back (len * i / n);
        setChopStarts (starts, um);
    }

    // Slice on a musical grid at 'bpm': a chop every 'beatsPerSlice' quarter
    // notes (0.25 = 1/16, 0.5 = 1/8, 1 = beat, 4 = bar). Capped so a tiny grid
    // on a long sample can't create thousands of chops.
    void sliceByBeats (double bpm, double sr, double beatsPerSlice, juce::UndoManager* um)
    {
        const auto len = getLengthSamples();
        if (len <= 0 || bpm <= 0.0 || sr <= 0.0 || beatsPerSlice <= 0.0) return;
        const double interval = (60.0 / bpm) * sr * beatsPerSlice;
        if (interval < 1.0) return;

        std::vector<juce::int64> starts;
        for (double s = 0.0; s < (double) len && starts.size() < 512; s += interval)
            starts.push_back ((juce::int64) s);
        setChopStarts (starts, um);
    }

private:
    juce::ValueTree tree;
};
} // namespace pablo
