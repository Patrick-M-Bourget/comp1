#include "SaturationPopover.h"

#include "ChipWidth.h"
#include "Parameters.h"
#include "plugkit/ui/ValueText.h"

namespace comp1
{

juce::String saturationReadoutText (double driveDb) { return "SAT " + plugkit::ui::valueText::signedDb (driveDb, { .decimals = 0 }); }

SaturationItem::SaturationItem (PluginProcessor& p)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      shown ("Saturation", "SAT", "Saturation: its Drive, dimmed while Saturation is off. Click for Saturation's controls"),
      saturationPopover (p), opens (shown, saturationPopover, *this)
{
    addAndMakeVisible (shown);
    show();
}

int SaturationItem::idealWidth()
{
    const auto topDb = parameters::parameterOf (processor.parameterState(), parameters::driveId).getNormalisableRange().end;
    return widestIdealWidth (shown, { saturationReadoutText (topDb) });
}

void SaturationItem::show()
{
    auto& state = processor.parameterState();
    const double driveDb = state.getRawParameterValue (parameters::driveId)->load (std::memory_order_relaxed);
    if (const auto text = saturationReadoutText (driveDb); text != shown.getButtonText())
        shown.setButtonText (text);
    const bool off = ! parameters::isOn (*state.getRawParameterValue (parameters::saturationId));
    if (const bool notApplied = off || parameters::isOn (*state.getRawParameterValue (parameters::globalBypassId)); notApplied != shown.isNotApplied())
        shown.setNotApplied (notApplied);
}

void SaturationItem::resized() { shown.setBounds (getLocalBounds()); }

} // namespace comp1
