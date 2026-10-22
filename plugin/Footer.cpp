#include "Footer.h"

#include "Parameters.h"
#include "comp1/Latency.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/ShowMenu.h"
#include "plugkit/ui/staple/LookAndFeel.h"

#include <algorithm>

namespace comp1
{

namespace
{
juce::RangedAudioParameter& parameterOf (PluginProcessor& processor, const juce::String& id) { return *processor.parameterState().getParameter (id); }

int choiceOf (const juce::RangedAudioParameter& parameter) { return juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue())); }

bool inHighQuality (PluginProcessor& processor)
{
    return choiceOf (parameterOf (processor, parameters::processingModeId)) == static_cast<int> (ProcessingMode::HighQuality);
}

double sampleRateOf (const PluginProcessor& processor)
{
    const double prepared = processor.preparedSampleRateHz();
    return prepared > 0.0 ? prepared : plugkit::ui::assumedSampleRate;
}
} // namespace

ChoiceChip::ChoiceChip (juce::RangedAudioParameter& p, Config c)
    : staple::TextChip ({}, Look::plain), plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), parameter (p),
      choices (p.getAllValueStrings()), config (std::move (c))
{
    setName (parameter.getName (100));
    setTitle (parameter.getName (100));
    setTooltip (parameter.getName (100));
    setTextIsValue (true);
    setChevron (true);
    onClick = [this] {
        plugkit::ui::showMenu (menu(), *this, juce::PopupMenu::Options().withTargetComponent (this));
    };
    show();
}

std::vector<int> ChoiceChip::offered() const
{
    if (config.offered != nullptr)
        return config.offered();
    std::vector<int> all (static_cast<size_t> (choices.size()));
    for (int i = 0; i < choices.size(); ++i)
        all[static_cast<size_t> (i)] = i;
    return all;
}

int ChoiceChip::shownChoice() const { return config.shown != nullptr ? config.shown() : choiceOf (parameter); }

juce::PopupMenu ChoiceChip::menu()
{
    juce::PopupMenu popup;
    const int shown = shownChoice();
    for (const int choice : offered())
        popup.addItem (choices[choice], true, choice == shown, [this, choice] { pick (choice); });
    return popup;
}

void ChoiceChip::pick (int choice)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (choice)));
    parameter.endChangeGesture();
    show();
}

bool ChoiceChip::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const int direction = code == juce::KeyPress::downKey || code == juce::KeyPress::rightKey ? 1
                          : code == juce::KeyPress::upKey || code == juce::KeyPress::leftKey  ? -1
                                                                                               : 0;
    if (direction == 0 || key.getModifiers().isAnyModifierKeyDown() || ! isEnabled())
        return staple::TextChip::keyPressed (key);
    staple::LookAndFeel::keyUsed (*this);
    const auto choicesOffered = offered();
    const auto at = std::find (choicesOffered.begin(), choicesOffered.end(), shownChoice());
    const auto index = (at == choicesOffered.end() ? 0 : static_cast<int> (at - choicesOffered.begin())) + direction;
    if (index >= 0 && index < static_cast<int> (choicesOffered.size()))
        pick (choicesOffered[static_cast<size_t> (index)]);
    return true;
}

int ChoiceChip::widestIdealWidth()
{
    const auto shown = getButtonText();
    int widest = 0;
    for (const auto& choice : choices)
    {
        setButtonText (choice);
        widest = std::max (widest, getIdealWidth());
    }
    setButtonText (shown);
    return widest;
}

void ChoiceChip::show()
{
    const auto text = choices[shownChoice()];
    if (getButtonText() != text)
        setButtonText (text);
    const bool available = config.available == nullptr || config.available();
    if (isEnabled() != available)
        setEnabled (available);
}

Footer::Footer (PluginProcessor& p)
    : globalBypassAttachment (p.parameterState(), parameters::globalBypassId, globalBypass),
      processingMode (parameterOf (p, parameters::processingModeId), {}),
      oversampling (parameterOf (p, parameters::oversamplingId),
                    { .offered =
                          [&p] {
                              std::vector<int> allowed;
                              for (int i = 0; i <= static_cast<int> (maxOversampling (sampleRateOf (p))); ++i)
                                  allowed.push_back (i);
                              return allowed;
                          },
                      .shown =
                          [&p] {
                              const auto stored = static_cast<Oversampling> (choiceOf (parameterOf (p, parameters::oversamplingId)));
                              return static_cast<int> (oversamplingInForce (stored, sampleRateOf (p)));
                          },
                      .available = [&p] { return inHighQuality (p); } }),
      uiScale (p.session())
{
    globalBypass.setClickingTogglesState (true);
    // Its "on" is bypassed, so on it shows Off.
    globalBypass.setOffLook (true);
    globalBypass.setIconSize (14.0f);
    globalBypass.setTitle (p.parameterState().getParameter (parameters::globalBypassId)->getName (100));
    globalBypass.setTooltip ("Global Bypass (Cmd/Ctrl+B)");
    // Left to right.
    int order = 0;
    for (juce::Component* child : std::initializer_list<juce::Component*> { &globalBypass, &processingMode, &oversampling, &uiScale })
    {
        addAndMakeVisible (*child);
        child->setExplicitFocusOrder (++order);
    }
}

void Footer::resized()
{
    // The buttons in a row centred down it, in from its sides, each chip as wide as its longest choice.
    auto row = getLocalBounds().reduced (inset, 0);
    row = row.withSizeKeepingCentre (row.getWidth(), buttonSize);
    globalBypass.setBounds (row.removeFromLeft (buttonSize));
    uiScale.setBounds (row.removeFromRight (buttonSize));
    for (auto* chip : { &processingMode, &oversampling })
    {
        row.removeFromLeft (chipGap);
        chip->setBounds (row.removeFromLeft (chip->widestIdealWidth()));
    }
}

} // namespace comp1
