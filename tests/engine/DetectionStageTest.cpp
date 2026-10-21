#include "Detection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <plugkit/Placement.h>

#include <vector>

using namespace comp1;

// The detection stage's contract with Detection Source Internal: one detection channel per main channel, each main
// channel following its own, carrying the Internal signal sample for sample. The Sidechain changes nothing.
TEST_CASE ("With Internal, each main channel follows a detection channel of its own, carrying the Internal signal")
{
    const int numChannels = GENERATE (1, 2);
    const bool withSidechain = GENERATE (false, true);
    CAPTURE (numChannels, withSidechain);
    constexpr int numSamples = 16;

    std::vector<std::vector<float>> internal, sidechainSamples (2, std::vector<float> (numSamples, 1.0f));
    for (int ch = 0; ch < numChannels; ++ch)
    {
        internal.emplace_back();
        for (int i = 0; i < numSamples; ++i)
            internal.back().push_back (0.01f * static_cast<float> ((ch + 1) * (i + 1)));
    }
    std::vector<const float*> internalChannels, sidechainChannels;
    for (const auto& channel : internal)
        internalChannels.push_back (channel.data());
    for (const auto& channel : sidechainSamples)
        sidechainChannels.push_back (channel.data());
    const ConstAudioBlock sidechain { sidechainChannels.data(), 2, numSamples };

    DetectionStage stage;
    stage.prepare (numChannels, numSamples);
    const auto detection = stage.process ({ internalChannels.data(), numChannels, numSamples }, withSidechain ? &sidechain : nullptr);

    REQUIRE (detection.numChannels == numChannels);
    REQUIRE (detection.numSamples == numSamples);
    const auto expected = internal;
    // The detection signal is the stage's own: changing the audio afterwards, as the compressor does when it applies
    // gain, leaves it as it was.
    for (auto& channel : internal)
        for (auto& sample : channel)
            sample = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        CAPTURE (ch);
        CHECK (detection.channelFollowedBy (ch) == detection.channels[ch]);
        const float* followed = detection.channelFollowedBy (ch);
        CHECK (std::vector<float> (followed, followed + numSamples) == expected[static_cast<std::size_t> (ch)]);
    }
}

TEST_CASE ("With External, a mono main follows the louder of a stereo Sidechain's channels at each sample, and the detection signal is the Sidechain's")
{
    const std::vector<float> left { 0.5f, -0.1f, 0.0f, -0.9f }, right { -0.2f, 0.3f, -0.4f, 0.8f }, mainSamples (4, 0.25f);
    const float* sidechainChannels[] = { left.data(), right.data() };
    const float* internalChannels[] = { mainSamples.data() };
    const ConstAudioBlock sidechain { sidechainChannels, 2, 4 };

    DetectionStage stage;
    stage.prepare (1, 4);
    stage.setSource (DetectionSource::External);
    const auto detection = stage.process ({ internalChannels, 1, 4 }, &sidechain);

    REQUIRE (detection.numChannels == 2);
    CHECK (std::vector<float> (detection.channels[0], detection.channels[0] + 4) == left);
    CHECK (std::vector<float> (detection.channels[1], detection.channels[1] + 4) == right);
    const float* followed = detection.channelFollowedBy (0);
    CHECK (std::vector<float> (followed, followed + 4) == std::vector<float> { 0.5f, 0.3f, -0.4f, -0.9f });
}

// Stereo Placement routing: a stereo detection signal's Mid and Side, encoded as plugkit encodes them, whichever
// Placement plays; a mono detection signal is its own Mid and Side, so it drives every Placement, Side included. The
// same with External, from the Sidechain's channels.
TEST_CASE ("The detection signal carries the Mid and the Side of a stereo detection signal, and a mono one is both")
{
    const int numChannels = GENERATE (1, 2);
    const auto source = GENERATE (DetectionSource::Internal, DetectionSource::External);
    CAPTURE (numChannels, source);
    constexpr int numSamples = 16;
    std::vector<std::vector<float>> signal (static_cast<std::size_t> (numChannels));
    for (int i = 0; i < numSamples; ++i)
    {
        signal[0].push_back (0.03f * static_cast<float> (i + 1));
        if (numChannels == 2)
            signal[1].push_back (-0.01f * static_cast<float> (i * i));
    }
    std::vector<const float*> channels;
    for (const auto& channel : signal)
        channels.push_back (channel.data());
    const ConstAudioBlock block { channels.data(), numChannels, numSamples };
    // With External the main input is a stereo signal of its own, which the detection signal doesn't carry.
    const std::vector<float> mainSamples (numSamples, 0.7f);
    const float* mainChannels[] = { mainSamples.data(), mainSamples.data() };
    const bool internal = source == DetectionSource::Internal;

    DetectionStage stage;
    stage.prepare (internal ? numChannels : 2, numSamples);
    stage.setSource (source);
    const auto detection = internal ? stage.process (block, nullptr) : stage.process ({ mainChannels, 2, numSamples }, &block);

    std::vector<float> mid, side;
    for (std::size_t i = 0; i < numSamples; ++i)
    {
        mid.push_back (numChannels == 2 ? plugkit::encodeMid (signal[0][i], signal[1][i]) : signal[0][i]);
        side.push_back (numChannels == 2 ? plugkit::encodeSide (signal[0][i], signal[1][i]) : signal[0][i]);
    }
    CHECK (std::vector<float> (detection.mid, detection.mid + numSamples) == mid);
    CHECK (std::vector<float> (detection.side, detection.side + numSamples) == side);
}
