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
    juce::Rectangle<int> header, display, levelTab, gainReduction, outputMeter, sections, character, footer;
    juce::Rectangle<int> gateGainReduction, limiterGainReduction;
    juce::Rectangle<int> spectrumTab;
};

// The showing component named name: the shown view's, as both views have a "Gate" section and a "Gain Reduction"
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
             in (named (editor, "Gain Reduction")),
             in (*meter),
             in (named (editor, "Sections")),
             in (named (editor, "Character")),
             in (*footer),
             in (named (editor, "Gate Gain Reduction")),
             in (named (editor, "Limiter Gain Reduction")),
             in (tabs.tabContent (comp1::DisplayTab::spectrum)) };
}

} // namespace

TEST_CASE ("At its default size the Expanded View is the header, the display with the Gain Reduction meter and the Output Meter's rail at its right, the sections and the footer, the display giving the Detection section the height it opens to")
{
    host::EditorHost host;
    REQUIRE (host.editor->getWidth() == 1400);
    REQUIRE (host.editor->getHeight() == 860);

    // 14 px padding and 12 px gaps; four 40 px meter rails and the two rows of sections, 164 and 16 px tall (the
    // first's Threshold ring reaching past its knob, the second the Detection section collapsed to its strip).
    const auto areas = areasOf (*host.editor);
    CHECK (areas.header == juce::Rectangle<int> (14, 14, 1372, 52));
    CHECK (areas.display == juce::Rectangle<int> (14, 78, 1164, 508));
    // The Gain Reduction meters in signal order, then the Output Meter, over their labels' 14 px line.
    CHECK (areas.gateGainReduction == juce::Rectangle<int> (1190, 78, 40, 494));
    CHECK (areas.gainReduction == juce::Rectangle<int> (1242, 78, 40, 494));
    CHECK (areas.limiterGainReduction == juce::Rectangle<int> (1294, 78, 40, 494));
    CHECK (areas.outputMeter == juce::Rectangle<int> (1346, 78, 40, 494));
    CHECK (areas.sections == juce::Rectangle<int> (14, 598, 1372, 192));
    CHECK (areas.footer == juce::Rectangle<int> (14, 802, 1372, 44));
    // Each tab's content is the display under its tabs.
    CHECK (areas.levelTab == areas.spectrumTab);
    CHECK (areas.levelTab == juce::Rectangle<int> (14, 112, 1164, 474));

    // Open, the Detection section makes the second row 134 px tall, and the display and the meters give up the 118 px.
    host.processor.setDetectionOpen (true);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().detectionSection().isOpen(); }));
    host.settleAnimations();
    const auto open = areasOf (*host.editor);
    CHECK (open.display == juce::Rectangle<int> (14, 78, 1164, 390));
    CHECK (open.outputMeter == juce::Rectangle<int> (1346, 78, 40, 376));
    CHECK (open.sections == juce::Rectangle<int> (14, 480, 1372, 310));
    CHECK (open.footer == areas.footer);
    CHECK (open.levelTab == juce::Rectangle<int> (14, 112, 1164, 356));
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
    CHECK (areas.display.contains (areas.spectrumTab));
    CHECK (areas.spectrumTab == areas.levelTab);
    CHECK (areas.sections.contains (areas.character));

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
    // The meters are as tall as the display but for their labels' line.
    CHECK (areas.gateGainReduction.getHeight() == areas.display.getHeight() - comp1::MeterLabel::lineHeight);
    CHECK (areas.limiterGainReduction.getHeight() == areas.display.getHeight() - comp1::MeterLabel::lineHeight);
    CHECK (areas.character.getWidth() == smallest.character.getWidth());
    CHECK (areas.character.getHeight() == smallest.character.getHeight());
    CHECK (areas.header.getHeight() == smallest.header.getHeight());
    CHECK (areas.footer.getHeight() == smallest.footer.getHeight());
}

TEST_CASE ("At the minimum size every section fits in the section rows, none overlapping another, each with its controls inside it, the Detection section collapsed or open")
{
    host::EditorHost host;
    const bool open = GENERATE (false, true);
    CAPTURE (open);
    host.processor.setDetectionOpen (open);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().detectionSection().isOpen() == open; }));
    host.settleAnimations();
    const auto& window = comp1::PluginProcessor::expandedWindow;
    host.editor->setSize (window.minimum.x, window.minimum.y);
    auto& sections = named (*host.editor, "Sections");
    const auto names = { "Gate", "Compressor", "Character", "Detection" };
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
            // A collapsed section's hidden controls aren't laid out for its strip.
            if (! control->isVisible())
                continue;
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

