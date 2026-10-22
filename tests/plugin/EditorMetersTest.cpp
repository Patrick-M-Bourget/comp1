// The Expanded View's Gain Reduction meter and the Detection Level ring on Threshold, which read the editor's level
// history; and which controls are available on mono, in Zero Latency, and at each sample rate's Oversampling cap.

#include "EditorHost.h"
#include "Footer.h"
#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <functional>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

// A 1 kHz sine at 48 kHz, at amplitude.
auto sine (double amplitude)
{
    return [amplitude] (int n) { return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / 48000.0); };
}

auto silence() { return [] (int) { return 0.0; }; }

// A steady level of levelDb, as the Engine's tests play one (Measure.h's steadyLevels): every sample at its magnitude,
// alternating in sign, which Clean's peak detection reads as levelDb without the ripple a sine's would have.
auto steady (double levelDb)
{
    const double magnitude = std::pow (10.0, levelDb / 20.0);
    return [magnitude] (int n) { return n % 2 == 0 ? magnitude : -magnitude; };
}

// The editor open on a stereo track at 48 kHz, compressing nothing until told: Comp Follow off.
struct Playing : host::EditorHost
{
    Playing()
    {
        editor.reset();
        harness::useLayout (processor, juce::AudioChannelSet::stereo());
        reopen();
        edit (parameters::compFollowId, 0.0f);
    }

    // Plays signal for seconds of blocks of 512, letting the editor's tick read between blocks, as a host's audio
    // thread and the message thread take turns.
    template <typename Signal>
    void playFor (double seconds, Signal&& signal)
    {
        const int blocks = static_cast<int> (std::ceil (seconds * 48000.0 / 512.0));
        for (int b = 0, n = 0; b < blocks; b += 4, n += 4 * 512)
        {
            play (4, 512, [&signal, n] (int i) { return signal (n + i); });
            settle (16);
        }
    }

    // Keeps playing signal, a block at a time with the editor's tick between, until done() holds (harness::settleUntil).
    template <typename Signal>
    bool keepPlayingUntil (Signal&& signal, const std::function<bool()>& done)
    {
        int n = 0;
        return settleUntil ([&] {
            play (1, 512, [&signal, n] (int i) { return signal (n + i); });
            n += 512;
            return done();
        });
    }

    comp1::GainReductionMeter& meter() { return expandedView().gainReductionMeter(); }
    comp1::DetectionLevelRing& ring() { return expandedView().detectionLevelRing(); }
};

// How far a meter falls over a wait with nothing to show: at most 20 dB/s of the wait and of the longest tick before it
// (EditorTick's 0.25 s), whose fall a busy machine may count in the wait; and, unless it reached its floor, well on its
// way. Falling at once, it would fall by tens of dB.
template <typename Shown>
void checkFallsAt20DbPerSecond (Playing& host, Shown&& shown, double floor)
{
    const double from = shown();
    const double start = juce::Time::getMillisecondCounterHiRes();
    host.playFor (0.4, silence());
    const double seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    const double fell = from - shown();
    CAPTURE (from, seconds, fell);
    CHECK (fell <= 20.0 * (seconds + 0.25));
    if (shown() > floor)
        CHECK (fell >= 2.0);
}

} // namespace

TEST_CASE ("The Gain Reduction meter reads 0 with no compression, and repaints only while it moves")
{
    Playing host;
    host.playFor (0.2, silence());
    CHECK (host.meter().shownDb() == 0.0);
    // Below Threshold: still nothing to show, and nothing repainted.
    auto& repaints = harness::countRepaintsOf (host.meter());
    host.playFor (0.3, sine (0.01));
    CHECK (host.meter().shownDb() == 0.0);
    CHECK (repaints.count == 0);
    host.meter().setCachedComponentImage (nullptr);
}

TEST_CASE ("On a steady level above Threshold the Gain Reduction meter reads the Transfer Curve's Gain Reduction, and falls at 20 dB/s when the tone stops")
{
    Playing host;
    // -30 dB at 4:1 with a hard knee and a quick Release, so the compressor gives its gain back at once and the meter's
    // fall is its own.
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 0.0f);
    host.edit (parameters::releaseId, 5.0f);
    // A steady -6 dB reads -6 dB: 24 dB over Threshold, of which 4:1 takes off 18.
    auto& repaints = harness::countRepaintsOf (host.meter());
    host.playFor (0.5, steady (-6.0));
    // Read just after a tick that read records, as it rises at once: a busy machine's tick may fall a little first.
    CHECK (host.keepPlayingUntil (steady (-6.0), [&] { return std::abs (host.meter().shownDb() - 18.0) <= 0.5; }));
    CHECK (repaints.count > 0);
    host.meter().setCachedComponentImage (nullptr);

    // Read as a meter, titled and valued, and no stop for Tab.
    auto* handler = host.meter().getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::progressBar);
    CHECK (handler->getTitle() == "Gain Reduction");
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK (handler->getValueInterface()->getCurrentValueAsString().endsWith (" dB"));
    CHECK_FALSE (host.meter().getWantsKeyboardFocus());

    checkFallsAt20DbPerSecond (host, [&] { return host.meter().shownDb(); }, 0.0);
}

