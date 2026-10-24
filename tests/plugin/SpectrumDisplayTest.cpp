// The Expanded View's Spectrum tab: the input and output spectra, and the Gain Reduction shaded between them.

#include "Measure.h"
#include "Playing.h"
#include "SpectrumDisplay.h"
#include "plugkit/ui/staple/Tokens.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
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

    // Keeps playing signal until the gap at 1 kHz is within tolerance of targetDb. The spectra move by the time between
    // the editor's ticks, at the Analyzer's default speed, so they settle as the clock runs, however fast a test plays.
    template <typename Signal>
    bool gapSettlesAt (Signal&& signal, double targetDb, double tolerance)
    {
        return keepPlayingUntil (signal, [&] { return std::abs (gapDb (1000.0) - targetDb) < tolerance; });
    }

    // Keeps playing signal until the two spectra are within tolerance of each other at every one of frequencies. They
    // don't meet everywhere at once: the Gain Reduction falls in the audio's time and the spectra follow in the
    // clock's, so how far one frequency lags another depends on how much audio plays between ticks, which is the
    // machine's speed.
    template <typename Signal>
    bool spectraMeetAt (Signal&& signal, const std::vector<double>& frequencies, double tolerance)
    {
        return keepPlayingUntil (signal, [&] {
            return std::all_of (frequencies.begin(), frequencies.end(), [&] (double f) { return std::abs (gapDb (f)) < tolerance; });
        });
    }

    // The x of the drawn column nearest frequency. The lines and the shading's edges run straight between columns
    // pixelStep apart, so only on a column are they drawn at yOf: between two, a jagged spectrum's line is drawn up to
    // a few pixels from it, and a point 1.5 px under yOf can land in the sliver a faint residual gap leaves.
    float columnAt (double frequency)
    {
        constexpr float step = plugkit::ui::display::DisplayGeometry::pixelStep;
        return std::round (spectrum().geometry().xOf (frequency) / step) * step;
    }

    // Whether the Gain Reduction is shaded just under the input's line at frequency's column, where it starts.
    bool shadedUnderInputAt (double frequency)
    {
        auto& display = spectrum();
        const float x = columnAt (frequency);
        return display.gainReductionArea().contains (x, display.yOf (display.inputSpectrum(), x) + 1.5f);
    }

    // How much lower the output spectrum reads than the input spectrum at frequency, in dB.
    double gapDb (double frequency) { return spectrum().inputSpectrum().levelDb (frequency, 0.0) - spectrum().outputSpectrum().levelDb (frequency, 0.0); }
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

// A software renderer that draws as the display's snapshot does, and keeps each stroke's colour and bounds in the
// order they were drawn.
class StrokeRecorder final : public juce::LowLevelGraphicsSoftwareRenderer
{
public:
    explicit StrokeRecorder (juce::Rectangle<int> area)
        : juce::LowLevelGraphicsSoftwareRenderer (juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true))
    {
    }

    void setFill (const juce::FillType& fill) override
    {
        colour = fill.isColour() ? std::optional<juce::Colour> (fill.colour) : std::nullopt;
        juce::LowLevelGraphicsSoftwareRenderer::setFill (fill);
    }

    void strokePath (const juce::Path& path, const juce::PathStrokeType& type, const juce::AffineTransform& transform) override
    {
        strokes.push_back ({ colour, path.getBoundsTransformed (transform) });
        juce::LowLevelGraphicsSoftwareRenderer::strokePath (path, type, transform);
    }

    // Paints component, as its snapshot would, keeping its strokes. Nothing reads the pixels.
    void paint (juce::Component& component)
    {
        juce::Graphics g (*this);
        component.paintEntireComponent (g, false);
    }

    // The last stroke drawn in c, by its place in the order.
    std::optional<std::size_t> lastIn (juce::Colour c) const
    {
        for (std::size_t i = strokes.size(); i-- > 0;)
            if (strokes[i].colour == c)
                return i;
        return std::nullopt;
    }

    juce::Rectangle<float> bounds (std::size_t stroke) const { return strokes[stroke].bounds; }

private:
    struct Stroke
    {
        std::optional<juce::Colour> colour;
        juce::Rectangle<float> bounds;
    };

    std::optional<juce::Colour> colour;
    std::vector<Stroke> strokes;
};

} // namespace

