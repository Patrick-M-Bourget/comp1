#pragma once

#include "CompactView.h"
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
// window. Under the header: the display, its two tabs over its full width, with three Gain Reduction meters and the
// Output Meter's rail at its right edge; under that the sections in signal order, in two rows; and the footer. The
// display takes whatever the window grows by, and the section rows its width; each section keeps its own size. The
// meters are the Gate's, the compressor's and the Limiter's, left to right in signal order (their placement is
// provisional, #94).
//
// It shows either view, the one the processor holds, the Compact View being its body (CompactView) under the same
// header and footer, after the header in Tab's order. Each view has its own window limits and size (PluginProcessor::windowOf, setView).
class PluginEditor final : public plugkit::ui::EditorShell
{
public:
    explicit PluginEditor (PluginProcessor& processor);
    ~PluginEditor() override;

    Display& display() { return displaySlot; }
    // The editor's one reader of the level history, which whatever shows levels over time subscribes to.
    LevelHistory& levelHistory() { return history; }
    GainReductionMeter& gainReductionMeter() { return gainReduction; }
    GainReductionMeter& gateGainReductionMeter() { return gateGainReduction; }
    GainReductionMeter& limiterGainReductionMeter() { return limiterGainReduction; }
    DetectionLevelRing& detectionLevelRing() { return compressor.detectionLevelRing(); }

    // The section rows' height, together with the gap between them, and the Style section's width.
    int sectionRowsHeight();
    static constexpr int styleSectionWidth = 130, sectionGap = 12;

    // The view shown, which follows the processor's View on the tick, as after a restore. The hidden view's
    // components are hidden; the Level and Gain Reduction Graph and the meters keep their subscriptions meanwhile.
    View shownView() const { return shown; }
    // The Compact View's body under the shared header and footer (CompactView).
    CompactView& compactView() { return compactBody; }
    // The footer's switch between the views, the one showing.
    juce::Button& viewSwitch() { return footer.viewSwitch(); }
    void layOutBody (juce::Rectangle<int> area) override;
    // The sections, row by row, each row left to right in signal order.
    std::vector<std::vector<Section*>> sectionRows();
    static int widthOf (Section& section);
    static int heightOf (Section& section);

    // Before every component that subscribes to it, so it outlives them.
    LevelHistory history;
    Display displaySlot;
    GainReductionMeter gainReduction;
    GainReductionMeter gateGainReduction { history, GainReductionMeter::Stage::gate };
    GainReductionMeter limiterGainReduction { history, GainReductionMeter::Stage::limiter };
    plugkit::ui::OutputMeter outputMeter;
    // The sections, left to right in signal order. Named "Sections", as a test finds it.
    juce::Component sections;
    StyleSection styleSection;
    GateSection gate;
    CompressorSection compressor;
    CharacterSection character;
    DryWetSection dryWet;
    OutputSection output;
    DetectionSection detection;
    StereoSection stereo;
    LookaheadSection lookahead;
    Footer footer;

    // Shows view, hiding the other's components: leaving the Expanded View lets go of Detection Audition, as leaving
    // the Detection EQ tab does.
    void show (View view);
    // Holds the window to the shown view's limits at the UI Scale, and its size within them: after the shell applies
    // the Session's (the envelope of both views'), at open, after a UI Scale pick and on the tick.
    void applyViewWindow();
    void followView();
    CompactView compactBody;
    View shown = View::expanded;
    // Follows the processor's View on the editor's tick.
    struct ViewFollower final : plugkit::ui::EditorTick::Client
    {
        explicit ViewFollower (PluginEditor& e) : Client (e.compactBody), editor (e) {}
        void tick (double) override { editor.followView(); }
        PluginEditor& editor;
    } viewFollower { *this };
};

} // namespace comp1
