#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;

// The Limiter on at a Ceiling, behind Output Gain, with the compressor at 1:1 so the Limiter meets the input as
// Output Gain sends it.
Settings limiting (ProcessingMode mode, double ceilingDb, double outputGainDb = 0.0)
{
    return { .ratio = 1.0, .outputGainDb = outputGainDb, .limiter = true, .ceilingDb = ceilingDb, .processingMode = mode };
}

// The grid the Ceiling is promised over: Output Gain pushing the signals further over, and Ceilings from the top of
// its range to the bottom.
const std::vector<double>& outputGains()
{
    static const std::vector<double> all { 0.0, 12.0, 24.0, 36.0 };
    return all;
}

const std::vector<double>& ceilings()
{
    static const std::vector<double> all { 0.0, -6.0, -30.0 };
    return all;
}

// Each sample rate comp1 supports, with a block cut in turn: the cut is shown not to matter on its own (below).
struct Host
{
    double sampleRate;
    std::vector<int> cut;
};

const std::vector<Host>& hosts()
{
    static const std::vector<Host> all = [] {
        std::vector<Host> h;
        for (std::size_t r = 0; r < test::sampleRates().size(); ++r)
            h.push_back ({ test::sampleRates()[r], test::blockCuts()[r % test::blockCuts().size()] });
        return h;
    }();
    return all;
}

// signal on the left channel and, at half its level, on the right, long enough to play out the latency too.
test::Channels stereo (const std::vector<float>& signal)
{
    test::Channels audio { signal, signal };
    for (auto& sample : audio[1])
        sample *= 0.5f;
    return audio;
}

double samplePeak (const test::Channels& audio)
{
    double peak = 0.0;
    for (const auto& channel : audio)
        for (const float sample : channel)
            peak = std::max (peak, static_cast<double> (std::abs (sample)));
    return peak;
}

} // namespace

TEST_CASE ("Zero Latency's Limiter never lets a sample past the Ceiling, at any Output Gain, sample rate and block size")
{
    const auto& host = GENERATE (from_range (hosts()));
    const double ceilingDb = GENERATE (from_range (ceilings()));
    const double outputGainDb = GENERATE (from_range (outputGains()));
    CAPTURE (host.sampleRate, host.cut, ceilingDb, outputGainDb);
    std::vector<std::pair<std::string, std::vector<float>>> signals;
    for (const auto& signal : test::limiterSignals())
        signals.push_back ({ signal.name, signal.make (ceilingDb, host.sampleRate, test::limiterSignalSamples) });
    // White noise too: the sample peak is promised whatever the signal's spectrum.
    signals.push_back ({ "seeded noise", test::noise (1, test::limiterSignalSamples).front() });
    for (const auto& [name, signal] : signals)
    {
        CAPTURE (name);
        const auto out = test::played (limiting (zeroLatency, ceilingDb, outputGainDb), host.sampleRate, stereo (signal), host.cut);
        CHECK (samplePeak (out) <= test::gainOf (ceilingDb));
    }
}

TEST_CASE ("High Quality's Limiter never lets the true peak past the Ceiling, at any Output Gain, sample rate and block size")
{
    const auto& host = GENERATE (from_range (hosts()));
    const double ceilingDb = GENERATE (from_range (ceilings()));
    const double outputGainDb = GENERATE (from_range (outputGains()));
    CAPTURE (host.sampleRate, host.cut, ceilingDb, outputGainDb);
    // Long enough for the whole signal to come out after the latency.
    const int length = test::limiterSignalSamples + latencySamples (highQuality, host.sampleRate);
    for (const auto& signal : test::limiterSignals())
    {
        CAPTURE (signal.name);
        const auto input = stereo (signal.make (ceilingDb, host.sampleRate, length));
        const auto out = test::played (limiting (highQuality, ceilingDb, outputGainDb), host.sampleRate, input, host.cut);
        CHECK (test::truePeak (out) <= test::gainOf (ceilingDb));
    }
}

TEST_CASE ("Zero Latency's Limiter alone lets the true peak past the Ceiling, between samples")
{
    // Why High Quality estimates the true peak: a 19 kHz tone held to the Ceiling sample by sample peaks over it
    // between them (python3 tools/dyn-lab/dynlab.py limiter: 2.1 dB over at 44.1 kHz).
    const auto out = test::played (limiting (zeroLatency, 0.0), 44100.0, stereo (test::limiterTone (19000.0, 12.0, 44100.0, 4096)));
    CHECK (test::truePeak (out) > test::gainOf (1.0));
}

