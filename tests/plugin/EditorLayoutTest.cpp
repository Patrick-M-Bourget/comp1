#include "EditorHost.h"
#include "plugkit/ui/HeaderBar.h"
#include "plugkit/ui/OutputMeter.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vector>

namespace
{

// The editor's areas, in the content's logical pixels (as at 100%).
struct Areas
{
    juce::Rectangle<int> header, display, levelTab, detectionEqTab, gainReduction, outputMeter, sections, style, footer;
    juce::Rectangle<int> gateGainReduction, limiterGainReduction;
    juce::Rectangle<int> transferCurveTab;
};

// The showing component named name: the shown view's, as both views have a "Style" section and a "Gain Reduction"
// meter.
juce::Component& named (juce::Component& editor, const juce::String& name)
{
    auto* found = harness::findChild<juce::Component> (editor, [&name] (juce::Component& c) { return c.getName() == name && c.isShowing(); });
    INFO ("Nothing showing is named " << name);
    REQUIRE (found != nullptr);
    return *found;
}

Areas areasOf (juce::AudioProcessorEditor& editor)
{
    auto* header = harness::findChild<plugkit::ui::HeaderBar> (editor);
    REQUIRE (header != nullptr);
    auto& content = *header->getParentComponent();
    const auto in = [&content] (juce::Component& c) { return content.getLocalArea (c.getParentComponent(), c.getBounds()); };
    auto* meter = harness::findChild<plugkit::ui::OutputMeter> (editor);
    auto* footer = harness::findChild<comp1::Footer> (editor);
    REQUIRE (meter != nullptr);
    REQUIRE (footer != nullptr);
    auto& tabs = dynamic_cast<comp1::PluginEditor&> (editor).display();
    return { in (*header),
             in (named (editor, "Display")),
             in (tabs.tabContent (comp1::DisplayTab::level)),
             in (tabs.tabContent (comp1::DisplayTab::detectionEq)),
             in (named (editor, "Gain Reduction")),
             in (*meter),
             in (named (editor, "Sections")),
             in (named (editor, "Style")),
             in (*footer),
             in (named (editor, "Gate Gain Reduction")),
             in (named (editor, "Limiter Gain Reduction")),
             in (tabs.tabContent (comp1::DisplayTab::transferCurve)) };
}

} // namespace

TEST_CASE ("At its default size the Expanded View is the header, the display with the Gain Reduction meter and the Output Meter's rail at its right, the sections and the footer")
{
    host::EditorHost host;
    REQUIRE (host.editor->getWidth() == 1400);
    REQUIRE (host.editor->getHeight() == 860);

    // 14 px padding and 12 px gaps; four 40 px meter rails and the two rows of sections, 190 and 108 px tall (the
    // first's Threshold ring reaching past its knob).
    const auto areas = areasOf (*host.editor);
    CHECK (areas.header == juce::Rectangle<int> (14, 14, 1372, 52));
    CHECK (areas.display == juce::Rectangle<int> (14, 78, 1164, 390));
    // The Gain Reduction meters in signal order, then the Output Meter.
    CHECK (areas.gateGainReduction == juce::Rectangle<int> (1190, 78, 40, 390));
    CHECK (areas.gainReduction == juce::Rectangle<int> (1242, 78, 40, 390));
    CHECK (areas.limiterGainReduction == juce::Rectangle<int> (1294, 78, 40, 390));
    CHECK (areas.outputMeter == juce::Rectangle<int> (1346, 78, 40, 390));
    CHECK (areas.sections == juce::Rectangle<int> (14, 480, 1372, 310));
    CHECK (areas.footer == juce::Rectangle<int> (14, 802, 1372, 44));
    // Each tab's content is the display under its tabs.
    CHECK (areas.levelTab == areas.detectionEqTab);
    CHECK (areas.levelTab == areas.transferCurveTab);
    CHECK (areas.levelTab == juce::Rectangle<int> (14, 112, 1164, 356));
}

