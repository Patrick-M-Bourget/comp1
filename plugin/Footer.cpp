#include "Footer.h"

#include "Parameters.h"
#include "comp1/Latency.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/ShowMenu.h"
#include "plugkit/ui/staple/LookAndFeel.h"

#include <algorithm>
#include <utility>

namespace comp1
{

namespace
{
int choiceOf (const juce::RangedAudioParameter& parameter) { return juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue())); }

double sampleRateOf (const PluginProcessor& processor)
{
    const double prepared = processor.preparedSampleRateHz();
    return prepared > 0.0 ? prepared : plugkit::ui::assumedSampleRate;
}
} // namespace

ChoiceChip::ChoiceChip (juce::RangedAudioParameter& p, plugkit::ui::EditHistory& h, Config c)
    : staple::TextChip ({}, Look::plain), plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), parameter (p), history (h),
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

ChoiceChip::~ChoiceChip() { endStep(); }

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
    const int direction = code == juce::KeyPress::downKey ? 1 : code == juce::KeyPress::upKey ? -1 : 0;
    if (direction == 0 || key.getModifiers().isAnyModifierKeyDown() || ! isEnabled())
        return staple::TextChip::keyPressed (key);
    staple::LookAndFeel::keyUsed (*this);
    const auto choicesOffered = offered();
    const auto at = std::find (choicesOffered.begin(), choicesOffered.end(), shownChoice());
    const auto index = (at == choicesOffered.end() ? 0 : static_cast<int> (at - choicesOffered.begin())) + direction;
    if (index >= 0 && index < static_cast<int> (choicesOffered.size()))
    {
        if (! std::exchange (stepping, true))
            history.beginTransaction();
        pick (choicesOffered[static_cast<size_t> (index)]);
    }
    return true;
}

bool ChoiceChip::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        endStep();
    return staple::TextChip::keyStateChanged (isKeyDown);
}

void ChoiceChip::focusLost (FocusChangeType cause)
{
    endStep();
    staple::TextChip::focusLost (cause);
}

void ChoiceChip::endStep()
{
    if (std::exchange (stepping, false))
        history.endTransaction();
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

Footer::Footer (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      globalBypassAttachment (p.parameterState(), parameters::globalBypassId, globalBypass),
      processingMode (parameters::parameterOf (p.parameterState(), parameters::processingModeId), p.session().editHistory(), {}),
      oversampling (parameters::parameterOf (p.parameterState(), parameters::oversamplingId),
                    p.session().editHistory(),
                    { .offered =
                          [&p] {
                              std::vector<int> allowed;
                              for (int i = 0; i <= static_cast<int> (maxOversampling (sampleRateOf (p))); ++i)
                                  allowed.push_back (i);
                              return allowed;
                          },
                      .shown =
                          [&p] {
                              const auto stored = static_cast<Oversampling> (choiceOf (parameters::parameterOf (p.parameterState(), parameters::oversamplingId)));
                              return static_cast<int> (oversamplingInForce (stored, sampleRateOf (p)));
                          },
                      .available = [&p] { return parameters::inHighQuality (p.parameterState()); } }),
      popover (p, history), uiScale (p.session())
{
    globalBypass.setClickingTogglesState (true);
    // Its "on" is bypassed, so on it shows Off.
    globalBypass.setOffLook (true);
    globalBypass.setIconSize (14.0f);
    globalBypass.setTitle (parameters::parameterOf (p.parameterState(), parameters::globalBypassId).getName (100));
    globalBypass.setTooltip ("Global Bypass (Cmd/Ctrl+B)");
    // Left to right.
    int order = 0;
    for (juce::Component* child : std::initializer_list<juce::Component*> { &globalBypass, &processingMode, &oversampling, &output, &uiScale })
    {
        addAndMakeVisible (*child);
        child->setExplicitFocusOrder (++order);
    }
    // Last, one of the two showing at a time, in the same place in Tab's order.
    ++order;
    for (auto* button : { &toCompact, &toExpanded })
    {
        button->setTitle (button->getName());
        button->setTooltip (button->getName());
        button->setExplicitFocusOrder (order);
        button->onClick = [this] {
            if (onSwitchView != nullptr)
                onSwitchView();
        };
        addChildComponent (*button);
    }
    showSwitchFor (p.view());

    output.onClick = [this] {
        if (popover.isOpen())
            popover.close();
        else
        {
            popover.openFrom (output);
            output.setOpen (popover.isOpen());
        }
    };
    // Back in the footer once closed, hidden, so it stays on the editor's tick.
    popover.onClose = [this] {
        output.setOpen (false);
        addChildComponent (popover);
    };
    addChildComponent (popover);
    showOutput();
}

Footer::~Footer()
{
    popover.onClose = nullptr;
    popover.close();
}

void Footer::showOutput()
{
    const double db = processor.parameterState().getRawParameterValue (parameters::outputGainId)->load (std::memory_order_relaxed)
                      + processor.appliedAutoGainDb();
    if (const auto text = plugkit::ui::outputReadoutText (db); text != output.getButtonText())
    {
        output.setButtonText (text);
        resized();
    }
    if (const bool bypassed = globalBypass.getToggleState(); bypassed != output.isNotApplied())
        output.setNotApplied (bypassed);
}

void Footer::showSwitchFor (View shown)
{
    auto& showing = shown == View::compact ? toExpanded : toCompact;
    auto& hiding = &showing == &toCompact ? toExpanded : toCompact;
    if (showing.isVisible() && ! hiding.isVisible())
        return;
    const bool focused = hiding.hasKeyboardFocus (false);
    showing.setVisible (true);
    hiding.setVisible (false);
    if (focused)
        showing.grabKeyboardFocus();
}

void Footer::resized()
{
    // The buttons in a row centred down it, in from its sides, each chip as wide as its longest choice.
    auto row = getLocalBounds().reduced (inset, 0);
    row = row.withSizeKeepingCentre (row.getWidth(), buttonSize);
    globalBypass.setBounds (row.removeFromLeft (buttonSize));
    const auto switchArea = row.removeFromRight (buttonSize);
    toCompact.setBounds (switchArea);
    toExpanded.setBounds (switchArea);
    row.removeFromRight (chipGap);
    uiScale.setBounds (row.removeFromRight (buttonSize));
    row.removeFromRight (chipGap);
    output.setBounds (row.removeFromRight (std::max (outputWidth, output.getIdealWidth())));
    for (auto* chip : { &processingMode, &oversampling })
    {
        row.removeFromLeft (chipGap);
        chip->setBounds (row.removeFromLeft (chip->widestIdealWidth()));
    }
}

} // namespace comp1
