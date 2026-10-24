#include "CompactView.h"

#include "Parameters.h"
#include "Sections.h"
#include "plugkit/ui/staple/Fonts.h"
#include "plugkit/ui/staple/LookAndFeel.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace layout = staple::tokens::layout;

namespace
{
bool isOnOff (const juce::RangedAudioParameter& parameter) { return dynamic_cast<const juce::AudioParameterBool*> (&parameter) != nullptr; }

int diameterOf (float knobSize) { return juce::roundToInt (knobSize); }

// A knob's face, diameter across, at the top of area and centred across it, with its label on the line under it.
void place (staple::Knob& knob, juce::Label& label, juce::Rectangle<int> area)
{
    const int diameter = diameterOf (knob.getDiameter());
    const juce::Point<int> centre { area.getCentreX(), area.getY() + diameter / 2 };
    knob.setBounds (juce::Rectangle<int> (knob.getIdealSize(), knob.getIdealSize()).withCentre (centre));
    label.setBounds (area.getX(), area.getY() + diameter + ControlSection::labelGap, area.getWidth(), ControlSection::labelHeight);
}

// The height of a knob diameter across with its label.
int knobWithLabel (float diameter) { return diameterOf (diameter) + ControlSection::labelGap + ControlSection::labelHeight; }
} // namespace

CompactSlot::CompactSlot (const juce::String& name, PluginProcessor& p)
    : processor (p), primaryKnob (knobSize::large), companionKnob (knobSize::small)
{
    setName (name);
    controls::setUpKnob (primaryKnob, primaryLabel, {});
    controls::setUpKnob (companionKnob, companionLabel, {});
    // Tab's order: the primary, then the Companion, whichever kind shows.
    primaryKnob.setExplicitFocusOrder (1);
    companionKnob.setExplicitFocusOrder (2);
    companionToggle.setExplicitFocusOrder (2);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &primaryKnob, &primaryLabel, &companionKnob, &companionLabel, &companionToggle })
        addChildComponent (*c);
    primaryKnob.setVisible (true);
    primaryLabel.setVisible (true);
    fade.apply = [this] (float progress) { applyCrossfade (progress); };
}

CompactSlot::~CompactSlot() = default;

juce::RangedAudioParameter& CompactSlot::parameter (const CompactSlotControl& control)
{
    return parameters::parameterOf (processor.parameterState(), *control.parameterId);
}

void CompactSlot::show (const CompactSlotRow& row, bool crossfading)
{
    // The slot as it looks now, mid-crossfade included, at the screen's pixel density so it stays sharp.
    juce::Image looked;
    if (crossfading && isShowing() && ! getLocalBounds().isEmpty())
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        const auto density = static_cast<float> (display != nullptr ? display->scale : 1.0);
        looked = createComponentSnapshot (getLocalBounds(), true, density * juce::Component::getApproximateScaleFactorForComponent (this));
    }
    outgoing = looked;

    // Each attachment goes before its control is set up again, so nothing the set-up does reaches a parameter; a new
    // attachment shows its parameter's value without writing it.
    primaryAttachment.reset();
    companionKnobAttachment.reset();
    companionToggleAttachment.reset();

    auto& primaryParameter = parameter (row.primary);
    primaryAttachment = std::make_unique<juce::SliderParameterAttachment> (primaryParameter, primaryKnob);
    controls::describe (primaryKnob, primaryParameter);
    primaryLabel.setText (row.primary.text, juce::dontSendNotification);

    auto& companionParameter = parameter (row.companion);
    const bool toggle = isOnOff (companionParameter);
    if (toggle)
    {
        companionToggle.setButtonText (row.companion.text);
        companionToggle.setTitle (companionParameter.getName (100));
        companionToggle.setTooltip (companionParameter.getName (100));
        companionToggleAttachment = std::make_unique<juce::ButtonParameterAttachment> (companionParameter, companionToggle);
    }
    else
    {
        companionKnobAttachment = std::make_unique<juce::SliderParameterAttachment> (companionParameter, companionKnob);
        controls::describe (companionKnob, companionParameter);
        companionLabel.setText (row.companion.text, juce::dontSendNotification);
    }
    companionToggle.setVisible (toggle);
    companionKnob.setVisible (! toggle);
    companionLabel.setVisible (! toggle);
    resized();

    // A screen reader keeps the handlers it holds, so it is told their names and values changed: one reading a control
    // now hears the new parameter. Nothing is announced; the Style chip announces the Style.
    for (juce::Component* control : { static_cast<juce::Component*> (&primaryKnob), &companion() })
        if (auto* handler = control->getAccessibilityHandler())
        {
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        }

    if (outgoing.isValid())
    {
        fade.jump (0.0f);
        fade.towards (1.0f);
    }
    else
        fade.jump (1.0f);
}

