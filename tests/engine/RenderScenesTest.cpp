// The null check's render scenes (tests/render/RenderScenes.h): that they cover every host parameter off its default,
// and that rendering one is repeatable, so a deviation between two builds is the Engine's and not the tool's.

#include "../render/Compare.h"
#include "../render/RenderScenes.h"
#include "Measure.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <string>

using namespace comp1;

namespace
{

// True when some scene plays settings for which wanted holds, from the start or after its change.
bool someScenePlays (const std::function<bool (const Settings&)>& wanted)
{
    for (const auto& scene : test::renderScenes())
        if (wanted (scene.settings) || (scene.change && wanted (*scene.change)))
            return true;
    return false;
}

const Settings defaults;

// A host parameter and when a scene plays it off its default, where it is heard: a stage's own settings with the stage
// on, High Quality's with High Quality playing.
struct Covered
{
    const char* parameter;
    std::function<bool (const Settings&)> offDefault;
};

const std::vector<Covered>& hostParameters()
{
    static const std::vector<Covered> all {
        { "gate", [] (const Settings& s) { return s.gate.on; } },
        { "gate_threshold", [] (const Settings& s) { return s.gate.on && s.gate.thresholdDb != defaults.gate.thresholdDb; } },
        { "gate_range", [] (const Settings& s) { return s.gate.on && s.gate.rangeDb != defaults.gate.rangeDb; } },
        { "gate_attack", [] (const Settings& s) { return s.gate.on && s.gate.attackMs != defaults.gate.attackMs; } },
        { "gate_release", [] (const Settings& s) { return s.gate.on && s.gate.releaseMs != defaults.gate.releaseMs; } },
        { "gate_hold", [] (const Settings& s) { return s.gate.on && s.gate.holdMs != defaults.gate.holdMs; } },
        { "threshold", [] (const Settings& s) { return s.thresholdDb != defaults.thresholdDb; } },
        { "ratio", [] (const Settings& s) { return s.ratio != defaults.ratio; } },
        { "knee", [] (const Settings& s) { return s.kneeDb != defaults.kneeDb; } },
        { "attack", [] (const Settings& s) { return s.attackMs != defaults.attackMs; } },
        { "release", [] (const Settings& s) { return s.releaseMs != defaults.releaseMs; } },
        { "auto_release", [] (const Settings& s) { return s.autoRelease != defaults.autoRelease; } },
        { "hold", [] (const Settings& s) { return s.holdMs != defaults.holdMs; } },
        { "range", [] (const Settings& s) { return s.rangeDb != defaults.rangeDb; } },
        { "colour", [] (const Settings& s) { return s.character != Character::Clean && s.colour != defaults.colour; } },
        { "auto_gain", [] (const Settings& s) { return s.autoGain != defaults.autoGain; } },
        { "saturation", [] (const Settings& s) { return s.saturation != defaults.saturation; } },
        { "drive", [] (const Settings& s) { return s.saturation && s.driveDb != defaults.driveDb; } },
        { "saturation_position", [] (const Settings& s) { return s.saturation && s.saturationPosition != defaults.saturationPosition; } },
        { "dry", [] (const Settings& s) { return s.dryDb != defaults.dryDb; } },
        { "wet", [] (const Settings& s) { return s.wetDb != defaults.wetDb; } },
        { "output_gain", [] (const Settings& s) { return s.outputGainDb != defaults.outputGainDb; } },
        { "limiter", [] (const Settings& s) { return s.limiter != defaults.limiter; } },
        { "ceiling", [] (const Settings& s) { return s.limiter && s.ceilingDb != defaults.ceilingDb; } },
        { "detection_source", [] (const Settings& s) { return s.detectionSource != defaults.detectionSource; } },
        { "detection_high_pass_on", [] (const Settings& s) { return s.detectionEq.highPass.on; } },
        { "detection_high_pass_frequency", [] (const Settings& s) {
             return s.detectionEq.highPass.on && s.detectionEq.highPass.filter.frequency != defaults.detectionEq.highPass.filter.frequency;
         } },
        { "detection_high_pass_slope", [] (const Settings& s) {
             return s.detectionEq.highPass.on && s.detectionEq.highPass.filter.slope != defaults.detectionEq.highPass.filter.slope;
         } },
        { "detection_low_pass_on", [] (const Settings& s) { return s.detectionEq.lowPass.on; } },
        { "detection_low_pass_frequency", [] (const Settings& s) {
             return s.detectionEq.lowPass.on && s.detectionEq.lowPass.filter.frequency != defaults.detectionEq.lowPass.filter.frequency;
         } },
        { "detection_low_pass_slope", [] (const Settings& s) {
             return s.detectionEq.lowPass.on && s.detectionEq.lowPass.filter.slope != defaults.detectionEq.lowPass.filter.slope;
         } },
        { "detection_band_on", [] (const Settings& s) { return s.detectionEq.freeBand.on; } },
        { "detection_band_shape", [] (const Settings& s) {
             return s.detectionEq.freeBand.on && s.detectionEq.freeBand.filter.shape != defaults.detectionEq.freeBand.filter.shape;
         } },
        { "detection_band_frequency", [] (const Settings& s) {
             return s.detectionEq.freeBand.on && s.detectionEq.freeBand.filter.frequency != defaults.detectionEq.freeBand.filter.frequency;
         } },
        { "detection_band_gain", [] (const Settings& s) {
             return s.detectionEq.freeBand.on && s.detectionEq.freeBand.filter.gain != defaults.detectionEq.freeBand.filter.gain;
         } },
        { "detection_band_q", [] (const Settings& s) {
             return s.detectionEq.freeBand.on && s.detectionEq.freeBand.filter.q != defaults.detectionEq.freeBand.filter.q;
         } },
        { "stereo_placement", [] (const Settings& s) { return s.stereoPlacement != defaults.stereoPlacement; } },
        { "stereo_link", [] (const Settings& s) { return s.stereoPlacement == StereoPlacement::Stereo && s.stereoLink != defaults.stereoLink; } },
        { "processing_mode", [] (const Settings& s) { return s.processingMode != defaults.processingMode; } },
        { "lookahead", [] (const Settings& s) { return s.processingMode == ProcessingMode::HighQuality && s.lookaheadMs != defaults.lookaheadMs; } },
        { "oversampling", [] (const Settings& s) {
             return s.processingMode == ProcessingMode::HighQuality && s.oversampling != defaults.oversampling;
         } },
        { "global_bypass", [] (const Settings& s) { return s.globalBypass != defaults.globalBypass; } },
    };
    return all;
}

} // namespace

