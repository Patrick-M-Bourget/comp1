#include "SaturationPopover.h"

#include "Parameters.h"
#include "plugkit/ui/ValueText.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace
{
constexpr int edge = staple::tokens::popover::edge;
} // namespace

juce::String saturationReadoutText (double driveDb) { return "SAT " + plugkit::ui::valueText::signedDb (driveDb, { .decimals = 0 }); }

SaturationPopover::SaturationPopover (PluginProcessor& processor) : saturation (processor)
{
    addAndMakeVisible (saturation);
    const int inset = 2 * (edge + padding);
    setCardSize (saturation.idealWidth() + inset, saturation.idealHeight() + inset);
    followProcessor();
}

void SaturationPopover::resized() { saturation.setBounds (getCardBounds().reduced (edge + padding)); }

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
    const auto text = shown.getButtonText();
    shown.setButtonText (saturationReadoutText (parameters::parameterOf (processor.parameterState(), parameters::driveId).getNormalisableRange().end));
    const int widest = shown.getIdealWidth();
    shown.setButtonText (text);
    return widest;
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
