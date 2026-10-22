#pragma once

#include "LevelHistory.h"
#include "plugkit/ui/display/EdgeFadeLayer.h"
#include "plugkit/ui/display/GridLayer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <vector>

namespace comp1
{

// The Level and Gain Reduction Graph, the Expanded View's Level tab: the Input Level, the Output Level and the
// compressor's Gain Reduction over the last spanSeconds, the newest audio at the right edge, older audio moving left.
// It takes every record of the editor's level history (LevelHistory) for its whole life, shown or not, so the Level
// tab shows the recent past when it is chosen again; it moves on audio time, as records arrive, and holds still (and
// repaints nothing) when they stop. Its span is spanSeconds at any width, a record being plugkit::runSamples at the
// processor's sample rate (or the assumed one); each pixel column shows the loudest Input and Output Level and the most
// Gain Reduction of the records it covers, so a one-run transient is never lost between columns.
//
// One dB scale: 0 dB at the top to scaleDb down at the bottom. The levels are dBFS, anything under the scale (the
// floor too) at the bottom; Gain Reduction hangs from the top, r dB of it reaching r dB down. Drawn with stock Staple
// tokens on plugkit-ui's grid layer: a line every second from the right edge and every 10 dB, the 0 dB one the zero
// line, with dB labels at the right; the Input Level as an an-fill area under an an-line trace, the Output Level as a
// curve-main line, and the Gain Reduction as a dyn-range line filled up to the top in the Gain Reduction meter's
// gradient; the edges dissolve into the window's background over the design system's display fades.
//
// A screen reader finds it as a read-only image titled "Level and Gain Reduction Graph", read with the latest record's
// levels ("Input Level −12.0 dBFS, Output Level −12.0 dBFS, Gain Reduction 0.0 dB"); it is no stop for Tab and takes no
// mouse input. Named "Level", as its tab, which a test finds it by.
class LevelGraph final : public juce::Component, private LevelHistory::Subscriber
{
public:
    LevelGraph (LevelHistory& history, const juce::AudioProcessor& processor);
    ~LevelGraph() override;

    static constexpr double spanSeconds = 5.0, scaleDb = 60.0, gridStepDb = 10.0, gridStepSeconds = 1.0;
    // The fastest sample rate whose span the graph keeps whole: the history it holds is bounded.
    static constexpr double fastestSampleRate = 768000.0;

    // What one pixel column draws, on the scale: the loudest Input and Output Level, in dBFS from -scaleDb (silence,
    // and anything under the scale) to 0, and the most Gain Reduction, in dB from 0 to scaleDb.
    struct Drawn
    {
        double inputDb = -scaleDb, outputDb = -scaleDb, gainReductionDb = 0.0;
    };
    // What the column secondsAgo before the newest record (0 to spanSeconds) draws: the test model of the drawing.
    Drawn drawnAt (double secondsAgo) const;
    // How long one pixel column covers: spanSeconds over the width.
    double columnSeconds() const;

    // How many records it has taken since it was made, and the one runsAgo before the newest (0, the newest, to
    // recordsHeld() - 1), as taken.
    std::uint64_t recordsTaken() const { return taken; }
    int recordsHeld() const { return held; }
    LevelRecord recordAgo (int runsAgo) const;

    // The text a screen reader reads as its value.
    juce::String valueText() const;

    void paint (juce::Graphics& g) override;

private:
    struct Levels
    {
        float inputDb, outputDb, gainReductionDb;
    };

    void levelsRead (std::span<const LevelRecord> records) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    double sampleRate() const;
    // How many records the span covers, at the sample rate.
    double spanRecords() const;
    // Sizes the history for the sample rate: only when that needs more than it holds, keeping what it holds.
    void sizeFor (double rate);
    // Column (0 at the right edge) of a width-column graph, from the records it covers.
    Drawn column (int index, int columns) const;
    const Levels& levelsAgo (int runsAgo) const;
    void paintEdgeFades (juce::Graphics& g);

    LevelHistory& history;
    const juce::AudioProcessor& processor;
    std::vector<Levels> levels; // a ring of the newest records, newest at newest
    int newest = -1, held = 0;
    std::uint64_t taken = 0;
    plugkit::ui::display::CachedLabels labels;
    // The edges' fades, made again when the graph's size, its place in the window or the scale it is drawn at changes.
    plugkit::ui::display::EdgeFadeOverlay fades;
    juce::Rectangle<float> fadesMadeFor;
};

} // namespace comp1
