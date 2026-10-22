// Vocal's chain at its defaults (tools/dyn-lab/USAGE.md, "Source Styles"): the Gate, then Clean with Auto Release, then
// the Limiter, each heard working through the Engine.

#include "Measure.h"
#include "comp1/Style.h"

#include <plugkit/RunGrid.h>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

using namespace comp1;

namespace
{

constexpr double sampleRate = 48000.0;

// How long the Gain Reduction takes, in ms, to fall to one time constant (1 - 1/e) of the way back from where it was
// when a burst loudDb for loudSeconds ends, with the level then held at quietDb, through Vocal's defaults: read off the
// level history, a run at a time.
double releaseAfterMs (double loudSeconds)
{
    const double loudDb = -8.0, quietDb = -30.0; // 10 dB over Vocal's Threshold, then 12 dB under it, above Gate Threshold
    const auto loudSamples = static_cast<std::size_t> (loudSeconds * sampleRate);
    auto audio = test::steadyLevel (1, static_cast<int> (loudSamples), loudDb);
    const auto quiet = test::steadyLevel (1, static_cast<int> (3.0 * sampleRate), quietDb).front();
    audio.front().insert (audio.front().end(), quiet.begin(), quiet.end());
    const auto records = test::levelHistory (styled (Style::Vocal), sampleRate, audio);
    const std::size_t burstEnd = loudSamples / plugkit::runSamples;
    REQUIRE (burstEnd < records.size());
    const double reached = records[burstEnd - 1].gainReductionDb;
    REQUIRE (reached > 1.0);
    for (std::size_t r = burstEnd; r < records.size(); ++r)
        if (records[r].gainReductionDb <= reached * (1.0 - test::oneTimeConstant))
            return static_cast<double> ((r - burstEnd) * plugkit::runSamples) * 1000.0 / sampleRate;
    FAIL ("the Gain Reduction never released");
    return 0.0;
}

} // namespace

TEST_CASE ("Vocal's Gate lowers a passage well under Gate Threshold by Gate Range")
{
    const auto vocal = styled (Style::Vocal);
    REQUIRE (vocal.gate.on);
    auto ungated = vocal;
    ungated.gate.on = false;
    // 30 dB under Gate Threshold, far under Threshold, so the compressor leaves it alone and only Auto Gain moves it,
    // the same with the Gate on and off. Settled: the level hold, Gate Hold and Gate Release are long gone in 1.5 s.
    const double quietDb = vocal.gate.thresholdDb - 30.0;
    const double gatedDb = test::steadyOutputDb (vocal, quietDb, sampleRate, 1, 1.5);
    const double openDb = test::steadyOutputDb (ungated, quietDb, sampleRate, 1, 1.5);
    CHECK (std::abs ((openDb - gatedDb) - vocal.gate.rangeDb) <= 0.001);
}

TEST_CASE ("Vocal's Auto Release releases more slowly after sustained compression than after a short burst")
{
    REQUIRE (styled (Style::Vocal).autoRelease);
    const double afterShort = releaseAfterMs (0.01), afterSustained = releaseAfterMs (2.0);
    CAPTURE (afterShort, afterSustained);
    // dyn-lab's bursts --style vocal: about 104 ms after 10 ms, and over 330 ms after a second or more.
    CHECK (afterShort < 1.1 * styled (Style::Vocal).releaseMs);
    CHECK (afterSustained > 3.0 * afterShort);
}

TEST_CASE ("Vocal's Limiter keeps every output sample under the Ceiling with Output Gain pushed 12 dB")
{
    auto vocal = styled (Style::Vocal);
    REQUIRE (vocal.limiter);
    vocal.outputGainDb = 12.0;
    const auto input = test::noise (2, 48000);
    const auto out = test::played (vocal, sampleRate, input);
    CHECK (test::samplePeak (out) <= test::gainOf (vocal.ceilingDb));
    // Without the Limiter the same push goes over, so the check above is the Limiter's doing.
    auto unlimited = vocal;
    unlimited.limiter = false;
    CHECK (test::samplePeak (test::played (unlimited, sampleRate, input)) > test::gainOf (vocal.ceilingDb));
}
