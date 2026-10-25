#include "Footer.h"

#include "Parameters.h"
#include "ShownAutoGain.h"
#include "comp1/Latency.h"
#include "comp1/Style.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>

namespace comp1
{

namespace
{
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
    const auto amount = shownAutoGain (processor);
    juce::StringArray description;
    if (parallel)
        description.add ("Parallel compression on: Dry is mixed in");
    if (amount.has_value())
        description.add ("Includes Auto Gain: " + *amount);
    if (const auto said = description.joinIntoString (". "); said != output.getDescription())
        output.setDescription (said);
    if (const bool bypassed = globalBypass.getToggleState(); bypassed != output.isNotApplied())
        output.setNotApplied (bypassed);

    if (amount.has_value())
        if (const auto tooltip = "Auto Gain: " + *amount; tooltip != dot.getTooltip())
            dot.setTooltip (tooltip);
    if (const bool shown = amount.has_value(); shown != dot.isVisible())
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
