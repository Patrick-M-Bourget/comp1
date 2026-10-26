#include "HighQualityPopover.h"

#include "ChipWidth.h"
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

// The Oversampling in force: the stored one, or the cap at the processor's sample rate when it is over it.
int oversamplingInForceOf (PluginProcessor& processor)
{
    const auto stored = static_cast<Oversampling> (choiceOf (oversamplingOf (processor)));
    return static_cast<int> (oversamplingInForce (stored, sampleRateOf (processor)));
}

juce::String readoutText (PluginProcessor& processor, int choice) { return "HQ " + oversamplingOf (processor).getAllValueStrings()[choice]; }
} // namespace

HighQualityReadout::HighQualityReadout (PluginProcessor& p)
    : FooterReadout ("High Quality", "HQ", "High Quality: the Oversampling in force. Click for Oversampling and Lookahead"),
      plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p)
{
    show();
}

int HighQualityReadout::widestIdealWidth()
{
    juce::StringArray texts;
    for (int choice = 0; choice < oversamplingOf (processor).getAllValueStrings().size(); ++choice)
        texts.add (readoutText (processor, choice));
    return comp1::widestIdealWidth (*this, texts);
}

void HighQualityReadout::show()
{
    if (const auto text = readoutText (processor, oversamplingInForceOf (processor)); text != getButtonText())
        setButtonText (text);
    if (const bool zeroLatency = ! parameters::inHighQuality (processor.parameterState()); zeroLatency != isNotApplied())
        setNotApplied (zeroLatency);
}

HighQualitySection::HighQualitySection (PluginProcessor& p)
    : ControlSection ("High Quality", p),
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
    addKnob (parameters::lookaheadId, knobSize::medium, "Lookahead");
    addSelfPlaced (oversampling);
    chipWidth = oversampling.widestIdealWidth();
    setAvailability ([&p] { return parameters::inHighQuality (p.parameterState()); });
}

int HighQualitySection::idealWidth() const { return std::max (ControlSection::idealWidth(), chipWidth); }

int HighQualitySection::idealHeight() const { return ControlSection::idealHeight() + chipTopGap + chipHeight; }

void HighQualitySection::resized()
{
    ControlSection::resized();
    oversampling.setBounds (controlArea().removeFromBottom (chipHeight).withWidth (chipWidth));
}

} // namespace comp1
