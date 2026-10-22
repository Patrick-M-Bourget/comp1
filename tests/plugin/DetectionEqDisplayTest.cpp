// The Expanded View's Detection EQ tab: its spectrum, curve, handles and Detection Audition.

#include "DetectionEqDisplay.h"
#include "Playing.h"
#include "Measure.h"

#include <plugkit/ShapeDesign.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <map>
#include <vector>

namespace parameters = comp1::parameters;
using Band = comp1::DetectionEqBand;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{

constexpr double testRate = 48000.0;

// A sine at frequency, at amplitude, at 48 kHz: sample n.
double sine (double frequency, double amplitude, int n)
{
    return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * n / testRate);
}

// The editor playing (Playing.h) on a stereo track with the Sidechain given, showing the Detection EQ tab.
struct DetectionEqHost : host::Playing
{
    explicit DetectionEqHost (const juce::AudioChannelSet& sidechain = juce::AudioChannelSet::stereo())
        : host::Playing (testRate, sidechain)
    {
        showTab (comp1::DisplayTab::detectionEq);
    }

    void showTab (comp1::DisplayTab tab)
    {
        processor.setDisplayTab (tab);
        REQUIRE (settleUntil ([&] { return expandedView().display().shownTab() == tab; }));
    }

    comp1::DetectionEqDisplay& eqDisplay() { return expandedView().display().detectionEqDisplay(); }

    // Plays a second of main and sidechain, a tick's worth of samples between ticks, as a host plays while the editor
    // shows it.
    template <typename Main, typename Sidechain>
    void playForASecond (Main&& main, Sidechain&& sidechain)
    {
        for (int i = 0; i < 60; ++i)
        {
            play (1, 800, [&, i] (int n) { return main (i * 800 + n); }, [&, i] (int n) { return sidechain (i * 800 + n); });
            settle (16);
        }
    }
};

// The Frequency where the spectrum shown is loudest, searched in sixteenths of an octave from 20 Hz to 20 kHz.
double loudestFrequency (const plugkit::ui::AnalyzerSpectrum& spectrum)
{
    double loudest = 20.0, loudestDb = -1.0e9;
    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 16.0))
        if (const double db = spectrum.levelDb (f, 0.0); db > loudestDb)
        {
            loudestDb = db;
            loudest = f;
        }
    return spectrum.peakNear (loudest);
}

// One bin of the spectrum's FFT, in Hz.
double binWidth (const plugkit::ui::AnalyzerSpectrum& spectrum)
{
    return spectrum.getSampleRate() / plugkit::ui::AnalyzerSpectrum::fftSize (spectrum.getSampleRate(), spectrum.getResolution());
}

} // namespace

TEST_CASE ("The Detection EQ tab shows a spectrum of the Detection Source: the main input with Internal, the Sidechain with External, and the floor with none")
{
    const auto silence = [] (int) { return 0.0; };
    const auto main1kHz = [] (int n) { return sine (1000.0, 0.5, n); };
    {
        DetectionEqHost host;
        host.playForASecond (main1kHz, silence);
        const auto& spectrum = host.eqDisplay().spectrum();
        CHECK_THAT (loudestFrequency (spectrum), WithinAbs (1000.0, binWidth (spectrum)));
    }
    {
        DetectionEqHost host;
        host.edit (parameters::detectionSourceId, 1.0f);
        host.playForASecond (main1kHz, [] (int n) { return sine (100.0, 0.5, n); });
        const auto& spectrum = host.eqDisplay().spectrum();
        CHECK_THAT (loudestFrequency (spectrum), WithinAbs (100.0, binWidth (spectrum)));
        CHECK (spectrum.levelDb (1000.0, 0.0) < spectrum.levelDb (100.0, 0.0) - 40.0);
    }
    {
        DetectionEqHost host (juce::AudioChannelSet::disabled());
        host.playForASecond (main1kHz, silence);
        const auto& spectrum = host.eqDisplay().spectrum();
        const double playing = spectrum.levelDb (1000.0, 0.0);
        CHECK (playing > -20.0);
        // External with no Sidechain: it falls from there toward the floor, further each second.
        host.edit (parameters::detectionSourceId, 1.0f);
        double last = playing;
        for (int second = 0; second < 3; ++second)
        {
            host.playForASecond (main1kHz, silence);
            const double now = spectrum.levelDb (1000.0, 0.0);
            CHECK (now < last - 10.0);
            last = now;
        }
    }
}

