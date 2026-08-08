#include "WaveformView.h"

namespace pablo
{
using namespace theme;

WaveformView::WaveformView (SessionState& s, SamplerEngine& e) : session (s), engine (e)
{
    setWantsKeyboardFocus (false);
    refresh();
    startTimerHz (45);
}

void WaveformView::timerCallback()
{
    // Read the read-heads of any voices playing the active track and repaint
    // only when something is (or just was) sounding, so an idle editor is quiet.
    float pos[32];
    const int n = engine.getActiveVoicePositions (session.getActiveTrackIndex(), pos, 32);
    const bool wasEmpty = playheads.empty();
    playheads.assign (pos, pos + n);
    if (n > 0 || ! wasEmpty)
        repaint (waveArea());
}

void WaveformView::refresh()
{
    track = session.getActiveTrack();
    auto newBuffer = track.isValid() ? session.getTrackBuffer (track) : nullptr;
    const int newUid = track.isValid() ? track.getUid() : -1;

    if (newUid != trackUid || newBuffer != buffer)
    {
        trackUid = newUid;
        buffer = newBuffer;
        rebuildPeaks();
        frameAll();
    }

    if (track.isValid())
        selectedChop = juce::jlimit (0, juce::jmax (0, track.getNumChops() - 1), selectedChop);
    repaint();
}

void WaveformView::setSelectedChop (int index)
{
    selectedChop = index;
    repaint();
}

void WaveformView::rebuildPeaks()
{
    levels.clear();
    if (buffer == nullptr || buffer->getNumSamples() == 0)
        return;

    const int numSamples = buffer->getNumSamples();
    const int numCh = buffer->getNumChannels();

    PeakLevel base;
    base.binSize = 128;
    const int numBins = (numSamples + base.binSize - 1) / base.binSize;
    base.minV.resize ((size_t) numBins, 0.0f);
    base.maxV.resize ((size_t) numBins, 0.0f);

    for (int bin = 0; bin < numBins; ++bin)
    {
        float lo = 1.0e9f, hi = -1.0e9f;
        const int start = bin * base.binSize;
        const int end = juce::jmin (numSamples, start + base.binSize);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* d = buffer->getReadPointer (ch);
            for (int i = start; i < end; ++i)
            {
                lo = juce::jmin (lo, d[i]);
                hi = juce::jmax (hi, d[i]);
            }
        }
        base.minV[(size_t) bin] = lo;
        base.maxV[(size_t) bin] = hi;
    }
    levels.push_back (std::move (base));

    while (levels.back().minV.size() > 512)
    {
        const auto& prev = levels.back();
        PeakLevel next;
        next.binSize = prev.binSize * 2;
        const auto n = (prev.minV.size() + 1) / 2;
        next.minV.resize (n);
        next.maxV.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto j = i * 2, k = juce::jmin (prev.minV.size() - 1, i * 2 + 1);
            next.minV[i] = juce::jmin (prev.minV[j], prev.minV[k]);
            next.maxV[i] = juce::jmax (prev.maxV[j], prev.maxV[k]);
        }
        levels.push_back (std::move (next));
    }
}

void WaveformView::frameAll()
{
    viewStart = 0.0;
    const auto len = buffer != nullptr ? buffer->getNumSamples() : 0;
    spp = juce::jmax (0.01, (double) len / (double) juce::jmax (1, waveArea().getWidth()));
}

void WaveformView::clampView()
{
    if (buffer == nullptr) return;
    const double len = buffer->getNumSamples();
    const double visible = spp * waveArea().getWidth();
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, len - visible), viewStart);
}

juce::Rectangle<int> WaveformView::waveArea() const
{
    return getLocalBounds().reduced (3).withTrimmedBottom (overviewHeight + 4);
}

juce::Rectangle<int> WaveformView::overviewArea() const
{
    return getLocalBounds().reduced (3).removeFromBottom (overviewHeight);
}

double WaveformView::xToSample (float x) const
{
    return viewStart + (double) (x - (float) waveArea().getX()) * spp;
}

