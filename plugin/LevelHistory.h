#pragma once

#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <span>
#include <vector>

namespace comp1
{

// The editor's one reader of the level history (PluginProcessor::readLevels), which the Engine allows one reader of:
// whatever in the editor shows levels over time (the Gain Reduction meter, the Detection Level ring, the scrolling
// level and Gain Reduction graph, #28) subscribes here rather than reading it. Opening, it drops what piled up while
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

} // namespace comp1
