#include "LevelHistory.h"

#include <algorithm>

namespace comp1
{

LevelHistory::LevelHistory (juce::Component& editor, PluginProcessor& p) : plugkit::ui::EditorTick::Client (editor), processor (p)
{
    processor.skipLevels();
}

void LevelHistory::subscribe (Subscriber& subscriber)
{
    if (std::find (subscribers.begin(), subscribers.end(), &subscriber) == subscribers.end())
        subscribers.push_back (&subscriber);
}

void LevelHistory::unsubscribe (Subscriber& subscriber)
{
    subscribers.erase (std::remove (subscribers.begin(), subscribers.end(), &subscriber), subscribers.end());
}

void LevelHistory::tick (double)
{
    // The capacity follows the sample rate the processor was last prepared at.
    buffer.resize (static_cast<size_t> (std::max (1, processor.levelHistoryCapacity())));
    const int read = processor.readLevels (buffer.data(), static_cast<int> (buffer.size()));
    arriving = read > 0;
    if (! arriving)
        return;
    const std::span<const LevelRecord> records (buffer.data(), static_cast<size_t> (read));
    // A copy, so a subscriber may unsubscribe as it's handed the records.
    for (auto* subscriber : std::vector<Subscriber*> (subscribers))
        subscriber->levelsRead (records);
}

ShownLevel::ShownLevel (LevelHistory& h, double LevelRecord::* r, double low, double high)
    : history (h), reading (r), floor (low), ceiling (high), loudest (low)
{
    history.subscribe (*this);
}

ShownLevel::~ShownLevel() { history.unsubscribe (*this); }

void ShownLevel::levelsRead (std::span<const LevelRecord> records)
{
    for (const auto& record : records)
        loudest = std::max (loudest, record.*reading);
}

double ShownLevel::next (double seconds)
{
    const double level = std::clamp (ballistics.update (std::min (loudest, ceiling), seconds), floor, ceiling);
    loudest = floor;
    return level;
}

} // namespace comp1
