#include "ProcessingModePopover.h"

#include "Parameters.h"
#include "Sections.h"
#include "comp1/Latency.h"
#include "plugkit/ui/AssumedSampleRate.h"

#include <algorithm>

namespace comp1
{

namespace
{
double sampleRateOf (const PluginProcessor& processor)
{
    const double prepared = processor.preparedSampleRateHz();
    return prepared > 0.0 ? prepared : plugkit::ui::assumedSampleRate;
}

juce::RangedAudioParameter& oversamplingOf (PluginProcessor& processor)
{
    return parameters::parameterOf (processor.parameterState(), parameters::oversamplingId);
}

juce::RangedAudioParameter& processingModeOf (PluginProcessor& processor)
{
    return parameters::parameterOf (processor.parameterState(), parameters::processingModeId);
}

// The Oversampling in force: the stored one, or the cap at the processor's sample rate when it is over it.
int oversamplingInForceOf (PluginProcessor& processor)
{
    const auto stored = static_cast<Oversampling> (choiceOf (oversamplingOf (processor)));
    return static_cast<int> (oversamplingInForce (stored, sampleRateOf (processor)));
}

// The readout's text: High Quality's name and an Oversampling's, "High Quality 4x", or Zero Latency's name.
juce::String highQualityText (PluginProcessor& processor, int oversampling)
{
    return processingModeOf (processor).getAllValueStrings()[static_cast<int> (ProcessingMode::HighQuality)] + " "
           + oversamplingOf (processor).getAllValueStrings()[oversampling];
}

juce::String zeroLatencyText (PluginProcessor& processor)
{
    return processingModeOf (processor).getAllValueStrings()[static_cast<int> (ProcessingMode::ZeroLatency)];
}
} // namespace

FooterReading processingModeReading (PluginProcessor& processor)
{
    return { .readout = std::make_unique<plugkit::ui::FooterReadout> (
                 "Processing Mode", "Mode",
                 "Processing Mode: High Quality and the Oversampling in force, or Zero Latency. Click for Processing Mode, Oversampling and Lookahead"),
             .show =
                 [&processor] (plugkit::ui::FooterReadout& readout) {
                     auto& state = processor.parameterState();
                     const auto text = parameters::inHighQuality (state) ? highQualityText (processor, oversamplingInForceOf (processor))
                                                                         : zeroLatencyText (processor);
                     if (text != readout.getButtonText())
                         readout.setButtonText (text);
                     if (const bool bypassed = parameters::isOn (*state.getRawParameterValue (parameters::globalBypassId)); bypassed != readout.isNotApplied())
                         readout.setNotApplied (bypassed);
                 },
             .widestTexts = [&processor] (const plugkit::ui::FooterReadout&) {
                 juce::StringArray texts { zeroLatencyText (processor) };
                 for (int choice = 0; choice < oversamplingOf (processor).getAllValueStrings().size(); ++choice)
                     texts.add (highQualityText (processor, choice));
                 return texts;
             } };
}

ProcessingModeSection::ProcessingModeSection (PluginProcessor& p)
    : ControlSection ("Processing Mode", p), processor (p), processingMode (processingModeOf (p), p.session().editHistory(), {}),
      oversampling (oversamplingOf (p),
                    p.session().editHistory(),
                    { .offered =
                          [&p] {
                              std::vector<int> allowed;
                              for (int i = 0; i <= static_cast<int> (maxOversampling (sampleRateOf (p))); ++i)
                                  allowed.push_back (i);
                              return allowed;
                          },
                      .shown = [&p] { return oversamplingInForceOf (p); },
                      .available = [&p] { return parameters::inHighQuality (p.parameterState()); },
                      .text = [names = oversamplingOf (p).getAllValueStrings()] (int choice) { return "Oversampling: " + names[choice]; } })
{
    // In Tab's order: Processing Mode, Lookahead, Oversampling.
    addSelfPlaced (processingMode);
    lookahead = &addKnob (parameters::lookaheadId, knobSize::medium, "Lookahead");
    addSelfPlaced (oversampling);
    chipWidth = std::max (processingMode.widestIdealWidth(), oversampling.widestIdealWidth());
}

bool ProcessingModeSection::isControlEnabled (juce::Component& control) const
{
    return &control == &processingMode || parameters::inHighQuality (processor.parameterState());
}

int ProcessingModeSection::idealWidth() const { return std::max (ControlSection::idealWidth(), chipWidth); }

int ProcessingModeSection::idealHeight() const { return ControlSection::idealHeight() + 2 * (chipGap + chipHeight); }

void ProcessingModeSection::resized()
{
    // The columns' layout puts the knob at the control area's top: it moves down under the Processing Mode chip.
    ControlSection::resized();
    auto area = controlArea();
    processingMode.setBounds (area.removeFromTop (chipHeight).withWidth (chipWidth));
    for (auto* c : { static_cast<juce::Component*> (&lookahead->knob), static_cast<juce::Component*> (&lookahead->label) })
        c->setTopLeftPosition (c->getPosition().translated (0, chipHeight + chipGap));
    oversampling.setBounds (area.removeFromBottom (chipHeight).withWidth (chipWidth));
}

} // namespace comp1
