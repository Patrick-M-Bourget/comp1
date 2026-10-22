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

} // namespace comp1
