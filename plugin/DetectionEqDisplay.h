#pragma once

#include "Parameters.h"
#include "PluginProcessor.h"
#include "plugkit/ui/AnalyzerSettings.h"
#include "plugkit/ui/AnalyzerSpectrum.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/display/DisplayGeometry.h"
#include "plugkit/ui/display/EdgeFadeLayer.h"
#include "plugkit/ui/display/GridLayer.h"
#include "plugkit/ui/staple/controls/IconButton.h"

#include <plugkit/ShapeDesign.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace comp1
{

// The Detection EQ's three bands, in Tab's order: left to right as they usually sit.
enum class DetectionEqBand
{
    highPass,
    freeBand,
    lowPass,
};

// The Expanded View's Detection EQ tab: the Detection EQ drawn over a spectrum of what the detector listens to, with a
// handle for each band and a held Detection Audition button. It paints no background of its own, and its edges fade into
// the window's. A screen reader finds it as a group titled "Detection EQ".
//
// - **Spectrum:** the Engine's detection source tap (PluginProcessor::readDetectionSourceSamples), the Detection Source
//   before the Detection EQ, as one line in the Detection EQ's colour (the Analyzer's Sidechain line). The tap is
//   skipped as the display opens, and drained every tick even while the Level tab is shown, so the reader never falls
//   behind; it is analysed and repainted only while this tab is shown.
// - **Curve:** the Detection EQ's combined magnitude response, worked out from the Settings the Engine gets
//   (parameters::SettingsReader) with plugkit's designBand and responseAt at the sample rate the Engine plays at, so the
//   two agree; an Off band contributes nothing. Redrawn only when the settings or the size change.
// - **Handles:** the High-pass and Low-pass at their Frequency on the curve, the Free band at its Frequency and Gain (a
//   shelf on the curve at its Frequency), each held inside the Display Range. A drag moves Frequency, and the Free
//   band's Gain vertically, Shift finely; the wheel steps the Free band's Q or a cut's Slope; arrows step Frequency by a
//   semitone and Gain by 0.5 dB or Slope by a step (finely with Shift). A drag, a wheel step, or a key press with its
//   repeats is one undo step, inside gestures the host sees, and grabbing or moving an Off band turns it on in it.
//   While dragged, a readout card shows the band's name and values.
// - **Detection Audition:** held by mouse, or by Space through the editor's keys, it holds
//   PluginProcessor::setDetectionAudition; it lets go on release, when this tab is hidden and when the editor closes.
//
// Everything follows the parameters on the editor's tick, so Host Automation, undo, A/B Compare and Presets move it.
class DetectionEqDisplay final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    explicit DetectionEqDisplay (PluginProcessor& processor);
    ~DetectionEqDisplay() override;

    // A band's handle, a focus element of its own, titled "High-pass handle", "Free band handle" or "Low-pass handle".
    juce::Component& handle (DetectionEqBand band);
    // Where a band's handle is drawn, from the display's top left.
    juce::Point<float> handleCentre (DetectionEqBand band) const;
    // The curve's level at frequency, in dB, as drawn.
    double curveDbAt (double frequency) const;
    // The spectrum shown.
    const plugkit::ui::AnalyzerSpectrum& spectrum() const { return detectionSpectrum; }
    staple::IconButton& detectionAudition() { return audition; }
    // Lets go of Detection Audition if this display holds it.
    void letGoOfDetectionAudition();

    // Where things are drawn: 10 Hz to 30 kHz across, +/- displayRangeDb up and down.
    plugkit::ui::display::DisplayGeometry geometry() const;
    static constexpr int displayRangeDb = 30;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

private:
    class Handle;

    void tick (double seconds) override;
    bool isActive() const override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // The settings the Engine gets, read again; true when the Detection EQ changed.
    bool readSettings();
    // Reads them, and shows what changed at once: what an edit from a handle does.
    void refresh();
    // The curve drawn again and the handles laid out for the settings read, or the size.
    void settingsChanged();
    // The tap's samples, into the spectrum.
    void readTap();
    void layOutHandles();
    double responseDbAt (double frequency) const;
    const plugkit::ui::display::EdgeFadeOverlay& edgeFadeAt (float scale);
    void auditionStateChanged();

    PluginProcessor& processor;
    parameters::SettingsReader reader;
    DetectionEq eq;      // as last read
    double sampleRate;   // the curve's and the spectrum's
    std::vector<float> tapBuffer;
    plugkit::ui::AnalyzerSpectrum detectionSpectrum;
    plugkit::ui::AnalyzerSettings analyzer;
    bool samplesArriving = false;
    std::vector<plugkit::Cascade> onCascades; // the bands that are on, designed as the Engine designs them
    juce::Path curve;
    plugkit::ui::display::CachedLabels gridLabelImages;
    struct EdgeFadeFor
    {
        juce::Rectangle<int> bounds, window;
        float scale = 0.0f;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal") // exactly equal: the same scale needs no new overlay
        bool operator== (const EdgeFadeFor&) const = default;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    };
    std::optional<EdgeFadeFor> edgeFadeFor;
    plugkit::ui::display::EdgeFadeOverlay edgeFade;
    std::array<std::unique_ptr<Handle>, 3> handles;
    staple::IconButton audition { "Detection Audition", staple::Icon::headphones };
    bool holdingAudition = false;
};

} // namespace comp1
