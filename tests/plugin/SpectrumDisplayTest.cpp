// The Expanded View's Spectrum tab: the input and output spectra, and the Gain Reduction shaded between them.

#include "Measure.h"
#include "Playing.h"
#include "SpectrumDisplay.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace parameters = comp1::parameters;
using Catch::Matchers::WithinAbs;

namespace
{

// The editor playing on a stereo track at 48 kHz, on the Spectrum tab, with Clean at Threshold -30 dB, Ratio 4:1 and
// a hard knee: a 1 kHz sine peaking at -10 dB, 20 dB over, has 15 dB of Gain Reduction. Clean smooths the Gain
// Reduction, which would fall between the sine's peaks, so a 20 ms Hold, longer than its period, keeps it there.
struct SpectrumHost : host::Playing
{
    SpectrumHost()
    {
        REQUIRE (processor.chooseStyle (comp1::Style::Clean));
        edit (parameters::compFollowId, 0.0f);
        edit (parameters::thresholdId, -30.0f);
        edit (parameters::ratioId, 4.0f);
        edit (parameters::kneeId, 0.0f);
        edit (parameters::holdId, 20.0f);
        showSpectrum();
    }

    void showSpectrum()
    {
        processor.setDisplayTab (comp1::DisplayTab::spectrum);
        REQUIRE (settleUntil ([this] { return spectrum().isShowing(); }));
    }

    comp1::SpectrumDisplay& spectrum() { return expandedView().display().spectrumDisplay(); }

    // Keeps playing signal until the gap at 1 kHz is within tolerance of gapDb. The spectra move by the time between
    // the editor's ticks, at the Analyzer's default speed, so they settle as the clock runs, however fast a test plays.
    template <typename Signal>
    bool gapSettlesAt (Signal&& signal, double gapDb, double tolerance)
    {
        return keepPlayingUntil (signal, [&] { return std::abs (gapAt1kHzDb() - gapDb) < tolerance; });
    }

    // Whether the Gain Reduction is shaded just under the input's line at frequency, where it starts.
    bool shadedUnderInputAt (double frequency)
    {
        auto& display = spectrum();
        const float x = display.geometry().xOf (frequency);
        return display.gainReductionArea().contains (x, display.yOf (display.inputSpectrum(), x) + 1.5f);
    }

    // How much lower the output spectrum reads than the input spectrum at 1 kHz, in dB.
    double gapAt1kHzDb() { return spectrum().inputSpectrum().levelDb (1000.0, 0.0) - spectrum().outputSpectrum().levelDb (1000.0, 0.0); }
};

constexpr double toneDb = -10.0, knownGainReductionDb = 15.0;
const auto tone = host::sine (std::pow (10.0, toneDb / 20.0));

// The brightest pixel of the Spectrum display's image in a column of rows around y at x, as it shows over black: a 1 px
// line drawn between two rows lights either. The display paints no background of its own, so a pixel's alpha is part
// of how bright it shows.
float brightestAround (const juce::Image& image, float x, float y)
{
    float brightest = 0.0f;
    for (int row = juce::roundToInt (y) - 1; row <= juce::roundToInt (y) + 1; ++row)
    {
        const auto pixel = image.getPixelAt (juce::roundToInt (x), row);
        brightest = std::max (brightest, pixel.getPerceivedBrightness() * pixel.getFloatAlpha());
    }
    return brightest;
}

} // namespace

TEST_CASE ("The Spectrum display shows the input and the compressor's output: compressing a tone by a known amount, the output reads that much lower")
{
    SpectrumHost host;
    CHECK (host.gapSettlesAt (tone, knownGainReductionDb, 0.5));
    // The input spectrum at about the tone's level, a full-scale sine reading 0 dB, give or take the Analyzer's
    // window between bins.
    CHECK_THAT (host.spectrum().inputSpectrum().levelDb (1000.0, 0.0), WithinAbs (toneDb, 2.0));

    // Make-up gain isn't Gain Reduction: Auto Gain and Output Gain leave the gap as it is.
    host.edit (parameters::autoGainId, 1.0f);
    host.edit (parameters::outputGainId, 12.0f);
    host.playFor (0.5, tone);
    CHECK_THAT (host.gapAt1kHzDb(), WithinAbs (knownGainReductionDb, 0.5));

    // At Ratio 1:1 the compressor is idle and the two meet.
    host.edit (parameters::ratioId, 1.0f);
    CHECK (host.gapSettlesAt (tone, 0.0, 0.1));
}