void CompactSlot::applyCrossfade (float progress)
{
    for (auto* child : getChildren())
        child->setAlpha (progress);
    if (progress >= 1.0f)
        outgoing = {};
    repaint();
}

void CompactSlot::paintOverChildren (juce::Graphics& g)
{
    if (! outgoing.isValid())
        return;
    g.setOpacity (1.0f - fade.value());
    g.drawImage (outgoing, getLocalBounds().toFloat());
}

juce::Component& CompactSlot::companion()
{
    if (companionToggle.isVisible())
        return companionToggle;
    return companionKnob;
}

int CompactSlot::idealWidth()
{
    int width = diameterOf (knobSize::large) + ControlSection::knobSidePadding;
    for (const auto& row : compactSlots)
        for (const auto* slot : { &row.a, &row.b })
        {
            width = std::max (width, staple::textWidth (controls::labelFont(), slot->primary.text));
            // A Companion's text, as a toggle's (the wider of the two kinds).
            width = std::max ({ width, staple::textWidth (controls::labelFont(), slot->companion.text), staple::LookAndFeel::idealToggleWidth (slot->companion.text) });
        }
    return width;
}

int CompactSlot::idealHeight()
{
    return knobWithLabel (knobSize::large) + layout::gap + std::max (knobWithLabel (knobSize::small), ControlSection::toggleHeight);
}

void CompactSlot::resized()
{
    auto area = getLocalBounds();
    place (primaryKnob, primaryLabel, area.removeFromTop (knobWithLabel (knobSize::large)));
    area.removeFromTop (layout::gap);
    place (companionKnob, companionLabel, area);
    // The toggle centred on the small knob's face, as wide as its text.
    const int toggleWidth = std::min (area.getWidth(), staple::LookAndFeel::idealToggleWidth (companionToggle.getButtonText()));
    companionToggle.setBounds (juce::Rectangle<int> (toggleWidth, ControlSection::toggleHeight)
                                   .withCentre ({ area.getCentreX(), area.getY() + diameterOf (knobSize::small) / 2 }));
}

CompactView::CompactView (PluginProcessor& p, LevelHistory& history)
    : processor (p),
      slots { std::make_unique<CompactSlot> ("Compact Slot A", p), std::make_unique<CompactSlot> ("Compact Slot B", p) },
      compressor ("Compressor", p), gainReduction (history)
{
    setName ("Compact View");
    hero.emplace (addCompKnob (compressor, p, compAttachment, ControlSection::Place::ownColumn, knobSize::hero), p, history);

    int order = 0;
    for (juce::Component* part : std::initializer_list<juce::Component*> { slots[0].get(), &compressor, slots[1].get() })
    {
        addAndMakeVisible (*part);
        part->setExplicitFocusOrder (++order);
    }
    // Last for a screen reader; it takes no keyboard focus, so Tab passes it.
    addAndMakeVisible (gainReduction);
    gainReduction.setExplicitFocusOrder (++order);
    followStyle();
}

CompactView::~CompactView() = default;

void CompactView::followStyle()
{
    const auto index = static_cast<std::size_t> (dynamic_cast<juce::AudioParameterChoice&> (parameters::parameterOf (processor.parameterState(), parameters::styleId)).getIndex());
    const auto style = compactSlots[std::min (index, std::size (compactSlots) - 1)].style;
    if (shownStyle == style)
        return;
    // The first Style shown appears at once; a change crossfades, except under reduced motion.
    const bool crossfade = shownStyle.has_value() && ! reducedMotion;
    shownStyle = style;
    const auto& row = compactSlots[compactSlotsRowOf (style)];
    slots[0]->show (row.a, crossfade);
    slots[1]->show (row.b, crossfade);
}

void CompactView::resized()
{
    auto row = getLocalBounds();
    gainReduction.setBounds (row.removeFromRight (layout::meterWidth));
    row.removeFromRight (layout::gap);
    // Three equal columns, each part centred in its own and down the body.
    const int column = row.getWidth() / 3;
    const auto centreIn = [this] (juce::Component& part, juce::Rectangle<int> area, int width, int height) {
        part.setBounds (juce::Rectangle<int> (width, height).withCentre ({ area.getCentreX(), getHeight() / 2 }));
    };
    centreIn (*slots[0], row.removeFromLeft (column), CompactSlot::idealWidth(), CompactSlot::idealHeight());
    centreIn (compressor, row.removeFromLeft (column), compressor.idealWidth(), compressor.idealHeight());
    centreIn (*slots[1], row, CompactSlot::idealWidth(), CompactSlot::idealHeight());
}

} // namespace comp1
