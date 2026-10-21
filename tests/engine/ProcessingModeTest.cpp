#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/test/AllocationGuard.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;

// audio through a fresh Engine at sampleRate with settings, cut as given; then, from sample switchAt on, with later.
test::Channels play (const test::Channels& audio, double sampleRate, const std::vector<int>& cut, const Settings& settings,
                     const Settings& later, int switchAt)
{
    return test::played (settings, sampleRate, audio, cut, nullptr, test::SettingsChange { .at = switchAt, .settings = later });
}

test::Channels play (const test::Channels& audio, double sampleRate, const std::vector<int>& cut, const Settings& settings)
{
    return test::played (settings, sampleRate, audio, cut);
}

} // namespace

TEST_CASE ("An impulse comes out delayed by the latency each Processing Mode reports, at every sample rate")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (static_cast<int> (mode), sampleRate);
    const Settings settings { .ratio = 1.0, .processingMode = mode };
    CHECK (test::impulseDelaySamples (settings, sampleRate) == latencySamples (mode, sampleRate));
}

TEST_CASE ("High Quality at Ratio 1:1 is the input delayed by the reported latency, bit for bit, at any Lookahead", "[sweep]")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    const double lookaheadMs = GENERATE (0.0, 2.5, 5.0);
    CAPTURE (sampleRate, cut, lookaheadMs);
    const auto input = test::noise (2, 6000);
    const Settings settings { .ratio = 1.0, .processingMode = highQuality, .lookaheadMs = lookaheadMs };
    CHECK (play (input, sampleRate, cut, settings) == test::delayed (input, latencySamples (highQuality, sampleRate)));

    // Lookahead jumping to the other end of its range, part-way through a run, moves nothing the listener hears.
    auto jumped = settings;
    jumped.lookaheadMs = lookaheadMs < 2.5 ? 5.0 : 0.0;
    CHECK (play (input, sampleRate, cut, settings, jumped, 2003) == test::delayed (input, latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("High Quality at Lookahead 0 is Zero Latency's output delayed by the reported latency, bit for bit")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    // Compressed throughout, with Hold, Auto Gain and Output Gain all at work.
    const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0, .holdMs = 5.0, .autoGain = true, .outputGainDb = -3.0 };
    auto highQualitySettings = compressing;
    highQualitySettings.processingMode = highQuality;
    const auto input = test::noise (2, 6000);
    const auto zeroLatencyOutput = play (input, sampleRate, cut, compressing);
    CHECK (play (input, sampleRate, cut, highQualitySettings) == test::delayed (zeroLatencyOutput, latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("High Quality at Lookahead 0 is Zero Latency's output delayed by the reported latency with Auto Release on, bit for bit")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    // Auto Release moves on a run at a time, on the run grid of what the detector hears, which High Quality delays.
    const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0, .releaseMs = 20.0, .autoRelease = true };
    auto highQualitySettings = compressing;
    highQualitySettings.processingMode = highQuality;
    const auto input = test::noise (2, 6000);
    const auto zeroLatencyOutput = play (input, sampleRate, cut, compressing);
    CHECK (play (input, sampleRate, cut, highQualitySettings) == test::delayed (zeroLatencyOutput, latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("On FET, High Quality at Lookahead 0 is Zero Latency's output delayed by the reported latency, bit for bit", "[sweep]")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    // FET's loop reads the detection signal times the gain of the same sample, which in High Quality is the input as
    // delayed for the detector: at Lookahead 0, the delayed audio itself. Compressed throughout, with Colour, Stereo
    // Link and Hold at work.
    const Settings compressing { .character = Character::FET, .thresholdDb = -30.0, .attackMs = 0.1, .holdMs = 5.0, .stereoLink = 50.0 };
    auto highQualitySettings = compressing;
    highQualitySettings.processingMode = highQuality;
    const auto input = test::noise (2, 4000);
    const auto zeroLatencyOutput = play (input, sampleRate, cut, compressing);
    CHECK (zeroLatencyOutput != input);
    CHECK (play (input, sampleRate, cut, highQualitySettings) == test::delayed (zeroLatencyOutput, latencySamples (highQuality, sampleRate)));
}

namespace
{

// A compressor setting that shapes what the detector hears, or how its Gain Reduction is applied.
struct Scene
{
    std::string name;
    Settings settings;
};

const std::vector<Scene>& detectionScenes()
{
    static const std::vector<Scene> all = [] {
        const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0, .releaseMs = 20.0 };
        std::vector<Scene> scenes;
        const auto add = [&] (std::string name, auto change) {
            auto settings = compressing;
            change (settings);
            scenes.push_back ({ std::move (name), settings });
        };
        add ("Detection EQ", [] (Settings& s) {
            s.detectionEq.highPass.on = true;
            s.detectionEq.highPass.filter.frequency = 400.0;
            s.detectionEq.highPass.filter.slope = 24.0;
            s.detectionEq.freeBand.on = true;
            s.detectionEq.freeBand.filter.gain = 12.0;
            s.detectionEq.freeBand.filter.frequency = 3000.0;
        });
        add ("Stereo Link 50%", [] (Settings& s) { s.stereoLink = 50.0; });
        add ("Stereo Placement Mid", [] (Settings& s) { s.stereoPlacement = StereoPlacement::Mid; });
        add ("Stereo Placement Side", [] (Settings& s) { s.stereoPlacement = StereoPlacement::Side; });
        add ("Auto Release", [] (Settings& s) { s.autoRelease = true; });
        return scenes;
    }();
    return all;
}

} // namespace

TEST_CASE ("High Quality at any Lookahead is Zero Latency fed the input Lookahead early as its Sidechain, delayed by the latency less Lookahead, bit for bit", "[sweep]")
{
    // In High Quality the compressor meets the audio delayed by the Lookahead budget and the detection signal by
    // Lookahead less: as Zero Latency does the audio delayed by Lookahead with the input itself on the Sidechain. The
    // two outputs then differ only by the rest of the latency.
    const auto& scene = GENERATE (from_range (detectionScenes()));
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const double lookaheadMs = GENERATE (0.0, 2.0, 5.0);
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (scene.name, sampleRate, lookaheadMs, cut);
    const auto input = test::noise (2, 6000);
    const int lookahead = static_cast<int> (std::lround (lookaheadMs * sampleRate / 1000.0));

    auto external = scene.settings;
    external.detectionSource = DetectionSource::External;
    const auto zeroLatencyOutput = test::played (external, sampleRate, test::delayed (input, lookahead), cut, &input);
    auto highQualitySettings = scene.settings;
    highQualitySettings.processingMode = highQuality;
    highQualitySettings.lookaheadMs = lookaheadMs;
    CHECK (play (input, sampleRate, cut, highQualitySettings) == test::delayed (zeroLatencyOutput, latencySamples (highQuality, sampleRate) - lookahead));
}

TEST_CASE ("In Zero Latency, Lookahead changes nothing, at every sample rate and block size", "[sweep]")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0 };
    auto looking = compressing;
    looking.lookaheadMs = 5.0;
    const auto input = test::noise (2, 6000);
    const auto without = play (input, sampleRate, cut, compressing);
    CHECK (play (input, sampleRate, cut, looking) == without);
    CHECK (play (input, sampleRate, cut, compressing, looking, 2003) == without);
}