TEST_CASE ("The Spectrum display shades the gap where the output is under the input, and nothing with no Gain Reduction")
{
    SpectrumHost host;
    REQUIRE (host.gapSettlesAt (tone, knownGainReductionDb, 0.5));
    auto& spectrum = host.spectrum();
    const float x = spectrum.geometry().xOf (1000.0);
    const float inputY = spectrum.yOf (spectrum.inputSpectrum(), x), outputY = spectrum.yOf (spectrum.outputSpectrum(), x);
    // The output's line lower on the display than the input's, and the shading between the two, not past either.
    REQUIRE (outputY > inputY + 5.0f);
    const auto shaded = spectrum.gainReductionArea();
    CHECK (shaded.contains (x, (inputY + outputY) / 2.0f));
    CHECK_FALSE (shaded.contains (x, inputY - 1.5f));
    CHECK_FALSE (shaded.contains (x, outputY + 1.5f));
    CHECK (host.shadedUnderInputAt (1000.0));

    // Idle, on the tone with the tests' seeded noise under it, so every frequency checked carries signal above the
    // Analyzer's floor.
    host.edit (parameters::ratioId, 1.0f);
    const auto seeded = comp1::test::noise (1, 48000).front();
    const auto toneOverNoise = [&seeded] (int n) { return tone (n) + 0.1 * static_cast<double> (seeded[static_cast<std::size_t> (n) % seeded.size()]); };
    REQUIRE (host.gapSettlesAt (toneOverNoise, 0.0, 0.05));
    for (const double frequency : { 100.0, 1000.0, 10000.0 })
    {
        CAPTURE (frequency);
        CHECK_FALSE (host.shadedUnderInputAt (frequency));
    }
}

