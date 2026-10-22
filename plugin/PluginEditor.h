#pragma once

#include "Display.h"
#include "Footer.h"
#include "GainReductionMeter.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "Sections.h"
#include "StyleSection.h"
#include "plugkit/ui/EditorShell.h"
#include "plugkit/ui/OutputMeter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace comp1
{

// comp1's Expanded View, on plugkit's editor shell (EditorShell), which gives it its header, keys, UI Scale and
// window. Under the header: the display, its two tabs over its full width, with the Gain Reduction meter and the Output
// Meter's rail at its right edge; under that the sections in signal order, in two rows; and the footer. The display
// takes whatever the window grows by, and the section rows its width; each section keeps its own size.
class PluginEditor final : public plugkit::ui::EditorShell
{
public:
    explicit PluginEditor (PluginProcessor& processor);
    ~PluginEditor() override;

    Display& display() { return displaySlot; }
    // The editor's one reader of the level history, which whatever shows levels over time subscribes to.
    LevelHistory& levelHistory() { return history; }
    GainReductionMeter& gainReductionMeter() { return gainReduction; }
    DetectionLevelRing& detectionLevelRing() { return compressor.detectionLevelRing(); }

    // The section rows' height, together with the gap between them, and the Style section's width.
    int sectionRowsHeight();
    static constexpr int styleSectionWidth = 130, sectionGap = 12;

private:
    void layOutBody (juce::Rectangle<int> area) override;
    // The sections, row by row, each row left to right in signal order.
    std::vector<std::vector<Section*>> sectionRows();
    static int widthOf (Section& section);
    static int heightOf (Section& section);

    // Before every component that subscribes to it, so it outlives them.
    LevelHistory history;
    Display displaySlot;
    GainReductionMeter gainReduction;
    plugkit::ui::OutputMeter outputMeter;
    // The sections, left to right in signal order. Named "Sections", as a test finds it.
    juce::Component sections;
    StyleSection styleSection;
    GateSection gate;
    CompressorSection compressor;
    CharacterSection character;
    MixSection mix;
    OutputSection output;
    DetectionSection detection;
    StereoSection stereo;
    LookaheadSection lookahead;
    Footer footer;
};

} // namespace comp1
