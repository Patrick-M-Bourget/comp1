#include "Detection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
