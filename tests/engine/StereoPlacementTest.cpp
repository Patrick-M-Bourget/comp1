#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <plugkit/Placement.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace comp1;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int numSamples = 24000;

// The loud part: a steady -9 dB, where dyn-lab's Transfer Curve takes 1.5 dB off at Threshold -12 dB and Ratio 2:1
// (python3 tools/dyn-lab/dynlab.py transfer --threshold -12 --ratio 2).
constexpr double loudDb = -9.0, loudReductionDb = 1.5;
const Settings partSettings { .thresholdDb = -12.0, .ratio = 2.0 };

// The quiet part: a 440 Hz tone at -60 dB, each sample a whole number of 2^-24. Left and Right are then exact float
// sums of the two parts, and as the loud part's gain moves they stay sums of the quiet part and one float, rounded the
// same way: from -9 dB down to the -10.5 dB the loud part settles at, Left and Right stay within one binade, 0.25 to
// 0.5, where every whole number of 2^-24 is a whole number of float steps. So the quiet part decodes back exactly
// when it is left untouched.
std::vector<float> quietTone()
{
    std::vector<float> tone;
    for (int i = 0; i < numSamples; ++i)
        tone.push_back (static_cast<float> (std::round (0.001 * std::sin (2.0 * std::numbers::pi * 440.0 * i / sampleRate) * 0x1p24) * 0x1p-24));
    return tone;
}

// Left and Right from a Mid and a Side, as plugkit decodes them.
test::Channels decoded (const std::vector<float>& mid, const std::vector<float>& side)
{
    test::Channels stereo (2);
    for (std::size_t i = 0; i < mid.size(); ++i)
    {
        stereo[0].push_back (plugkit::decodeLeft (mid[i], side[i]));
        stereo[1].push_back (plugkit::decodeRight (mid[i], side[i]));
    }
    return stereo;
}

// The Mid (true) or the Side of a stereo signal, as plugkit encodes it.
std::vector<float> part (const test::Channels& stereo, bool mid)
{
    std::vector<float> encoded;
    for (std::size_t i = 0; i < stereo[0].size(); ++i)
        encoded.push_back (mid ? plugkit::encodeMid (stereo[0][i], stereo[1][i]) : plugkit::encodeSide (stereo[0][i], stereo[1][i]));
    return encoded;
}

// The gain in dB the Mid (true) or the Side of input got by the end of output.
double partGainDb (const test::Channels& input, const test::Channels& output, bool mid)
{
    return 20.0 * std::log10 (static_cast<double> (part (output, mid).back()) / static_cast<double> (part (input, mid).back()));
}

} // namespace

TEST_CASE ("Stereo Placement Mid or Side compresses that part by the Transfer Curve and passes the other bit for bit")
{
    const bool midLoud = GENERATE (true, false);
    CAPTURE (midLoud);
    const auto loud = test::steadyLevel (1, numSamples, loudDb).front();
    const auto quiet = quietTone();
    const auto input = midLoud ? decoded (loud, quiet) : decoded (quiet, loud);
    auto settings = partSettings;
    settings.stereoPlacement = midLoud ? StereoPlacement::Mid : StereoPlacement::Side;
    const auto output = test::played (settings, sampleRate, input);

    // The quiet part passes untouched, decoded back from the output.
    CHECK (part (output, ! midLoud) == quiet);
    // The loud part is reduced by the Transfer Curve's amount at its own level.
    const auto loudOut = part (output, midLoud);
    CHECK (std::abs (20.0 * std::log10 (static_cast<double> (loudOut.back()) / static_cast<double> (loud.back())) + loudReductionDb) <= 0.01);
}

TEST_CASE ("With no Gain Reduction the output is the input bit for bit in every Stereo Placement, at every Stereo Link")
{
    const auto placement = GENERATE (from_range (test::stereoPlacements()));
    const double stereoLink = GENERATE (0.0, 37.0, 100.0);
    CAPTURE (placement, stereoLink);
    // Noise peaking at -6 dB, under the Knee of a 0 dB Threshold, in every part.
    const auto input = test::noise (2, numSamples);
    CHECK (test::played ({ .thresholdDb = 0.0, .stereoPlacement = placement, .stereoLink = stereoLink }, sampleRate, input) == input);
}

TEST_CASE ("A mono track is compressed as Stereo whatever Stereo Placement and Stereo Link hold")
{
    const auto placement = GENERATE (from_range (test::stereoPlacements()));
    const double stereoLink = GENERATE (0.0, 37.0, 100.0);
    CAPTURE (placement, stereoLink);
    const auto input = test::noise (1, numSamples);
    CHECK (test::played ({ .stereoPlacement = placement, .stereoLink = stereoLink }, sampleRate, input) == test::played ({}, sampleRate, input));
}