TEST_CASE ("An impulse comes out at the reported latency with the Limiter on and off, in each Processing Mode")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const bool on = GENERATE (false, true);
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (static_cast<int> (mode), on, sampleRate);
    // The impulse, 0.5, is over the Ceiling, so on, the Limiter takes it down.
    const Settings settings { .ratio = 1.0, .limiter = on, .ceilingDb = -12.0, .processingMode = mode };
    CHECK (test::impulseDelaySamples (settings, sampleRate) == latencySamples (mode, sampleRate));
}

TEST_CASE ("With the Limiter off, the output is its input, delayed in High Quality, bit for bit, whatever the Ceiling")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const auto& host = GENERATE (from_range (hosts()));
    CAPTURE (static_cast<int> (mode), host.sampleRate, host.cut);
    // Noise pushed 12 dB, far over the Ceiling: the Limiter, running, would take it down.
    const auto input = test::noise (2, 6000);
    const Settings off { .ratio = 1.0, .outputGainDb = 12.0, .ceilingDb = -30.0, .processingMode = mode };
    const auto out = test::played (off, host.sampleRate, input, host.cut);
    auto expected = test::delayed (input, latencySamples (mode, host.sampleRate));
    for (auto& channel : expected)
        for (auto& sample : channel)
            sample = static_cast<float> (test::gainOf (12.0) * sample);
    CHECK (out == expected);
    CHECK (samplePeak (out) > 1.0);
}

TEST_CASE ("Below the Ceiling, once the release has settled, the Limiter on is bit for bit the Limiter off")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const auto& host = GENERATE (from_range (hosts()));
    CAPTURE (static_cast<int> (mode), host.sampleRate, host.cut);
    // A tone that never reaches the Ceiling.
    const auto quiet = stereo (test::limiterTone (1000.0, -7.0, host.sampleRate, 6000));
    auto on = limiting (mode, -6.0), off = on;
    off.limiter = false;
    CHECK (test::played (on, host.sampleRate, quiet, host.cut) == test::played (off, host.sampleRate, quiet, host.cut));

    // 50 ms 12 dB over it, then the quiet tone: once the release has given back all of the Gain Reduction, down to
    // dyn-lab's LIMITER_SETTLED_DB (from 12 dB, 200 ms × ln 1.2e8: 3.7 s), the two match again.
    const int loud = static_cast<int> (0.05 * host.sampleRate), settle = static_cast<int> (4.0 * host.sampleRate);
    auto audio = stereo (test::limiterTone (1000.0, 6.0, host.sampleRate, loud));
    const auto after = stereo (test::limiterTone (1000.0, -7.0, host.sampleRate, settle + 4000));
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
        audio[ch].insert (audio[ch].end(), after[ch].begin(), after[ch].end());
    const auto limited = test::played (on, host.sampleRate, audio, host.cut), plain = test::played (off, host.sampleRate, audio, host.cut);
    const auto from = static_cast<std::ptrdiff_t> (loud + settle);
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
    {
        CHECK (std::vector<float> (limited[ch].begin() + from, limited[ch].end()) == std::vector<float> (plain[ch].begin() + from, plain[ch].end()));
        CHECK (limited[ch] != plain[ch]); // the loud part was limited
    }
}

TEST_CASE ("The Limiter takes one gain on every channel, from the loudest")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const double sampleRate = 48000.0;
    // The left 12 dB over the Ceiling, the right at half its level: both take the left's Gain Reduction.
    const auto input = stereo (test::limiterTone (1000.0, 6.0, sampleRate, 8000));
    const auto out = test::played (limiting (mode, -6.0), sampleRate, input);
    const auto latency = static_cast<std::size_t> (latencySamples (mode, sampleRate));
    int compared = 0;
    for (std::size_t i = 4000; i < out[0].size(); ++i)
    {
        const std::size_t heard = i - latency;
        if (std::abs (input[0][heard]) < 0.1f)
            continue;
        const double left = out[0][i] / static_cast<double> (input[0][heard]), right = out[1][i] / static_cast<double> (input[1][heard]);
        REQUIRE (std::abs (left - right) <= 1.0e-6 * left);
        REQUIRE (left < test::gainOf (-6.0));
        ++compared;
    }
    CHECK (compared > 1000);
}

