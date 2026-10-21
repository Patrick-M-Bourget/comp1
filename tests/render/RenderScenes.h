#pragma once

// The null check's render scenes (docs/performance.md, "Null check"): fixed, seeded scenes that scripts/check.sh null
// renders through two builds' Engines and compares sample for sample. The list is data the render tool
// (RenderTool.cpp) and its test (tests/engine/RenderScenesTest.cpp) share: a scene added here is rendered and compared
// on both sides. Every scene is stereo, renderSeconds of the shared noise bursts (Measure.h's bursts), in
// renderBlockSize-sample blocks, at each of renderSampleRates.

#include "../engine/Measure.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace comp1::test
{

// 44.1 kHz, the lowest rate, where the filters and the Oversampling cap sit closest to Nyquist; 96 kHz, where 8x
// Oversampling is capped to 4x; and 192 kHz, the highest, capped to 2x.
inline constexpr std::array renderSampleRates = { 44100.0, 96000.0, 192000.0 };
// Not a multiple of the Engine's 16-sample runs, so blocks end part-way through a run.
inline constexpr int renderBlockSize = 97;
inline constexpr int renderChannels = 2;
inline constexpr double renderSeconds = 1.0;
// When a scene's change of settings arrives: late enough for the first settings to be heard, early enough for the
// glide or crossfade and what follows to be rendered.
inline constexpr double renderChangeSeconds = 0.25;

struct RenderScene
{
    std::string name; // lowercase letters, digits and hyphens: the output's file name
    Settings settings;
    std::optional<Settings> change; // settings from renderChangeSeconds on
    bool sidechain = false;         // fed noise bursts on the Sidechain, half a period out from the main input's
};

// The scenes, one per line where they fit, so tickets adding scenes in parallel merge cleanly.
inline std::vector<RenderScene> renderScenes()
{
    const auto with = [] (auto apply) {
        Settings settings;
        apply (settings);
        return settings;
    };
    return {
        // The compressor.
        { .name = "defaults", .settings = {} },
        { .name = "ratio-inf-knee-0", .settings = with ([] (Settings& s) { s.ratio = parameterRanges::inf; s.kneeDb = 0.0; }) },
        { .name = "knee-72", .settings = with ([] (Settings& s) { s.kneeDb = 72.0; }) },
        { .name = "range-6", .settings = with ([] (Settings& s) { s.rangeDb = 6.0; }) },
        { .name = "attack-release-hold", .settings = with ([] (Settings& s) { s.attackMs = 0.5; s.releaseMs = 50.0; s.holdMs = 20.0; }) },
        { .name = "auto-release", .settings = with ([] (Settings& s) { s.autoRelease = true; }) },
        { .name = "auto-gain", .settings = with ([] (Settings& s) { s.autoGain = true; }) },
        { .name = "output-gain-6", .settings = with ([] (Settings& s) { s.outputGainDb = 6.0; }) },
        { .name = "threshold-glide", .settings = {}, .change = with ([] (Settings& s) { s.thresholdDb = -40.0; }) },
        { .name = "auto-gain-on", .settings = {}, .change = with ([] (Settings& s) { s.autoGain = true; }) },
        // The Characters and Colour.
        { .name = "vca", .settings = with ([] (Settings& s) { s.character = Character::VCA; }) },
        { .name = "fet", .settings = with ([] (Settings& s) { s.character = Character::FET; }) },
        { .name = "vca-colour-40", .settings = with ([] (Settings& s) { s.character = Character::VCA; s.colour = 40.0; }) },
        { .name = "fet-colour-40", .settings = with ([] (Settings& s) { s.character = Character::FET; s.colour = 40.0; }) },
        // Colour tracking Threshold: VCA at full Colour at a low Threshold, where the compressed signal is far under
        // full scale, and Threshold gliding there from the default.
        { .name = "vca-threshold-50", .settings = with ([] (Settings& s) { s.character = Character::VCA; s.thresholdDb = -50.0; }) },
        { .name = "vca-threshold-glide",
          .settings = with ([] (Settings& s) { s.character = Character::VCA; }),
          .change = with ([] (Settings& s) { s.character = Character::VCA; s.thresholdDb = -50.0; }) },
        // Saturation, and Dry/Wet.
        { .name = "saturation", .settings = with ([] (Settings& s) { s.saturation = true; }) },
        { .name = "saturation-before-drive-12", .settings = with ([] (Settings& s) {
              s.saturation = true;
              s.saturationPosition = SaturationPosition::Before;
              s.driveDb = 12.0;
          }) },
        { .name = "dry-0-wet-6", .settings = with ([] (Settings& s) { s.dryDb = 0.0; s.wetDb = -6.0; }) },
        // The Gate: its defaults, and every one of its settings moved, closing on the quiet bursts.
        { .name = "gate", .settings = with ([] (Settings& s) { s.gate.on = true; }) },
        { .name = "gate-settings", .settings = with ([] (Settings& s) {
              s.gate = { .on = true, .thresholdDb = -20.0, .rangeDb = 40.0, .attackMs = 0.2, .releaseMs = 50.0, .holdMs = 5.0 };
          }) },
        // The Limiter, driven over its Ceiling: the sample peak in Zero Latency, the true peak in High Quality.
        { .name = "limiter", .settings = with ([] (Settings& s) { s.limiter = true; s.outputGainDb = 6.0; }) },
        { .name = "limiter-ceiling-6", .settings = with ([] (Settings& s) { s.limiter = true; s.ceilingDb = -6.0; }) },
        { .name = "high-quality-limiter", .settings = with ([] (Settings& s) {
              s.processingMode = ProcessingMode::HighQuality;
              s.limiter = true;
              s.outputGainDb = 6.0;
          }) },
        // High Quality, its Lookahead, and Oversampling with the nonlinear stages it runs (FET's Colour, Saturation).
        { .name = "high-quality", .settings = with ([] (Settings& s) { s.processingMode = ProcessingMode::HighQuality; }) },
        { .name = "high-quality-lookahead-5", .settings = with ([] (Settings& s) {
              s.processingMode = ProcessingMode::HighQuality;
              s.lookaheadMs = 5.0;
          }) },
        { .name = "high-quality-x1-saturation", .settings = with ([] (Settings& s) {
              s.processingMode = ProcessingMode::HighQuality;
              s.oversampling = Oversampling::x1;
              s.character = Character::FET;
              s.saturation = true;
          }) },
        { .name = "high-quality-x8-saturation", .settings = with ([] (Settings& s) {
              s.processingMode = ProcessingMode::HighQuality;
              s.oversampling = Oversampling::x8;
              s.character = Character::FET;
              s.saturation = true;
          }) },
        // Stereo Placement and Stereo Link.
        { .name = "stereo-link-50", .settings = with ([] (Settings& s) { s.stereoLink = 50.0; }) },
        { .name = "mid", .settings = with ([] (Settings& s) { s.stereoPlacement = StereoPlacement::Mid; }) },
        { .name = "side", .settings = with ([] (Settings& s) { s.stereoPlacement = StereoPlacement::Side; }) },
        // The Detection Source and the Detection EQ.
        { .name = "external-sidechain", .settings = with ([] (Settings& s) { s.detectionSource = DetectionSource::External; }), .sidechain = true },
        { .name = "detection-high-pass", .settings = with ([] (Settings& s) {
              s.detectionEq.highPass.on = true;
              s.detectionEq.highPass.filter.frequency = 200.0;
              s.detectionEq.highPass.filter.slope = 24.0;
          }) },
        { .name = "detection-low-pass", .settings = with ([] (Settings& s) {
              s.detectionEq.lowPass.on = true;
              s.detectionEq.lowPass.filter.frequency = 4000.0;
              s.detectionEq.lowPass.filter.slope = 24.0;
          }) },
        { .name = "detection-band", .settings = with ([] (Settings& s) {
              s.detectionEq.freeBand.on = true;
              s.detectionEq.freeBand.filter.shape = shapeOf (FreeBandShape::HighShelf);
              s.detectionEq.freeBand.filter.frequency = 3000.0;
              s.detectionEq.freeBand.filter.gain = 12.0;
              s.detectionEq.freeBand.filter.q = 2.0;
          }) },
        // Global Bypass switched on, crossfading to the input.
        { .name = "global-bypass-on", .settings = {}, .change = with ([] (Settings& s) { s.globalBypass = true; }) },
        // The CPU budget's worst case.
        { .name = "worst-case", .settings = worstCaseSettings() },
    };
}

// Renders a scene at a sample rate: renderSeconds of the bursts through a fresh Engine, as interleaved stereo.
inline std::vector<float> render (const RenderScene& scene, double sampleRate)
{
    const auto length = static_cast<int> (renderSeconds * sampleRate);
    const auto sidechain = bursts (renderChannels, length, sampleRate, 2, burstPeriodSeconds / 2.0);
    const auto change = scene.change ? std::optional { SettingsChange { static_cast<int> (renderChangeSeconds * sampleRate), *scene.change } }
                                     : std::nullopt;
    const auto output = played (scene.settings, sampleRate, bursts (renderChannels, length, sampleRate), { renderBlockSize },
                                scene.sidechain ? &sidechain : nullptr, change);
    std::vector<float> interleaved;
    interleaved.reserve (static_cast<std::size_t> (length * renderChannels));
    for (std::size_t i = 0; i < static_cast<std::size_t> (length); ++i)
        for (const auto& channel : output)
            interleaved.push_back (channel[i]);
    return interleaved;
}

} // namespace comp1::test
