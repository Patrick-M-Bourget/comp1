// The Expanded View's Detection section: whether it is open, saved per instance as view state, and in no Preset.

#include "EditorHost.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("The Detection section is collapsed for a new instance; whether it is open is saved with the session as detection_open and restored, collapsed for a session saved without it")
{
    host::EditorHost host;
    CHECK_FALSE (host.processor.detectionOpen());
    CHECK (harness::savedState (host.processor).getProperty ("detection_open").toString() == "collapsed");

    host.processor.setDetectionOpen (true);
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedState (saved).getProperty ("detection_open").toString() == "open");

    // Into a new instance.
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.detectionOpen());

    // Without it, and with a value it doesn't know: collapsed.
    for (const auto* value : { "", "half" })
    {
        CAPTURE (value);
        auto xml = harness::savedXml (saved);
        REQUIRE (xml != nullptr);
        if (juce::String (value).isEmpty())
            xml->removeAttribute ("detection_open");
        else
            xml->setAttribute ("detection_open", value);
        other.processor.setDetectionOpen (true);
        host::restore (other.processor, *xml);
        CHECK_FALSE (other.processor.detectionOpen());
    }
}

TEST_CASE ("Whether the Detection section is open is in no Preset, adds no undo step and makes no side Modified, and loading a Preset, undo or an A/B switch leave it alone")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    host.processor.setDetectionOpen (true);
    CHECK (harness::savedNamesContaining (session.presetState(), "detection_open").isEmpty());
    CHECK (history.undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());

    host.edit (comp1::parameters::thresholdId, -30.0f);
    REQUIRE (history.canUndo());
    history.undo();
    CHECK (host.processor.detectionOpen());

    REQUIRE (session.loadPreset (preset, "Start"));
    host.settle();
    CHECK (host.processor.detectionOpen());

    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    host.settle();
    CHECK (host.processor.detectionOpen());
}