TEST_CASE ("The Spectrum display's Global Bypass fade glides over 150 ms each way")
{
    using comp1::SpectrumDisplay;
    // Half the fade time goes half way; the whole of it, all the way, and no further.
    CHECK_THAT (SpectrumDisplay::globalBypassFadeAfter (0.0f, true, 0.075), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (SpectrumDisplay::globalBypassFadeAfter (0.5f, true, 0.075), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (SpectrumDisplay::globalBypassFadeAfter (0.0f, true, 0.25), WithinAbs (1.0, 1.0e-6));
    // Back the same way.
    CHECK_THAT (SpectrumDisplay::globalBypassFadeAfter (1.0f, false, 0.075), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (SpectrumDisplay::globalBypassFadeAfter (0.5f, false, 0.25), WithinAbs (0.0, 1.0e-6));
    // A tick at 60 Hz moves it a ninth of the way.
    CHECK_THAT (SpectrumDisplay::globalBypassFadeAfter (0.0f, true, 1.0 / 60.0), WithinAbs (1.0 / 9.0, 1.0e-6));
}

TEST_CASE ("Under Global Bypass the Spectrum display dims the output and the Gain Reduction shading, and keeps showing the gap")
{
    SpectrumHost host;
    REQUIRE (host.gapSettlesAt (tone, knownGainReductionDb, 0.5));
    auto& spectrum = host.spectrum();
    // Not applied: the output at full strength, the shading at its own faint alpha.
    CHECK (juce::exactlyEqual (spectrum.globalBypassFade(), 0.0f));
    CHECK_THAT (spectrum.outputAlpha(), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (spectrum.gainReductionAlpha(), WithinAbs (0.35, 1.0e-6));
    // With no more audio the spectra hold still, so the two images differ only by Global Bypass.
    host.settle (100);
    const float x = spectrum.geometry().xOf (1000.0);
    const float inputY = spectrum.yOf (spectrum.inputSpectrum(), x), outputY = spectrum.yOf (spectrum.outputSpectrum(), x);
    const auto applied = spectrum.createComponentSnapshot (spectrum.getLocalBounds(), false);

    // Switched on, the fade only rises until it gets there. How many ticks see it part-way depends on the machine's
    // speed (the glide's timing is the test above).
    host.edit (parameters::globalBypassId, 1.0f);
    float last = 0.0f;
    bool monotonic = true;
    REQUIRE (host.settleUntil ([&] {
        const float fade = spectrum.globalBypassFade();
        monotonic = monotonic && fade >= last;
        last = fade;
        return juce::exactlyEqual (fade, 1.0f);
    }));
    CHECK (monotonic);
    // Bypassed: both at 45 % of their alphas, the gap still there to show what the compressor would do.
    CHECK_THAT (spectrum.outputAlpha(), WithinAbs (0.45, 1.0e-6));
    CHECK_THAT (spectrum.gainReductionAlpha(), WithinAbs (0.1575, 1.0e-6));
    CHECK (spectrum.gainReductionArea().contains (x, (inputY + outputY) / 2.0f));
    const auto bypassed = spectrum.createComponentSnapshot (spectrum.getLocalBounds(), false);
    CHECK (brightestAround (bypassed, x, (inputY + outputY) / 2.0f) < brightestAround (applied, x, (inputY + outputY) / 2.0f));
    CHECK (brightestAround (bypassed, x, outputY) < brightestAround (applied, x, outputY));

    // Switched off, it only falls until it is back.
    host.edit (parameters::globalBypassId, 0.0f);
    REQUIRE (host.settleUntil ([&] {
        const float fade = spectrum.globalBypassFade();
        monotonic = monotonic && fade <= last;
        last = fade;
        return juce::exactlyEqual (fade, 0.0f);
    }));
    CHECK (monotonic);
    CHECK_THAT (spectrum.outputAlpha(), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (spectrum.gainReductionAlpha(), WithinAbs (0.35, 1.0e-6));
}

TEST_CASE ("An editor opened under Global Bypass shows the Spectrum display bypassed at once")
{
    SpectrumHost host;
    host.edit (parameters::globalBypassId, 1.0f);
    host.reopen();
    CHECK (juce::exactlyEqual (host.spectrum().globalBypassFade(), 1.0f));
    CHECK_THAT (host.spectrum().outputAlpha(), WithinAbs (0.45, 1.0e-6));
}

TEST_CASE ("The Spectrum display's taps are skipped as the editor opens, and drained on every tick on another tab too, while its spectra move only while it shows")
{
    SpectrumHost host;
    // More than the taps hold, played with the editor closed: none of it reaches the spectra.
    host.editor.reset();
    host.play (40, 512, tone);
    host.reopen();
    host.settle (200);
    CHECK (host.spectrum().inputSpectrum().levelDb (1000.0, 0.0) < -100.0);
    CHECK (host.spectrum().outputSpectrum().levelDb (1000.0, 0.0) < -100.0);

    // On the Level tab, the taps are drained each tick, so they hold nothing a tick after playing, and the spectra stay
    // where they were.
    host.processor.setDisplayTab (comp1::DisplayTab::level);
    REQUIRE (host.settleUntil ([&] { return ! host.spectrum().isShowing(); }));
    std::vector<float> left (static_cast<std::size_t> (std::max (host.processor.inputTapSampleCapacity(), host.processor.outputTapSampleCapacity())));
    for (int i = 0; i < 10; ++i)
    {
        host.play (2, 512, tone);
        const int ticks = host.editorTick().count();
        REQUIRE (host.settleUntil ([&] { return host.editorTick().count() > ticks; }));
        CHECK (host.processor.readInputTapSamples (left.data(), static_cast<int> (left.size())) == 0);
        CHECK (host.processor.readOutputTapSamples (left.data(), static_cast<int> (left.size())) == 0);
    }
    CHECK (host.spectrum().inputSpectrum().levelDb (1000.0, 0.0) < -100.0);
}

TEST_CASE ("A screen reader finds the Spectrum display as a group titled Spectrum, with no Tab stop")
{
    SpectrumHost host;
    auto& spectrum = host.spectrum();
    auto* group = spectrum.getAccessibilityHandler();
    REQUIRE (group != nullptr);
    CHECK (group->getRole() == juce::AccessibilityRole::group);
    CHECK (group->getTitle() == "Spectrum");
    CHECK_FALSE (spectrum.getWantsKeyboardFocus());
}

TEST_CASE ("With no audio the Spectrum display asks for no repaint; samples arriving repaint it")
{
    SpectrumHost host;
    host.playFor (0.5, tone);
    host.settle (16);
    auto& repaints = harness::countRepaintsOf (host.spectrum());
    host.settle (300);
    CHECK (repaints.count == 0);

    host.playFor (0.5, tone);
    CHECK (repaints.count > 0);
    host.spectrum().setCachedComponentImage (nullptr);
}

TEST_CASE ("Expanded View snapshot: the Spectrum tab with a tone and noise compressed, and under Global Bypass", "[.screens]")
{
    SpectrumHost host;
    // The tests' seeded noise under the tone, the same on every platform, so the spectra fill the width.
    const auto seeded = comp1::test::noise (1, 48000).front();
    const auto signal = [&seeded] (int n) { return tone (n) + 0.5 * static_cast<double> (seeded[static_cast<std::size_t> (n) % seeded.size()]); };
    // Long enough by the editor's clock for the spectra to settle: about 3 s of ticks.
    const int ticks = host.editorTick().count();
    REQUIRE (host.keepPlayingUntil (signal, [&] { return host.editorTick().count() > ticks + 180; }));
    host.settleAnimations();
    harness::writeSnapshot (*host.editor, "expanded-spectrum", {}, 1.0f);

    // With no more audio the spectra hold still while the display fades into Global Bypass.
    host.edit (parameters::globalBypassId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (host.spectrum().globalBypassFade(), 1.0f); }));
    host.settleAnimations();
    harness::writeSnapshot (*host.editor, "expanded-spectrum-global-bypass", {}, 1.0f);
}