TEST_CASE ("Render scenes play every host parameter off its default, where it is heard", "[render]")
{
    for (const auto& covered : hostParameters())
    {
        CAPTURE (covered.parameter);
        CHECK (someScenePlays (covered.offDefault));
    }
}

TEST_CASE ("Render scenes play every Character, each at a Colour off its default too, and every Stereo Placement", "[render]")
{
    for (const auto& character : test::characters())
        CHECK (someScenePlays ([&character] (const Settings& s) { return s.character == character.character; }));
    for (const auto character : { Character::VCA, Character::FET, Character::Opto })
        CHECK (someScenePlays ([character] (const Settings& s) { return s.character == character && s.colour != Settings {}.colour; }));
    for (const auto placement : test::stereoPlacements())
        CHECK (someScenePlays ([placement] (const Settings& s) { return s.stereoPlacement == placement; }));
    // Colour tracking Threshold: Colour on at a low Threshold, where it would be inaudible were it not.
    CHECK (someScenePlays ([] (const Settings& s) { return (s.character == Character::VCA || s.character == Character::FET) && s.colour > 0.0 && s.thresholdDb <= -40.0; }));
}

TEST_CASE ("Render scenes play the Sidechain, Threshold gliding, Auto Gain and Global Bypass switched on, and the worst case", "[render]")
{
    const auto scenes = test::renderScenes();
    const auto some = [&scenes] (const std::function<bool (const test::RenderScene&)>& wanted) {
        return std::any_of (scenes.begin(), scenes.end(), wanted);
    };
    CHECK (some ([] (const test::RenderScene& s) { return s.sidechain && s.settings.detectionSource == DetectionSource::External; }));
    CHECK (some ([] (const test::RenderScene& s) {
        return s.change && s.settings.thresholdDb == -18.0 && s.change->thresholdDb == -40.0;
    }));
    CHECK (some ([] (const test::RenderScene& s) { return s.change && s.change->autoGain && ! s.settings.autoGain; }));
    CHECK (some ([] (const test::RenderScene& s) { return s.change && s.change->globalBypass && ! s.settings.globalBypass; }));
    CHECK (some ([] (const test::RenderScene& s) { return ! s.change && s.settings == test::worstCaseSettings(); }));
}

