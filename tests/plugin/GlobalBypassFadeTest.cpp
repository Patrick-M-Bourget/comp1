// Global Bypass as the displays show it (GlobalBypassFade): its glide, stepped by durations the test sets rather than by
// the editor's tick, and the share of their alphas the displays keep.

#include "GlobalBypassFade.h"
#include "Host.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using comp1::GlobalBypassFade;

TEST_CASE ("The Global Bypass fade glides over 150 ms each way")
{
    // Half the fade time goes half way; the whole of it, all the way, and no further.
    CHECK_THAT (GlobalBypassFade::after (0.0f, true, 0.075), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (GlobalBypassFade::after (0.5f, true, 0.075), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (GlobalBypassFade::after (0.0f, true, 0.25), WithinAbs (1.0, 1.0e-6));
    // Back the same way.
    CHECK_THAT (GlobalBypassFade::after (1.0f, false, 0.075), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (GlobalBypassFade::after (0.5f, false, 0.25), WithinAbs (0.0, 1.0e-6));
    // A tick at 60 Hz moves it a ninth of the way.
    CHECK_THAT (GlobalBypassFade::after (0.0f, true, 1.0 / 60.0), WithinAbs (1.0 / 9.0, 1.0e-6));
}

TEST_CASE ("The Global Bypass fade follows Global Bypass by the durations stepped, dimming an alpha to 45 % of itself")
{
    comp1::PluginProcessor processor;
    GlobalBypassFade fade (processor.parameterState());
    // Off: at rest at 0, an alpha as it is.
    CHECK (juce::exactlyEqual (fade.value(), 0.0f));
    CHECK_FALSE (fade.isMoving());
    CHECK_FALSE (fade.step (0.075));
    CHECK_THAT (fade.dim (0.8f), WithinAbs (0.8, 1.0e-6));

    host::set (processor, comp1::parameters::globalBypassId, 1.0f);
    CHECK (fade.isMoving());
    CHECK (fade.step (0.075));
    CHECK_THAT (fade.value(), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (fade.dim (0.8f), WithinAbs (0.8 * (1.0 + 0.45) / 2.0, 1.0e-6));
    CHECK (fade.step (0.075));
    CHECK_FALSE (fade.isMoving());
    CHECK_THAT (fade.dim (0.8f), WithinAbs (0.36, 1.0e-6));
    CHECK_FALSE (fade.step (0.075));

    host::set (processor, comp1::parameters::globalBypassId, 0.0f);
    CHECK (fade.step (0.25));
    CHECK (juce::exactlyEqual (fade.value(), 0.0f));
    CHECK_FALSE (fade.isMoving());
}

TEST_CASE ("A Global Bypass fade made under Global Bypass starts there, with no fade")
{
    comp1::PluginProcessor processor;
    host::set (processor, comp1::parameters::globalBypassId, 1.0f);
    const GlobalBypassFade fade (processor.parameterState());
    CHECK (juce::exactlyEqual (fade.value(), 1.0f));
    CHECK_FALSE (fade.isMoving());
    CHECK_THAT (fade.dim (1.0f), WithinAbs (0.45, 1.0e-6));
}