TEST_CASE ("The Spectrum display shows the input and the compressor's output: compressing a tone by a known amount, the output reads that much lower")
{
    SpectrumHost host;
    CHECK (host.gapSettlesAt (tone, knownGainReductionDb, 0.5));
    // The input spectrum at about the tone's level, a full-scale sine reading 0 dB, give or take the Analyzer's
    // window between bins. It rises from silence by the clock as the gap does, so it is still on its way when the gap
    // is within tolerance, by however far the ticks so far have taken it.
    CHECK (host.keepPlayingUntil (tone, [&] { return std::abs (host.spectrum().inputSpectrum().levelDb (1000.0, 0.0) - toneDb) < 2.0; }));

    // Auto Gain and Output Gain aren't Gain Reduction: they leave the gap as it is.
    host.edit (parameters::autoGainId, 1.0f);
    host.edit (parameters::outputGainId, 12.0f);
    host.playFor (0.5, tone);
    CHECK_THAT (host.gapDb (1000.0), WithinAbs (knownGainReductionDb, 0.5));

    // At Ratio 1:1 the compressor is idle and the two meet.
    host.edit (parameters::ratioId, 1.0f);
    CHECK (host.gapSettlesAt (tone, 0.0, 0.1));
}

TEST_CASE ("The Spectrum display shades the gap where the output is under the input, and nothing with no Gain Reduction")
{
    SpectrumHost host;
    REQUIRE (host.gapSettlesAt (tone, knownGainReductionDb, 0.5));
    auto& spectrum = host.spectrum();
    const float x = host.columnAt (1000.0);
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
    // Every sixth of an octave from 100 Hz to 16 kHz: the tone's skirts, where the spectra meet last, and the noise's
    // jagged line.
    std::vector<double> frequencies;
    for (double frequency = 100.0; frequency <= 16000.0; frequency *= std::pow (2.0, 1.0 / 6.0))
        frequencies.push_back (frequency);
    REQUIRE (host.spectraMeetAt (toneOverNoise, frequencies, 0.05));
    for (const double frequency : frequencies)
    {
        CAPTURE (frequency);
        CHECK_FALSE (host.shadedUnderInputAt (frequency));
    }
}

TEST_CASE ("Where the input and the output meet, with no Gain Reduction, the Spectrum display draws the output's line over the input's, with or without Global Bypass")
{
    SpectrumHost host;
    host.edit (parameters::ratioId, 1.0f);
    REQUIRE (host.gapSettlesAt (tone, 0.0, 0.1));
    host.settle (100);
    auto& spectrum = host.spectrum();
    for (const bool bypassed : { false, true })
    {
        CAPTURE (bypassed);
        host.edit (parameters::globalBypassId, bypassed ? 1.0f : 0.0f);
        REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (spectrum.globalBypassFade(), bypassed ? 1.0f : 0.0f); }));
        StrokeRecorder strokes (spectrum.getLocalBounds());
        strokes.paint (spectrum);
        // The input's line, in the Analyzer's pre line colour, then the output's, in its line colour, where the
        // input's was: the last stroke over it.
        const auto inputLine = strokes.lastIn (staple::tokens::colour::anPre), outputLine = strokes.lastIn (staple::tokens::colour::anLine);
        REQUIRE (inputLine.has_value());
        REQUIRE (outputLine.has_value());
        CHECK (*inputLine < *outputLine);
        CHECK (strokes.bounds (*outputLine).expanded (1.0f).contains (strokes.bounds (*inputLine)));
    }
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
    // speed (the glide's timing is GlobalBypassFadeTest's).
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
    std::vector<float> left (static_cast<std::size_t> (std::max (host.processor.capacity (comp1::Tap::input), host.processor.capacity (comp1::Tap::output))));
    for (int i = 0; i < 10; ++i)
    {
        host.play (2, 512, tone);
        const int ticks = host.editorTick().count();
        REQUIRE (host.settleUntil ([&] { return host.editorTick().count() > ticks; }));
        CHECK (host.processor.read (comp1::Tap::input, left.data(), static_cast<int> (left.size())) == 0);
        CHECK (host.processor.read (comp1::Tap::output, left.data(), static_cast<int> (left.size())) == 0);
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