TEST_CASE ("Both rows of sections start at the left edge on one grid of columns: Detection under all three, spanning the grid, and Character no wider than it needs, the Detection section collapsed or open, at 100% and 200% and the minimum and default sizes")
{
    host::EditorHost host;
    auto* menu = harness::findChild<plugkit::ui::UiScaleMenu> (*host.editor);
    REQUIRE (menu != nullptr);
    const bool open = GENERATE (false, true);
    CAPTURE (open);
    host.processor.setDetectionOpen (open);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().detectionSection().isOpen() == open; }));
    host.settleAnimations();
    const auto& window = comp1::PluginProcessor::expandedWindow;
    const int percent = GENERATE (100, 200);
    CAPTURE (percent);
    menu->pick (percent);
    const auto size = GENERATE_COPY (window.minimum, window.defaultSize);
    CAPTURE (size.x, size.y);
    host.editor->setSize (juce::roundToInt (size.x * percent / 100.0), juce::roundToInt (size.y * percent / 100.0));

    auto& sections = named (*host.editor, "Sections");
    const auto at = [&sections] (const char* name) { return named (sections, name).getBounds(); };
    const auto gate = at ("Gate"), compressor = at ("Compressor"), character = at ("Character");
    const auto detection = at ("Detection");
    const int gap = compressor.getX() - gate.getRight();

    // The first row's columns, from the left edge, a gap apart.
    CHECK (gap > 0);
    CHECK (gate.getX() == 0);
    CHECK (character.getX() - compressor.getRight() == gap);
    // The second row under them: Detection spans all three columns.
    CHECK (detection.getX() == gate.getX());
    CHECK (detection.getRight() == character.getRight());
    // Character's column is as wide as Character needs: Detection, under it, doesn't widen it.
    CHECK (character.getWidth() == dynamic_cast<comp1::Section&> (named (sections, "Character")).idealWidth());
    // Each at least as wide as its own content.
    for (const auto* name : { "Gate", "Compressor", "Character", "Detection" })
    {
        CAPTURE (name);
        auto& section = dynamic_cast<comp1::Section&> (named (sections, name));
        CHECK (section.getWidth() >= section.idealWidth());
        CHECK (sections.getLocalBounds().contains (section.getBounds()));
    }
    CHECK_FALSE (detection.intersects (character));
}

TEST_CASE ("Each meter is labelled under its bar, GATE, COMP, LIM and OUT, the labels overlapping nothing and the bars giving up only the label's line, at 100% and 200% and the minimum and default sizes")
{
    host::EditorHost host;
    auto* menu = harness::findChild<plugkit::ui::UiScaleMenu> (*host.editor);
    REQUIRE (menu != nullptr);
    const auto& window = comp1::PluginProcessor::expandedWindow;
    const int percent = GENERATE (100, 200);
    CAPTURE (percent);
    menu->pick (percent);
    const auto size = GENERATE_COPY (window.minimum, window.defaultSize);
    CAPTURE (size.x, size.y);
    host.editor->setSize (juce::roundToInt (size.x * percent / 100.0), juce::roundToInt (size.y * percent / 100.0));

    const auto areas = areasOf (*host.editor);
    auto& content = *harness::findChild<plugkit::ui::HeaderBar> (*host.editor)->getParentComponent();
    const std::vector<std::pair<juce::String, juce::Rectangle<int>>> meters {
        { "GATE", areas.gateGainReduction }, { "COMP", areas.gainReduction }, { "LIM", areas.limiterGainReduction }, { "OUT", areas.outputMeter }
    };
    std::vector<juce::Rectangle<int>> placed { areas.header,        areas.display,       areas.sections,
                                               areas.footer,        areas.gateGainReduction, areas.gainReduction,
                                               areas.limiterGainReduction, areas.outputMeter };
    for (const auto& [text, meter] : meters)
    {
        CAPTURE (text);
        auto* label = harness::findChild<comp1::MeterLabel> (*host.editor, [&text] (comp1::MeterLabel& l) { return l.getName() == text && l.isShowing(); });
        REQUIRE (label != nullptr);
        const auto area = content.getLocalArea (label->getParentComponent(), label->getBounds());
        // Its line, straight under its meter and centred on it, its text whole.
        CHECK (area.getY() == meter.getBottom());
        CHECK (area.getHeight() == comp1::MeterLabel::lineHeight);
        CHECK (area.getCentreX() == meter.getCentreX());
        CHECK (area.getWidth() <= meter.getWidth());
        CHECK (area.getWidth() >= label->textWidth());
        // Clear of every other area and label.
        for (const auto& other : placed)
            CHECK_FALSE (other.intersects (area));
        placed.push_back (area);
        // The meter keeps the display's height but for the label's line.
        CHECK (meter.getY() == areas.display.getY());
        CHECK (meter.getHeight() == areas.display.getHeight() - comp1::MeterLabel::lineHeight);
    }
}