TEST_CASE ("At every UI Scale and at the minimum, default and maximum sizes nothing overlaps, and only the display and the section row's width grow")
{
    host::EditorHost host;
    auto* menu = harness::findChild<plugkit::ui::UiScaleMenu> (*host.editor);
    REQUIRE (menu != nullptr);
    const auto& window = comp1::PluginProcessor::expandedWindow;

    const int percent = GENERATE (75, 100, 125, 150, 200);
    CAPTURE (percent);
    menu->pick (percent);
    const auto scaled = [percent] (int logical) { return juce::roundToInt (logical * percent / 100.0); };
    host.editor->setSize (scaled (window.minimum.x), scaled (window.minimum.y));
    const auto smallest = areasOf (*host.editor);

    const auto size = GENERATE_COPY (window.minimum, window.defaultSize, window.maximum);
    CAPTURE (size.x, size.y);
    host.editor->setSize (scaled (size.x), scaled (size.y));
    const auto areas = areasOf (*host.editor);

    const juce::Rectangle<int> inside { 14, 14, size.x - 28, size.y - 28 };
    const std::vector<juce::Rectangle<int>> rows { areas.header, areas.display, areas.gainReduction, areas.outputMeter, areas.sections, areas.footer,
                                                   areas.gateGainReduction, areas.limiterGainReduction };
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CAPTURE (i);
        CHECK (inside.contains (rows[i]));
        CHECK_FALSE (rows[i].isEmpty());
        for (size_t j = i + 1; j < rows.size(); ++j)
            CHECK_FALSE (rows[i].intersects (rows[j]));
    }
    CHECK (areas.display.contains (areas.levelTab));
    CHECK (areas.display.contains (areas.transferCurveTab));
    CHECK (areas.transferCurveTab == areas.levelTab);
    CHECK (areas.sections.contains (areas.style));

    // What the window grows by goes to the display, and its width to the section row; the rest keep their size.
    const int wider = size.x - window.minimum.x, taller = size.y - window.minimum.y;
    CHECK (areas.display.getWidth() == smallest.display.getWidth() + wider);
    CHECK (areas.display.getHeight() == smallest.display.getHeight() + taller);
    CHECK (areas.sections.getWidth() == smallest.sections.getWidth() + wider);
    CHECK (areas.sections.getHeight() == smallest.sections.getHeight());
    CHECK (areas.outputMeter.getWidth() == smallest.outputMeter.getWidth());
    CHECK (areas.gainReduction.getWidth() == smallest.gainReduction.getWidth());
    CHECK (areas.gateGainReduction.getWidth() == smallest.gateGainReduction.getWidth());
    CHECK (areas.limiterGainReduction.getWidth() == smallest.limiterGainReduction.getWidth());
    // The meters are as tall as the display.
    CHECK (areas.gateGainReduction.getHeight() == areas.display.getHeight());
    CHECK (areas.limiterGainReduction.getHeight() == areas.display.getHeight());
    CHECK (areas.style.getWidth() == smallest.style.getWidth());
    CHECK (areas.style.getHeight() == smallest.style.getHeight());
    CHECK (areas.header.getHeight() == smallest.header.getHeight());
    CHECK (areas.footer.getHeight() == smallest.footer.getHeight());
}

TEST_CASE ("At the minimum size every section fits in the section rows, none overlapping another, each with its controls inside it")
{
    host::EditorHost host;
    const auto& window = comp1::PluginProcessor::expandedWindow;
    host.editor->setSize (window.minimum.x, window.minimum.y);
    auto& sections = named (*host.editor, "Sections");
    const auto names = { "Style", "Gate", "Compressor", "Character and Saturation", "Dry/Wet", "Detection", "Stereo", "Lookahead" };
    REQUIRE (static_cast<size_t> (sections.getNumChildComponents()) == names.size());
    std::vector<juce::Rectangle<int>> placed;
    for (const auto* name : names)
    {
        CAPTURE (name);
        auto& section = named (sections, name);
        REQUIRE (section.getParentComponent() == &sections);
        CHECK (sections.getLocalBounds().contains (section.getBounds()));
        for (const auto& other : placed)
            CHECK_FALSE (other.intersects (section.getBounds()));
        placed.push_back (section.getBounds());
        // Its title shows whole.
        CHECK (section.getWidth() >= dynamic_cast<comp1::Section&> (section).titleWidth());
        // Each control's own area: a knob's face (its shadow may reach past it), anything else its bounds.
        for (auto* control : section.getChildren())
        {
            CAPTURE (control->getTitle(), control->getName());
            auto area = control->getBounds();
            if (auto* knob = dynamic_cast<staple::Knob*> (control))
            {
                const auto face = knob->getFaceCentre().toInt() + knob->getPosition();
                const int radius = juce::roundToInt (knob->getFaceRadius());
                area = juce::Rectangle<int> (2 * radius, 2 * radius).withCentre (face);
            }
            CHECK (section.getLocalBounds().contains (area));
        }
    }
}

