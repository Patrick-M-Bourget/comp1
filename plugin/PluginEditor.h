#pragma once

#include "Display.h"
#include "Footer.h"
#include "PluginProcessor.h"
#include "StyleSection.h"
#include "plugkit/ui/EditorShell.h"
#include "plugkit/ui/OutputMeter.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace comp1
{

// comp1's Expanded View, on plugkit's editor shell (EditorShell), which gives it its header, keys, UI Scale and
// window. Under the header: the display, its two tabs over its full width, with the Output Meter's rail at its right
// edge; under that the row of sections in signal order; and the footer. The display takes whatever the window grows
// by, and the section row its width.
class PluginEditor final : public plugkit::ui::EditorShell
{
public:
    explicit PluginEditor (PluginProcessor& processor);

    Display& display() { return displaySlot; }

    static constexpr int sectionRowHeight = 150, styleSectionWidth = 200;

private:
    void layOutBody (juce::Rectangle<int> area) override;

    Display displaySlot;
    plugkit::ui::OutputMeter outputMeter;
    // The sections, left to right in signal order. Named "Sections", as a test finds it.
    juce::Component sections;
    StyleSection styleSection;
    Footer footer;
};

} // namespace comp1
