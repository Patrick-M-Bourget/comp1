#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;
constexpr double off = -std::numeric_limits<double>::infinity();

// The Gate on at gate's settings, with Ratio 1:1, so the compressor applies no Gain Reduction and the Gate's alone is
// heard.
Settings gated (Settings::Gate gate, ProcessingMode mode = zeroLatency)
{
    gate.on = true;
    return { .gate = gate, .ratio = 1.0, .processingMode = mode };
}

// The level in dB of each channel of audio over its last samples, as a power.
std::vector<double> tailLevelsDb (const test::Channels& audio, std::size_t samples)
{
    std::vector<double> levels;
    for (const auto& channel : audio)
    {
        double power = 0.0;
        for (std::size_t i = channel.size() - samples; i < channel.size(); ++i)
            power += static_cast<double> (channel[i]) * channel[i];
        levels.push_back (10.0 * std::log10 (power / static_cast<double> (samples)));
    }
    return levels;
}

// audio followed by more, on every channel.
test::Channels joined (test::Channels audio, const test::Channels& more)
{
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
        audio[ch].insert (audio[ch].end(), more[ch].begin(), more[ch].end());
    return audio;
}

// A Gate step response's times in ms, as dyn-lab's gate prints them.
struct GateStep
{
    double releaseBegins, release63, release90, attack63, attack90;
};

struct GateRate
{
    double sampleRate;
    GateStep step;
};

struct GateResponse
{
    std::string command; // the dyn-lab command that printed rates
    Settings::Gate gate;
    std::vector<GateRate> rates;
};

// Each table is the output of the dyn-lab command named with it, copied as printed.
const std::vector<GateResponse>& gateResponses()
{
    static const std::vector<GateResponse> all {
        { "python3 tools/dyn-lab/dynlab.py gate --rates 44100 48000 88200 96000 176400 192000",
          {},
          { { 44100, { 30.0000, 100.0227, 230.2721, 1.0204, 2.3129 } },
            { 48000, { 30.0000, 100.0208, 230.2708, 1.0000, 2.3125 } },
            { 88200, { 30.0000, 100.0113, 230.2608, 1.0091, 2.3129 } },
            { 96000, { 30.0000, 100.0104, 230.2604, 1.0000, 2.3125 } },
            { 176400, { 30.0000, 100.0057, 230.2608, 1.0034, 2.3073 } },
            { 192000, { 30.0000, 100.0000, 230.2604, 1.0000, 2.3073 } } } },
        { "python3 tools/dyn-lab/dynlab.py gate --gate-attack 0.5 --gate-release 50 --gate-hold 0 --rates 44100 48000 88200 96000 176400 192000",
          { .attackMs = 0.5, .releaseMs = 50.0, .holdMs = 0.0 },
          { { 44100, { 10.0000, 50.0227, 115.1474, 0.5215, 1.1565 } },
            { 48000, { 10.0000, 50.0000, 115.1458, 0.5000, 1.1667 } },
            { 88200, { 10.0000, 50.0113, 115.1361, 0.5102, 1.1565 } },
            { 96000, { 10.0000, 50.0104, 115.1354, 0.5000, 1.1562 } },
            { 176400, { 10.0000, 50.0057, 115.1304, 0.5045, 1.1565 } },
            { 192000, { 10.0000, 50.0052, 115.1302, 0.5000, 1.1562 } } } },
    };
    return all;
}

constexpr double stepDb = 20.0;
constexpr double silent = -std::numeric_limits<double>::infinity();

// The Gain Reduction in dB on each sample of a steady level held for toSeconds, after another held for fromSeconds,
// through the Gate alone (Ratio 1:1), Zero Latency.
std::vector<double> gateReductionAfterStep (const Settings::Gate& gate, double sampleRate, double fromDb, double fromSeconds, double toDb, double toSeconds)
{
    const auto before = test::steadyLevel (1, static_cast<int> (fromSeconds * sampleRate), fromDb);
    const auto after = test::steadyLevel (1, static_cast<int> (toSeconds * sampleRate), toDb);
    const auto output = test::played (gated (gate), sampleRate, joined (before, after));
    const std::vector<float> heard (output.front().begin() + static_cast<std::ptrdiff_t> (before.front().size()), output.front().end());
    return test::gainReductionDb (after.front(), heard);
}

} // namespace

