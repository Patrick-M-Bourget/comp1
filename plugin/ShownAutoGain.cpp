#include "ShownAutoGain.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "plugkit/ui/ValueText.h"

#include <cmath>

namespace comp1
{

std::optional<juce::String> shownAutoGain (PluginProcessor& processor)
{
    if (parameters::inGlobalBypass (processor.parameterState()))
        return std::nullopt;
    const double db = processor.appliedAutoGainDb();
    // Tenths of a dB, as signedDb writes it to one decimal.
    if (juce::exactlyEqual (std::round (std::abs (db) * 10.0), 0.0))
        return std::nullopt;
    return plugkit::ui::valueText::signedDb (db);
}

} // namespace comp1
