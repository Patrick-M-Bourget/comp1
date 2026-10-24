#pragma once

#include "CompactView.h"
#include "Display.h"
#include "Footer.h"
#include "GainReductionMeter.h"
#include "LevelHistory.h"
#include "MeterLabel.h"
#include "PluginProcessor.h"
#include "Sections.h"
#include "plugkit/ui/EditorShell.h"
#include "plugkit/ui/OutputMeter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <utility>
#include <vector>

namespace comp1
{

// comp1's editor, on plugkit's editor shell (EditorShell), which gives it its header, keys, UI Scale and window. It
// shows either view, the one the processor holds, each between the same header and footer (Footer), and each with its
// own window limits and size (PluginProcessor::windowOf, setView).
//
// The Expanded View's body, under the header: the display, its two tabs over its full width (Display), with four
// meters at its right edge, left to right in signal order: the Gate's, the compressor's and the Limiter's Gain
// Reduction, then the Output Meter, each labelled on one line under its bar (GATE, COMP, LIM, OUT; MeterLabel), the
// meters as tall as the display but for that line. Under that the sections in signal order, in two rows from the left
// edge on one grid of three columns: Gate, Compressor, and Character and Saturation over Detection, spanning the first
// two, and Stereo and Lookahead, sharing the third. Each column is as wide as the widest it holds, and each section
// takes its columns' width (its controls from the left) and its own height, the Detection section's following its
// slide open or closed (DetectionSection), the display taking up what it gives. The display takes whatever the window
// grows by, and the section rows its width.
//
// The Compact View's body is CompactView, after the header in Tab's order.
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
    LevelRing& detectionLevelRing() { return compressor.detectionLevelRing(); }
    LevelRing& ceilingRing() { return footer.outputPopover().ceilingRing(); }
    DetectionSection& detectionSection() { return detection; }

    // The view shown, which follows the processor's View on the tick, as after a restore. The hidden view's
    // components are hidden; the Level and Gain Reduction Graph and the meters keep their subscriptions meanwhile.
    View shownView() const { return shown; }
    // The Compact View's body under the shared header and footer (CompactView).
    CompactView& compactView() { return compactBody; }
    // The footer's switch between the views, the one showing.
    juce::Button& viewSwitch() { return footer.viewSwitch(); }

private:
    void layOutBody (juce::Rectangle<int> area) override;
    // The meters at the display's right, left to right, each with its label.
    std::array<std::pair<juce::Component*, MeterLabel*>, 4> labelledMeters();
    // The sections, row by row, each row left to right in signal order.
    std::vector<std::vector<Section*>> sectionRows();
    // The section rows' height, together with the gap between them.
    int sectionRowsHeight();
    static constexpr int sectionGap = 12;
    // The area last laid out, for laying it out again as the Detection section slides.
    juce::Rectangle<int> bodyArea;

    // Shows view, hiding the other's components: leaving the Expanded View lets go of Detection Audition, as collapsing
    // the Detection section does.
    void show (View view);
    // Holds the window to the shown view's limits at the UI Scale, and its size within them: after the shell applies
    // the Session's (the envelope of both views'), at open, after a UI Scale pick and on the tick.
    void applyViewWindow();
    void followView();

    // Before every component that subscribes to it, so it outlives them.
    LevelHistory history;
    Display displaySlot;
    GainReductionMeter gainReduction;
    GainReductionMeter gateGainReduction { history, GainReductionMeter::Stage::gate };
    GainReductionMeter limiterGainReduction { history, GainReductionMeter::Stage::limiter };
    plugkit::ui::OutputMeter outputMeter;
    MeterLabel gateLabel { "Gate" }, compLabel { "Comp" }, limiterLabel { "Lim" }, outputLabel { "Out" };
    // The sections, left to right in signal order. Named "Sections", as a test finds it.
    juce::Component sections;
    GateSection gate;
    CompressorSection compressor;
    CharacterSection character;
    DetectionSection detection;
    StereoSection stereo;
    LookaheadSection lookahead;
    Footer footer;
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