TEST_CASE ("The tap is skipped as the editor opens, and drained on every tick on the Level tab too, while the spectrum moves only on its own tab")
{
    const auto main1kHz = [] (int n) { return sine (1000.0, 0.5, n); };
    const auto silence = [] (int) { return 0.0; };
    DetectionEqHost host;
    // More than the tap holds, played with the editor closed: none of it reaches the spectrum.
    host.editor.reset();
    host.play (40, 512, main1kHz, silence);
    host.reopen();
    host.settle (200);
    CHECK (host.eqDisplay().spectrum().levelDb (1000.0, 0.0) < -100.0);

    // On the Level tab the tap is drained each tick, so it holds nothing a tick after playing, and the spectrum stays
    // where it was.
    host.showTab (comp1::DisplayTab::level);
    for (int i = 0; i < 10; ++i)
    {
        host.play (2, 512, main1kHz, silence);
        const int ticks = host.editorTick().count();
        REQUIRE (host.settleUntil ([&] { return host.editorTick().count() > ticks; }));
        std::vector<float> left (static_cast<std::size_t> (host.processor.detectionSourceSampleCapacity()));
        CHECK (host.processor.readDetectionSourceSamples (left.data(), static_cast<int> (left.size())) == 0);
    }
    CHECK (host.eqDisplay().spectrum().levelDb (1000.0, 0.0) < -100.0);
}

namespace
{

// The Detection EQ's level at frequency, in dB: the product of plugkit's responses of the bands that are on, at 48 kHz,
// worked out here from the host parameters' values.
double expectedDb (host::EditorHost& host, double frequency)
{
    const auto settings = comp1::parameters::SettingsReader (host.processor.parameterState()).read().detectionEq;
    double magnitude = 1.0;
    for (const auto* band : { &settings.highPass, &settings.freeBand, &settings.lowPass })
        if (band->on)
            magnitude *= std::abs (plugkit::responseAt (plugkit::designBand (band->filter, testRate), frequency, testRate));
    return 20.0 * std::log10 (magnitude);
}

const std::vector<double> checkedFrequencies { 20.0, 50.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0, 15000.0 };

// Every band on: a high-pass at 120 Hz, 24 dB/oct, a low-pass at 8 kHz, 12 dB/oct, and a Bell at 1 kHz, +6 dB, Q 2.
void everyBandOn (host::EditorHost& host)
{
    host.edit (parameters::detectionHighPassOnId, 1.0f);
    host.edit (parameters::detectionHighPassFrequencyId, 120.0f);
    host.edit (parameters::detectionHighPassSlopeId, 24.0f);
    host.edit (parameters::detectionLowPassOnId, 1.0f);
    host.edit (parameters::detectionLowPassFrequencyId, 8000.0f);
    host.edit (parameters::detectionBandOnId, 1.0f);
    host.edit (parameters::detectionBandFrequencyId, 1000.0f);
    host.edit (parameters::detectionBandGainId, 6.0f);
    host.edit (parameters::detectionBandQId, 2.0f);
}

// True once the curve shows what the parameters hold.
bool curveFollows (DetectionEqHost& host)
{
    for (const double f : checkedFrequencies)
        if (std::abs (host.eqDisplay().curveDbAt (f) - expectedDb (host, f)) > 1.0e-6)
            return false;
    return true;
}

} // namespace

