#pragma once
#include <juce_core/juce_core.h>

// ValueTree schema identifiers. The SESSION tree is the single source of truth
// for everything structural (tracks, chops); APVTS covers only the fixed
// automatable parameters (see engine/ParamIDs.h).
namespace pablo::id
{
#define PABLO_DECLARE_ID(name) inline const juce::Identifier name { #name };

PABLO_DECLARE_ID (SESSION)
PABLO_DECLARE_ID (TRACK)
PABLO_DECLARE_ID (CHOPS)
PABLO_DECLARE_ID (CHOP)
PABLO_DECLARE_ID (AUDIO)

PABLO_DECLARE_ID (version)
PABLO_DECLARE_ID (activeTrack)

// TRACK properties
PABLO_DECLARE_ID (uid)
PABLO_DECLARE_ID (name)
PABLO_DECLARE_ID (filePath)
PABLO_DECLARE_ID (sampleRate)
PABLO_DECLARE_ID (lengthSamples)
PABLO_DECLARE_ID (gain)
PABLO_DECLARE_ID (embedAudio)
PABLO_DECLARE_ID (filterCarve)   // -1 = high-pass (cut lows) .. 0 = off .. +1 = low-pass (cut highs)

// AUDIO properties (embedded FLAC copy so FL projects survive moved files)
PABLO_DECLARE_ID (flacBase64)

// CHOP properties. A chop is defined by its start sample; its end is the next
// chop's start (or the end of the sample). This keeps slices contiguous like
// an MPC slice grid and makes marker-dragging trivially consistent.
PABLO_DECLARE_ID (startSample)
PABLO_DECLARE_ID (pitchSemis)
PABLO_DECLARE_ID (reverse)
PABLO_DECLARE_ID (velSens)      // 0 = ignore MIDI velocity .. 1 = full dynamic range
PABLO_DECLARE_ID (stretchRatio) // length multiplier for pitch-preserving time-stretch (1 = off)
PABLO_DECLARE_ID (chopGain)     // per-chop volume, linear (1 = 100%)

#undef PABLO_DECLARE_ID
} // namespace pablo::id
