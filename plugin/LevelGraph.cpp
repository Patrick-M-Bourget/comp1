#include "LevelGraph.h"

#include "plugkit/OutputLevel.h"
#include "plugkit/RunGrid.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/ValueText.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;
namespace display = plugkit::ui::display;

// The dB labels: right-aligned this far in from the right edge, each this big, kept inside the graph.
constexpr float labelFromRight = 6.0f, labelWidth = 32.0f, labelHeight = 12.0f;
constexpr float levelLineWidth = 1.0f, curveLineWidth = 1.5f;

// A level as a screen reader reads it: "−12.0 dBFS", and the floor (silence) "−inf dBFS".
juce::String spokenLevel (double db)
{
    const double shown = db <= plugkit::levelFloorDb ? -std::numeric_limits<double>::infinity() : db;
    return plugkit::ui::valueText::signedDb (shown, { .decimals = 1, .unit = false }) + " dBFS";
}
} // namespace

LevelGraph::LevelGraph (LevelHistory& h, const juce::AudioProcessor& p) : history (h), processor (p)
{
    setName ("Level");
    setTitle ("Level and Gain Reduction Graph");
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (false, false);
    sizeFor (sampleRate());
    history.subscribe (*this);
}

LevelGraph::~LevelGraph()
{
    history.unsubscribe (*this);
}

double LevelGraph::sampleRate() const { return std::min (plugkit::ui::sampleRateOrAssumed (processor), fastestSampleRate); }

double LevelGraph::spanRecords() const { return spanSeconds * sampleRate() / plugkit::runSamples; }

void LevelGraph::sizeFor (double rate)
{
    const auto needed = static_cast<size_t> (std::ceil (spanSeconds * rate / plugkit::runSamples)) + 1;
    if (needed <= levels.size())
        return;
    // Oldest first, then the room to grow.
    std::vector<Levels> grown;
    grown.reserve (needed);
    for (int ago = held - 1; ago >= 0; --ago)
        grown.push_back (levelsAgo (ago));
    newest = static_cast<int> (grown.size()) - 1;
    grown.resize (needed);
    levels = std::move (grown);
}

void LevelGraph::levelsRead (std::span<const LevelRecord> records)
{
    sizeFor (sampleRate());
    const int size = static_cast<int> (levels.size());
    for (const auto& record : records)
    {
        newest = (newest + 1) % size;
        levels[static_cast<size_t> (newest)] = { static_cast<float> (record.inputDb), static_cast<float> (record.outputDb), static_cast<float> (record.gainReductionDb) };
    }
    held = std::min (size, held + static_cast<int> (records.size()));
    taken += records.size();
    repaint();
}

const LevelGraph::Levels& LevelGraph::levelsAgo (int runsAgo) const
{
    const int size = static_cast<int> (levels.size());
    return levels[static_cast<size_t> (((newest - runsAgo) % size + size) % size)];
}

LevelRecord LevelGraph::recordAgo (int runsAgo) const
{
    jassert (runsAgo >= 0 && runsAgo < held);
    const auto& l = levelsAgo (runsAgo);
    LevelRecord record;
    record.inputDb = l.inputDb;
    record.outputDb = l.outputDb;
    record.gainReductionDb = l.gainReductionDb;
    return record;
}

double LevelGraph::columnSeconds() const { return spanSeconds / std::max (1, getWidth()); }

LevelGraph::Drawn LevelGraph::column (int index, int columns) const
{
    // The runs it covers, ago, from its right edge to its left: those whose time ago falls in it.
    const double perColumn = spanRecords() / columns;
    const int from = static_cast<int> (std::ceil (index * perColumn));
    const int to = std::min (held, static_cast<int> (std::ceil ((index + 1) * perColumn)));
    Drawn drawn;
    for (int ago = from; ago < to; ++ago)
    {
        const auto& l = levelsAgo (ago);
        drawn.inputDb = std::max (drawn.inputDb, static_cast<double> (l.inputDb));
        drawn.outputDb = std::max (drawn.outputDb, static_cast<double> (l.outputDb));
        drawn.gainReductionDb = std::max (drawn.gainReductionDb, static_cast<double> (l.gainReductionDb));
    }
    drawn.inputDb = std::min (drawn.inputDb, 0.0);
    drawn.outputDb = std::min (drawn.outputDb, 0.0);
    drawn.gainReductionDb = std::min (drawn.gainReductionDb, scaleDb);
    return drawn;
}

