#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// A meter's label, one line under its bar in the Expanded View, for the Gain Reduction meters and the Output Meter
// alike: its text in uppercase, in the kit's label style (staple::labelFont, its 14 px line) in text3, letter-spaced as
// uppercase text is, centred. Named by its text, as a test finds it. The meter it labels is already read by its own
// title, so a screen reader skips it.
class MeterLabel final : public juce::Component
{
public:
    explicit MeterLabel (const juce::String& text);

    void paint (juce::Graphics& g) override;

    // The width its text takes, rounded up to whole pixels.
    int textWidth() const;

    static constexpr int lineHeight = 14;
};

} // namespace comp1