TEST_CASE ("Gate Attack, Gate Release and the delay before Gate Release begins match dyn-lab's gate within one sample, at every sample rate", "[sweep]")
{
    const auto& response = GENERATE (from_range (gateResponses()));
    const auto& rate = GENERATE_REF (from_range (response.rates));
    CAPTURE (response.command, rate.sampleRate);
    const double sampleMs = 1000.0 / rate.sampleRate;
    const auto& gate = response.gate;
    const double quiet = gate.thresholdDb - stepDb, loud = gate.thresholdDb + stepDb;

    // Closing, from open: when Gate Release begins, and its times from then.
    const double closingSeconds = (rate.step.releaseBegins + rate.step.release90) / 1000.0 + 0.01;
    const auto closing = test::crossings (gateReductionAfterStep (gate, rate.sampleRate, loud, 0.05, quiet, closingSeconds), 0.0, gate.rangeDb);
    REQUIRE (closing.ninety);
    CHECK (std::abs (closing.held * sampleMs - rate.step.releaseBegins) <= sampleMs);
    CHECK (std::abs ((*closing.sixtyThree - closing.held) * sampleMs - rate.step.release63) <= sampleMs);
    CHECK (std::abs ((*closing.ninety - closing.held) * sampleMs - rate.step.release90) <= sampleMs);

    // Opening, from closed and settled: 14 Gate Release time constants leave under 1e-5 of Gate Range to go.
    const double settleSeconds = (rate.step.releaseBegins + 14.0 * gate.releaseMs) / 1000.0;
    const auto opening = test::crossings (gateReductionAfterStep (gate, rate.sampleRate, quiet, settleSeconds, loud, 0.01), gate.rangeDb, 0.0);
    REQUIRE (opening.ninety);
    CHECK (std::abs (*opening.sixtyThree * sampleMs - rate.step.attack63) <= sampleMs);
    CHECK (std::abs (*opening.ninety * sampleMs - rate.step.attack90) <= sampleMs);
}

TEST_CASE ("With the Gate off, its settings change nothing, bit for bit, in both Processing Modes", "[sweep]")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (44100.0, 192000.0);
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (static_cast<int> (mode), sampleRate, cut);
    // Noise the Gate would close on (its peaks, 0.5, are under 0 dB less the hysteresis), compressed and summed with
    // Dry.
    const auto input = test::noise (2, 4000);
    Settings plain { .processingMode = mode };
    plain.dryDb = -6.0;
    auto gateSet = plain;
    gateSet.gate = { .on = false, .thresholdDb = 0.0, .rangeDb = 60.0, .attackMs = 0.005, .releaseMs = 5.0, .holdMs = 0.0 };
    CHECK (test::played (gateSet, sampleRate, input, cut) == test::played (plain, sampleRate, input, cut));
}

TEST_CASE ("The Gate lowers the Dry path too: at Dry 0 dB and Wet Off, closed, the output is the delayed input lowered by Gate Range")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const Settings::Gate gate {};
    auto settings = gated (gate, mode);
    settings.dryDb = 0.0;
    settings.wetDb = off;
    const auto input = test::steadyLevel (2, 72000, gate.thresholdDb - stepDb);
    const auto output = test::played (settings, 48000.0, input);
    const auto expected = test::delayed (input, latencySamples (mode, 48000.0));
    const auto outputDb = tailLevelsDb (output, 480), expectedDb = tailLevelsDb (expected, 480);
    for (std::size_t ch = 0; ch < 2; ++ch)
        CHECK (std::abs (outputDb[ch] - (expectedDb[ch] - gate.rangeDb)) <= 0.001);
}

TEST_CASE ("The Gate listens after the Detection EQ: its high-pass closes the Gate on a low tone that would keep it open")
{
    // A -20 dB 50 Hz tone, 10 dB over Gate Threshold.
    const double sampleRate = 48000.0;
    test::Channels tone (1, std::vector<float> (72000));
    for (std::size_t i = 0; i < tone[0].size(); ++i)
        tone[0][i] = static_cast<float> (0.1 * std::sin (2.0 * std::numbers::pi * 50.0 * static_cast<double> (i) / sampleRate));
    auto settings = gated ({ .thresholdDb = -30.0 });
    CHECK (test::played (settings, sampleRate, tone) == tone);
    // The high-pass at 500 Hz, 48 dB/oct, takes the tone about 160 dB down in what the Gate hears.
    settings.detectionEq.highPass.on = true;
    settings.detectionEq.highPass.filter.frequency = 500.0;
    settings.detectionEq.highPass.filter.slope = 48.0;
    const auto gatedDb = tailLevelsDb (test::played (settings, sampleRate, tone), 4800);
    CHECK (std::abs (gatedDb[0] - (tailLevelsDb (tone, 4800)[0] - settings.gate.rangeDb)) <= 0.001);
}

