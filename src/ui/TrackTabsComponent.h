#pragma once
#include "Theme.h"
#include "../model/SessionState.h"

namespace pablo
{
// MPC-ish sample track tabs: click to switch, "+" to load another sample,
// right-click a tab to rename or delete.
class TrackTabsComponent : public juce::Component
{
public:
    explicit TrackTabsComponent (SessionState& session);

    void refresh() { repaint(); }
    std::function<void()> onAddTrackRequested;      // owner opens the file chooser

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> tabBounds (int index) const;

    SessionState& session;
    static constexpr float tabWidth = 118.0f, addWidth = 34.0f;
};
} // namespace pablo
