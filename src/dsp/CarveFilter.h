#pragma once
#include <cmath>

namespace pablo
{
// Result of the one-knob "carve" tone control.
struct CarveSetting
{
    int   mode = 0;              // 0 = off, 1 = low-pass, 2 = high-pass
    float cutoff = 20000.0f;     // Hz
};

// Map a carve value in [-1, 1] to a filter setting: the left half sweeps a
// high-pass up through the low end (cleaning mud), the right half sweeps a
// low-pass down through the top (taming highs), centre is bypass. Pure and
// header-only so it is shared by the snapshot builder and the tests.
inline CarveSetting carveToFilter (float carve) noexcept
{
    CarveSetting s;
    constexpr float dead = 0.04f;                 // centre dead-zone = off
    if (carve < -dead)
    {
        const float t = (-carve - dead) / (1.0f - dead);        // 0..1
        s.mode = 2;                                             // high-pass
        s.cutoff = 20.0f * std::pow (1200.0f / 20.0f, t);       // 20 -> 1200 Hz
    }
    else if (carve > dead)
    {
        const float t = (carve - dead) / (1.0f - dead);         // 0..1
        s.mode = 1;                                             // low-pass
        s.cutoff = 20000.0f * std::pow (250.0f / 20000.0f, t);  // 20k -> 250 Hz
    }
    return s;
}
} // namespace pablo
