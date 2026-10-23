#include "Footer.h"

#include "Parameters.h"
#include "comp1/Latency.h"
#include "comp1/Style.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/ShowMenu.h"
#include "plugkit/ui/ValueText.h"
#include "plugkit/ui/staple/LookAndFeel.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace comp1
{

namespace
{
int choiceOf (const juce::RangedAudioParameter& parameter) { return juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue())); }

// The Style chip's: the offered Styles' choices, its text and its menu items (Footer).
ChoiceChip::Config styleChip (PluginProcessor& processor, const juce::StringArray& names)
{
    // The Characters are Style's first four, in its order (comp1/Settings.h), so each is named as its Style is.
    const auto item = [names] (int choice) {
        const int character = static_cast<int> (characterOf (static_cast<Style> (choice)));
        return character == choice ? names[choice] : names[choice] + " (" + names[character] + ")";
    };
    return { .offered =
                 [] {
                     std::vector<int> offered;
                     for (const auto style : PluginProcessor::offeredStyles())
                         offered.push_back (static_cast<int> (style));
                     return offered;
                 },
             .text = [names] (int choice) { return "Style: " + names[choice]; },
             .item = item,
             .choose = [&processor] (int choice) { processor.chooseStyle (static_cast<Style> (choice)); },
             .steps = false };
}

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

juce::String ChoiceChip::textOf (int choice) const { return config.text != nullptr ? config.text (choice) : choices[choice]; }

juce::String ChoiceChip::itemOf (int choice) const { return config.item != nullptr ? config.item (choice) : choices[choice]; }

std::unique_ptr<juce::AccessibilityHandler> ChoiceChip::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::button, [this] { return itemOf (shownChoice()); }, [this] { triggerClick(); });
}

juce::PopupMenu ChoiceChip::menu()
{
    juce::PopupMenu popup;
    const int shown = shownChoice();
    for (const int choice : offered())
        popup.addItem (itemOf (choice), true, choice == shown, [this, choice] { pick (choice); });
    return popup;
}

void ChoiceChip::pick (int choice)
{
    if (config.choose != nullptr)
    {
        config.choose (choice);
        show();
        return;
    }
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (choice)));
    parameter.endChangeGesture();
    show();
}

bool ChoiceChip::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const int direction = code == juce::KeyPress::downKey ? 1 : code == juce::KeyPress::upKey ? -1 : 0;
    if (direction == 0 || ! config.steps || key.getModifiers().isAnyModifierKeyDown() || ! isEnabled())
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
    for (int choice = 0; choice < choices.size(); ++choice)
    {
        setButtonText (textOf (choice));
        widest = std::max (widest, getIdealWidth());
    }
    setButtonText (shown);
    return widest;
}

void ChoiceChip::show()
{
    const auto text = textOf (shownChoice());
    if (getButtonText() != text)
        setButtonText (text);
    const bool available = config.available == nullptr || config.available();
    if (isEnabled() != available)
        setEnabled (available);
}

AutoGainDot::AutoGainDot()
{
    setWantsKeyboardFocus (false);
    setAccessible (false);
}

void AutoGainDot::paint (juce::Graphics& g)
{
    g.setColour (staple::tokens::colour::text1);
    g.fillEllipse (getLocalBounds().toFloat().withSizeKeepingCentre (diameter, diameter));
}

Footer::Footer (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      globalBypassAttachment (p.parameterState(), parameters::globalBypassId, globalBypass),
      style (parameters::parameterOf (p.parameterState(), parameters::styleId),
             p.session().editHistory(),
             styleChip (p, parameters::parameterOf (p.parameterState(), parameters::styleId).getAllValueStrings())),
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
    for (juce::Component* child : std::initializer_list<juce::Component*> { &globalBypass, &style, &processingMode, &oversampling, &output, &uiScale })
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
    addChildComponent (dot);
    showOutput();
}

Footer::~Footer()
{
    popover.onClose = nullptr;
    popover.close();
}

void Footer::showOutput()
{
    auto& state = processor.parameterState();
    const double autoGainDb = processor.appliedAutoGainDb();
    const double db = state.getRawParameterValue (parameters::outputGainId)->load (std::memory_order_relaxed) + autoGainDb;
    const bool parallel = ! std::isinf (state.getRawParameterValue (parameters::dryId)->load (std::memory_order_relaxed));
    // Two bars, as Manrope, the readout's font, has no U+2225 (parallel to).
    static const juce::String mark = "|| ";
    if (const auto text = (parallel ? mark : juce::String()) + plugkit::ui::outputReadoutText (db); text != output.getButtonText())
    {
        output.setButtonText (text);
        resized();
    }
    // Auto Gain counts as included once its amount reads other than zero, as the readout writes it.
    const auto amount = plugkit::ui::valueText::signedDb (autoGainDb);
    const bool includesAutoGain = amount != plugkit::ui::valueText::signedDb (0.0);
    juce::StringArray description;
    if (parallel)
        description.add ("Parallel compression on: Dry is mixed in");
    if (includesAutoGain)
        description.add ("Includes Auto Gain: " + amount);
    if (const auto said = description.joinIntoString (". "); said != output.getDescription())
        output.setDescription (said);
    if (const bool bypassed = globalBypass.getToggleState(); bypassed != output.isNotApplied())
        output.setNotApplied (bypassed);

    if (const auto tooltip = "Auto Gain: " + amount; tooltip != dot.getTooltip())
        dot.setTooltip (tooltip);
    if (const bool shown = includesAutoGain && ! output.isNotApplied(); shown != dot.isVisible())
        dot.setVisible (shown);
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
    // The gap left of the UI Scale menu holds the Auto Gain dot, so the readout doesn't move as it shows.
    dot.setBounds (row.removeFromRight (chipGap));
    output.setBounds (row.removeFromRight (std::max (outputWidth, output.getIdealWidth())));
    for (auto* chip : { &style, &processingMode, &oversampling })
    {
        row.removeFromLeft (chipGap);
        chip->setBounds (row.removeFromLeft (chip->widestIdealWidth()));
    }
}

} // namespace comp1
