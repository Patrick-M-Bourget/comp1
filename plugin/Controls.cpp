#include "Controls.h"

#include "Parameters.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/ValueText.h"
#include "plugkit/ui/staple/Fonts.h"
#include "plugkit/ui/staple/LookAndFeel.h"
#include "plugkit/ui/staple/Painting.h"
#include "plugkit/ui/staple/Tokens.h"
#include "plugkit/ui/staple/controls/ParseValue.h"

#include <algorithm>
#include <cmath>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;

const juce::String infinitySign { juce::CharPointer_UTF8 ("\xe2\x88\x9e") };

// "4", "4.5", "4.29": Ratio to its parameter's two decimals, without the zeros they end in.
juce::String withoutTrailingZeros (double value)
{
    auto text = juce::String (value, 2);
    if (text.containsChar ('.'))
        text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return text;
}
} // namespace

namespace controls
{

juce::String spokenValue (const juce::RangedAudioParameter& parameter, double plain)
{
    const auto& id = parameter.getParameterID();
    if (id == parameters::ratioId)
        return (std::isinf (plain) ? infinitySign : withoutTrailingZeros (plain)) + ":1";
    if (id == parameters::ceilingId)
        return plugkit::ui::valueText::signedDb (plain, { .decimals = 2, .unit = false }) + " dBFS";
    return plugkit::ui::accessibility::spokenValue (parameter, parameter.convertTo0to1 (static_cast<float> (plain)));
}

double valueFromText (const juce::RangedAudioParameter& parameter, const juce::String& text)
{
    const auto unit = parameter.getLabel();
    if (const auto parsed = staple::parseValue (text, unit))
        return *parsed;
    auto trimmed = text.trim();
    if (unit.isNotEmpty() && trimmed.endsWithIgnoreCase (unit))
        trimmed = trimmed.dropLastCharacters (unit.length()).trim();
    return parameter.convertFrom0to1 (parameter.getValueForText (trimmed));
}

void showRangeOf (juce::Slider& knob, const juce::RangedAudioParameter& parameter)
{
    // As SliderParameterAttachment sets a slider up, so the knob moves as an attached one does.
    const auto range = parameter.getNormalisableRange();
    juce::NormalisableRange<double> shown {
        range.start,
        range.end,
        [range] (double, double, double normalised) { return static_cast<double> (range.convertFrom0to1 (static_cast<float> (normalised))); },
        [range] (double, double, double plain) { return static_cast<double> (range.convertTo0to1 (static_cast<float> (plain))); },
        [range] (double, double, double plain) { return static_cast<double> (range.snapToLegalValue (static_cast<float> (plain))); }
    };
    shown.interval = range.interval;
    shown.skew = range.skew;
    shown.symmetricSkew = range.symmetricSkew;
    knob.setNormalisableRange (shown);
    knob.textFromValueFunction = [&parameter] (double plain) { return parameter.getText (parameter.convertTo0to1 (static_cast<float> (plain)), 0); };
    knob.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

void describe (staple::Knob& knob, const juce::RangedAudioParameter& parameter)
{
    knob.setTitle (parameter.getName (100));
    knob.spokenValue = [&parameter] (double plain) { return spokenValue (parameter, plain); };
    knob.valueFromTextFunction = [&parameter] (const juce::String& text) { return valueFromText (parameter, text); };
}

} // namespace controls

ControlSection::KnobControl::KnobControl (juce::RangedAudioParameter& p, float diameter, const juce::String& labelText)
    : parameter (p), knob (diameter, p.getName (100))
{
    knob.setArcColour (colour::curveMain);
    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (staple::font (size::fs2));
    label.setColour (juce::Label::textColourId, colour::text2);
    label.setInterceptsMouseClicks (false, false);
    // The knob is read by its title; its label would read the same name again.
    label.setAccessible (false);
}

ControlSection::ControlSection (const juce::String& title, PluginProcessor& p)
    : Section (title), plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p)
{
}

