#include "Detection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <plugkit/Placement.h>

#include <vector>

using namespace comp1;

// The detection stage's contract with Detection Source Internal, the only source there is so far: under Stereo Placement
// Stereo, each main channel follows a detection channel of its own, carrying the Internal signal sample for sample. The
// Sidechain is accepted and changes nothing.
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

    // A stereo signal's Mid and Side follow its own two channels (the next test).
    REQUIRE (detection.numChannels == (numChannels == 2 ? 4 : 1));
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
        CHECK (detection.followedBy[ch] == ch);
        const float* followed = detection.channelFollowedBy (ch);
        CHECK (std::vector<float> (followed, followed + numSamples) == expected[static_cast<std::size_t> (ch)]);
    }
}

// Stereo Placement routing: a stereo signal's Mid and Side are detection channels of their own, encoded as plugkit
// encodes them, whichever Placement plays; a mono signal is its own Mid and Side, so it drives every Placement.
TEST_CASE ("The detection signal carries the Mid and the Side of a stereo signal, and a mono signal is both")
{
    const int numChannels = GENERATE (1, 2);
    CAPTURE (numChannels);
    constexpr int numSamples = 16;
    std::vector<std::vector<float>> internal (static_cast<std::size_t> (numChannels));
    for (int i = 0; i < numSamples; ++i)
    {
        internal[0].push_back (0.03f * static_cast<float> (i + 1));
        if (numChannels == 2)
            internal[1].push_back (-0.01f * static_cast<float> (i * i));
    }
    std::vector<const float*> channels;
    for (const auto& channel : internal)
        channels.push_back (channel.data());

    DetectionStage stage;
    stage.prepare (numChannels, numSamples);
    const auto detection = stage.process ({ channels.data(), numChannels, numSamples }, nullptr);

    std::vector<float> mid, side;
    for (std::size_t i = 0; i < numSamples; ++i)
    {
        mid.push_back (numChannels == 2 ? plugkit::encodeMid (internal[0][i], internal[1][i]) : internal[0][i]);
        side.push_back (numChannels == 2 ? plugkit::encodeSide (internal[0][i], internal[1][i]) : internal[0][i]);
    }
    CHECK (std::vector<float> (detection.mid(), detection.mid() + numSamples) == mid);
    CHECK (std::vector<float> (detection.side(), detection.side() + numSamples) == side);
}