namespace
{
// dyn-lab's MODE_FADE_MS (python3 tools/dyn-lab/dynlab.py latency): a switch's fade-out, and its fade-in, each this long.
constexpr double modeFadeMs = 10.0;
} // namespace

TEST_CASE ("A Processing Mode switch falls to silence and rises again within the mode fade each way, the same at every block size")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto [from, to] = GENERATE (table<ProcessingMode, ProcessingMode> ({ { zeroLatency, highQuality }, { highQuality, zeroLatency } }));
    // Dry Off, the Wet path alone; and Dry and Wet both at 0 dB, so each path plays the input twice over. And the
    // Limiter on, the Wet path alone, at its Ceiling's top, which the noise never reaches: it changes nothing on either
    // path.
    const auto [dryDb, gain, limiter] = GENERATE (table<double, float, bool> ({ { -std::numeric_limits<double>::infinity(), 1.0f, false },
                                                                                { 0.0, 2.0f, false },
                                                                                { -std::numeric_limits<double>::infinity(), 1.0f, true } }));
    // 1x, and 8x, which plays each rate's cap.
    const auto oversampling = GENERATE (Oversampling::x1, Oversampling::x8);
    CAPTURE (sampleRate, static_cast<int> (to), dryDb, limiter, factorOf (oversampling));
    const int fade = static_cast<int> (std::lround (modeFadeMs * sampleRate / 1000.0));
    // Into High Quality, the silence lasts while its delays fill with the new path's audio, so the fade-in starts from
    // it: every one of them, its whole latency, as the Oversampling section ahead of the detection stage moves what the
    // Lookahead budget is fed.
    const int padding = to == highQuality ? latencySamples (highQuality, sampleRate) : 0;
    // Part-way through a run, once the old path has played long enough to fill its delays.
    const int switchAt = 4003;
    const auto input = test::noise (2, switchAt + 2 * fade + padding + 3000);
    Settings before { .ratio = 1.0, .limiter = limiter, .processingMode = from, .oversampling = oversampling };
    auto after = before;
    after.processingMode = to;
    before.dryDb = after.dryDb = dryDb;
    const auto reference = play (input, sampleRate, { 4096 }, before, after, switchAt);

    const auto scaled = [gain] (test::Channels audio) {
        for (auto& channel : audio)
            for (auto& sample : channel)
                sample *= gain;
        return audio;
    };
    const auto oldPath = scaled (test::delayed (input, latencySamples (from, sampleRate)));
    const auto newPath = scaled (test::delayed (input, latencySamples (to, sampleRate)));
    const auto at = [] (int i) { return static_cast<std::size_t> (i); };
    for (std::size_t ch = 0; ch < input.size(); ++ch)
    {
        CAPTURE (ch);
        const auto& out = reference[ch];
        for (int i = 0; i < switchAt; ++i)
            REQUIRE (out[at (i)] == oldPath[ch][at (i)]);
        // Fading out, never louder than the old path.
        for (int i = switchAt; i < switchAt + fade; ++i)
            REQUIRE (std::abs (out[at (i)]) <= std::abs (oldPath[ch][at (i)]));
        // Silent by the fade-out's last sample, and until the fade-in starts.
        for (int i = switchAt + fade - 1; i < switchAt + fade + padding; ++i)
            REQUIRE (out[at (i)] == 0.0f);
        // Fading in, never louder than the new path, which then plays as it is.
        for (int i = switchAt + fade + padding; i < switchAt + 2 * fade + padding; ++i)
            REQUIRE (std::abs (out[at (i)]) <= std::abs (newPath[ch][at (i)]));
        for (auto i = at (switchAt + 2 * fade + padding); i < out.size(); ++i)
            REQUIRE (out[i] == newPath[ch][i]);
    }

    for (const auto& cut : test::blockCuts())
    {
        CAPTURE (cut);
        CHECK (play (input, sampleRate, cut, before, after, switchAt) == reference);
    }
}

