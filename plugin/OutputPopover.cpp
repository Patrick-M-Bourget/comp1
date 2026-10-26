#include "OutputPopover.h"

#include "Parameters.h"
#include "ShownAutoGain.h"
#include "plugkit/ui/OutputReadout.h"

#include <cmath>

namespace comp1
{

FooterReading outputReading (PluginProcessor& processor)
{
    return { .readout = std::make_unique<plugkit::ui::OutputReadout>(),
             .show =
                 [&processor] (plugkit::ui::FooterReadout& readout) {
                     auto& state = processor.parameterState();
                     const double db = state.getRawParameterValue (parameters::outputGainId)->load (std::memory_order_relaxed)
                                       + processor.appliedAutoGainDb();
                     const bool parallel = ! std::isinf (state.getRawParameterValue (parameters::dryId)->load (std::memory_order_relaxed));
                     // Two bars, as Manrope, the readout's font, has no U+2225 (parallel to).
                     static const juce::String mark = "|| ";
                     if (const auto text = (parallel ? mark : juce::String()) + plugkit::ui::outputReadoutText (db); text != readout.getButtonText())
                         readout.setButtonText (text);
                     juce::StringArray description;
                     if (parallel)
                         description.add ("Parallel compression on: Dry is mixed in");
                     if (const auto amount = shownAutoGain (processor))
                         description.add ("Includes Auto Gain: " + *amount);
                     if (const auto said = description.joinIntoString (". "); said != readout.getDescription())
                         readout.setDescription (said);
                     if (const bool bypassed = parameters::isOn (*state.getRawParameterValue (parameters::globalBypassId)); bypassed != readout.isNotApplied())
                         readout.setNotApplied (bypassed);
                 },
             .widestTexts = [] (const plugkit::ui::FooterReadout& readout) -> juce::StringArray { return { readout.getButtonText() }; } };
}

} // namespace comp1
