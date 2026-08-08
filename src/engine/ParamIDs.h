#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace pablo::params
{
inline constexpr const char* masterGain   = "masterGain";
inline constexpr const char* globalPitch  = "globalPitch";
inline constexpr const char* choke        = "choke";        // same-track chops cut each other (classic MPC)
inline constexpr const char* gateMode     = "gateMode";     // false = one-shot, true = gate (stop on release)
inline constexpr const char* baseNote     = "baseNote";     // MIDI note that triggers chop 0 of the active track
inline constexpr const char* swing        = "swing";        // 0..100 %: pushes off-beat grid cells late
inline constexpr const char* quantize     = "quantize";     // snap incoming note timing to the grid
inline constexpr const char* grid         = "grid";         // groove grid: 0=1/4 1=1/8 2=1/16 3=1/32

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { masterGain, 1 }, "Master Gain",
        NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { globalPitch, 1 }, "Global Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { choke, 1 }, "Choke", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { gateMode, 1 }, "Gate Mode", false));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { baseNote, 1 }, "Base Note", 24, 84, 60));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { swing, 1 }, "Swing",
        NormalisableRange<float> (0.0f, 100.0f, 0.5f), 0.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { quantize, 1 }, "Quantize", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { grid, 1 }, "Grid",
        juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));

    return layout;
}
} // namespace pablo::params
