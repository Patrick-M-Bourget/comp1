#include "Measure.h"

#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <plugkit/Placement.h>

#include <vector>

using namespace comp1;

namespace
{

constexpr int numSamples = 8192;

// Settings that compress noise hard, so the compressed signal and the detection signal differ throughout.
Settings compressing (DetectionSource source, bool audition)
{
    return { .thresholdDb = -30.0, .ratio = 8.0, .detectionSource = source, .detectionAudition = audition };
}

// The output of an Engine given settings from the start, so Detection Audition is held from the first sample with no
// fade, for main and the Sidechain samples given (none when null).
test::Channels play (const Settings& settings, const test::Channels& main, const test::Channels* sidechainSamples = nullptr)
{
    return test::played (settings, 48000.0, main, { 512 }, sidechainSamples);
}

} // namespace

TEST_CASE ("With Detection Audition held and Internal, the output is the main input")
{
    const int channels = GENERATE (1, 2);
    CAPTURE (channels);
    const auto input = test::noise (channels, numSamples);
    REQUIRE (play (compressing (DetectionSource::Internal, false), input) != input); // compressed, when not held
    CHECK (play (compressing (DetectionSource::Internal, true), input) == input);
}

TEST_CASE ("With Detection Audition held and External, the output is the Sidechain, on every output channel")
{
    const int mainChannels = GENERATE (1, 2);
    const int sidechainChannels = GENERATE (1, 2);
    CAPTURE (mainChannels, sidechainChannels);
    const auto input = test::noise (mainChannels, numSamples);
    const auto sidechainSamples = test::noise (sidechainChannels, numSamples, 5);
    const auto output = play (compressing (DetectionSource::External, true), input, &sidechainSamples);

    // A stereo detection signal plays each channel on its own output channel, a mono one on every output channel,
    // and a stereo one on a mono output as the average of its two channels.
    test::Channels expected (static_cast<std::size_t> (mainChannels), std::vector<float> (numSamples));
    for (std::size_t ch = 0; ch < expected.size(); ++ch)
        for (std::size_t i = 0; i < numSamples; ++i)
        {
            if (sidechainChannels == 1)
                expected[ch][i] = sidechainSamples[0][i];
            else if (mainChannels == 2)
                expected[ch][i] = sidechainSamples[ch][i];
            else
                expected[ch][i] = static_cast<float> ((static_cast<double> (sidechainSamples[0][i]) + sidechainSamples[1][i]) / 2.0);
        }
    CHECK (output == expected);
}

TEST_CASE ("With Detection Audition held under Stereo Placement Mid or Side, the output is that part, placed as it plays back")
{
    const auto placement = GENERATE (StereoPlacement::Mid, StereoPlacement::Side);
    const auto source = GENERATE (DetectionSource::Internal, DetectionSource::External);
    const int sidechainChannels = GENERATE (1, 2);
    CAPTURE (placement, source, sidechainChannels);
    const auto input = test::noise (2, numSamples);
    const auto sidechainSamples = test::noise (sidechainChannels, numSamples, 5);
    auto settings = compressing (source, true);
    settings.stereoPlacement = placement;
    const auto output = play (settings, input, &sidechainSamples);

    // The detection signal's Mid or Side, as plugkit encodes it, or a mono one itself: the Mid on both channels, the
    // Side on the left and, inverted, on the right.
    const auto& detected = source == DetectionSource::Internal ? input : sidechainSamples;
    test::Channels expected (2, std::vector<float> (numSamples));
    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float left = detected[0][i], right = detected.back()[i];
        const bool mono = detected.size() == 1;
        const float part = mono ? left : placement == StereoPlacement::Mid ? plugkit::encodeMid (left, right) : plugkit::encodeSide (left, right);
        expected[0][i] = part;
        expected[1][i] = placement == StereoPlacement::Mid ? part : -part;
    }
    CHECK (output == expected);
}

TEST_CASE ("With Detection Audition held, External and no Sidechain, the output is silence")
{
    const auto output = play (compressing (DetectionSource::External, true), test::noise (2, numSamples));
    CHECK (output == test::Channels (2, std::vector<float> (numSamples, 0.0f)));
}

TEST_CASE ("In High Quality, Detection Audition is delayed by the reported latency, as the compressed signal is")
{
    const double sampleRate = GENERATE (44100.0, 96000.0);
    CAPTURE (sampleRate);
    auto settings = compressing (DetectionSource::Internal, true);
    settings.processingMode = ProcessingMode::HighQuality;
    settings.lookaheadMs = 2.0;
    const auto input = test::noise (2, numSamples);
    auto output = input;
    Engine engine;
    engine.prepare (sampleRate, 512, 2);
    engine.setSettings (settings);
    test::processInBlocks (engine, output, { 512 });
    CHECK (output == test::delayed (input, latencySamples (ProcessingMode::HighQuality, sampleRate)));
}

TEST_CASE ("Output Gain applies to Detection Audition")
{
    auto settings = compressing (DetectionSource::Internal, true);
    settings.outputGainDb = -6.0;
    const auto input = test::noise (1, numSamples);
    const auto output = play (settings, input);
    for (std::size_t i = 0; i < numSamples; ++i)
        REQUIRE (output[0][i] == static_cast<float> (test::minus6DbGain * input[0][i]));
}

TEST_CASE ("The detector runs on while Detection Audition is held, so letting go doesn't restart compression")
{
    // One Engine holds Detection Audition for the first half second, the other never does. Once the fade out has
    // settled, the two play the same, bit for bit.
    const auto input = test::noise (2, 48000, 11);
    auto held = input, neverHeld = input;
    for (auto* audio : { &held, &neverHeld })
    {
        Engine engine;
        engine.prepare (48000.0, 512, 2);
        auto settings = compressing (DetectionSource::Internal, audio == &held);
        engine.setSettings (settings);
        auto first = test::Channels { { (*audio)[0].begin(), (*audio)[0].begin() + 24000 }, { (*audio)[1].begin(), (*audio)[1].begin() + 24000 } };
        auto second = test::Channels { { (*audio)[0].begin() + 24000, (*audio)[0].end() }, { (*audio)[1].begin() + 24000, (*audio)[1].end() } };
        test::processInBlocks (engine, first, { 512 });
        settings.detectionAudition = false;
        engine.setSettings (settings);
        test::processInBlocks (engine, second, { 512 });
        for (std::size_t ch = 0; ch < 2; ++ch)
            (*audio)[ch].assign (second[ch].begin() + 9600, second[ch].end()); // 200 ms on: the fade out has settled
    }
    CHECK (held == neverHeld);
}
