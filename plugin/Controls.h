#pragma once

#include "PluginProcessor.h"
#include "Section.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/Icons.h"
#include "plugkit/ui/staple/controls/EdgeSelector.h"
#include "plugkit/ui/staple/controls/Knob.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace comp1
{

namespace controls
{
// A host parameter's value as a screen reader reads it and a knob's tooltip shows it: plugkit's spokenValue
// (accessibility::spokenValue), in comp1's own units where it has them: Ratio as "4:1", "4.5:1" and "∞:1", Ceiling
// as "−3.00 dBFS". Dry's and Wet's bottom is the parameter's own "Off".
juce::String spokenValue (const juce::RangedAudioParameter& parameter, double plain);

// A value typed for parameter, as the user types it or a screen reader sets it: in its unit, as staple::parseValue
// reads it ("−3 dBFS", "4:1", "250 ms", "1.2k"), else the parameter's own text ("∞", "Off"), with the unit it is read
// with taken off ("∞:1").
double valueFromText (const juce::RangedAudioParameter& parameter, const juce::String& text);

// Sets knob up to show parameter: its range, its text, and its default for a double-click, as JUCE's
// SliderParameterAttachment does, for a knob the editor writes the parameter from itself (CompFollowAttachment).
void showRangeOf (juce::Slider& knob, const juce::RangedAudioParameter& parameter);

// Titles knob with parameter's name, and has it read, show and take typed values in comp1's units (above).
void describe (staple::Knob& knob, const juce::RangedAudioParameter& parameter);
} // namespace controls

// A section of controls (Section), in either view: its controls in columns, left to right in the order
// they are added, which is Tab's order too. A column holds a knob with its label under its face, or a stack of small
// controls (toggles, choices, Edge selectors), or a stack with a knob under it. The knobs of a section sit on one
// line, under the tallest stack over a knob. Each control is titled with its host parameter's name, and attached to
// it unless the section writes it itself.
//
// While the section isn't available (setAvailability), its controls are disabled: dimmed, their values kept, and
// skipped by Tab. The section follows it on the editor's tick.
class ControlSection : public Section, private plugkit::ui::EditorTick::Client
{
public:
    // A knob and its label: the label is the knob's name within its section ("Range" in the Gate's).
    struct KnobControl
    {
        KnobControl (juce::RangedAudioParameter& parameter, float diameter, const juce::String& labelText);

        juce::RangedAudioParameter& parameter;
        staple::Knob knob;
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        bool ringed = false;

        // Gives the knob's lane, outside its face, to handler, and puts the label under it.
        void setRing (staple::Knob::RingHandler& handler);
    };

    ControlSection (const juce::String& title, PluginProcessor& processor);
    ~ControlSection() override;

    // Whether the controls are enabled, read on the tick; always, unless set.
    void setAvailability (std::function<bool()> available);
    // Follows the processor now, as on the editor's tick: what the section shows beyond its attachments, and which of
    // its controls are enabled.
    void followProcessor() { tick (0.0); }

    // Where a knob goes: in a column of its own, or under the stack of the column being filled.
    enum class Place
    {
        ownColumn,
        underStack
    };
    // A knob of diameter, attached unless attached is false.
    KnobControl& addKnob (const juce::String& parameterId, float diameter, const juce::String& label, Place place = Place::ownColumn, bool attached = true);
    // An on/off parameter's toggle, labelled text, stacked in the column being filled, or starting a new one.
    juce::ToggleButton& addToggle (const juce::String& parameterId, const juce::String& text, bool newColumn, bool attached = true);
    // A choice parameter's ComboBox, width wide, listing its choices.
    juce::ComboBox& addChoice (const juce::String& parameterId, int width, bool newColumn);
    // A choice parameter's Edge selector, an icon for each of its choices in order, and a context icon for each
    // (drawn under it at 30 %) where given.
    staple::EdgeSelector& addEdgeSelector (const juce::String& parameterId,
                                           const std::vector<staple::Icon>& icons,
                                           const std::vector<std::optional<staple::Icon>>& contexts,
                                           bool newColumn);

    int idealWidth() const override;
    int idealHeight() const override;

    void resized() override;

    static constexpr int columnGap = 8, rowGap = 4, labelHeight = 14, labelGap = 2, toggleHeight = 22, choiceHeight = 24;
    static constexpr int edgeSelectorWidth = 104, edgeSelectorHeight = 34, minimumKnobColumn = 50, knobSidePadding = 14;

protected:
    juce::RangedAudioParameter& parameter (const juce::String& id);
    // Called on the editor's tick, before the availability is shown: what a section that shows more follows.
    virtual void follow() {}
    // Whether control is enabled while the section is available; by default, always.
    virtual bool isControlEnabled (juce::Component& control) const
    {
        juce::ignoreUnused (control);
        return true;
    }

private:
    void tick (double seconds) override;

    struct Item
    {
        juce::Component* component;
        int width, height;
    };
    struct Column
    {
        std::vector<Item> stack;
        KnobControl* knob = nullptr;
    };
    Column& columnFor (bool newColumn);
    void addControl (juce::Component& control, int width, int height, bool newColumn);
    int columnWidth (const Column& column) const;
    int stackHeight (const Column& column) const;
    // How far the knobs' faces sit under the control area's top: under the tallest stack over a knob.
    int knobTop() const;
    static int knobExtent (const KnobControl& control);

    PluginProcessor& processor;
    std::function<bool()> availability;
    std::vector<std::unique_ptr<KnobControl>> knobs;
    std::vector<std::unique_ptr<juce::Component>> others;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> choiceAttachments;
    std::vector<Column> columns;
    int focusOrder = 0;
};

} // namespace comp1
