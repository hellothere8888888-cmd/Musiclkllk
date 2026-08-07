#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/SamplerEngine.h"

namespace pablo
{
// Laptop-keyboard chop triggering (works when the plugin window has focus).
// Note that inside FL Studio the typing keyboard also reaches us as MIDI, so
// chops are playable either way. Layout, bottom row upward like an MPC bank:
//   z x c v b n m ,   -> chops 1-8
//   a s d f g h j k   -> chops 9-16
//   q w e r t y u i   -> chops 17-24
//   1 2 3 4 5 6 7 8   -> chops 25-32
namespace keymap
{
    inline constexpr const char* rows[4] = { "zxcvbnm,", "asdfghjk", "qwertyui", "12345678" };

    inline int padIndexForChar (juce::juce_wchar c)
    {
        c = juce::CharacterFunctions::toLowerCase (c);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; rows[row][col] != 0; ++col)
                if ((juce::juce_wchar) rows[row][col] == c)
                    return row * 8 + col;
        return -1;
    }

    inline juce::String keyForPadIndex (int pad)
    {
        if (pad < 0 || pad >= 32)
            return {};
        return juce::String::charToString ((juce::juce_wchar) rows[pad / 8][pad % 8]).toUpperCase();
    }
} // namespace keymap

class KeyboardHandler : public juce::KeyListener
{
public:
    explicit KeyboardHandler (SamplerEngine& engineToUse) : engine (engineToUse) {}

    bool keyPressed (const juce::KeyPress&, juce::Component*) override
    {
        // Handled in keyStateChanged (so we get releases too); returning true
        // for mapped keys stops the host shortcut system reacting.
        return false;
    }

    bool keyStateChanged (bool, juce::Component*) override
    {
        // Don't let Ctrl/Cmd shortcuts (undo etc.) double as pad hits.
        if (juce::ModifierKeys::currentModifiers.testFlags (juce::ModifierKeys::ctrlModifier)
            || juce::ModifierKeys::currentModifiers.testFlags (juce::ModifierKeys::commandModifier))
        {
            releaseAll();
            return false;
        }

        bool consumed = false;
        for (int pad = 0; pad < 32; ++pad)
        {
            const auto c = (juce::juce_wchar) keymap::rows[pad / 8][pad % 8];
            const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) c)
                           || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase (c));
            if (down != padDown[pad])
            {
                padDown[pad] = down;
                engine.triggerFromUI (-1, pad, down);
                consumed = true;
            }
        }
        return consumed;
    }

    void releaseAll()
    {
        for (int pad = 0; pad < 32; ++pad)
            if (padDown[pad])
            {
                padDown[pad] = false;
                engine.triggerFromUI (-1, pad, false);
            }
    }

private:
    SamplerEngine& engine;
    bool padDown[32] = {};
};
} // namespace pablo
