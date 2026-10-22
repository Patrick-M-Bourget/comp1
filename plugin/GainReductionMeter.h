#pragma once

#include "LevelHistory.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/LevelBallistics.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// The compressor's Gain Reduction (LevelRecord::gainReductionDb), as one bar hanging from the top of a rail, in the
// kit's dynRange to dynRangeInner gradient, on a fixed scale of 0 to scaleDb, labelled every 6 dB. Each tick it shows
// the most Gain Reduction read since the last, rising at once and falling at 20 dB/s (LevelBallistics), and repaints
// only while it moves: not on silence. A screen reader finds it as a read-only element titled "Gain Reduction", read
// with its value ("6.0 dB"); it is no stop for Tab. Named "Gain Reduction", as a test finds it.
class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient, private LevelHistory::Subscriber, private plugkit::ui::EditorTick::Client
{
public:
    explicit GainReductionMeter (LevelHistory& history);
    ~GainReductionMeter() override;

    // The Gain Reduction shown, in dB: 0 and up.
    double shownDb() const { return shown; }
    // Where a Gain Reduction reaches down the scale: 0 at the top, 1 at scaleDb and over.
    static double position (double db);

    void paint (juce::Graphics& g) override;

    static constexpr double scaleDb = 24.0, labelStepDb = 6.0;
    static constexpr int labelHeight = 12, barInset = 6;

private:
    void levelsRead (std::span<const LevelRecord> records) override;
    void tick (double seconds) override;
    bool isActive() const override { return shown > 0.0; }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    // Where the bar runs, from 0 dB at its top to scaleDb at its bottom.
    juce::Rectangle<float> barArea() const;

    LevelHistory& history;
    plugkit::ui::LevelBallistics ballistics;
    double most = 0.0; // read since the last tick
    double shown = 0.0;
};

} // namespace comp1
