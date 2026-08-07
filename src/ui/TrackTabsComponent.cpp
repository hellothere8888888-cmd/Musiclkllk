#include "TrackTabsComponent.h"

namespace pablo
{
using namespace theme;

TrackTabsComponent::TrackTabsComponent (SessionState& s) : session (s) {}

juce::Rectangle<float> TrackTabsComponent::tabBounds (int index) const
{
    return { 2.0f + index * (tabWidth + 4.0f), 2.0f, tabWidth, (float) getHeight() - 6.0f };
}

void TrackTabsComponent::paint (juce::Graphics& g)
{
    const int numTracks = session.getNumTracks();
    const int active = session.getActiveTrackIndex();

    for (int i = 0; i < numTracks; ++i)
    {
        const auto r = tabBounds (i);
        const bool isActive = i == active;

        g.setColour (ink);
        g.fillRect (r.translated (2.0f, 2.0f));
        g.setColour (isActive ? ink : pink);
        g.fillRect (r);
        g.setColour (ink);
        g.drawRect (r, 1.2f);

        g.setColour (isActive ? cream : ink);
        g.setFont (markerFont (15.0f));
        g.drawFittedText (session.getTrack (i).getName().toUpperCase(),
                          r.toNearestInt().reduced (6, 2), juce::Justification::centred, 2);
    }

    // "+" tab
    const auto addR = juce::Rectangle<float> (2.0f + numTracks * (tabWidth + 4.0f), 2.0f,
                                              addWidth, (float) getHeight() - 6.0f);
    g.setColour (ink);
    g.fillRect (addR.translated (2.0f, 2.0f));
    g.setColour (cream);
    g.fillRect (addR);
    g.setColour (ink);
    g.drawRect (addR, 1.2f);
    g.setFont (markerFont (20.0f));
    g.drawText ("+", addR, juce::Justification::centred);
}

void TrackTabsComponent::mouseDown (const juce::MouseEvent& e)
{
    const int numTracks = session.getNumTracks();

    for (int i = 0; i < numTracks; ++i)
    {
        if (! tabBounds (i).contains (e.position))
            continue;

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Rename");
            menu.addItem (2, "Delete track", numTracks > 0);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this, i, safeThis = juce::Component::SafePointer<TrackTabsComponent> (this)] (int result)
            {
                if (safeThis == nullptr || i >= session.getNumTracks())
                    return;
                if (result == 1)
                {
                    auto* window = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon);
                    window->addTextEditor ("name", session.getTrack (i).getName());
                    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    window->enterModalState (true, juce::ModalCallbackFunction::create (
                        [this, i, window, safe2 = juce::Component::SafePointer<TrackTabsComponent> (safeThis.getComponent())] (int r)
                        {
                            if (safe2 != nullptr && r == 1 && i < session.getNumTracks())
                                session.getTrack (i).setName (window->getTextEditorContents ("name"), nullptr);
                        }), true);
                }
                else if (result == 2)
                {
                    session.removeTrack (i);
                }
            });
        }
        else
        {
            session.setActiveTrackIndex (i);
        }
        return;
    }

    const auto addR = juce::Rectangle<float> (2.0f + numTracks * (tabWidth + 4.0f), 2.0f,
                                              addWidth, (float) getHeight() - 6.0f);
    if (addR.contains (e.position) && onAddTrackRequested)
        onAddTrackRequested();
}
} // namespace pablo
