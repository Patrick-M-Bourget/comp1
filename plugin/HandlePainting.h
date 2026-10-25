#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// How the editor's drag handles draw: the Transfer Curve inset's Threshold and Ratio handles and the Timing Response
// inset's Attack and Release handles, drawn at their own radius.
namespace handles
{
// The ring around a handle's disc, in px.
inline constexpr float ringWidth = 1.5f;
// How far the readout card sits from a handle's edge, in px.
inline constexpr float readoutGap = 8.0f;

struct Look
{
    float radius = 0.0f;
    bool selected = false; // focused or dragged: a white ring
    bool hovered = false;  // the pointer over it: the disc lit as hover lights a control (staple::lit)
    float alpha = 1.0f;    // the whole handle's, dimmed while its band is Off
};

// A curve-main disc of look.radius at centre, ringed in handleRing, or handleSelectedRing while selected.
void paint (juce::Graphics& g, juce::Point<float> centre, const Look& look);

// The readout card a dragged handle shows: title and value, above the handle at centre (of radius), or below it where
// above would leave the area's top, and kept across inside the area's width.
void paintReadout (juce::Graphics& g, juce::Point<float> centre, float radius, float areaWidth, const juce::String& title,
                   const juce::String& value);
} // namespace handles

} // namespace comp1
