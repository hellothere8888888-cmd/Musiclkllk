#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace pablo::params
{
inline constexpr const char* masterGain   = "masterGain";
inline constexpr const char* globalPitch  = "globalPitch";
inline constexpr const char* choke        = "choke";        // same-track chops cut each other (classic MPC)
inline constexpr const char* gateMode     = "gateMode";     // false = one-shot, true = gate (stop on release)
inline constexpr const char* baseNote     = "baseNote";     // MIDI note that triggers chop 0 of the active track

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

    return layout;
}
} // namespace pablo::params
