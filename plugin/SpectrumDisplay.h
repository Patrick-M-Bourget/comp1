#pragma once

#include "DisplayFades.h"
#include "GlobalBypassFade.h"
#include "PluginProcessor.h"
#include "plugkit/ui/AnalyzerSettings.h"
#include "plugkit/ui/AnalyzerSpectrum.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/display/DisplayGeometry.h"
#include "plugkit/ui/display/GridLayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace comp1
{

// The Spectrum display, the Expanded View's Spectrum tab: the spectrum of the input and of the compressor's output,
// overlaid, with the gap between them shaded as Gain Reduction. comp1 is single-band, so its Gain Reduction is the same
// at every frequency, and the gap shows it across the spectrum.
//
// - **Spectra:** the Engine's input tap (PluginProcessor::read with Tap::input), the main input before any stage, as
//   the quieter line (the Analyzer's pre line); and its output tap (Tap::output), the Wet path directly
//   after the compressor, as the main line with its fill (the Analyzer's post line). Any later gain (Auto Gain, Output
//   Gain) comes after the output tap, so it never closes the gap. Both at the Analyzer's default speed, resolution and
//   tilt, with no Peak Hold, on the Analyzer's dB scale: the top of the display 0 dB, the bottom its range below.
// - **Gain Reduction:** the area between the two lines where the output is under the input, shaded in the Gain
//   Reduction meter's colours (dynRange to dynRangeInner), faint enough that the grid shows through.
// - **Global Bypass:** it passes the input through unprocessed, but the Engine's taps are as without it, so the gap
//   still shows what the compressor would do; the output's line and fill and the Gain Reduction shading dim to a share
//   of their alphas, gliding over the design system's fade, so it reads as not applied. The input's line stays.
// - **Grid:** plugkit-ui's Frequency lines and labels, and the Analyzer's dB scale with a line at each of its labels'
//   levels; the edges fade into the window's background over the design system's display fades.
//
// The taps are skipped as the display opens, and drained every tick even while it is hidden, so the reader never falls
// behind; they are analysed and repainted only while the display shows and samples arrive, so with no audio it holds
// still and asks for no repaint, except while the Global Bypass fade moves. A screen reader finds it as a group titled
// "Spectrum", with no Tab stop; it takes no mouse input. Named "Spectrum", as its tab.
class SpectrumDisplay final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    explicit SpectrumDisplay (PluginProcessor& processor);

    const plugkit::ui::AnalyzerSpectrum& inputSpectrum() const { return input; }
    const plugkit::ui::AnalyzerSpectrum& outputSpectrum() const { return output; }

    // Where things are drawn across: 10 Hz to 30 kHz, as the other Frequency displays.
    plugkit::ui::display::DisplayGeometry geometry() const;
    // Where spectrum is drawn at x, from the display's top.
    float yOf (const plugkit::ui::AnalyzerSpectrum& spectrum, float x) const;
    // The shaded Gain Reduction: between the input's line and the output's, where the output is under the input.
    juce::Path gainReductionArea() const;
    // How far into Global Bypass the display shows, 0 to 1: it glides over the design system's globalBypassFadeMs as
    // Global Bypass switches, and an editor opened under Global Bypass starts at 1.
    float globalBypassFade() const { return bypassFade.value(); }
    // The output's line and fill's alpha, and the Gain Reduction shading's, each dimmed toward a share of its own as
    // the fade goes to 1 (above).
    float outputAlpha() const;
    float gainReductionAlpha() const;

    void paint (juce::Graphics& g) override;

private:
    void tick (double seconds) override;
    bool isActive() const override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // Both taps' samples, into their spectra.
    void readTaps();

    PluginProcessor& processor;
    double sampleRate; // the spectra's
    std::vector<float> tapBuffer;
    plugkit::ui::AnalyzerSpectrum input, output;
    plugkit::ui::AnalyzerSettings analyzer;
    bool samplesArriving = false;
    GlobalBypassFade bypassFade;
    plugkit::ui::display::CachedLabels labels;
    DisplayFades fades;
};

} // namespace comp1
