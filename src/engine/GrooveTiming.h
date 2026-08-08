#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace pablo::groove
{
// Fraction of one grid cell that *full* swing pushes an off-beat late.
// 0.5 means the off-beat lands exactly halfway to the next grid line (an
// extreme shuffle); classic MPC/Dilla feel lives around 0.1–0.3 of this.
inline constexpr double kSwingMax = 0.5;

// Map a Grid choice index (see ParamIDs::grid) to notes-per-bar-quarter
// divisions: 1/4, 1/8, 1/16, 1/32.
inline int gridDivisionsForIndex (int choiceIndex) noexcept
{
    switch (choiceIndex)
    {
        case 0:  return 4;
        case 1:  return 8;
        case 3:  return 32;
        default: return 16;   // 1/16
    }
}

// Given a note that arrives 'samplePos' samples into the current audio block,
// return the sample offset (relative to the block start, always >= 0) at which
// it should actually fire after applying quantize (snap to grid) then swing
// (delay the off-beat subdivisions). The returned offset may exceed the block
// length, in which case the caller defers the trigger to a later block.
//
// Deterministic and allocation-free: safe on the audio thread and unit
// testable in isolation.
//
//   clockStartPpq : host musical position (in quarter notes) at block start
//   bpm           : host tempo; <= 0 disables grooving (returns samplePos)
//   hostRate      : output sample rate (Hz)
//   swing01       : 0 = straight .. 1 = maximum swing
//   quantize      : snap timing to the nearest grid line
//   gridDivisions : grid resolution (4 = 1/4, 8 = 1/8, 16 = 1/16, 32 = 1/32)
//   maxOffset     : clamp ceiling in samples
inline int applyGroove (int samplePos, double clockStartPpq, double bpm, double hostRate,
                        float swing01, bool quantize, int gridDivisions, int maxOffset) noexcept
{
    if (bpm <= 0.0 || hostRate <= 0.0 || gridDivisions <= 0)
        return samplePos;

    const double samplesPerQuarter = 60.0 / bpm * hostRate;
    if (samplesPerQuarter <= 0.0)
        return samplePos;

    const double notePpq  = clockStartPpq + (double) samplePos / samplesPerQuarter;
    const double step     = 4.0 / (double) gridDivisions;          // quarters per grid cell
    const long   stepIndex = (long) std::llround (notePpq / step);

    double target = quantize ? (double) stepIndex * step : notePpq;

    // Swing delays every other grid cell (the "and" of the beat).
    if (swing01 > 0.0f && (stepIndex & 1) != 0)
        target += (double) swing01 * kSwingMax * step;

    const double offset = std::llround ((target - clockStartPpq) * samplesPerQuarter);
    return (int) juce::jlimit<double> (0.0, (double) juce::jmax (0, maxOffset), offset);
}
} // namespace pablo::groove
