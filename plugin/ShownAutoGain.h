#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace comp1
{

class PluginProcessor;

// The Auto Gain amount the editor shows (PluginProcessor::appliedAutoGainDb), signed to one decimal, "+4.2 dB"; or
// nothing while it rounds to 0.0 dB at one decimal or Global Bypass is on. The Output readout's description, the
// Auto Gain dot and the A toggle's tooltip all read it, so they agree on when there is an amount to show.
std::optional<juce::String> shownAutoGain (PluginProcessor& processor);

} // namespace comp1