namespace
{

// dyn-lab's timing signal (timing_signal): a quiet level, alternating samples at -40 dBFS, for 256 samples; the
// full-scale burst, 1 kHz from its peak, for 50 ms; then the quiet level for 600 ms. With the burst's first sample
// and the first after it.
struct TimingSignal
{
    std::vector<float> samples;
    int start = 0, end = 0;
};

TimingSignal timingSignal (double sampleRate)
{
    TimingSignal t;
    t.start = test::burstStart;
    t.end = t.start + static_cast<int> (std::lround (0.05 * sampleRate));
    const int length = t.end + static_cast<int> (std::lround (0.6 * sampleRate));
    const auto quiet = static_cast<float> (test::gainOf (-40.0));
    for (int n = 0; n < length; ++n)
        t.samples.push_back (n < t.start || n >= t.end ? (n % 2 == 0 ? quiet : -quiet)
                                                       : static_cast<float> (std::cos (2.0 * std::numbers::pi * 1000.0 * (n - t.start) / sampleRate)));
    return t;
}

// The figures of python3 tools/dyn-lab/dynlab.py limiter's timing table, at its Ceiling of -12 dBFS: the release's
// 63% and 90% in ms, counted from the first quiet sample heard after the burst.
struct TimingRow
{
    double sampleRate;
    ProcessingMode mode;
    double release63, release90;
};

const std::vector<TimingRow>& timingTable()
{
    static const std::vector<TimingRow> rows {
        { 44100, zeroLatency, 200.0227, 460.5215 }, { 44100, highQuality, 200.4308, 460.9524 },
        { 48000, zeroLatency, 200.0208, 460.5208 }, { 48000, highQuality, 200.4375, 460.9375 },
        { 96000, zeroLatency, 200.0000, 460.5208 }, { 96000, highQuality, 200.4688, 460.9792 },
    };
    return rows;
}

} // namespace

TEST_CASE ("The Limiter's gain is fully down when a full-scale burst arrives, and releases as dyn-lab's does")
{
    const auto& row = GENERATE (from_range (timingTable()));
    CAPTURE (row.sampleRate, static_cast<int> (row.mode));
    const auto signal = timingSignal (row.sampleRate);
    const auto out = test::played (limiting (row.mode, -12.0), row.sampleRate, { signal.samples });
    // The Gain Reduction heard on each sample: against the input sample it carries, the latency earlier.
    const auto latency = static_cast<std::ptrdiff_t> (latencySamples (row.mode, row.sampleRate));
    const std::vector<float> carried (signal.samples.begin(), signal.samples.end() - latency);
    const auto heard = test::gainReductionDb (carried, { out[0].begin() + latency, out[0].end() });
    const auto at = [] (int i) { return static_cast<std::size_t> (i); };

    // Fully down on the burst's first sample: as much as the burst ever asks for, at least the 12 dB it is over.
    const double most = *std::max_element (heard.begin() + signal.start, heard.begin() + signal.end);
    CHECK (heard[at (signal.start)] >= most - 1.0e-4);
    CHECK (most >= 12.0 - 1.0e-4);

    const auto c = test::crossings (heard, heard[at (signal.end - 1)], 0.0, at (signal.end));
    REQUIRE (c.sixtyThree);
    REQUIRE (c.ninety);
    const double ms = 1000.0 / row.sampleRate;
    CHECK (std::abs (*c.sixtyThree * ms - row.release63) <= ms);
    CHECK (std::abs (*c.ninety * ms - row.release90) <= ms);
}

TEST_CASE ("With the Limiter on, the output is the same however the audio is cut into blocks")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const auto& cut = GENERATE (from_range (test::blockCuts()));
    CAPTURE (static_cast<int> (mode), cut);
    const auto input = test::noise (2, 20000);
    const auto settings = limiting (mode, -12.0, 6.0);
    CHECK (test::played (settings, 44100.0, input, cut) == test::played (settings, 44100.0, input, { 4096 }));
}

