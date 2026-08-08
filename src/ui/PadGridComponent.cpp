#include "PadGridComponent.h"

namespace pablo
{
using namespace theme;

PadGridComponent::PadGridComponent (SessionState& s, SamplerEngine& e) : session (s), engine (e)
{
    startTimerHz (30);
}

void PadGridComponent::refresh()
{
    const auto track = session.getActiveTrack();
    const int numChops = track.isValid() ? track.getNumChops() : 0;
    if (page * 16 >= juce::jmax (1, numChops))
        page = 0;
    repaint();
}

void PadGridComponent::timerCallback()
{
    bool dirty = false;
    for (auto& [trackIdx, chopIdx] : engine.drainFlashes())
    {
        juce::ignoreUnused (trackIdx);
        if (chopIdx >= 0 && chopIdx < 32)
        {
            flash[chopIdx] = 1.0f;
            dirty = true;
        }
    }
    for (auto& f : flash)
    {
        if (f > 0.003f) { f *= 0.82f; dirty = true; }
        else f = 0.0f;
    }
    if (dirty)
        repaint();
}

juce::Rectangle<float> PadGridComponent::padBounds (int padOnPage) const
{
    auto area = getLocalBounds().toFloat().reduced (2.0f).withTrimmedTop (18.0f);
    const float w = area.getWidth() / 4.0f, h = area.getHeight() / 4.0f;
    const int col = padOnPage % 4;
    const int row = 3 - padOnPage / 4;      // pad 1 bottom-left, like an MPC
    return { area.getX() + col * w, area.getY() + row * h, w, h };
}

int PadGridComponent::padAt (juce::Point<float> pos) const
{
    for (int i = 0; i < 16; ++i)
        if (padBounds (i).reduced (2.0f).contains (pos))
            return i;
    return -1;
}

void PadGridComponent::paint (juce::Graphics& g)
{
    const auto track = session.getActiveTrack();
    const int numChops = track.isValid() ? track.getNumChops() : 0;
    const int numPages = juce::jmax (1, (numChops + 15) / 16);
    page = juce::jlimit (0, numPages - 1, page);

    g.setColour (ink);
    g.setFont (markerFont (15.0f));
    auto header = getLocalBounds().reduced (2).removeFromTop (18);
    g.drawText ("PADS", header, juce::Justification::centredLeft);
    if (numPages > 1)
        g.drawText ("BANK " + juce::String (page + 1) + "/" + juce::String (numPages) + "  [-/+]",
                    header, juce::Justification::centredRight);

    for (int i = 0; i < 16; ++i)
    {
        const int chop = page * 16 + i;
        const bool padExists = chop < numChops;
        const float glow = (padExists && flash[chop] > 0.0f) ? flash[chop] : 0.0f;
        // A struck pad pops outward slightly and brightens — "it's alive".
        const auto r = padBounds (i).reduced (3.0f - glow * 2.0f);

        g.setColour (ink);
        g.fillRect (r.translated (2.0f, 2.0f));
        auto fill = padExists ? pink : pink.withAlpha (0.35f);
        if (glow > 0.0f)
            fill = fill.interpolatedWith (cream.brighter (0.4f), juce::jmin (1.0f, glow * 1.2f));
        if (chop == heldPad)
            fill = ink;
        g.setColour (fill);
        g.fillRect (r);
        if (glow > 0.0f)
        {
            // A brief bright rim on trigger.
            g.setColour (cream.withAlpha (glow * 0.8f));
            g.drawRect (r, 1.0f + glow * 2.0f);
        }
        g.setColour (ink);
        g.drawRect (r, 1.2f);

        if (padExists)
        {
            g.setColour (chop == heldPad ? cream : ink);
            g.setFont (markerFont (juce::jmin (20.0f, r.getHeight() * 0.42f)));
            g.drawText (juce::String (chop + 1), r.reduced (5.0f, 3.0f), juce::Justification::topLeft);
            g.setFont (monoFont (11.0f));
            g.drawText (keymap::keyForPadIndex (chop), r.reduced (5.0f, 3.0f),
                        juce::Justification::bottomRight);
            if (track.getChopReverse (chop))
                g.drawText ("REV", r.reduced (5.0f, 3.0f), juce::Justification::bottomLeft);
        }
    }
}

void PadGridComponent::mouseDown (const juce::MouseEvent& e)
{
    // Header click cycles banks.
    if (e.position.y < 20.0f)
    {
        const auto track = session.getActiveTrack();
        const int numPages = juce::jmax (1, ((track.isValid() ? track.getNumChops() : 0) + 15) / 16);
        if (numPages > 1)
        {
            page = (page + 1) % numPages;
            repaint();
        }
        return;
    }

    const int pad = padAt (e.position);
    if (pad < 0)
        return;

    const int chop = page * 16 + pad;
    const auto track = session.getActiveTrack();
    if (! track.isValid() || chop >= track.getNumChops())
        return;

    heldPad = chop;
    engine.triggerFromUI (-1, chop, true);
    if (onChopSelected)
        onChopSelected (chop);
    repaint();
}

void PadGridComponent::mouseUp (const juce::MouseEvent&)
{
    if (heldPad >= 0)
    {
        engine.triggerFromUI (-1, heldPad, false);
        heldPad = -1;
        repaint();
    }
}
} // namespace pablo