float WaveformView::sampleToX (double sample) const
{
    return (float) waveArea().getX() + (float) ((sample - viewStart) / spp);
}

int WaveformView::hitTestMarker (juce::Point<float> pos) const
{
    if (! track.isValid() || ! waveArea().toFloat().contains (pos))
        return -1;
    for (int i = track.getNumChops(); --i >= 0;)
        if (std::abs (sampleToX ((double) track.getChopStart (i)) - pos.x) <= (float) markerHitDistance)
            return i;
    return -1;
}

int WaveformView::chopIndexAt (double sample) const
{
    return track.isValid() ? track.findChopContaining ((juce::int64) sample) : -1;
}

// ---- painting -----------------------------------------------------------

void WaveformView::drawWave (juce::Graphics& g, juce::Rectangle<int> area,
                             double startSample, double samplesPerPixel, juce::Colour colour) const
{
    if (buffer == nullptr || levels.empty())
        return;

    const float midY = (float) area.getCentreY();
    const float halfH = (float) area.getHeight() * 0.48f;
    const int numSamples = buffer->getNumSamples();
    g.setColour (colour);

    if (samplesPerPixel >= 2.0)
    {
        // Pick the finest pyramid level whose bins are not bigger than a pixel.
        const PeakLevel* level = &levels[0];
        for (const auto& l : levels)
            if ((double) l.binSize <= samplesPerPixel)
                level = &l;
            else
                break;

        for (int px = 0; px < area.getWidth(); ++px)
        {
            const double s0 = startSample + px * samplesPerPixel;
            const double s1 = s0 + samplesPerPixel;
            if (s1 <= 0 || s0 >= numSamples) continue;

            const auto b0 = (size_t) juce::jlimit (0.0, (double) level->minV.size() - 1.0, s0 / level->binSize);
            const auto b1 = (size_t) juce::jlimit (0.0, (double) level->minV.size() - 1.0, s1 / level->binSize);
            float lo = 1.0e9f, hi = -1.0e9f;
            for (auto b = b0; b <= b1; ++b)
            {
                lo = juce::jmin (lo, level->minV[b]);
                hi = juce::jmax (hi, level->maxV[b]);
            }
            const float yTop = midY - hi * halfH;
            const float yBot = midY - lo * halfH;
            g.fillRect ((float) area.getX() + (float) px, yTop, 1.0f, juce::jmax (1.0f, yBot - yTop));
        }
    }
    else
    {
        // Sample-accurate polyline (zoomed right in for surgical chops).
        const auto* d = buffer->getReadPointer (0);
        juce::Path p;
        bool started = false;
        for (int px = 0; px <= area.getWidth(); ++px)
        {
            const auto s = (juce::int64) (startSample + px * samplesPerPixel);
            if (s < 0 || s >= numSamples) continue;
            const float x = (float) area.getX() + (float) px;
            const float y = midY - d[s] * halfH;
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else p.lineTo (x, y);
        }
        g.strokePath (p, juce::PathStrokeType (1.6f));

        if (samplesPerPixel < 0.2)
            for (juce::int64 s = (juce::int64) startSample;
                 s < (juce::int64) (startSample + area.getWidth() * samplesPerPixel + 1); ++s)
                if (s >= 0 && s < numSamples)
                    g.fillEllipse (sampleToX ((double) s) - 2.0f, midY - d[s] * halfH - 2.0f, 4.0f, 4.0f);
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Pink block with the offset "misprint" shadow.
    g.setColour (ink);
    g.fillRect (bounds.reduced (1.0f).translated (2.5f, 2.5f));
    g.setColour (pink);
    g.fillRect (bounds.reduced (1.0f));
    g.setColour (ink);
    g.drawRect (bounds.reduced (1.0f), 1.4f);

    const auto area = waveArea();

    if (buffer == nullptr)
    {
        g.setColour (ink);
        g.setFont (markerFont (30.0f));
        g.drawFittedText (track.isValid() ? "SAMPLE MISSING\nWHICH / ONE"
                                          : "DROP A SAMPLE\nOR HIT LOAD\nWHICH / ONE",
                          area, juce::Justification::centred, 4);
        return;
    }

    // Selected slice highlight.
    if (track.isValid() && selectedChop >= 0 && selectedChop < track.getNumChops())
    {
        const float x0 = sampleToX ((double) track.getChopStart (selectedChop));
        const float x1 = sampleToX ((double) track.getChopEnd (selectedChop));
        juce::Rectangle<float> r (juce::jmax (x0, (float) area.getX()), (float) area.getY(),
                                  juce::jmin (x1, (float) area.getRight()) - juce::jmax (x0, (float) area.getX()),
                                  (float) area.getHeight());
        if (r.getWidth() > 0)
        {
            g.setColour (cream.withAlpha (0.75f));
            g.fillRect (r);
        }
    }

    drawWave (g, area, viewStart, spp, ink);

    // Moving playheads for voices sounding on this track.
    for (const float p : playheads)
    {
        const float x = sampleToX ((double) p);
        if (x >= (float) area.getX() && x <= (float) area.getRight())
        {
            g.setColour (cream.withAlpha (0.5f));
            g.fillRect (x - 1.5f, (float) area.getY(), 3.0f, (float) area.getHeight());
            g.setColour (juce::Colours::white);
            g.fillRect (x - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
        }
    }

    // Chop markers + flags.
    if (track.isValid())
    {
        for (int i = 0; i < track.getNumChops(); ++i)
        {
            const float x = sampleToX ((double) track.getChopStart (i));
            if (x < (float) area.getX() - 20 || x > (float) area.getRight() + 20)
                continue;

            const bool isSelected = i == selectedChop;
            g.setColour (ink);
            g.fillRect (x - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight());

            // Numbered flag.
            juce::Rectangle<float> flag (x, (float) area.getY(), 26.0f, 18.0f);
            g.setColour (isSelected ? ink : cream);
            g.fillRect (flag);
            g.setColour (isSelected ? cream : ink);
            g.drawRect (flag, 1.0f);
            g.setFont (monoFont (12.0f));

            juce::String label (i + 1);
            if (track.getChopReverse (i)) label = "<" + label;
            const auto pitch = track.getChopPitch (i);
            if (std::abs (pitch) > 0.01f)
                label += (pitch > 0 ? "+" : "") + juce::String (pitch, std::abs (pitch - std::round (pitch)) > 0.01f ? 1 : 0);
            g.drawFittedText (label, flag.toNearestInt().reduced (2, 1), juce::Justification::centred, 1);
        }
    }

    // Overview strip.
    const auto ov = overviewArea();
    g.setColour (creamDim);
    g.fillRect (ov);
    g.setColour (ink);
    g.drawRect (ov.toFloat(), 1.0f);

    const double len = (double) buffer->getNumSamples();
    drawWave (g, ov.reduced (2), 0.0, len / juce::jmax (1, ov.getWidth() - 4), ink.withAlpha (0.55f));

    const float wx0 = (float) ov.getX() + (float) (viewStart / len) * (float) ov.getWidth();
    const float wx1 = (float) ov.getX() + (float) ((viewStart + spp * waveArea().getWidth()) / len) * (float) ov.getWidth();
    g.setColour (ink.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (wx0, (float) ov.getY(),
                                        juce::jmax (4.0f, wx1 - wx0), (float) ov.getHeight()));
}

void WaveformView::resized()
{
    clampView();
}

// ---- interaction --------------------------------------------------------

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (buffer == nullptr || ! track.isValid())
        return;

    if (e.mods.isPopupMenu())
    {
        const int marker = hitTestMarker (e.position);
        const int chop = marker >= 0 ? marker : chopIndexAt (xToSample (e.position.x));
        if (chop >= 0)
        {
            selectedChop = chop;
            if (onChopSelected) onChopSelected (chop);
            showChopMenu (chop, e.getScreenPosition());
        }
        return;
    }

    if (overviewArea().contains (e.position.toInt()))
    {
        dragMode = Drag::overview;
        mouseDrag (e);
        return;
    }

    const int marker = hitTestMarker (e.position);
    if (marker > 0)   // chop 0's start stays put; slices always begin somewhere
    {
        dragMode = Drag::marker;
        dragMarkerIndex = marker;
        session.getUndoManager().beginNewTransaction ("Move chop");
        return;
    }

    dragMode = Drag::pan;
    dragAnchorViewStart = viewStart;
    dragAnchorPos = e.position;
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (buffer == nullptr)
        return;

    switch (dragMode)
    {
        case Drag::marker:
            if (track.isValid() && dragMarkerIndex >= 0)
            {
                track.moveChopStart (dragMarkerIndex, (juce::int64) xToSample (e.position.x),
                                     &session.getUndoManager());
                repaint();
            }
            break;

        case Drag::pan:
            viewStart = dragAnchorViewStart - (double) (e.position.x - dragAnchorPos.x) * spp;
            clampView();
            repaint();
            break;

        case Drag::overview:
        {
            const auto ov = overviewArea();
            const double len = (double) buffer->getNumSamples();
            const double centre = ((double) e.position.x - ov.getX()) / juce::jmax (1, ov.getWidth()) * len;
            viewStart = centre - spp * waveArea().getWidth() * 0.5;
            clampView();
            repaint();
            break;
        }

        case Drag::none:
            break;
    }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (dragMode == Drag::pan && e.mouseWasClicked() && buffer != nullptr && track.isValid())
    {
        // Plain click: select + audition the slice under the cursor.
        const int chop = chopIndexAt (xToSample (e.position.x));
        if (chop >= 0 && waveArea().contains (e.position.toInt()))
        {
            selectedChop = chop;
            if (onChopSelected) onChopSelected (chop);
            engine.triggerFromUI (-1, chop, true);
            repaint();
        }
    }
    dragMode = Drag::none;
    dragMarkerIndex = -1;
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const int marker = hitTestMarker (e.position);
    setMouseCursor (marker > 0 ? juce::MouseCursor::LeftRightResizeCursor
                               : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (buffer == nullptr || ! track.isValid() || ! waveArea().contains (e.position.toInt()))
        return;

    session.getUndoManager().beginNewTransaction ("Add chop");
    const int added = track.addChopAt ((juce::int64) xToSample (e.position.x),
                                       &session.getUndoManager());
    if (added >= 0)
    {
        selectedChop = added;
        if (onChopSelected) onChopSelected (added);
    }
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (buffer == nullptr)
        return;

    const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;

    if (e.mods.isShiftDown())
    {
        viewStart -= (double) delta * spp * 120.0;
        clampView();
    }
    else
    {
        const double sampleUnderMouse = xToSample (e.position.x);
        const double zoomFactor = std::pow (1.25, (double) -delta * 3.0);
        const double maxSpp = (double) buffer->getNumSamples() / juce::jmax (1, waveArea().getWidth());
        spp = juce::jlimit (0.02, juce::jmax (0.02, maxSpp), spp * zoomFactor);
        viewStart = sampleUnderMouse - (double) (e.position.x - (float) waveArea().getX()) * spp;
        clampView();
    }
    repaint();
}

void WaveformView::showChopMenu (int chopIndex, juce::Point<int>)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Reverse chop", true, track.getChopReverse (chopIndex));
    menu.addItem (2, "Reset pitch");
    menu.addSeparator();
    menu.addItem (3, "Delete chop", chopIndex > 0 || track.getNumChops() > 1);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, chopIndex, safeThis = juce::Component::SafePointer<WaveformView> (this)] (int result)
    {
        if (safeThis == nullptr || ! track.isValid() || chopIndex >= track.getNumChops())
            return;
        auto& um = session.getUndoManager();
        um.beginNewTransaction();
        switch (result)
        {
            case 1: track.setChopReverse (chopIndex, ! track.getChopReverse (chopIndex), &um); break;
            case 2: track.setChopPitch (chopIndex, 0.0f, &um); break;
            case 3:
                track.removeChop (chopIndex, &um);
                selectedChop = juce::jmax (0, chopIndex - 1);
                if (onChopSelected) onChopSelected (selectedChop);
                break;
            default: break;
        }
        repaint();
    });
}
} // namespace pablo