TEST_CASE ("Back in High Quality, the Limiter doesn't hear what the path played before it was left")
{
    // High Quality limiting loud noise, a switch to Zero Latency while it is still loud, then quiet, and a switch back:
    // the delays High Quality left behind still hold its loud audio, which the mode fade keeps in silence. Once the
    // fade-in is over, the quiet audio plays as it is, delayed, with no Gain Reduction left from the loud.
    const double sampleRate = 48000.0;
    constexpr int toZeroLatency = 4000, quietFrom = 6000, toHighQuality = 9000, length = 14000;
    auto input = test::noise (2, length);
    for (std::size_t ch = 0; ch < input.size(); ++ch)
        for (std::size_t i = 0; i < input[ch].size(); ++i)
            input[ch][i] *= i < quietFrom ? 4.0f : 0.25f; // 12 dB over a -12 dBFS Ceiling, then under it
    auto settings = limiting (highQuality, -12.0);
    Engine engine;
    engine.prepare (sampleRate, 512, 2);
    auto out = input;
    std::vector<float*> channels (2);
    for (int start = 0; start < length; start += 100)
    {
        settings.processingMode = start >= toZeroLatency && start < toHighQuality ? zeroLatency : highQuality;
        engine.setSettings (settings);
        for (std::size_t ch = 0; ch < 2; ++ch)
            channels[ch] = out[ch].data() + start;
        engine.process ({ channels.data(), 2, 100 });
    }
    const int latency = latencySamples (highQuality, sampleRate);
    const auto expected = test::delayed (input, latency);
    // The fade-out, the silence while High Quality's delays after the compressor fill, and the fade-in.
    const int settled = toHighQuality + 2 * 480 + latency - lookaheadBudgetSamples (sampleRate);
    for (std::size_t ch = 0; ch < 2; ++ch)
        CHECK (std::vector<float> (out[ch].begin() + settled, out[ch].end()) == std::vector<float> (expected[ch].begin() + settled, expected[ch].end()));
}

namespace
{

// What each mode promises of a peak: the sample peak in Zero Latency, the true peak in High Quality, from sample from
// on, the audio before it heard.
double promisedPeak (ProcessingMode mode, const test::Channels& audio, int from = 0)
{
    if (mode == highQuality)
        return test::truePeak (audio, from);
    double peak = 0.0;
    for (const auto& channel : audio)
        for (auto i = static_cast<std::size_t> (from); i < channel.size(); ++i)
            peak = std::max (peak, static_cast<double> (std::abs (channel[i])));
    return peak;
}

} // namespace

TEST_CASE ("While the Ceiling glides, no peak passes the higher of the two Ceilings, and once settled the new one holds")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const auto [fromDb, toDb] = GENERATE (table<double, double> ({ { 0.0, -30.0 }, { -30.0, 0.0 }, { -6.0, -12.0 } }));
    const double sampleRate = GENERATE (44100.0, 192000.0);
    CAPTURE (static_cast<int> (mode), fromDb, toDb, sampleRate);
    // Noise 12 dB over the higher Ceiling, limited throughout.
    const auto input = stereo (test::bandLimitedNoise (12.0, sampleRate, static_cast<int> (1.0 * sampleRate)));
    const int at = static_cast<int> (0.1 * sampleRate);
    const test::SettingsChange jumped { .at = at, .settings = limiting (mode, toDb) };
    const auto out = test::played (limiting (mode, fromDb), sampleRate, input, { 512 }, nullptr, jumped);
    CHECK (promisedPeak (mode, out) <= test::gainOf (std::max (fromDb, toDb)));
    // The Ceiling's glide settles within 0.7 s (24 of its time constants, 28 ms); then, past the latency, the audio
    // heard came in after it.
    const int settled = at + static_cast<int> (0.7 * sampleRate) + latencySamples (mode, sampleRate);
    CHECK (promisedPeak (mode, out, settled) <= test::gainOf (toDb));
    CHECK (promisedPeak (mode, out, settled) > test::gainOf (toDb - 1.0));
}

TEST_CASE ("Once the Limiter has switched on, the Ceiling holds")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (44100.0, 192000.0);
    CAPTURE (static_cast<int> (mode), sampleRate);
    const auto input = stereo (test::bandLimitedNoise (6.0, sampleRate, static_cast<int> (0.5 * sampleRate)));
    auto off = limiting (mode, -6.0);
    off.limiter = false;
    const int at = static_cast<int> (0.1 * sampleRate);
    const auto out = test::played (off, sampleRate, input, { 512 }, nullptr, test::SettingsChange { .at = at, .settings = limiting (mode, -6.0) });
    // Over the Ceiling before the switch; the switch's crossfade settles within 0.1 s.
    CHECK (promisedPeak (mode, out) > test::gainOf (0.0));
    CHECK (promisedPeak (mode, out, at + static_cast<int> (0.1 * sampleRate) + latencySamples (mode, sampleRate)) <= test::gainOf (-6.0));
}
