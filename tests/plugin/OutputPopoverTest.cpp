// The footer's Output readout, in both views: what it reads, Output Gain plus Auto Gain's estimate while Auto Gain is
// in effect, and how it is drawn under Global Bypass.

#include "EditorHost.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace parameters = comp1::parameters;

namespace
{

const juce::String minus = juce::String::charToString (0x2212);

// The compressor settings dyn-lab's Auto Gain check reads beyond Threshold at their defaults (tools/dyn-lab/USAGE.md,
// "autogain"), on Clean, with Comp Follow and Auto Gain off.
void dynLabDefaults (host::EditorHost& host)
{
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 6.0f);
    host.edit (parameters::attackId, 10.0f);
    host.edit (parameters::releaseId, 100.0f);
    host.edit (parameters::autoReleaseId, 0.0f);
    host.edit (parameters::holdId, 0.0f);
    host.edit (parameters::rangeId, 60.0f);
}

} // namespace

TEST_CASE ("The Output readout reads Output Gain, plus Auto Gain's estimate while Auto Gain is on or Comp Follow holds it on, signed to one decimal, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& readout = host.outputReadout();
    REQUIRE (readout.isShowing());
    CHECK (readout.getTitle() == "Output");
    const auto reads = [&] (const juce::String& text) { return host.settleUntil ([&] { return readout.getButtonText() == text; }); };

    // Output Gain alone, with Auto Gain off.
    dynLabDefaults (host);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::outputGainId, 3.5f);
    CHECK (reads ("+3.5 dB"));
    host.edit (parameters::outputGainId, -1.8f);
    CHECK (reads (minus + "1.8 dB"));
    // Its bottom, -36 dB: comp1's Output Gain has no silent bottom, so it reads as a level, never -inf dB.
    host.edit (parameters::outputGainId, -36.0f);
    CHECK (reads (minus + "36.0 dB"));

    // Auto Gain on, at Threshold -30 dB on Clean: dyn-lab's estimate, 7.9161 dB (`python3 tools/dyn-lab/dynlab.py
    // autogain --threshold -30 --character clean`), plus Output Gain's +1 dB.
    host.edit (parameters::outputGainId, 1.0f);
    host.edit (parameters::autoGainId, 1.0f);
    CHECK (reads ("+8.9 dB"));

    // Comp Follow holding Auto Gain on, Auto Gain itself off: VCA at Comp 50%, the estimate dyn-lab's compcurve prints
    // for it, 3.5473 dB (`python3 tools/dyn-lab/dynlab.py compcurve --style vca`), plus Output Gain's -2 dB.
    REQUIRE (host.processor.chooseStyle (comp1::Style::VCA));
    host.edit (parameters::compId, 50.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::outputGainId, -2.0f);
    REQUIRE (host.value (parameters::compFollowId) == 1.0f);
    CHECK (reads ("+1.5 dB"));
    // Comp Follow off, Auto Gain off: Output Gain alone again.
    host.edit (parameters::compFollowId, 0.0f);
    CHECK (reads (minus + "2.0 dB"));
}

TEST_CASE ("Under Global Bypass the Output readout is drawn not applied, in text4, and applied again once it is off")
{
    host::EditorHost host;
    auto& readout = host.outputReadout();
    CHECK_FALSE (readout.isNotApplied());
    host.edit (parameters::globalBypassId, 1.0f);
    CHECK (host.settleUntil ([&] { return readout.isNotApplied(); }));
    host.edit (parameters::globalBypassId, 0.0f);
    CHECK (host.settleUntil ([&] { return ! readout.isNotApplied(); }));
}