TEST_CASE ("The Gate never listens to a feedback path: from silence, closed, a tone 1 dB over Gate Threshold opens it within Gate Attack's 90% time")
{
    // A Gate listening to its own output would hear the tone Gate Range down, under the closing level, and stay shut.
    const double sampleRate = 48000.0;
    const Settings::Gate gate { .releaseMs = 5.0, .holdMs = 0.0 };
    const auto silence = test::steadyLevel (1, 9600, silent);
    const auto tone = test::steadyLevel (1, 480, gate.thresholdDb + 1.0);
    const auto output = test::played (gated (gate), sampleRate, joined (silence, tone));
    const std::vector<float> heard (output.front().begin() + 9600, output.front().end());
    const auto reduction = test::gainReductionDb (tone.front(), heard);
    REQUIRE (reduction.front() > 0.9 * gate.rangeDb); // closed when the tone comes
    const auto opening = test::crossings (reduction, gate.rangeDb, 0.0);
    REQUIRE (opening.ninety);
    // dyn-lab's Gate Attack 90% time at the default Gate Attack, at 48 kHz (python3 tools/dyn-lab/dynlab.py gate).
    CHECK (*opening.ninety * 1000.0 / sampleRate <= 2.3125 + 1000.0 / sampleRate);
}

TEST_CASE ("With External, the Sidechain opens and closes the Gate whatever the main's level")
{
    const Settings::Gate gate {};
    auto settings = gated (gate);
    settings.detectionSource = DetectionSource::External;
    const auto quiet = test::steadyLevel (2, 72000, gate.thresholdDb - stepDb);
    const auto loud = test::steadyLevel (2, 72000, gate.thresholdDb + stepDb);
    // A quiet main under a loud Sidechain stays open, bit for bit.
    CHECK (test::played (settings, 48000.0, quiet, { 512 }, &loud) == quiet);
    // A loud main under a quiet Sidechain is closed by Gate Range.
    for (const double levelDb : tailLevelsDb (test::played (settings, 48000.0, loud, { 512 }, &quiet), 480))
        CHECK (std::abs (levelDb - (gate.thresholdDb + stepDb - gate.rangeDb)) <= 0.001);
}

TEST_CASE ("With External and no Sidechain connected the Gate stays open; a connected silent Sidechain closes it by Gate Range")
{
    const Settings::Gate gate {};
    auto settings = gated (gate);
    settings.detectionSource = DetectionSource::External;
    // A main the Gate would close on were it listening to it.
    const auto quiet = test::steadyLevel (2, 72000, gate.thresholdDb - stepDb);
    auto gateOff = settings;
    gateOff.gate.on = false;
    CHECK (test::played (settings, 48000.0, quiet) == test::played (gateOff, 48000.0, quiet));
    const auto silence = test::steadyLevel (2, 72000, silent);
    for (const double levelDb : tailLevelsDb (test::played (settings, 48000.0, quiet, { 512 }, &silence), 480))
        CHECK (std::abs (levelDb - (gate.thresholdDb - stepDb - gate.rangeDb)) <= 0.001);
}

TEST_CASE ("The compressor hears the gated signal")
{
    // A steady -6 dB at the compressor's defaults. dyn-lab's transfer (python3 tools/dyn-lab/dynlab.py transfer):
    // 9.00 dB of Gain Reduction at -6 dB, and 0.56 dB at -18 dB.
    const double levelDb = -6.0;
    const Settings open { .gate = { .on = true } };
    CHECK (std::abs (test::steadyOutputDb (open, levelDb, 48000.0) - (levelDb - 9.0)) <= 0.01);
    // Closed by Gate Range 12 dB at Gate Threshold 0 dB, the compressor hears -18 dB. The input sits on the closing
    // level, where the Gate stays as it is, so silence comes first to close it.
    const Settings closed { .gate = { .on = true, .thresholdDb = 0.0, .rangeDb = 12.0 } };
    const auto silence = test::steadyLevel (1, 2400, silent);
    const auto level = test::steadyLevel (1, 72000, levelDb);
    const auto output = test::played (closed, 48000.0, joined (silence, level));
    const double gainDb = 20.0 * std::log10 (std::abs (static_cast<double> (output.front().back()) / level.front().back()));
    CHECK (std::abs (gainDb - -12.56) <= 0.01);
}

