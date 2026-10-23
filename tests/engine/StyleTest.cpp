// The Style table (comp1/Style.h): each Style's Character and Comp Curve, and the defaults choosing it writes.

#include "Measure.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using namespace comp1;

namespace
{

// One row of dyn-lab's Style table, as `python3 tools/dyn-lab/dynlab.py styles` prints it: the Style, its Character,
// its default Comp, the compressor's Threshold, Ratio, Knee, Attack, Release, Auto Release, Hold, Range and Colour, the Gate's on/off,
// Gate Threshold, Gate Range, Gate Attack, Gate Release and Gate Hold, and the Limiter's on/off and Ceiling.
struct LabRow
{
    Style style;
    Character character;
    double comp;
    double threshold, ratio, knee, attack, release;
    bool autoRelease;
    double hold, range, colour;
    bool gate;
    double gateThreshold, gateRange, gateAttack, gateRelease, gateHold;
    bool limiter;
    double ceiling;
};

// The lab's output, copied row for row.
const std::vector<LabRow> labRows {
    { Style::Clean, Character::Clean, 50, -18, 4, 6, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::VCA, Character::VCA, 50, -18, 4, 3, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::FET, Character::FET, 50, -18, 4, 3, 0.5, 50, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::Opto, Character::Opto, 50, -18, 3, 12, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::Vocal, Character::Clean, 50, -18, 3, 6, 10, 100, true, 0, 60, 100, true, -40, 20, 1, 100, 20, true, 0 },
    { Style::Instrument, Character::Opto, 50, -18, 4, 6, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::Bus, Character::VCA, 50, -18, 2, 6, 30, 100, true, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::Drums, Character::FET, 50, -18, 4, 3, 5, 50, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
};

// A Style's Comp Curve as `python3 tools/dyn-lab/dynlab.py compcurve --style <name>` prints it, at Comp 0, 10, ...
// 100%: the Threshold and the Ratio, each to two decimals.
struct LabCurve
{
    Style style;
    std::vector<std::pair<double, double>> points;
};

const std::vector<LabCurve> labCurves {
    { Style::Clean,
      { { 0.00, 1.00 }, { -3.60, 1.18 }, { -7.20, 1.43 }, { -10.80, 1.82 }, { -14.40, 2.50 }, { -18.00, 4.00 },
        { -21.60, 4.44 }, { -25.20, 5.00 }, { -28.80, 5.71 }, { -32.40, 6.67 }, { -36.00, 8.00 } } },
    { Style::VCA,
      { { 0.00, 1.00 }, { -3.60, 1.18 }, { -7.20, 1.43 }, { -10.80, 1.82 }, { -14.40, 2.50 }, { -18.00, 4.00 },
        { -21.60, 4.55 }, { -25.20, 5.26 }, { -28.80, 6.25 }, { -32.40, 7.69 }, { -36.00, 10.00 } } },
    { Style::FET,
      { { 0.00, 1.00 }, { -3.60, 1.18 }, { -7.20, 1.43 }, { -10.80, 1.82 }, { -14.40, 2.50 }, { -18.00, 4.00 },
        { -20.00, 4.76 }, { -22.00, 5.88 }, { -24.00, 7.69 }, { -26.00, 11.11 }, { -28.00, 20.00 } } },
    { Style::Opto,
      { { 0.00, 1.00 }, { -3.60, 1.15 }, { -7.20, 1.36 }, { -10.80, 1.67 }, { -14.40, 2.14 }, { -18.00, 3.00 },
        { -22.80, 3.33 }, { -27.60, 3.75 }, { -32.40, 4.29 }, { -37.20, 5.00 }, { -42.00, 6.00 } } },
    { Style::Vocal,
      { { 0.00, 1.00 }, { -3.60, 1.15 }, { -7.20, 1.36 }, { -10.80, 1.67 }, { -14.40, 2.14 }, { -18.00, 3.00 },
        { -21.60, 3.33 }, { -25.20, 3.75 }, { -28.80, 4.29 }, { -32.40, 5.00 }, { -36.00, 6.00 } } },
    { Style::Instrument,
      { { 0.00, 1.00 }, { -3.60, 1.18 }, { -7.20, 1.43 }, { -10.80, 1.82 }, { -14.40, 2.50 }, { -18.00, 4.00 },
        { -21.60, 4.44 }, { -25.20, 5.00 }, { -28.80, 5.71 }, { -32.40, 6.67 }, { -36.00, 8.00 } } },
    { Style::Bus,
      { { 0.00, 1.00 }, { -3.60, 1.11 }, { -7.20, 1.25 }, { -10.80, 1.43 }, { -14.40, 1.67 }, { -18.00, 2.00 },
        { -20.40, 2.22 }, { -22.80, 2.50 }, { -25.20, 2.86 }, { -27.60, 3.33 }, { -30.00, 4.00 } } },
    { Style::Drums,
      { { 0.00, 1.00 }, { -3.60, 1.18 }, { -7.20, 1.43 }, { -10.80, 1.82 }, { -14.40, 2.50 }, { -18.00, 4.00 },
        { -20.40, 4.55 }, { -22.80, 5.26 }, { -25.20, 6.25 }, { -27.60, 7.69 }, { -30.00, 10.00 } } },
};

const std::vector<Style> sourceStyles { Style::Vocal, Style::Instrument, Style::Bus, Style::Drums };

// Every setting moved off its default, the ones a Style writes and the ones it never touches.
Settings everythingMoved()
{
    Settings s;
    s.gate = { .on = true, .thresholdDb = -25.0, .rangeDb = 30.0, .attackMs = 5.0, .releaseMs = 250.0, .holdMs = 50.0 };
    s.gatePosition = GatePosition::Post;
    s.character = Character::Opto;
    s.comp = 80.0;
    s.compFollow = false;
    s.compCurve = CompCurve { { CompPoint { 0.0, -6.0, 1.0 }, CompPoint { 30.0, -20.0, 2.0 }, CompPoint { 100.0, -50.0, 30.0 } } };
    s.thresholdDb = -30.0;
    s.ratio = 8.0;
    s.kneeDb = 20.0;
    s.attackMs = 40.0;
    s.releaseMs = 700.0;
    s.autoRelease = true;
    s.holdMs = 25.0;
    s.rangeDb = 12.0;
    s.colour = 40.0;
    s.autoGain = true;
    s.saturation = true;
    s.driveDb = 12.0;
    s.saturationPosition = SaturationPosition::Before;
    s.dryDb = -6.0;
    s.wetDb = -3.0;
    s.outputGainDb = 4.0;
    s.limiter = true;
    s.ceilingDb = -3.0;
    s.detectionSource = DetectionSource::External;
    s.detectionEq.highPass.on = true;
    s.detectionEq.highPass.filter.frequency = 120.0;
    s.detectionEq.lowPass.on = true;
    s.detectionEq.freeBand.on = true;
    s.detectionEq.freeBand.filter.gain = 6.0;
    s.stereoPlacement = StereoPlacement::Mid;
    s.stereoLink = 30.0;
    s.processingMode = ProcessingMode::HighQuality;
    s.lookaheadMs = 2.0;
    s.oversampling = Oversampling::x4;
    s.globalBypass = true;
    return s;
}

} // namespace

TEST_CASE ("The Styles are Clean, VCA, FET, Opto, Vocal, Instrument, Bus and Drums, in that frozen order")
{
    CHECK (styleCount == 8);
    CHECK (static_cast<int> (Style::Clean) == 0);
    CHECK (static_cast<int> (Style::VCA) == 1);
    CHECK (static_cast<int> (Style::FET) == 2);
    CHECK (static_cast<int> (Style::Opto) == 3);
    CHECK (static_cast<int> (Style::Vocal) == 4);
    CHECK (static_cast<int> (Style::Instrument) == 5);
    CHECK (static_cast<int> (Style::Bus) == 6);
    CHECK (static_cast<int> (Style::Drums) == 7);
}

TEST_CASE ("The Style table is dyn-lab's, row for row")
{
    REQUIRE (labRows.size() == styleCount);
    for (const auto& lab : labRows)
    {
        CAPTURE (static_cast<int> (lab.style));
        CHECK (characterOf (lab.style) == lab.character);
        REQUIRE (hasDefaults (lab.style));
        const auto s = styled (lab.style);
        CHECK (s.character == lab.character);
        CHECK (s.comp == lab.comp);
        CHECK (s.compFollow);
        CHECK (s.compCurve == compCurveOf (lab.style));
        CHECK (s.thresholdDb == lab.threshold);
        CHECK (s.ratio == lab.ratio);
        CHECK (s.kneeDb == lab.knee);
        CHECK (s.attackMs == lab.attack);
        CHECK (s.releaseMs == lab.release);
        CHECK (s.autoRelease == lab.autoRelease);
        CHECK (s.holdMs == lab.hold);
        CHECK (s.rangeDb == lab.range);
        CHECK (s.colour == lab.colour);
        CHECK (s.gate.on == lab.gate);
        CHECK (s.gate.thresholdDb == lab.gateThreshold);
        CHECK (s.gate.rangeDb == lab.gateRange);
        CHECK (s.gate.attackMs == lab.gateAttack);
        CHECK (s.gate.releaseMs == lab.gateRelease);
        CHECK (s.gate.holdMs == lab.gateHold);
        CHECK (s.limiter == lab.limiter);
        CHECK (s.ceilingDb == lab.ceiling);
    }
}

TEST_CASE ("Each Style's Comp Curve is dyn-lab's, point for point")
{
    REQUIRE (labCurves.size() == styleCount);
    for (const auto& lab : labCurves)
    {
        CAPTURE (static_cast<int> (lab.style));
        const auto& compCurve = compCurveOf (lab.style);
        for (std::size_t i = 0; i < lab.points.size(); ++i)
        {
            const double comp = 10.0 * static_cast<double> (i);
            CAPTURE (comp);
            CHECK (std::abs (compThresholdDb (compCurve, comp) - lab.points[i].first) <= 0.005);
            CHECK (std::abs (compRatio (compCurve, comp) - lab.points[i].second) <= 0.005);
        }
    }
}

TEST_CASE ("On each Style's Comp Curve, 0% compresses nothing, Threshold falls and Ratio rises, and the default Comp is the default Threshold and Ratio exactly")
{
    const auto style = GENERATE (from_range (test::everyStyle()));
    CAPTURE (static_cast<int> (style));
    const auto& compCurve = compCurveOf (style);
    CHECK (compRatio (compCurve, 0.0) == 1.0);
    double lastThreshold = std::numeric_limits<double>::infinity(), lastRatio = 0.0;
    for (int quarter = 0; quarter <= 400; ++quarter)
    {
        const double comp = quarter / 4.0;
        CAPTURE (comp);
        const double threshold = compThresholdDb (compCurve, comp), ratio = compRatio (compCurve, comp);
        CHECK (threshold < lastThreshold);
        CHECK (ratio > lastRatio);
        CHECK (threshold >= parameterRanges::threshold.low);
        CHECK (threshold <= parameterRanges::threshold.high);
        CHECK (ratio >= parameterRanges::ratio.low);
        lastThreshold = threshold;
        lastRatio = ratio;
    }
    const auto& defaults = *describe (style).defaults;
    CHECK (compThresholdDb (compCurve, defaults.comp) == defaults.thresholdDb);
    CHECK (compRatio (compCurve, defaults.comp) == defaults.ratio);
}

TEST_CASE ("The Comp nearest a Threshold is the one that sets it, clamped to the Comp Curve's span")
{
    const auto style = GENERATE (from_range (test::everyStyle()));
    CAPTURE (static_cast<int> (style));
    const auto& compCurve = compCurveOf (style);
    for (int comp = 0; comp <= 100; comp += 5)
        CHECK (std::abs (compNearestThreshold (compCurve, compThresholdDb (compCurve, comp)) - comp) <= 1.0e-9);
    CHECK (compNearestThreshold (compCurve, compCurve.points.front().thresholdDb + 6.0) == 0.0);
    CHECK (compNearestThreshold (compCurve, compCurve.points.back().thresholdDb - 6.0) == 100.0);
    // Clean's runs straight from 0 dB at 0% to -18 dB at 50%: -9 dB is a quarter of the way.
    CHECK (std::abs (compNearestThreshold (compCurveOf (Style::Clean), -9.0) - 25.0) <= 1.0e-9);
}

TEST_CASE ("Clean's row is a default Settings in every field it writes, but for Comp Follow and Auto Gain, which choosing any Style turns on")
{
    // With the next test, which applies it over every setting moved, every field it writes is the default's. A
    // default Settings holds Comp Follow and Auto Gain off, so an instance plays its stored settings.
    auto clean = styled (Style::Clean);
    CHECK (clean.compFollow);
    CHECK (clean.autoGain);
    CHECK_FALSE (Settings {}.compFollow);
    CHECK_FALSE (Settings {}.autoGain);
    clean.compFollow = false;
    clean.autoGain = false;
    CHECK (clean == Settings {});
}

TEST_CASE ("Choosing any Style turns Auto Gain on, from on or off")
{
    const auto style = GENERATE (from_range (test::everyStyle()));
    const bool autoGainBefore = GENERATE (false, true);
    CAPTURE (static_cast<int> (style), autoGainBefore);
    Settings settings;
    settings.autoGain = autoGainBefore;
    CHECK (applyStyle (style, settings));
    CHECK (settings.autoGain);
}

TEST_CASE ("Every Style writes only the settings ADR 0002 lists, and its Character")
{
    const auto style = GENERATE (from_range (test::everyStyle()));
    CAPTURE (static_cast<int> (style));
    const auto moved = everythingMoved();
    auto expected = styled (style);
    // Everything a Style never touches keeps its value: the routing, the levels and the whole plugin's.
    expected.gatePosition = moved.gatePosition;
    expected.saturation = moved.saturation;
    expected.driveDb = moved.driveDb;
    expected.saturationPosition = moved.saturationPosition;
    expected.dryDb = moved.dryDb;
    expected.wetDb = moved.wetDb;
    expected.outputGainDb = moved.outputGainDb;
    expected.detectionSource = moved.detectionSource;
    expected.detectionEq = moved.detectionEq;
    expected.stereoPlacement = moved.stereoPlacement;
    expected.stereoLink = moved.stereoLink;
    expected.processingMode = moved.processingMode;
    expected.lookaheadMs = moved.lookaheadMs;
    expected.oversampling = moved.oversampling;
    expected.globalBypass = moved.globalBypass;
    auto settings = moved;
    CHECK (applyStyle (style, settings));
    CHECK (settings == expected);
}

TEST_CASE ("Vocal is the Gate, then Clean with Auto Release, then the Limiter; the other Source Styles leave the Gate and the Limiter off")
{
    const auto vocal = styled (Style::Vocal);
    CHECK (vocal.character == Character::Clean);
    CHECK (vocal.autoRelease);
    CHECK (vocal.gate.on);
    CHECK (vocal.limiter);
    for (const auto style : { Style::Instrument, Style::Bus, Style::Drums })
    {
        CAPTURE (static_cast<int> (style));
        const auto s = styled (style, everythingMoved());
        CHECK_FALSE (s.gate.on);
        CHECK_FALSE (s.limiter);
    }
}

TEST_CASE ("Every Source Style has defaults, and applying one writes its own Character and Comp Curve")
{
    const auto style = GENERATE (from_range (sourceStyles));
    CAPTURE (static_cast<int> (style));
    CHECK (hasDefaults (style));
    auto settings = everythingMoved();
    CHECK (applyStyle (style, settings));
    CHECK (settings.character == characterOf (style));
    CHECK (settings.compCurve == compCurveOf (style));
}

TEST_CASE ("dyn-lab's compressor defaults are the Engine's, so its Colour's Threshold reference is the Engine's too")
{
    // dyn-lab's Settings, each field and its default as the script writes it ("    threshold: float = -18.0"). Its
    // COLOUR_THRESHOLD_REFERENCE_DB is its default Threshold, as the Engine's is a default Settings' (engine/src/Colour.h).
    std::ifstream script (COMP1_DYN_LAB);
    REQUIRE (script.is_open());
    std::map<std::string, std::string> lab;
    bool inSettings = false;
    const std::regex fieldLine (R"(    (\w+): \w+ = (.+))");
    for (std::string line; std::getline (script, line);)
    {
        std::smatch field;
        if (line == "class Settings:")
            inSettings = true;
        else if (inSettings && ! line.empty() && line.front() != ' ')
            break;
        else if (inSettings && std::regex_match (line, field, fieldLine))
            lab[field[1]] = field[2];
    }
    const Settings defaults;
    const std::vector<std::pair<std::string, double>> engine {
        { "threshold", defaults.thresholdDb }, { "ratio", defaults.ratio },     { "knee", defaults.kneeDb },
        { "attack", defaults.attackMs },       { "release", defaults.releaseMs }, { "hold", defaults.holdMs },
        { "range", defaults.rangeDb },         { "colour", defaults.colour },
    };
    for (const auto& [name, value] : engine)
    {
        CAPTURE (name);
        REQUIRE (lab.contains (name));
        CHECK (std::stod (lab[name]) == value);
    }
    CHECK (lab["auto_release"] == (defaults.autoRelease ? "True" : "False"));
    CHECK (lab["character"] == "\"clean\"");
    CHECK (defaults.character == Character::Clean);
    CHECK (defaults.compCurve == compCurveOf (Style::Clean));
}
