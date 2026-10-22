#include "EditorHost.h"
#include "comp1/Style.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{

// The editor as drawn, one pixel per pixel of its window, into the snapshot folder (check.sh screens).
void snapshot (host::EditorHost& host, const juce::String& name)
{
    host.settleAnimations();
    harness::writeSnapshot (*host.editor, name, {}, 1.0f);
}

void openOn (host::EditorHost& host, const juce::AudioChannelSet& channels)
{
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
}

// A 1 kHz sine at 48 kHz, at amplitude.
auto sine (double amplitude)
{
    return [amplitude] (int n) { return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / 48000.0); };
}

} // namespace

TEST_CASE ("Expanded View snapshots: default at 100% and 200%, on mono, with Comp Follow on and off, in High Quality, with signal playing, a lit Clip Light, and the Preset browser open", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    snapshot (host, "expanded-default-100");

    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (200);
    snapshot (host, "expanded-default-200");
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (100);

    openOn (host, juce::AudioChannelSet::mono());
    snapshot (host, "expanded-mono");

    openOn (host, juce::AudioChannelSet::stereo());
    // Comp Follow on (Opto, Comp at 80%), Threshold and Ratio showing the Comp Curve's and Auto Gain held on; then off,
    // as the toggle turns it off.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (comp1::parameters::compId, 80.0f);
    snapshot (host, "expanded-comp-follow-on");
    REQUIRE (host.processor.stopFollowingComp());
    snapshot (host, "expanded-comp-follow-off");
    host.processor.session().editHistory().undo();
    host.processor.session().editHistory().undo();
    host.processor.session().editHistory().undo();

    // High Quality: Lookahead and Oversampling enabled.
    host.edit (comp1::parameters::processingModeId, 1.0f);
    snapshot (host, "expanded-high-quality");
    host.processor.session().editHistory().undo();

    // Playing: the Output Meter's bars, the Gain Reduction meter and the Detection Level ring up, as the editor's tick
    // reads them between blocks.
    for (int i = 0; i < 12; ++i)
    {
        host.play (4, 512, sine (0.5));
        host.settle (20);
    }
    harness::writeSnapshot (*host.editor, "expanded-playing", {}, 1.0f);

    // An over, well above whatever Gain Reduction the sine left.
    host.play (1, 512, [] (int n) { return n == 100 ? 8.0 : 0.0; });
    REQUIRE (host.settleUntil ([&] { return host.processor.isClipLit (0); }));
    snapshot (host, "expanded-clip-light");
    host.processor.clearClipLights();

    auto& presets = host.showing<juce::Button> ("Presets");
    REQUIRE (presets.onClick != nullptr);
    presets.onClick();
    snapshot (host, "expanded-preset-browser");
}