TEST_CASE ("On a stereo track the Gate puts one gain on both channels, from the louder, at every Stereo Placement and Stereo Link")
{
    const auto placement = GENERATE (from_range (test::stereoPlacements()));
    const double link = GENERATE (0.0, 100.0);
    CAPTURE (static_cast<int> (placement), link);
    const Settings::Gate gate {};
    // The left over Gate Threshold, the right far under it: both stay open, so the Gate changes nothing.
    const auto input = test::steadyLevels ({ gate.thresholdDb + stepDb, gate.thresholdDb - stepDb }, 24000);
    Settings settings { .gate = gate, .stereoPlacement = placement, .stereoLink = link };
    const auto gateOff = test::played (settings, 48000.0, input);
    settings.gate.on = true;
    CHECK (test::played (settings, 48000.0, input) == gateOff);
    // Both under it, at different levels, both are lowered by Gate Range: the same gain. With Ratio 1:1, so the
    // compressor's own gains, which differ by channel, don't come into it.
    settings.ratio = 1.0;
    const auto quiet = test::steadyLevels ({ gate.thresholdDb - stepDb, gate.thresholdDb - 2.0 * stepDb }, 72000);
    const auto closedDb = tailLevelsDb (test::played (settings, 48000.0, quiet), 480), quietDb = tailLevelsDb (quiet, 480);
    for (std::size_t ch = 0; ch < 2; ++ch)
        CHECK (std::abs (closedDb[ch] - (quietDb[ch] - gate.rangeDb)) <= 0.001);
}

TEST_CASE ("The reported latency is unchanged with the Gate on, at either Gate Position")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const auto position = GENERATE (GatePosition::Pre, GatePosition::Post);
    CAPTURE (static_cast<int> (mode), static_cast<int> (position));
    auto settings = gated ({}, mode);
    settings.gatePosition = position;
    CHECK (test::impulseDelaySamples (settings, 48000.0) == latencySamples (mode, 48000.0));
}

TEST_CASE ("With its Detection Level 20 dB under Gate Threshold the Gate lowers the input by Gate Range; above it, it passes bit for bit")
{
    const double rangeDb = GENERATE (6.0, 20.0, 60.0);
    CAPTURE (rangeDb);
    const Settings::Gate gate { .rangeDb = rangeDb };
    // Settled: the level hold, Gate Hold and 14 Gate Release time constants leave under 1e-4 dB of Gate Range to go.
    const double quietDb = gate.thresholdDb - 20.0;
    CHECK (std::abs (test::steadyOutputDb (gated (gate), quietDb, 48000.0, 2, 1.5) - (quietDb - rangeDb)) <= 0.001);
    const auto loud = test::steadyLevel (2, 4800, gate.thresholdDb + 20.0);
    CHECK (test::played (gated (gate), 48000.0, loud) == loud);
}

TEST_CASE ("With Gate Position Post the Dry path isn't gated: at Dry 0 dB and Wet Off, the Gate closed, the output is the delayed input")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const Settings::Gate gate {};
    auto settings = gated (gate, mode);
    settings.gatePosition = GatePosition::Post;
    settings.dryDb = 0.0;
    settings.wetDb = off;
    const auto input = test::steadyLevel (2, 72000, gate.thresholdDb - stepDb);
    const auto outputDb = tailLevelsDb (test::played (settings, 48000.0, input), 480);
    const auto expectedDb = tailLevelsDb (test::delayed (input, latencySamples (mode, 48000.0)), 480);
    for (std::size_t ch = 0; ch < 2; ++ch)
        CHECK (std::abs (outputDb[ch] - expectedDb[ch]) <= 0.001);
    // On the Wet path alone, the same input is lowered by Gate Range: the Gate is on, and closed.
    settings.dryDb = off;
    settings.wetDb = 0.0;
    const auto wetDb = tailLevelsDb (test::played (settings, 48000.0, input), 480);
    for (std::size_t ch = 0; ch < 2; ++ch)
        CHECK (std::abs (wetDb[ch] - (expectedDb[ch] - gate.rangeDb)) <= 0.001);
}

