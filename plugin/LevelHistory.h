#pragma once

#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/LevelBallistics.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <limits>
#include <span>
#include <vector>

namespace comp1
{

// The editor's one reader of the level history (PluginProcessor::readLevels), which the Engine allows one reader of:
// whatever in the editor shows levels over time (the Gain Reduction meters, the knobs' rings, the Level
// and Gain Reduction Graph) subscribes here rather than reading it. Opening, it drops what piled up while
// no editor read it (skipLevels), so it shows only runs completed after the editor opened. Each tick of the editor's
// it reads the history once, into a buffer of levelHistoryCapacity() records, and hands every record read to every
// subscriber, oldest first; it keeps the tick at its active rate while records arrive.
class LevelHistory final : public plugkit::ui::EditorTick::Client
{
public:
    class Subscriber
    {
    public:
        virtual ~Subscriber() = default;
        // The records read this tick, oldest first, one per 16-sample run; never empty.
        virtual void levelsRead (std::span<const LevelRecord> records) = 0;
    };

    // Reads processor's level history on the tick of editor, the EditorTick::Host, which adds it to its tick.
    LevelHistory (juce::Component& editor, PluginProcessor& processor);

    // A subscriber gets every record read from then on until it unsubscribes, which it does before it goes.
    void subscribe (Subscriber& subscriber);
    void unsubscribe (Subscriber& subscriber);

    // The editor's tick: reads the history and hands what it read on. Active while records arrive.
    void tick (double seconds) override;
    bool isActive() const override { return arriving; }

private:
    PluginProcessor& processor;
    std::vector<LevelRecord> buffer;
    std::vector<Subscriber*> subscribers;
    bool arriving = false; // the last tick read records
};

// One reading of the level history's records (one of LevelRecord's levels) as a meter shows it: subscribed to the
// history for its whole life, it keeps the loudest read since the last tick; each tick, next() gives the level to show,
// rising at once and falling at 20 dB/s (LevelBallistics), held within floor and ceiling, and starts again from floor.
// What the Gain Reduction meters, the knobs' rings and the Transfer Curve Graph's dot show.
class ShownLevel final : private LevelHistory::Subscriber
{
public:
    ShownLevel (LevelHistory& history, double LevelRecord::* reading, double floor, double ceiling = std::numeric_limits<double>::infinity());
    ~ShownLevel() override;

    // The level to show, seconds after the last tick.
    double next (double seconds);

private:
    void levelsRead (std::span<const LevelRecord> records) override;

    LevelHistory& history;
    double LevelRecord::* reading;
    const double floor, ceiling;
    plugkit::ui::LevelBallistics ballistics;
    double loudest; // since the last tick
};

} // namespace comp1
