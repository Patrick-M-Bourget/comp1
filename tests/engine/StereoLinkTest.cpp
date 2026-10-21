#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using namespace comp1;

namespace
{

// Steady levels on the two channels, -6 and -15 dB, and the output dyn-lab's Transfer Curve gives each at the default
// settings (python3 tools/dyn-lab/dynlab.py transfer): -15.00 dB, 9 dB of Gain Reduction, and -17.25 dB, 2.25 dB.
const std::vector<double> levelsDb { -6.0, -15.0 };
constexpr double louderOutputDb = -15.00, quieterOutputDb = -17.25;
constexpr double louderReductionDb = 9.0, quieterReductionDb = 2.25;

Settings linkedBy (double stereoLink) { return { .stereoLink = stereoLink }; }

} // namespace

TEST_CASE ("At Stereo Link 0% each channel follows the Transfer Curve at its own level")
{
    const auto outputDb = test::steadyOutputsDb (linkedBy (0.0), levelsDb, 48000.0);
    CHECK (std::abs (outputDb[0] - louderOutputDb) <= 0.01);
    CHECK (std::abs (outputDb[1] - quieterOutputDb) <= 0.01);
}

TEST_CASE ("At Stereo Link 0% each channel is compressed exactly as it is on its own")
{
    // Noise louder on the left than on the right, each compressed alone on a mono track for the reference.
    auto stereo = test::noise (2, 20000);
    for (auto& sample : stereo[1])
        sample *= 0.2f;
    const auto input = stereo;
    Engine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setSettings (linkedBy (0.0));
    test::processInBlocks (engine, stereo, { 512 });
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        CAPTURE (ch);
        test::Channels alone { input[ch] };
        Engine mono;
        mono.prepare (48000.0, 512, 1);
        mono.setSettings (linkedBy (0.0));
        test::processInBlocks (mono, alone, { 512 });
        CHECK (stereo[ch] == alone.front());
    }
}

TEST_CASE ("At Stereo Link 100% both channels get the same Gain Reduction at every sample")
{
    // The right channel is the left a quarter as loud, a power of two, so the same gain leaves it exactly a quarter of
    // the left's output; its own Gain Reduction would be 12 dB less.
    auto stereo = test::noise (1, 20000);
    stereo.push_back (stereo.front());
    for (auto& sample : stereo[1])
        sample *= 0.25f;
    Engine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setSettings (linkedBy (100.0));
    test::processInBlocks (engine, stereo, { 512 });
    int differing = 0;
    for (std::size_t i = 0; i < stereo[0].size(); ++i)
        differing += stereo[1][i] * 4.0f != stereo[0][i] ? 1 : 0;
    CHECK (differing == 0);
}

TEST_CASE ("At Stereo Link 100% both channels get the Transfer Curve's Gain Reduction at the louder level")
{
    const auto outputDb = test::steadyOutputsDb (linkedBy (100.0), levelsDb, 48000.0);
    CHECK (std::abs (outputDb[0] - louderOutputDb) <= 0.01);
    CHECK (std::abs (outputDb[1] - (levelsDb[1] - louderReductionDb)) <= 0.01);
}

TEST_CASE ("At Stereo Link 100% a step on one channel alone moves both with dyn-lab's step timing")
{
    // python3 tools/dyn-lab/dynlab.py step --rates 48000: 63% and 90% of Attack and Release, in ms.
    constexpr double sampleRate = 48000.0, sampleMs = 1000.0 / sampleRate;
    constexpr double attack63 = 10.0208, attack90 = 23.0417, release63 = 100.0208, release90 = 230.2708;
    // The left steps from 20 dB under Threshold (no Gain Reduction) to 20 dB over it (15 dB) and back; the right stays
    // 20 dB under throughout, so whatever Gain Reduction it gets is the left's.
    const Settings settings = linkedBy (100.0);
    const double quiet = settings.thresholdDb - 20.0, loud = settings.thresholdDb + 20.0;
    const auto rightReduction = [&] (double fromDb, double toDb, double seconds) {
        const int length = static_cast<int> (seconds * sampleRate);
        auto before = test::steadyLevels ({ fromDb, quiet }, length);
        auto after = test::steadyLevels ({ toDb, quiet }, length);
        const auto input = after[1];
        Engine engine;
        engine.prepare (sampleRate, 512, 2);
        engine.setSettings (settings);
        test::processInBlocks (engine, before, { 512 });
        test::processInBlocks (engine, after, { 512 });
        return test::gainReductionDb (input, after[1]);
    };

    const auto attack = test::crossings (rightReduction (quiet, loud, 1.0), 0.0, 15.0);
    REQUIRE (attack.ninety);
    CHECK (std::abs (*attack.sixtyThree * sampleMs - attack63) <= sampleMs);
    CHECK (std::abs (*attack.ninety * sampleMs - attack90) <= sampleMs);

    const auto release = test::crossings (rightReduction (loud, quiet, 2.0), 15.0, 0.0);
    REQUIRE (release.ninety);
    CHECK (std::abs (*release.sixtyThree * sampleMs - release63) <= sampleMs);
    CHECK (std::abs (*release.ninety * sampleMs - release90) <= sampleMs);
}

TEST_CASE ("At Stereo Link 50% the quieter channel's Gain Reduction lies halfway to the louder's")
{
    // Each channel's own, plus Stereo Link times the larger of the two less its own: the louder keeps its 9 dB, the
    // quieter gets 2.25 + 0.5 (9 - 2.25) dB.
    const auto outputDb = test::steadyOutputsDb (linkedBy (50.0), levelsDb, 48000.0);
    CHECK (std::abs (outputDb[0] - louderOutputDb) <= 0.01);
    CHECK (std::abs (outputDb[1] - (levelsDb[1] - (quieterReductionDb + 0.5 * (louderReductionDb - quieterReductionDb)))) <= 0.01);
}

TEST_CASE ("At every Stereo Link the Gain Reduction tap reads the larger channel's, as the Engine applies it")
{
    const double stereoLink = GENERATE (0.0, 50.0, 100.0);
    CAPTURE (stereoLink);
    // Stereo Link raises the quieter channel's Gain Reduction toward the louder's, never past it: the tap, the larger
    // channel's, reads the louder's 9 dB throughout, and its Detection Level.
    const auto records = test::levelHistory (linkedBy (stereoLink), 48000.0, test::steadyLevels (levelsDb, 24000));
    REQUIRE (! records.empty());
    CHECK (std::abs (records.back().gainReductionDb - louderReductionDb) <= 0.01);
    CHECK (std::abs (records.back().detectionLevelDb - levelsDb[0]) <= 0.01);
}
