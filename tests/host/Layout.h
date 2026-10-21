#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The bus layouts comp1 is checked at, for the Plugin Shell tests (tests/plugin/Host.h) and the Sidechain routing
// check alike. JUCE only, so the routing check, which has no test framework, can include it.
namespace host
{

// A main bus in and out, and a Sidechain in.
inline juce::AudioProcessor::BusesLayout layout (const juce::AudioChannelSet& main, const juce::AudioChannelSet& sidechain)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (main);
    layout.inputBuses.add (sidechain);
    layout.outputBuses.add (main);
    return layout;
}

} // namespace host