TEST_CASE ("At the Compact View's 780 px and at the Expanded View's minimum and default widths, in Zero Latency and in High Quality, the footer holds the Stereo and the Saturation readouts at full length, between the High Quality readout and the Output readout")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    const bool highQuality = GENERATE (false, true);
    CAPTURE (compact, highQuality);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == (compact ? comp1::View::compact : comp1::View::expanded));
    host.edit (comp1::parameters::processingModeId, highQuality ? 1.0f : 0.0f);
    // At their widest readings, "Stereo 100%" and Drive's top.
    host.edit (comp1::parameters::driveId, 24.0f);
    const auto& window = comp1::PluginProcessor::windowOf (compact ? comp1::View::compact : comp1::View::expanded);
    const int width = GENERATE_COPY (window.minimum.x, window.defaultSize.x);
    CAPTURE (width);
    host.editor->setSize (width, window.minimum.y);
    auto& stereo = host.stereoReadout();
    auto& saturation = host.saturationReadout();
    REQUIRE (host.settleUntil ([&] { return stereo.getButtonText() == "Stereo 100%" && saturation.getButtonText() == "SAT +24 dB"; }));
    // The minimum is 780 px for the Compact View, 1356 px for the Expanded View.
    CHECK (window.minimum.x == (compact ? 780 : 1356));

    CHECK_FALSE (stereo.isShrunk());
    CHECK_FALSE (saturation.isShrunk());
    const auto hq = host.inEditor (host.highQualityReadout()), st = host.inEditor (stereo), sat = host.inEditor (saturation),
               out = host.inEditor (host.outputReadout());
    CHECK (st.getX() >= hq.getRight() + comp1::Footer::chipGap);
    CHECK (sat.getX() == st.getRight() + comp1::Footer::chipGap);
    CHECK (sat.getRight() + comp1::Footer::chipGap == out.getX());
}

TEST_CASE ("Narrowed a pixel at a time, the footer shrinks the Stereo readout to ST, then the Saturation readout to SAT, each staying shrunk once shrunk, both showing between the High Quality readout and the Output readout, overlapping neither")
{
    host::EditorHost host;
    auto& footer = host.footer();
    auto& stereo = host.stereoReadout();
    auto& saturation = host.saturationReadout();
    auto& highQuality = host.highQualityReadout();
    auto& output = host.outputReadout();
    // Placed in the footer: the readouts fill their items.
    const auto in = [&footer] (juce::Component& c) { return footer.getLocalArea (c.getParentComponent(), c.getBounds()); };
    REQUIRE_FALSE (stereo.isShrunk());
    REQUIRE_FALSE (saturation.isShrunk());
    bool stereoShrank = false;
    int stereoShrankAt = 0, saturationShrankAt = 0;
    while (! saturation.isShrunk() && footer.getWidth() > 0)
    {
        footer.setSize (footer.getWidth() - 1, footer.getHeight());
        CAPTURE (footer.getWidth());
        // Saturation never shrinks while Stereo is at full length, and neither grows back as the row narrows.
        if (saturation.isShrunk())
        {
            CHECK (stereoShrank);
            saturationShrankAt = footer.getWidth();
        }
        CHECK ((! stereoShrank || stereo.isShrunk()));
        if (stereo.isShrunk() && ! stereoShrank)
            stereoShrankAt = footer.getWidth();
        stereoShrank = stereo.isShrunk();
        const auto st = in (stereo), sat = in (saturation);
        CHECK (st.getX() >= in (highQuality).getRight() + comp1::Footer::chipGap);
        CHECK (sat.getX() == st.getRight() + comp1::Footer::chipGap);
        CHECK (sat.getRight() + comp1::Footer::chipGap == in (output).getX());
    }
    REQUIRE (saturation.isShrunk());
    // Shrunk, each at its label's width, still showing, and read whole by a screen reader.
    for (auto* readout : { &stereo, &saturation })
    {
        CHECK (readout->isShowing());
        CHECK (readout->getWidth() == readout->getShrunkWidth());
    }
    CHECK (stereo.getButtonText() == "Stereo 100%");
    CHECK (saturation.getButtonText() == comp1::saturationReadoutText (6.0));
    CHECK (saturationShrankAt < stereoShrankAt);
}