ControlSection::~ControlSection() = default;

juce::RangedAudioParameter& ControlSection::parameter (const juce::String& id) { return parameters::parameterOf (processor.parameterState(), id); }

void ControlSection::setAvailability (std::function<bool()> available)
{
    availability = std::move (available);
    tick (0.0);
}

ControlSection::Column& ControlSection::columnFor (bool newColumn)
{
    if (newColumn || columns.empty() || columns.back().knob != nullptr)
        columns.emplace_back();
    return columns.back();
}

void ControlSection::addControl (juce::Component& control, int width, int height, bool newColumn)
{
    columnFor (newColumn).stack.push_back ({ &control, width, height });
    control.setExplicitFocusOrder (++focusOrder);
    addAndMakeVisible (control);
}

void ControlSection::addSelfPlaced (juce::Component& control)
{
    control.setExplicitFocusOrder (++focusOrder);
    addAndMakeVisible (control);
}

ControlSection::KnobControl& ControlSection::addKnob (const juce::String& parameterId, float diameter, const juce::String& labelText, Place place, bool attached)
{
    auto& control = *knobs.emplace_back (std::make_unique<KnobControl> (parameter (parameterId), diameter, labelText));
    if (attached)
        control.attachment = std::make_unique<juce::SliderParameterAttachment> (control.parameter, control.knob);
    else
        controls::showRangeOf (control.knob, control.parameter);
    controls::describe (control.knob, control.parameter);
    control.knob.setLabel (&control.label);
    if (place == Place::ownColumn || columns.empty() || columns.back().knob != nullptr)
        columns.emplace_back();
    columns.back().knob = &control;
    control.knob.setExplicitFocusOrder (++focusOrder);
    addAndMakeVisible (control.knob);
    addAndMakeVisible (control.label);
    return control;
}

juce::ToggleButton& ControlSection::addToggle (const juce::String& parameterId, const juce::String& text, bool newColumn, bool attached)
{
    auto& p = parameter (parameterId);
    auto owned = std::make_unique<juce::ToggleButton> (text);
    auto& toggle = *owned;
    others.push_back (std::move (owned));
    toggle.setTitle (p.getName (100));
    toggle.setTooltip (p.getName (100));
    if (attached)
        buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (p, toggle));
    addControl (toggle, staple::LookAndFeel::idealToggleWidth (text), toggleHeight, newColumn);
    return toggle;
}

juce::ComboBox& ControlSection::addChoice (const juce::String& parameterId, int width, bool newColumn)
{
    auto& p = parameter (parameterId);
    auto owned = std::make_unique<juce::ComboBox> (p.getName (100));
    auto& box = *owned;
    others.push_back (std::move (owned));
    box.addItemList (p.getAllValueStrings(), 1);
    box.setTitle (p.getName (100));
    box.setTooltip (p.getName (100));
    choiceAttachments.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (p, box));
    addControl (box, width, choiceHeight, newColumn);
    return box;
}

staple::EdgeSelector& ControlSection::addEdgeSelector (const juce::String& parameterId,
                                                       const std::vector<staple::Icon>& icons,
                                                       const std::vector<std::optional<staple::Icon>>& contexts,
                                                       bool newColumn)
{
    auto& p = parameter (parameterId);
    auto owned = std::make_unique<staple::EdgeSelector> (p.getName (100));
    auto& selector = *owned;
    others.push_back (std::move (owned));
    const auto choices = p.getAllValueStrings();
    jassert (static_cast<size_t> (choices.size()) == icons.size());
    for (int i = 0; i < choices.size(); ++i)
    {
        const auto index = static_cast<size_t> (i);
        selector.addItem (choices[i], i + 1, icons[index], std::nullopt, index < contexts.size() ? contexts[index] : std::nullopt);
    }
    selector.setTitle (p.getName (100));
    selector.setTooltip (p.getName (100));
    choiceAttachments.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (p, selector));
    addControl (selector, edgeSelectorWidth, edgeSelectorHeight, newColumn);
    return selector;
}

