#pragma once

#include "plugkit/ui/staple/controls/TextChip.h"

#include <algorithm>

namespace comp1
{

// The width chip needs for the widest of texts, so it keeps its width whichever it shows: each set as its text and
// measured, and its own text put back after.
inline int widestIdealWidth (staple::TextChip& chip, const juce::StringArray& texts)
{
    const auto shown = chip.getButtonText();
    int widest = 0;
    for (const auto& text : texts)
    {
        chip.setButtonText (text);
        widest = std::max (widest, chip.getIdealWidth());
    }
    chip.setButtonText (shown);
    return widest;
}

} // namespace comp1