TEST_CASE ("The curve is the Detection EQ's response the Engine plays, an Off band leaving it flat, and it follows changes from outside on the tick")
{
    DetectionEqHost host;
    auto& eq = host.eqDisplay();
    // All Off by default: flat.
    for (const double f : checkedFrequencies)
        CHECK_THAT (eq.curveDbAt (f), WithinAbs (0.0, 1.0e-9));

    everyBandOn (host);
    REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
    CHECK (eq.curveDbAt (20.0) < -20.0);
    CHECK_THAT (eq.curveDbAt (1000.0), WithinAbs (6.0, 0.5));
    // Drawn there in curveMain, away from the handles.
    const auto image = eq.createComponentSnapshot (eq.getLocalBounds(), true, 1.0f);
    for (const double f : { 300.0, 2500.0 })
    {
        CAPTURE (f);
        const auto shape = eq.geometry();
        const auto pixel = image.getPixelAt (juce::roundToInt (shape.xOf (f)), juce::roundToInt (shape.yOf (eq.curveDbAt (f))));
        CHECK (pixel.getRed() > 200);
        CHECK (pixel.getGreen() > 150);
        CHECK (pixel.getBlue() < 100);
    }

    // Each band off in turn leaves the curve as the others make it.
    for (const auto& id : { parameters::detectionHighPassOnId, parameters::detectionBandOnId, parameters::detectionLowPassOnId })
    {
        CAPTURE (id);
        host.edit (id, 0.0f);
        REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
        host.edit (id, 1.0f);
        REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
    }

    // Host Automation (no gesture), and undo.
    host.set (parameters::detectionBandGainId, -12.0f);
    REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
    CHECK_THAT (eq.curveDbAt (1000.0), WithinAbs (-12.0, 0.5));
    host.processor.session().editHistory().undo();
    REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
}