TEST_CASE ("The settings in force at prepare apply at once, with no mode fade")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const auto input = test::noise (2, 3000);
    CHECK (play (input, 48000.0, { 512 }, { .ratio = 1.0, .processingMode = mode }) == test::delayed (input, latencySamples (mode, 48000.0)));
}

TEST_CASE ("A non-finite input sample never reaches the output in High Quality, nor across a switch")
{
    const auto later = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (later));
    const auto input = test::noise (2, 8192);
    auto broken = input, silenced = input;
    for (const std::size_t i : { 100u, 2000u, 3001u, 5000u })
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            broken[ch][i] = i % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][i] = 0.0f;
        }
    // The Limiter on, at a Ceiling the noise passes, so it works through the switch too.
    const Settings highQualitySettings { .autoGain = true, .limiter = true, .ceilingDb = -12.0, .processingMode = highQuality, .lookaheadMs = 3.0 };
    auto laterSettings = highQualitySettings;
    laterSettings.processingMode = later;
    CHECK (play (broken, 48000.0, { 512 }, highQualitySettings, laterSettings, 2500)
           == play (silenced, 48000.0, { 512 }, highQualitySettings, laterSettings, 2500));
}

TEST_CASE ("The Engine does not allocate in either Processing Mode, nor across a switch")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (48000.0, blockSize, 2);
    auto audio = test::noise (2, blockSize);
    std::vector<float*> main { audio[0].data(), audio[1].data() };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // The mode switching every few blocks, some of the time mid-fade, Lookahead moving, the Limiter switching and
        // its Ceiling moving, and blocks cutting runs.
        engine.setSettings ({ .thresholdDb = -30.0,
                              .outputGainDb = 12.0,
                              .limiter = block % 5 != 2,
                              .ceilingDb = -3.0 * (block % 11),
                              .processingMode = (block / 3) % 2 == 0 ? zeroLatency : highQuality,
                              .lookaheadMs = 0.5 * (block % 11) });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main.data(), 2, count });
    }
    REQUIRE (guard.allocations() == 0);
}
