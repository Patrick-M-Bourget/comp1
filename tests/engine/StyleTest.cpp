// The Style table (comp1/Style.h): each Style's Character, and the defaults choosing a Character Style writes.

#include "Measure.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <string>
#include <vector>

using namespace comp1;

namespace
{

// One row of dyn-lab's Style table, as `python3 tools/dyn-lab/dynlab.py styles` prints it: the Style, its Character,
// the compressor's Threshold, Ratio, Knee, Attack, Release, Auto Release, Hold, Range and Colour, the Gate's on/off,
// Gate Threshold, Gate Range, Gate Attack, Gate Release and Gate Hold, and the Limiter's on/off and Ceiling.
struct LabRow
{
    Style style;
    Character character;
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
    { Style::Clean, Character::Clean, -18, 4, 6, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::VCA, Character::VCA, -18, 4, 3, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::FET, Character::FET, -18, 4, 3, 0.5, 50, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
    { Style::Opto, Character::Opto, -18, 3, 12, 10, 100, false, 0, 60, 100, false, -40, 20, 1, 100, 20, false, 0 },
};

const std::vector<Style> characterStyles { Style::Clean, Style::VCA, Style::FET, Style::Opto };
const std::vector<Style> sourceStyles { Style::Vocal, Style::Instrument, Style::Bus, Style::Drums };

// Every setting moved off its default, the ones a Style writes and the ones it never touches.
Settings everythingMoved()
{
    Settings s;
    s.gate = { .on = true, .thresholdDb = -25.0, .rangeDb = 30.0, .attackMs = 5.0, .releaseMs = 250.0, .holdMs = 50.0 };
    s.character = Character::Opto;
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
    for (const auto& lab : labRows)
    {
        CAPTURE (static_cast<int> (lab.style));
        CHECK (characterOf (lab.style) == lab.character);
        REQUIRE (hasDefaults (lab.style));
        const auto s = styled (lab.style);
        CHECK (s.character == lab.character);
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

TEST_CASE ("Clean's row is a default Settings in every field it writes")
{
    CHECK (styled (Style::Clean, everythingMoved()) == styled (Style::Clean));
    CHECK (styled (Style::Clean) == Settings {});
}

TEST_CASE ("A Character Style writes only the settings ADR 0002 lists, and its Character")
{
    const auto style = GENERATE (from_range (characterStyles));
    CAPTURE (static_cast<int> (style));
    const auto moved = everythingMoved();
    auto expected = styled (style);
    // Everything a Style never touches keeps its value: the routing, the levels and the whole plugin's.
    expected.autoGain = moved.autoGain;
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

TEST_CASE ("A Source Style plays Clean and has no defaults yet, so applying it changes nothing")
{
    const auto style = GENERATE (from_range (sourceStyles));
    CAPTURE (static_cast<int> (style));
    CHECK (characterOf (style) == Character::Clean);
    CHECK_FALSE (hasDefaults (style));
    auto settings = everythingMoved();
    CHECK_FALSE (applyStyle (style, settings));
    CHECK (settings == everythingMoved());
}
