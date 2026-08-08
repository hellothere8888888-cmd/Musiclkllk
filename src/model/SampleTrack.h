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

    void setName (const juce::String& n, juce::UndoManager* um) { tree.setProperty (id::name, n, um); }

    // ---- chops ----------------------------------------------------------
    juce::ValueTree getChopsTree() const { return tree.getChildWithName (id::CHOPS); }
    int getNumChops() const              { return getChopsTree().getNumChildren(); }
    juce::ValueTree getChop (int i) const{ return getChopsTree().getChild (i); }

    juce::int64 getChopStart (int i) const { return (juce::int64) getChop (i).getProperty (id::startSample, 0); }

    juce::int64 getChopEnd (int i) const
    {
        if (i + 1 < getNumChops())
            return getChopStart (i + 1);
        return getLengthSamples();
    }

    float getChopPitch (int i) const   { return (float) getChop (i).getProperty (id::pitchSemis, 0.0f); }
    bool  getChopReverse (int i) const { return (bool) getChop (i).getProperty (id::reverse, false); }
    // How strongly this chop responds to MIDI velocity (1 = full, 0 = ignore).
    float getChopVelSens (int i) const { return (float) getChop (i).getProperty (id::velSens, 1.0f); }

    void setChopPitch (int i, float semis, juce::UndoManager* um)  { getChop (i).setProperty (id::pitchSemis, semis, um); }
    void setChopReverse (int i, bool rev, juce::UndoManager* um)   { getChop (i).setProperty (id::reverse, rev, um); }
    void setChopVelSens (int i, float s, juce::UndoManager* um)    { getChop (i).setProperty (id::velSens, juce::jlimit (0.0f, 1.0f, s), um); }

    int findChopContaining (juce::int64 sample) const
    {
        for (int i = getNumChops(); --i >= 0;)
            if (getChopStart (i) <= sample)
                return i;
        return -1;
    }

    // Inserts a new chop boundary, keeping the list sorted. Returns the new
    // chop's index, or -1 if a chop already starts there.
    int addChopAt (juce::int64 sample, juce::UndoManager* um)
    {
        if (sample < 0 || sample >= getLengthSamples())
            return -1;

        auto chops = getChopsTree();
        int insertAt = 0;
        for (int i = 0; i < chops.getNumChildren(); ++i)
        {
            const auto s = getChopStart (i);
            if (s == sample) return -1;
            if (s < sample)  insertAt = i + 1;
        }

        juce::ValueTree chop (id::CHOP);
        chop.setProperty (id::startSample, sample, nullptr);
        chop.setProperty (id::pitchSemis, 0.0f, nullptr);
        chop.setProperty (id::reverse, false, nullptr);
        chops.addChild (chop, insertAt, um);
        return insertAt;
    }

    void removeChop (int i, juce::UndoManager* um)
    {
        auto chops = getChopsTree();
        if (i >= 0 && i < chops.getNumChildren())
            chops.removeChild (i, um);
    }

    // Drags chop i's start marker, clamped strictly between its neighbours.
    void moveChopStart (int i, juce::int64 newStart, juce::UndoManager* um)
    {
        const juce::int64 lo = (i > 0 ? getChopStart (i - 1) + 1 : 0);
        const juce::int64 hi = (i + 1 < getNumChops() ? getChopStart (i + 1) - 1
                                                      : getLengthSamples() - 1);
        getChop (i).setProperty (id::startSample, juce::jlimit (lo, hi, newStart), um);
    }

    void clearChops (juce::UndoManager* um) { getChopsTree().removeAllChildren (um); }

    void setChopStarts (const std::vector<juce::int64>& starts, juce::UndoManager* um)
    {
        clearChops (um);
        for (auto s : starts)
            addChopAt (s, um);
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

private:
    juce::ValueTree tree;
};
} // namespace pablo