TEST_CASE ("The Detection Level ring fills Threshold's lane to the Detection Level on Threshold's scale, and falls at 20 dB/s after")
{
    Playing host;
    CHECK (host.ring().shownDb() == -60.0);
    host.playFor (0.3, sine (std::pow (10.0, -12.0 / 20.0)));
    CHECK (host.keepPlayingUntil (sine (std::pow (10.0, -12.0 / 20.0)), [&] { return std::abs (host.ring().shownDb() + 12.0) <= 0.5; }));
    checkFallsAt20DbPerSecond (host, [&] { return host.ring().shownDb(); }, -60.0);

    // Display only: a press on the lane leaves Threshold where it was, and its tooltip lines are empty.
    auto& knob = host.knob ("Threshold");
    const float threshold = host.value (parameters::thresholdId);
    const auto onLane = knob.getFaceCentre().translated (0.0f, -(knob.getFaceRadius() + staple::tokens::knob::ringOffset));
    REQUIRE (knob.isOnRing (onLane));
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    knob.mouseDown (harness::mouseEvent (knob, onLane, mods));
    knob.mouseDrag (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), mods, onLane));
    knob.mouseUp (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), {}, onLane));
    CHECK (host.value (parameters::thresholdId) == threshold);
    CHECK (host.ring().ringTitle().isEmpty());
    CHECK (host.ring().ringValue().isEmpty());
}

TEST_CASE ("With Detection Source External and no Sidechain, the Detection Level ring stays at the floor")
{
    Playing host;
    host.edit (parameters::detectionSourceId, 1.0f);
    host.playFor (0.3, sine (0.5));
    CHECK (host.ring().shownDb() == -60.0);
}

TEST_CASE ("On a mono track Stereo Placement and Stereo Link are disabled, their values kept; on stereo they are enabled")
{
    host::EditorHost host;
    host.edit (parameters::stereoPlacementId, 1.0f);
    host.edit (parameters::stereoLinkId, 40.0f);
    const auto channels = GENERATE (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
    CAPTURE (channels.getDescription());
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
    const bool stereo = channels == juce::AudioChannelSet::stereo();
    auto& placement = host.showing<juce::ComboBox> ("Stereo Placement");
    auto& link = host.knob ("Stereo Link");
    REQUIRE (host.settleUntil ([&] { return placement.isEnabled() == stereo && link.isEnabled() == stereo; }));
    CHECK (placement.isVisible());
    CHECK (link.isVisible());
    CHECK (placement.getSelectedItemIndex() == 1);
    CHECK_THAT (link.getValue(), WithinAbs (40.0, 1.0e-4));
    CHECK (host.value (parameters::stereoPlacementId) == 1.0f);
    CHECK_THAT (host.value (parameters::stereoLinkId), WithinAbs (40.0f, 1.0e-4));
}

TEST_CASE ("In Zero Latency Lookahead and Oversampling are disabled, their values kept; in High Quality they are enabled")
{
    host::EditorHost host;
    host.edit (parameters::lookaheadId, 2.0f);
    auto& lookahead = host.knob ("Lookahead");
    auto& oversampling = host.showing<comp1::ChoiceChip> ("Oversampling");
    const auto enabled = [&] (bool on) { return lookahead.isEnabled() == on && oversampling.isEnabled() == on; };
    REQUIRE (host.settleUntil ([&] { return enabled (false); }));
    host.edit (parameters::processingModeId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return enabled (true); }));
    host.edit (parameters::processingModeId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return enabled (false); }));
    CHECK_THAT (host.value (parameters::lookaheadId), WithinAbs (2.0f, 1.0e-4));
}

TEST_CASE ("Oversampling offers the factors up to each sample rate's cap and shows the one in force; before a sample rate, the assumed one's")
{
    host::EditorHost host;
    host.edit (parameters::processingModeId, 1.0f);
    auto& chip = host.showing<comp1::ChoiceChip> ("Oversampling");
    const auto offered = [&chip] {
        juce::StringArray items;
        const auto menu = chip.menu();
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            items.add (it.getItem().text);
        return items;
    };
    const juce::StringArray all { "1x", "2x", "4x", "8x" };
    // Not prepared yet: 48 kHz assumed.
    CHECK (offered() == all);

    const double sampleRate = GENERATE (from_range (comp1::test::sampleRates()));
    CAPTURE (sampleRate);
    host.processor.prepareToPlay (sampleRate, 512);
    // 1x to 8x at 44.1 and 48 kHz, to 4x at 88.2 and 96 kHz, to 2x at 176.4 and 192 kHz.
    const int factors = sampleRate <= 48000.0 ? 4 : sampleRate <= 96000.0 ? 3 : 2;
    juce::StringArray expected;
    for (int i = 0; i < factors; ++i)
        expected.add (all[i]);
    CHECK (offered() == expected);

    // 8x stored: the cap shown, and ticked.
    host.edit (parameters::oversamplingId, 3.0f);
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == expected[factors - 1]; }));
    const auto menu = chip.menu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        CHECK (it.getItem().isTicked == (it.getItem().text == expected[factors - 1]));
    // Picking one writes it.
    auto action = harness::actionOf (menu, "1x");
    REQUIRE (action != nullptr);
    action();
    CHECK (host.value (parameters::oversamplingId) == 0.0f);
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == "1x"; }));
}