void ControlSection::KnobControl::setRing (staple::Knob::RingHandler& handler)
{
    // The lane grows with the face above the kit's Gain size, as the knob's strokes do (staple::knobStrokes), so the
    // hero's ring reads as the same recipe.
    const float scale = staple::knobStrokeScale (knob.getDiameter());
    knob.setRing (&handler, scale * staple::tokens::knob::ringLane, scale * staple::tokens::knob::ringOffset);
    ringed = true;
}

int ControlSection::knobExtent (const KnobControl& control)
{
    // A ring's lane reaches past the face (staple::Knob::setRing), and the label goes under it.
    if (! control.ringed)
        return 0;
    const float scale = staple::knobStrokeScale (control.knob.getDiameter());
    return juce::roundToInt (scale * (staple::tokens::knob::ringOffset + staple::tokens::knob::ringLane / 2.0f));
}

int ControlSection::columnWidth (const Column& column) const
{
    int width = 0;
    for (const auto& item : column.stack)
        width = std::max (width, item.width);
    if (column.knob != nullptr)
        width = std::max ({ width,
                            minimumKnobColumn,
                            juce::roundToInt (column.knob->knob.getDiameter()) + 2 * knobExtent (*column.knob) + knobSidePadding });
    return width;
}

int ControlSection::stackHeight (const Column& column) const
{
    int height = 0;
    for (const auto& item : column.stack)
        height += item.height + rowGap;
    return height;
}

int ControlSection::knobTop() const
{
    int top = 0;
    for (const auto& column : columns)
        if (column.knob != nullptr)
            top = std::max (top, stackHeight (column) + knobExtent (*column.knob));
    return top;
}

int ControlSection::idealWidth() const
{
    int width = 0;
    for (const auto& column : columns)
        width += columnWidth (column) + columnGap;
    return std::max (titleWidth() + 2, width - columnGap);
}

int ControlSection::idealHeight() const
{
    int height = 0;
    const int top = knobTop();
    for (const auto& column : columns)
    {
        height = std::max (height, stackHeight (column));
        if (column.knob != nullptr)
            height = std::max (height,
                               top + juce::roundToInt (column.knob->knob.getDiameter()) + 2 * knobExtent (*column.knob) + labelGap + labelHeight);
    }
    return titleHeight + dividerGap + height;
}

void ControlSection::resized()
{
    const auto area = controlArea();
    const int top = knobTop();
    int x = area.getX();
    for (const auto& column : columns)
    {
        const int width = columnWidth (column);
        int y = area.getY();
        for (const auto& item : column.stack)
        {
            item.component->setBounds (x, y, std::min (width, item.width), item.height);
            y += item.height + rowGap;
        }
        if (auto* control = column.knob)
        {
            // The face at the top of the knobs' line, centred in the column; the knob's square takes in its shadow
            // and ring, which may reach over its neighbours: only its face and ring take the mouse.
            const int diameter = juce::roundToInt (control->knob.getDiameter());
            const int extent = knobExtent (*control);
            const juce::Point<int> centre { x + width / 2, area.getY() + top + diameter / 2 };
            const int side = control->knob.getIdealSize();
            control->knob.setBounds (juce::Rectangle<int> (side, side).withCentre (centre));
            control->label.setBounds (x, centre.y + diameter / 2 + extent + labelGap, width, labelHeight);
        }
        x += width + columnGap;
    }
}

void ControlSection::tick (double)
{
    follow();
    const bool available = availability == nullptr || availability();
    for (auto* child : getChildren())
        if (const bool enabled = available && isControlEnabled (*child); child->isEnabled() != enabled)
            child->setEnabled (enabled);
}

} // namespace comp1
