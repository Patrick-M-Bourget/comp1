#include "comp1/TimingResponse.h"

#include "Character.h"
#include "Clean.h"
#include "comp1/TransferCurve.h"

#include <plugkit/RunGrid.h>

#include <algorithm>
#include <cmath>

namespace comp1
{

namespace
{
// Under this, in dB, the recovery is over: the rest of it reads 0.
constexpr double recoveredDb = 1.0e-6;
} // namespace

TimingResponse timingResponse (const Settings& settings, double sampleRate)
{
    TimingResponse response;
    const auto curve = clean::playedCurveOf (settings);
    const auto timing = clean::timing (settings.attackMs, settings.releaseMs, settings.holdMs, sampleRate);
    const auto& character = describe (settings.character);
    response.burstLevelDb = std::min (curve.thresholdDb + TimingResponse::burstOverDb, 0.0);
    const auto magnitude = static_cast<float> (std::pow (10.0, response.burstLevelDb / 20.0));

    CharacterDetector detector;
    detector.prepare (sampleRate);
    long played = 0; // samples since the start, for the run grid, which starts with the burst
    double reductionDb = 0.0;
    // Plays count samples of the burst, or of silence, noting the Gain Reduction at each point's sample into values; the
    // points past the last sample read the Gain Reduction there. Silence stops early once it has recovered.
    const auto play = [&] (std::array<double, TimingResponse::points>& values, long count, bool silence) {
        const float level = silence ? 0.0f : magnitude;
        int point = 0;
        long next = TimingResponse::sampleAt (point, sampleRate); // the next point's sample
        for (long n = 0; n < count && point < TimingResponse::points; ++n, ++played)
        {
            if (played % plugkit::runSamples == 0)
                detector.startRun (settings.releaseMs, settings.autoRelease);
            const float sample = n % 2 == 0 ? level : -level;
            reductionDb = detector.process (character, DetectionSource::Internal, sample, curve, timing);
            while (point < TimingResponse::points && next <= n)
            {
                values[static_cast<std::size_t> (point++)] = reductionDb;
                next = TimingResponse::sampleAt (point, sampleRate);
            }
            if (silence && reductionDb < recoveredDb)
            {
                reductionDb = 0.0;
                break;
            }
        }
        for (; point < TimingResponse::points; ++point)
            values[static_cast<std::size_t> (point)] = reductionDb;
    };
    play (response.rise, std::lround (TimingResponse::burstSeconds * sampleRate), false);
    play (response.recovery, TimingResponse::sampleAt (TimingResponse::points - 1, sampleRate) + 1, true);
    return response;
}

} // namespace comp1