TEST_CASE ("Render scene names are distinct file names, and the block size isn't a multiple of 16", "[render]")
{
    std::set<std::string> names;
    for (const auto& scene : test::renderScenes())
    {
        CHECK (names.insert (scene.name).second);
        CHECK (scene.name.find_first_not_of ("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos);
    }
    CHECK (test::renderBlockSize % 16 != 0);
    CHECK (test::renderSampleRates == std::array { 44100.0, 96000.0, 192000.0 });
}

TEST_CASE ("A render scene renders the same samples twice, and not silence", "[render]")
{
    for (const auto& scene : test::renderScenes())
    {
        CAPTURE (scene.name);
        const auto first = test::render (scene, 44100.0);
        REQUIRE (first.size() == static_cast<std::size_t> (test::renderSeconds * 44100.0) * test::renderChannels);
        CHECK (*std::max_element (first.begin(), first.end()) > 0.1f);
        CHECK (test::render (scene, 44100.0) == first);
    }
}

TEST_CASE ("The worst case's Limiter limits and its Gate closes, on the bursts", "[render]")
{
    for (const double sampleRate : { 48000.0, 96000.0 })
    {
        CAPTURE (sampleRate);
        const auto records = test::levelHistory (test::worstCaseSettings(), sampleRate, test::bursts (2, static_cast<int> (sampleRate), sampleRate));
        const auto most = [&records] (double LevelRecord::* field) {
            double m = 0.0;
            for (const auto& record : records)
                m = std::max (m, record.*field);
            return m;
        };
        CHECK (most (&LevelRecord::limiterGainReductionDb) > 6.0);
        CHECK (most (&LevelRecord::gainReductionDb) > 6.0);
        CHECK (most (&LevelRecord::gateGainReductionDb) > 1.0);
    }
}

TEST_CASE ("Comparing renders: exact, the worst deviation in dBFS, or lengths that differ", "[render]")
{
    std::vector<float> a (1000, 0.5f), b = a;
    CHECK (test::compare (a, b) == test::Comparison { .exact = true });

    b[500] = 0.5f + 1.0e-3f; // 1e-3 is -60 dBFS
    const auto deviation = test::compare (a, b);
    CHECK_FALSE (deviation.exact);
    CHECK (deviation.sameLength);
    CHECK (deviation.worstDb > -60.5);
    CHECK (deviation.worstDb < -59.5);
    CHECK_FALSE (deviation.passes (-120.0));
    CHECK (test::compare (b, a).worstDb == deviation.worstDb);

    b[500] = 0.5f + 1.0e-7f; // about -140 dBFS, within the bar
    CHECK (test::compare (a, b).passes (-120.0));

    b.push_back (0.0f);
    CHECK_FALSE (test::compare (a, b).sameLength);
    CHECK_FALSE (test::compare (a, b).passes (-120.0));
}
