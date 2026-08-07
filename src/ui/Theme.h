#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace pablo::theme
{
// The Life Of Pablo palette: saturated orange ground, pale pink blocks,
// near-black ink, cream accents.
inline const juce::Colour orange      { 0xfff06a17 };
inline const juce::Colour orangeDark  { 0xffd6570e };
inline const juce::Colour pink        { 0xfff2c4c6 };
inline const juce::Colour pinkDark    { 0xffdfa6a9 };
inline const juce::Colour ink         { 0xff181310 };
inline const juce::Colour cream       { 0xfff4ebdd };
inline const juce::Colour creamDim    { 0xffe4d8c4 };

inline juce::Typeface::Ptr getMarkerTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::PermanentMarkerRegular_ttf, BinaryData::PermanentMarkerRegular_ttfSize);
    return tf;
}

inline juce::Font markerFont (float height)
{
    return juce::Font (juce::FontOptions (getMarkerTypeface()).withHeight (height));
}

inline juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                          height, juce::Font::bold));
}
} // namespace pablo::theme