LevelGraph::Drawn LevelGraph::drawnAt (double secondsAgo) const
{
    const int columns = std::max (1, getWidth());
    const int index = static_cast<int> (std::floor (secondsAgo * sampleRate() / plugkit::runSamples / (spanRecords() / columns)));
    return column (std::clamp (index, 0, columns - 1), columns);
}
juce::String LevelGraph::valueText() const
{
    const auto latest = held > 0 ? recordAgo (0) : LevelRecord {};
    return "Input Level " + spokenLevel (latest.inputDb) + ", Output Level " + spokenLevel (latest.outputDb) + ", Gain Reduction "
           + juce::String (std::max (0.0, latest.gainReductionDb), 1) + " dB";
}

std::unique_ptr<juce::AccessibilityHandler> LevelGraph::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::image, [this] { return valueText(); });
}

void LevelGraph::paint (juce::Graphics& g)
{
    const int width = getWidth(), height = getHeight();
    if (width <= 0 || height <= 0)
        return;
    const auto w = static_cast<float> (width), h = static_cast<float> (height);
    const auto yOfLevel = [h] (double db) { return static_cast<float> (std::clamp (-db / scaleDb, 0.0, 1.0)) * h; };
    const auto yOfReduction = [h] (double db) { return static_cast<float> (std::clamp (db / scaleDb, 0.0, 1.0)) * h; };

    // The grid: a line every second from the right edge, and every 10 dB, the 0 dB one at the top.
    display::GridLines lines;
    const float perSecond = w / static_cast<float> (spanSeconds);
    for (double s = gridStepSeconds; s < spanSeconds; s += gridStepSeconds)
        lines.majorX.push_back (w - static_cast<float> (s) * perSecond);
    for (double db = gridStepDb; db < scaleDb; db += gridStepDb)
        lines.gainY.push_back (yOfLevel (-db));
    lines.zeroY = yOfLevel (0.0);
    display::paintGrid (g, lines, { width, height });

    // The columns holding records, from the right edge.
    const double perColumn = spanRecords() / width;
    const int columns = std::min (width, static_cast<int> (std::ceil (held / perColumn)));
    if (columns > 0)
    {
        juce::Path input, output, reduction;
        for (int c = 0; c < columns; ++c)
        {
            const auto drawn = column (c, width);
            const float x = w - static_cast<float> (c) - 0.5f;
            const juce::Point<float> in { x, yOfLevel (drawn.inputDb) }, out { x, yOfLevel (drawn.outputDb) },
                reduced { x, yOfReduction (drawn.gainReductionDb) };
            if (c == 0)
            {
                input.startNewSubPath (in);
                output.startNewSubPath (out);
                reduction.startNewSubPath (reduced);
            }
            else
            {
                input.lineTo (in);
                output.lineTo (out);
                reduction.lineTo (reduced);
            }
        }
        const float left = w - static_cast<float> (columns) + 0.5f, right = w - 0.5f;
        // The Gain Reduction, filled up to the top, as the Gain Reduction meter's bar hangs, under the levels.
        auto reductionArea = reduction;
        reductionArea.lineTo (left, 0.0f);
        reductionArea.lineTo (right, 0.0f);
        reductionArea.closeSubPath();
        g.setGradientFill (juce::ColourGradient::vertical (colour::dynRange, 0.0f, colour::dynRangeInner, h));
        g.fillPath (reductionArea);
        // The Input Level's area, down to the bottom, under its trace.
        auto inputArea = input;
        inputArea.lineTo (left, h);
        inputArea.lineTo (right, h);
        inputArea.closeSubPath();
        g.setGradientFill (juce::ColourGradient::vertical (colour::anFillTop, 0.0f, colour::anFillMid, h));
        g.fillPath (inputArea);
        g.setColour (colour::anLine);
        g.strokePath (input, juce::PathStrokeType (levelLineWidth));
        g.setColour (colour::dynRange);
        g.strokePath (reduction, juce::PathStrokeType (curveLineWidth));
        g.setColour (colour::curveMain);
        g.strokePath (output, juce::PathStrokeType (curveLineWidth));
    }

    fades.paint (g, *this);

    // The dB labels at the right, over the fades.
    std::vector<display::Label> scale;
    for (double db = 0.0; db < scaleDb; db += gridStepDb)
    {
        const float y = std::clamp (yOfLevel (-db), labelHeight / 2.0f, h - labelHeight / 2.0f);
        scale.push_back ({ plugkit::ui::valueText::signedDb (-db, { .decimals = 0, .unit = false }),
                           { w - labelFromRight - labelWidth, y - labelHeight / 2.0f, labelWidth, labelHeight },
                           juce::Justification::centredRight,
                           colour::text3,
                           size::fs1 });
    }
    labels.paint (g, DisplayFades::originInWindow (*this), scale);
}

} // namespace comp1
