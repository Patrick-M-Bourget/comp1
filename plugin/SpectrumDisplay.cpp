#include "SpectrumDisplay.h"

#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/display/AnalyzerLayer.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace display = plugkit::ui::display;

// How strongly the Gain Reduction is shaded: faint enough that the grid and the lines show through it.
constexpr float gainReductionAlpha = 0.35f;
// The Analyzer's dB scale's step, as its labels take it at its default range.
constexpr int scaleStepDb = 10;

} // namespace

SpectrumDisplay::SpectrumDisplay (PluginProcessor& p)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      sampleRate (plugkit::ui::sampleRateOrAssumed (p))
{
    setName ("Spectrum");
    setTitle ("Spectrum");
    setInterceptsMouseClicks (false, false);
    analyzer.showPreEq = true;
    analyzer.showPostEq = true;
    analyzer.showSidechain = false;
    analyzer.peakHold = false;
    input.prepare (sampleRate, analyzer.resolution);
    output.prepare (sampleRate, analyzer.resolution);
    // What piled up while no editor read the taps.
    processor.skipInputTapSamples();
    processor.skipOutputTapSamples();
}

display::DisplayGeometry SpectrumDisplay::geometry() const { return { .width = getWidth(), .height = getHeight() }; }

float SpectrumDisplay::yOf (const plugkit::ui::AnalyzerSpectrum& spectrum, float x) const
{
    return display::spectrumYAt (geometry(), analyzer, spectrum, x);
}

juce::Path SpectrumDisplay::gainReductionArea() const
{
    // Along the input's line, then back along the lower of the two: where the output is over the input, the two
    // edges meet and nothing is shaded.
    const auto width = static_cast<float> (getWidth());
    std::vector<juce::Point<float>> lower;
    juce::Path area;
    for (float x = 0.0f; x <= width; x += display::DisplayGeometry::pixelStep)
    {
        const float inputY = yOf (input, x);
        const juce::Point<float> point { x, inputY };
        if (juce::exactlyEqual (x, 0.0f))
            area.startNewSubPath (point);
        else
            area.lineTo (point);
        lower.push_back ({ x, std::max (inputY, yOf (output, x)) });
    }
    for (auto it = lower.rbegin(); it != lower.rend(); ++it)
        area.lineTo (*it);
    area.closeSubPath();
    return area;
}

void SpectrumDisplay::readTaps()
{
    tapBuffer.resize (static_cast<std::size_t> (std::max ({ 1, processor.inputTapSampleCapacity(), processor.outputTapSampleCapacity() })));
    const int size = static_cast<int> (tapBuffer.size());
    const int inputRead = processor.readInputTapSamples (tapBuffer.data(), size);
    if (inputRead > 0)
        input.push (tapBuffer.data(), inputRead);
    const int outputRead = processor.readOutputTapSamples (tapBuffer.data(), size);
    if (outputRead > 0)
        output.push (tapBuffer.data(), outputRead);
    samplesArriving = inputRead > 0 || outputRead > 0;
}

void SpectrumDisplay::tick (double seconds)
{
    if (const double rate = plugkit::ui::sampleRateOrAssumed (processor); ! juce::exactlyEqual (rate, sampleRate))
    {
        sampleRate = rate;
        input.prepare (sampleRate, analyzer.resolution);
        output.prepare (sampleRate, analyzer.resolution);
    }
    // Drained whether it shows or not, so the taps never fill.
    readTaps();
    // Analysed and repainted only while shown, and only as samples arrive: with no audio it holds still.
    if (isShowing() && samplesArriving)
    {
        input.update (seconds, analyzer.speed);
        output.update (seconds, analyzer.speed);
        repaint();
    }
}

bool SpectrumDisplay::isActive() const { return isShowing() && samplesArriving; }

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto shape = geometry();
    const auto height = static_cast<float> (shape.height);
    // The Frequency lines, and a line at each level of the Analyzer's dB scale.
    auto lines = display::gridLines (shape);
    lines.gainY.clear();
    for (int level = scaleStepDb; level < analyzer.rangeDb; level += scaleStepDb)
        lines.gainY.push_back (static_cast<float> (level) / static_cast<float> (analyzer.rangeDb) * height);
    lines.zeroY = 0.0f;
    display::paintGrid (g, lines, { shape.width, shape.height });

    g.setGradientFill (juce::ColourGradient::vertical (colour::dynRange.withAlpha (gainReductionAlpha), 0.0f,
                                                       colour::dynRangeInner.withAlpha (gainReductionAlpha), height));
    g.fillPath (gainReductionArea());
    display::paintAnalyzer (g, shape, { .settings = analyzer, .preEq = input, .postEq = output, .sidechain = output });
    fades.paint (g, *this);

    // The labels go over the fades, unfaded: the Frequency labels along the bottom (gridLabels' first row, without its
    // Display Range labels, which are no scale here) and the Analyzer's dB scale.
    auto frequencyLabels = display::gridLabels (shape);
    const float bottomRow = frequencyLabels.empty() ? 0.0f : frequencyLabels.front().area.getY();
    frequencyLabels.erase (std::remove_if (frequencyLabels.begin(), frequencyLabels.end(),
                                           [bottomRow] (const display::Label& l) { return ! juce::exactlyEqual (l.area.getY(), bottomRow); }),
                           frequencyLabels.end());
    const auto scale = display::analyzerScaleLabels (shape, analyzer);
    frequencyLabels.insert (frequencyLabels.end(), scale.begin(), scale.end());
    labels.paint (g, DisplayFades::originInWindow (*this), frequencyLabels);
}

std::unique_ptr<juce::AccessibilityHandler> SpectrumDisplay::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::group, [] { return juce::String(); });
}

} // namespace comp1