TEST_CASE ("With Gate Position Post and Internal the Gate hears the compressed signal: a level the compressor pulls under Gate Threshold closes it")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    // A steady -10 dB, 10 dB over Gate Threshold, held at Threshold -30 dB by Ratio ∞:1 with a hard Knee.
    const Settings::Gate gate { .on = true, .thresholdDb = -20.0 };
    Settings settings { .gate = gate, .thresholdDb = -30.0, .ratio = parameterRanges::inf, .kneeDb = 0.0, .processingMode = mode };
    const auto input = test::steadyLevel (2, 96000, -10.0);
    // At Pre it hears the input, and stays open: the output is the compressor's alone.
    const auto preDb = tailLevelsDb (test::played (settings, 48000.0, input), 480);
    // At Post it hears -30 dB, 10 dB under Gate Threshold, past the closing level, and closes by Gate Range.
    settings.gatePosition = GatePosition::Post;
    const auto postDb = tailLevelsDb (test::played (settings, 48000.0, input), 480);
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        CHECK (std::abs (preDb[ch] - -30.0) <= 0.01);
        CHECK (std::abs (postDb[ch] - (-30.0 - gate.rangeDb)) <= 0.01);
    }
}

TEST_CASE ("With Gate Position Post and External, the Sidechain opens and closes the Gate whatever the main's level")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const Settings::Gate gate {};
    auto settings = gated (gate, mode);
    settings.gatePosition = GatePosition::Post;
    settings.detectionSource = DetectionSource::External;
    auto gateOff = settings;
    gateOff.gate.on = false;
    const auto quiet = test::steadyLevel (2, 72000, gate.thresholdDb - stepDb);
    const auto loud = test::steadyLevel (2, 72000, gate.thresholdDb + stepDb);
    // A quiet main under a loud Sidechain stays open, bit for bit.
    CHECK (test::played (settings, 48000.0, quiet, { 512 }, &loud) == test::played (gateOff, 48000.0, quiet, { 512 }, &loud));
    // A loud main under a quiet Sidechain is closed by Gate Range.
    for (const double levelDb : tailLevelsDb (test::played (settings, 48000.0, loud, { 512 }, &quiet), 480))
        CHECK (std::abs (levelDb - (gate.thresholdDb + stepDb - gate.rangeDb)) <= 0.001);
}

TEST_CASE ("In High Quality the Gate stays aligned with the audio it gates: its output is Zero Latency's, delayed, at each Gate Position and Detection Source, with Lookahead")
{
    const auto position = GENERATE (GatePosition::Pre, GatePosition::Post);
    const auto source = GENERATE (DetectionSource::Internal, DetectionSource::External);
    CAPTURE (static_cast<int> (position), static_cast<int> (source));
    // Bursts on the main and, half a period out, on the Sidechain, which the Gate opens and closes on, with quick
    // times, so a Gate a few samples early or late opens and closes on other samples.
    const double sampleRate = 48000.0;
    const auto input = test::bursts (2, 48000, sampleRate);
    const auto sidechain = test::bursts (2, 48000, sampleRate, 2, test::burstPeriodSeconds / 2.0);
    auto settings = gated ({ .thresholdDb = -20.0, .rangeDb = 40.0, .attackMs = 0.05, .releaseMs = 5.0, .holdMs = 0.0 });
    settings.gatePosition = position;
    settings.detectionSource = source;
    settings.lookaheadMs = 5.0;
    const auto zeroLatencyOutput = test::played (settings, sampleRate, input, { 512 }, &sidechain);
    settings.processingMode = highQuality;
    const auto highQualityOutput = test::played (settings, sampleRate, input, { 512 }, &sidechain);
    const int extra = latencySamples (highQuality, sampleRate) - latencySamples (zeroLatency, sampleRate);
    const auto expected = test::delayed (zeroLatencyOutput, extra);
    // From the second burst period on: at Post in High Quality the Gate first hears the delays' silence and closes,
    // where in Zero Latency it starts open on the first burst; the quiet part after it closes both alike.
    const auto from = static_cast<std::size_t> (test::burstPeriodSeconds * sampleRate) + static_cast<std::size_t> (extra);
    double worst = 0.0;
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = from; i < expected[ch].size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (highQualityOutput[ch][i] - expected[ch][i])));
    CHECK (worst <= 1.0e-6);
}