TEST_CASE ("Each band's handle sits at its Frequency, the cuts on the curve and the Bell at its Gain, a shelf on the curve, and beyond the Display Range at its edge")
{
    DetectionEqHost host;
    everyBandOn (host);
    REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
    auto& eq = host.eqDisplay();
    const auto shape = eq.geometry();
    const auto at = [&] (Band band) { return eq.handleCentre (band); };
    CHECK_THAT (at (Band::highPass).x, WithinAbs (shape.xOf (120.0), 1.0e-3));
    CHECK_THAT (at (Band::highPass).y, WithinAbs (shape.yOf (eq.curveDbAt (120.0)), 1.0e-3));
    CHECK_THAT (at (Band::lowPass).x, WithinAbs (shape.xOf (8000.0), 1.0e-3));
    CHECK_THAT (at (Band::lowPass).y, WithinAbs (shape.yOf (eq.curveDbAt (8000.0)), 1.0e-3));
    CHECK_THAT (at (Band::freeBand).x, WithinAbs (shape.xOf (1000.0), 1.0e-3));
    CHECK_THAT (at (Band::freeBand).y, WithinAbs (shape.yOf (6.0), 1.0e-3));
    // The handle components sit there too.
    for (const auto band : { Band::highPass, Band::freeBand, Band::lowPass })
        CHECK (eq.handle (band).getBounds().toFloat().getCentre().getDistanceFrom (at (band)) < 1.0f);

    // A Low Shelf of +6 dB at 1 kHz: on the curve there, not at its Gain.
    host.edit (parameters::detectionBandShapeId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
    CHECK_THAT (at (Band::freeBand).y, WithinAbs (shape.yOf (eq.curveDbAt (1000.0)), 1.0e-3));
    CHECK (std::abs (eq.curveDbAt (1000.0) - 6.0) > 1.0);

    // A low-pass at 10 Hz takes the curve at the high-pass's 120 Hz far below the Display Range: its handle sits at
    // the display's bottom edge.
    host.edit (parameters::detectionLowPassFrequencyId, 10.0f);
    REQUIRE (host.settleUntil ([&] { return curveFollows (host); }));
    REQUIRE (eq.curveDbAt (120.0) < -30.0);
    CHECK_THAT (at (Band::highPass).y, WithinAbs (shape.yOf (-30.0), 1.0e-3));
}

namespace
{

// What the host sees of the parameters: each change, and whether it came inside a gesture on that parameter.
struct HostView final : juce::AudioProcessorListener
{
    explicit HostView (juce::AudioProcessor& p) : processor (p) { processor.addListener (this); }
    ~HostView() override { processor.removeListener (this); }

    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
    {
        ++changes;
        if (open.count (index) == 0)
            ++outsideGestures;
    }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { ++open[index]; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override
    {
        if (--open[index] == 0)
            open.erase (index);
    }

    juce::AudioProcessor& processor;
    std::map<int, int> open; // gestures open, per parameter index
    int changes = 0, outsideGestures = 0;
};

// A drag of handle by offset, from its centre, through its own handlers (CODING_STANDARDS "Editor behaviour"), with
// mods held throughout.
void dragHandle (juce::Component& handle, juce::Point<float> offset, juce::ModifierKeys mods = {})
{
    const auto centre = handle.getLocalBounds().toFloat().getCentre();
    const auto held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    handle.mouseDown (harness::mouseEvent (handle, centre, held));
    handle.mouseDrag (harness::mouseEvent (handle, centre + offset * 0.5f, held, centre));
    handle.mouseDrag (harness::mouseEvent (handle, centre + offset, held, centre));
    handle.mouseUp (harness::mouseEvent (handle, centre + offset, mods, centre));
}

// A wheel step over handle, up for a positive direction.
void wheel (juce::Component& handle, int direction)
{
    juce::MouseWheelDetails details {};
    details.deltaY = 0.25f * static_cast<float> (direction);
    handle.mouseWheelMove (harness::mouseEvent (handle, handle.getLocalBounds().toFloat().getCentre()), details);
}

} // namespace

TEST_CASE ("Dragging a handle moves its band, Shift finely, as one undo step inside gestures, and grabbing an Off band turns it on in it")
{
    DetectionEqHost host;
    auto& eq = host.eqDisplay();
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    HostView view (host.processor);

    // The high-pass, Off: dragged right, it is on and higher.
    REQUIRE (host.value (parameters::detectionHighPassOnId) < 0.5f);
    const float highPassBefore = host.value (parameters::detectionHighPassFrequencyId);
    dragHandle (eq.handle (Band::highPass), { 100.0f, 0.0f });
    CHECK (host.value (parameters::detectionHighPassOnId) > 0.5f);
    const float dragged = host.value (parameters::detectionHighPassFrequencyId);
    // 100 px to the right of where it sat.
    const auto shape = eq.geometry();
    CHECK_THAT (dragged, WithinRel (static_cast<float> (shape.frequencyAt (shape.xOf (highPassBefore) + 100.0f)), 1.0e-3f));
    CHECK (history.undoSteps() == start + 1);
    CHECK (view.changes > 0);
    CHECK (view.outsideGestures == 0);
    CHECK (view.open.empty());

    // With Shift, a quarter as far.
    REQUIRE (host.settleUntil ([&] { return eq.handle (Band::highPass).getBounds().getCentreX() > shape.xOf (highPassBefore) + 90.0f; }));
    dragHandle (eq.handle (Band::highPass), { 100.0f, 0.0f }, juce::ModifierKeys::shiftModifier);
    CHECK_THAT (host.value (parameters::detectionHighPassFrequencyId),
                WithinRel (static_cast<float> (shape.frequencyAt (shape.xOf (dragged) + 25.0f)), 1.0e-3f));
    CHECK (history.undoSteps() == start + 2);

    // The free band, dragged up: its Gain rises, by the dB the pixels span.
    const float gainBefore = host.value (parameters::detectionBandGainId);
    dragHandle (eq.handle (Band::freeBand), { 0.0f, -40.0f });
    CHECK (host.value (parameters::detectionBandOnId) > 0.5f);
    CHECK_THAT (host.value (parameters::detectionBandGainId), WithinAbs (static_cast<float> (shape.dbAt (shape.yOf (gainBefore) - 40.0f)), 1.0e-3f));
    CHECK (host.value (parameters::detectionBandGainId) > gainBefore + 1.0f);
    CHECK (history.undoSteps() == start + 3);
    CHECK (view.outsideGestures == 0);

    // Undo puts everything back, the bands Off again.
    for (int i = 0; i < 3; ++i)
        history.undo();
    CHECK (host.value (parameters::detectionHighPassOnId) < 0.5f);
    CHECK_THAT (host.value (parameters::detectionHighPassFrequencyId), WithinRel (highPassBefore, 1.0e-5f));
    CHECK (host.value (parameters::detectionBandOnId) < 0.5f);
    CHECK_THAT (host.value (parameters::detectionBandGainId), WithinAbs (gainBefore, 1.0e-5f));
}

TEST_CASE ("The wheel over a handle steps the free band's Q, and a cut's Slope by 6 dB/oct, each step one undo step")
{
    DetectionEqHost host;
    auto& eq = host.eqDisplay();
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    HostView view (host.processor);

    const float qBefore = host.value (parameters::detectionBandQId);
    wheel (eq.handle (Band::freeBand), 1);
    const float qUp = host.value (parameters::detectionBandQId);
    CHECK (qUp > qBefore);
    CHECK (history.undoSteps() == start + 1);
    wheel (eq.handle (Band::freeBand), -1);
    CHECK (host.value (parameters::detectionBandQId) < qUp);
    CHECK (history.undoSteps() == start + 2);

    const float slope = host.value (parameters::detectionLowPassSlopeId);
    wheel (eq.handle (Band::lowPass), 1);
    CHECK_THAT (host.value (parameters::detectionLowPassSlopeId), WithinAbs (slope + 6.0f, 1.0e-4));
    wheel (eq.handle (Band::lowPass), -1);
    wheel (eq.handle (Band::lowPass), -1);
    CHECK_THAT (host.value (parameters::detectionLowPassSlopeId), WithinAbs (slope - 6.0f, 1.0e-4));
    CHECK (history.undoSteps() == start + 5);
    CHECK (view.outsideGestures == 0);
    CHECK (view.open.empty());
}

TEST_CASE ("Arrows move a focused handle: Frequency by a semitone, Gain by 0.5 dB or a cut's Slope by a step, a tenth with Shift, a held key one undo step")
{
    DetectionEqHost host;
    auto& eq = host.eqDisplay();
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    HostView view (host.processor);
    const auto right = juce::KeyPress (juce::KeyPress::rightKey), up = juce::KeyPress (juce::KeyPress::upKey);

    auto& free = eq.handle (Band::freeBand);
    free.grabKeyboardFocus();
    REQUIRE (free.hasKeyboardFocus (false));
    const float frequency = host.value (parameters::detectionBandFrequencyId);
    // Off, it turns on with the first move.
    CHECK (host.press (right));
    CHECK (host.value (parameters::detectionBandOnId) > 0.5f);
    CHECK_THAT (host.value (parameters::detectionBandFrequencyId), WithinRel (frequency * std::pow (2.0f, 1.0f / 12.0f), 1.0e-4f));
    CHECK (history.undoSteps() == start + 1);
    CHECK (host.press (harness::withShift (right)));
    CHECK_THAT (host.value (parameters::detectionBandFrequencyId), WithinRel (frequency * std::pow (2.0f, 1.1f / 12.0f), 1.0e-4f));
    const float gain = host.value (parameters::detectionBandGainId);
    CHECK (host.press (up));
    CHECK_THAT (host.value (parameters::detectionBandGainId), WithinAbs (gain + 0.5f, 1.0e-4));
    CHECK (host.press (harness::withShift (up)));
    CHECK_THAT (host.value (parameters::detectionBandGainId), WithinAbs (gain + 0.55f, 1.0e-4));
    CHECK (history.undoSteps() == start + 4);

    // A held key: its repeats, then the release, one step.
    REQUIRE (host.hold (up));
    REQUIRE (host.hold (up));
    REQUIRE (host.hold (up));
    host.release();
    CHECK_THAT (host.value (parameters::detectionBandGainId), WithinAbs (gain + 2.05f, 1.0e-4));
    CHECK (history.undoSteps() == start + 5);

    // A cut's Slope.
    auto& high = eq.handle (Band::highPass);
    high.grabKeyboardFocus();
    const float slope = host.value (parameters::detectionHighPassSlopeId);
    CHECK (host.press (up));
    CHECK_THAT (host.value (parameters::detectionHighPassSlopeId), WithinAbs (slope + 6.0f, 1.0e-4));
    CHECK (host.value (parameters::detectionHighPassOnId) > 0.5f);
    CHECK (view.outsideGestures == 0);
    CHECK (view.open.empty());

    // Undo restores everything.
    while (history.canUndo())
        history.undo();
    CHECK (host.value (parameters::detectionBandOnId) < 0.5f);
    CHECK (host.value (parameters::detectionHighPassOnId) < 0.5f);
    CHECK_THAT (host.value (parameters::detectionBandFrequencyId), WithinRel (frequency, 1.0e-5f));
    CHECK_THAT (host.value (parameters::detectionBandGainId), WithinAbs (gain, 1.0e-5f));
}

TEST_CASE ("Tab reaches the High-pass, Free band and Low-pass handles, then Detection Audition, after the display's tabs and before the Output Meter")
{
    DetectionEqHost host;
    juce::Component::unfocusAllComponents();
    juce::StringArray titles;
    for (int i = 0; i < 200; ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        REQUIRE (focused != nullptr);
        if (titles.contains (focused->getTitle()))
            break;
        titles.add (focused->getTitle());
    }
    CAPTURE (titles.joinIntoString (", "));
    // After the last of the display's tabs.
    const int tabs = titles.indexOf ("Transfer Curve");
    REQUIRE (tabs >= 2);
    CHECK (titles[tabs - 2] == "Level");
    CHECK (titles[tabs - 1] == "Detection EQ");
    CHECK (titles[tabs + 1] == "High-pass handle");
    CHECK (titles[tabs + 2] == "Free band handle");
    CHECK (titles[tabs + 3] == "Low-pass handle");
    CHECK (titles[tabs + 4] == "Detection Audition");
    CHECK (titles[tabs + 5] == "Output Meter");
}

TEST_CASE ("A screen reader finds the group Detection EQ, each handle by its own title with the band's settings, and Detection Audition")
{
    DetectionEqHost host;
    auto& eq = host.eqDisplay();
    auto* group = eq.getAccessibilityHandler();
    REQUIRE (group != nullptr);
    CHECK (group->getRole() == juce::AccessibilityRole::group);
    CHECK (group->getTitle() == "Detection EQ");

    const auto read = [&] (Band band) {
        auto* handler = eq.handle (band).getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        REQUIRE (handler->getValueInterface() != nullptr);
        return handler->getValueInterface()->getCurrentValueAsString();
    };
    const auto title = [&] (Band band) { return eq.handle (band).getAccessibilityHandler()->getTitle(); };
    CHECK (title (Band::highPass) == "High-pass handle");
    CHECK (title (Band::freeBand) == "Free band handle");
    CHECK (title (Band::lowPass) == "Low-pass handle");
    // Never a host parameter's name: the Detection section's controls carry those.
    for (auto* parameter : host.processor.getParameters())
        for (const auto band : { Band::highPass, Band::freeBand, Band::lowPass })
            CHECK (title (band) != parameter->getName (100));

    // The defaults, every band Off.
    CHECK (read (Band::highPass) == "20 Hz, 12 dB/oct, Off");
    CHECK (read (Band::lowPass) == "20.0 kHz, 12 dB/oct, Off");
    CHECK (read (Band::freeBand) == "Bell, 1.00 kHz, 0.00 dB, Q 1.000, Off");

    host.edit (parameters::detectionHighPassOnId, 1.0f);
    host.edit (parameters::detectionHighPassFrequencyId, 120.0f);
    host.edit (parameters::detectionBandOnId, 1.0f);
    host.edit (parameters::detectionBandGainId, 3.0f);
    host.edit (parameters::detectionBandShapeId, 2.0f);
    REQUIRE (host.settleUntil ([&] { return read (Band::freeBand) == "High Shelf, 1.00 kHz, +3.00 dB, Q 1.000"; }));
    CHECK (read (Band::highPass) == "120 Hz, 12 dB/oct");

    auto* audition = eq.detectionAudition().getAccessibilityHandler();
    REQUIRE (audition != nullptr);
    CHECK (audition->getTitle() == "Detection Audition");
    CHECK (audition->getRole() == juce::AccessibilityRole::button);
}

namespace
{

// The loudest sample of a block.
float peakOf (const juce::AudioBuffer<float>& block)
{
    float peak = 0.0f;
    for (int ch = 0; ch < block.getNumChannels(); ++ch)
        peak = std::max (peak, block.getMagnitude (ch, 0, block.getNumSamples()));
    return peak;
}

void press (staple::IconButton& button)
{
    button.mouseDown (harness::mouseEvent (button, button.getLocalBounds().toFloat().getCentre(), juce::ModifierKeys::leftButtonModifier));
}

void release (staple::IconButton& button)
{
    const auto centre = button.getLocalBounds().toFloat().getCentre();
    button.mouseUp (harness::mouseEvent (button, centre, {}, centre));
}

} // namespace

TEST_CASE ("Detection Audition, held by the mouse or Space, plays the detection signal, and lets go on release, on the Level tab, as the editor closes and on a restore")
{
    DetectionEqHost host;
    auto& session = host.processor.session();
    // External, the Sidechain carrying a 100 Hz sine and the main input silence: only Detection Audition plays it.
    host.edit (parameters::detectionSourceId, 1.0f);
    REQUIRE (session.loadPreset (session.presetState(), "External"));
    const int steps = session.editHistory().undoSteps();
    const auto silence = [] (int) { return 0.0; };
    const auto tone = [] (int n) { return sine (100.0, 0.5, n); };
    CHECK (peakOf (host.play (20, 512, silence, tone)) < 1.0e-4f);

    auto& audition = host.eqDisplay().detectionAudition();
    press (audition);
    CHECK (host.processor.detectionAudition());
    CHECK (audition.isLit());
    CHECK (peakOf (host.play (20, 512, silence, tone)) > 0.4f);
    release (audition);
    CHECK_FALSE (host.processor.detectionAudition());
    CHECK_FALSE (audition.isLit());

    // Space, while focused: held while the key is.
    audition.grabKeyboardFocus();
    REQUIRE (audition.hasKeyboardFocus (false));
    REQUIRE (host.hold (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.detectionAudition());
    CHECK (audition.isLit());
    host.release();
    CHECK_FALSE (host.processor.detectionAudition());

    // Not an undo step, nothing Modified, and not saved.
    press (audition);
    CHECK (session.editHistory().undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedNamesContaining (harness::savedState (saved), "audition").isEmpty());

    // The Level tab shown lets go.
    host.showTab (comp1::DisplayTab::level);
    CHECK_FALSE (host.processor.detectionAudition());
    host.showTab (comp1::DisplayTab::detectionEq);

    // A restore lets go, and the button follows.
    press (audition);
    REQUIRE (host.processor.detectionAudition());
    host::restore (host.processor, saved);
    CHECK_FALSE (host.processor.detectionAudition());
    REQUIRE (host.settleUntil ([&] { return ! audition.isLit(); }));

    // Closing the editor lets go.
    press (host.eqDisplay().detectionAudition());
    REQUIRE (host.processor.detectionAudition());
    host.editor.reset();
    CHECK_FALSE (host.processor.detectionAudition());
}

TEST_CASE ("Expanded View snapshots on the Detection EQ tab: signal playing with every band on, External with a Sidechain, and Detection Audition held", "[.screens]")
{
    const auto playFor = [] (DetectionEqHost& host, auto main, auto sidechain) {
        host.playForASecond (main, sidechain);
        host.settleAnimations();
    };
    // A second of the tests' seeded noise, the same on every platform.
    const auto seeded = comp1::test::noise (1, static_cast<int> (testRate)).front();
    const auto noise = [&seeded] (int n) { return static_cast<double> (seeded[static_cast<std::size_t> (n) % seeded.size()]); };
    const auto silence = [] (int) { return 0.0; };
    {
        DetectionEqHost host;
        everyBandOn (host);
        playFor (host, noise, silence);
        harness::writeSnapshot (*host.editor, "expanded-detection-eq-playing", {}, 1.0f);
    }
    {
        DetectionEqHost host;
        host.edit (parameters::detectionSourceId, 1.0f);
        host.edit (parameters::detectionHighPassOnId, 1.0f);
        host.edit (parameters::detectionHighPassFrequencyId, 60.0f);
        playFor (host, noise, [] (int n) { return sine (60.0, 0.5, n) + sine (180.0, 0.1, n); });
        harness::writeSnapshot (*host.editor, "expanded-detection-eq-external", {}, 1.0f);
    }
    {
        DetectionEqHost host;
        everyBandOn (host);
        press (host.eqDisplay().detectionAudition());
        REQUIRE (host.processor.detectionAudition());
        playFor (host, noise, silence);
        harness::writeSnapshot (*host.editor, "expanded-detection-eq-audition", {}, 1.0f);
    }
}

TEST_CASE ("With no audio the Detection EQ tab asks for no repaint; samples arriving, or a setting changing, repaint it")
{
    DetectionEqHost host;
    const auto tone = [] (int n) { return sine (1000.0, 0.5, n); };
    const auto none = [] (int) { return 0.0; };
    host.playForASecond (tone, none);
    // The last samples read, then ticks with nothing played.
    host.settle (16);
    auto& repaints = harness::countRepaintsOf (host.eqDisplay());
    host.settle (300);
    CHECK (repaints.count == 0);

    // A setting changed by Host Automation repaints it, on the tick.
    host.edit (parameters::detectionHighPassOnId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return repaints.count > 0; }));
    host.settle (16);
    repaints.count = 0;
    host.settle (300);
    CHECK (repaints.count == 0);

    // Playing again repaints it.
    host.playForASecond (tone, none);
    CHECK (repaints.count > 0);
    host.eqDisplay().setCachedComponentImage (nullptr);
}