namespace
{

// A Sidechain whose loud part is a steady -6 dB: on the Mid (both channels alike), on the Side (one the other's
// negative), or a mono one.
enum class SidechainPart
{
    loudMid,
    loudSide,
    mono,
};

test::Channels sidechainOf (SidechainPart loud)
{
    auto channels = test::steadyLevel (loud == SidechainPart::mono ? 1 : 2, numSamples, -6.0);
    if (loud == SidechainPart::loudSide)
        for (auto& sample : channels[1])
            sample = -sample;
    return channels;
}

} // namespace

TEST_CASE ("With External, a stereo Sidechain's Mid or Side drives Stereo Placement Mid or Side, and a mono one drives every Placement")
{
    const auto loud = GENERATE (SidechainPart::loudMid, SidechainPart::loudSide, SidechainPart::mono);
    const auto placement = GENERATE (StereoPlacement::Mid, StereoPlacement::Side);
    CAPTURE (loud, placement);
    // The main input's Mid and Side are steady and far under Threshold, so only the Sidechain moves them.
    const auto input = decoded (test::steadyLevel (1, numSamples, -30.0).front(), test::steadyLevel (1, numSamples, -36.0).front());
    const auto sidechainSamples = sidechainOf (loud);
    const auto output = test::played ({ .detectionSource = DetectionSource::External, .stereoPlacement = placement }, sampleRate, input, { 512 }, &sidechainSamples);

    // The Placement's part takes the Transfer Curve's 9 dB at the Sidechain's -6 dB (python3 tools/dyn-lab/dynlab.py
    // transfer) when its part of the Sidechain is the loud one, or the Sidechain is mono, and none otherwise; the
    // other part passes.
    const bool mid = placement == StereoPlacement::Mid;
    const bool driven = loud == SidechainPart::mono || (loud == SidechainPart::loudMid) == mid;
    CHECK (std::abs (partGainDb (input, output, mid) + (driven ? 9.0 : 0.0)) <= 0.01);
    CHECK (std::abs (partGainDb (input, output, ! mid)) <= 0.01);
}

TEST_CASE ("During a Stereo Placement fade, the Gain Reduction is the gain the Engine applies")
{
    const auto [from, to] = GENERATE (table<StereoPlacement, StereoPlacement> ({ { StereoPlacement::Stereo, StereoPlacement::Side },
                                                                                 { StereoPlacement::Side, StereoPlacement::Stereo },
                                                                                 { StereoPlacement::Stereo, StereoPlacement::Mid },
                                                                                 { StereoPlacement::Mid, StereoPlacement::Side } }));
    CAPTURE (from, to);
    // Both channels alike, the loud part's steady level: all Mid, with no Side. So Stereo and Mid take the Transfer
    // Curve's 1.5 dB off both channels and Side none, and through each fade the gain moves between the two on both
    // channels alike.
    const auto input = test::steadyLevel (2, numSamples, loudDb);
    auto before = partSettings, after = partSettings;
    before.stereoPlacement = from;
    after.stereoPlacement = to;
    // Once Attack has long settled.
    const int changeAt = numSamples / 2;
    const auto records = test::levelHistory (before, sampleRate, input, test::SettingsChange { .at = changeAt, .settings = after });
    REQUIRE (records.size() == static_cast<std::size_t> (numSamples / 16));
    // Within what the gain moves in a run as it fades.
    for (auto run = static_cast<std::size_t> (changeAt / 16 - 10); run < records.size(); ++run)
    {
        CAPTURE (run);
        REQUIRE (std::abs (records[run].gainReductionDb - (records[run].inputDb - records[run].outputDb)) <= 0.05);
    }
}

TEST_CASE ("Under Stereo Placement Mid or Side, the level taps read that part's Detection Level and Gain Reduction")
{
    const bool midLoud = GENERATE (true, false);
    const auto placement = GENERATE (StereoPlacement::Mid, StereoPlacement::Side);
    CAPTURE (midLoud, placement);
    // The loud part alone: all Mid (both channels alike) or all Side (one the other's negative), the other part silent.
    const auto loud = test::steadyLevel (1, numSamples, loudDb).front();
    const std::vector<float> silent (numSamples, 0.0f);
    const auto input = midLoud ? decoded (loud, silent) : decoded (silent, loud);
    auto settings = partSettings;
    settings.stereoPlacement = placement;
    const auto records = test::levelHistory (settings, sampleRate, input);
    REQUIRE (! records.empty());

    // The Placement's part, when it is the loud one, reads its level and takes the Transfer Curve's 1.5 dB there;
    // otherwise it reads silence, the floor, and takes none.
    const bool heard = midLoud == (placement == StereoPlacement::Mid);
    CHECK (std::abs (records.back().gainReductionDb - (heard ? loudReductionDb : 0.0)) <= 0.01);
    CHECK (std::abs (records.back().detectionLevelDb - (heard ? loudDb : plugkit::levelFloorDb)) <= 0.01);
}
