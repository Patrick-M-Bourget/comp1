#include "comp1/TimingResponse.h"

#include "AutoRelease.h"
#include "Character.h"
#include "Clean.h"
#include "Opto.h"
#include "comp1/TransferCurve.h"

#include <plugkit/RunGrid.h>

#include <algorithm>
#include <cmath>
#include <utility>

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
    const auto& character = describe (settings.character);
    response.burstLevelDb = std::min (curve.thresholdDb + TimingResponse::burstOverDb, 0.0);
    const auto magnitude = static_cast<float> (std::pow (10.0, response.burstLevelDb / 20.0));

    // A detector, the Release and Auto Release it plays at, and the samples it has played since the burst's start, for
    // the run grid, which starts with the burst.
    struct Playing
    {
        const CharacterDescription* character = nullptr;
        CharacterDetector detector;
        clean::Timing timing;
        double releaseMs = 0.0;
        bool autoRelease = false;
        long played = 0;
    };
    // Plays count samples of the burst, or of silence, noting the Gain Reduction at each point's sample into values; the
    // points past the last sample read the Gain Reduction there. Silence stops early once it has recovered. A run starts
    // on the grid, and at the first sample too when startsRun.
    const auto play = [&] (Playing& playing, std::array<double, TimingResponse::points>& values, long count, bool silence, bool startsRun) {
        const float level = silence ? 0.0f : magnitude;
        double reductionDb = 0.0;
        int point = 0;
        long next = TimingResponse::sampleAt (point, sampleRate); // the next point's sample
        for (long n = 0; n < count && point < TimingResponse::points; ++n, ++playing.played)
        {
            if (playing.played % plugkit::runSamples == 0 || (startsRun && n == 0))
                playing.detector.startRun (playing.releaseMs, playing.autoRelease);
            const float sample = n % 2 == 0 ? level : -level;
            reductionDb = playing.detector.process (*playing.character, DetectionSource::Internal, sample, curve, playing.timing);
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
    const long silenceSamples = TimingResponse::sampleAt (TimingResponse::points - 1, sampleRate) + 1;

    const long burstSamples = std::lround (TimingResponse::burstSeconds * sampleRate);
    Playing playing { .character = &character,
                      .timing = clean::timing (settings.attackMs, settings.releaseMs, settings.holdMs, sampleRate),
                      .releaseMs = settings.releaseMs,
                      .autoRelease = settings.autoRelease };
    playing.detector.prepare (sampleRate);
    auto fromRest = playing;
    play (playing, response.rise, burstSamples, false, false);
    if (! settings.autoRelease)
        play (playing, response.recovery, silenceSamples, true, false);
    else
    {
        const auto atBurstEnd = playing;
        // Each edge from the detector as the burst leaves it, Auto Release off and Release fixed at the edge's.
        auto& band = response.recoveryBand.emplace();
        for (const auto& [edge, scale] : { std::pair { &band.fastest, 1.0 }, std::pair { &band.slowest, AutoRelease::multiplier } })
        {
            auto from = atBurstEnd;
            from.releaseMs = scale * settings.releaseMs;
            from.timing = clean::timing (settings.attackMs, from.releaseMs, settings.holdMs, sampleRate);
            from.autoRelease = false;
            play (from, *edge, silenceSamples, true, true);
        }
        response.recovery = band.fastest;
    }

    if (character.opticalTiming)
    {
        // Attacking, both of Opto's parts move at Attack's time scaled by the overshoot, so with the scale held its rise is
        // one pole at the scaled time, its loop solved as Opto's is: the Character without Opto's timing.
        auto held = character;
        held.opticalTiming = false;
        fromRest.character = &held;
        auto& band = response.riseBand.emplace();
        for (const auto& [edge, scale] : { std::pair { &band.fastest, opto::attackFastest }, std::pair { &band.slowest, opto::attackSlowest } })
        {
            auto from = fromRest;
            from.timing = clean::timing (scale * settings.attackMs, settings.releaseMs, settings.holdMs, sampleRate);
            play (from, *edge, burstSamples, false, false);
        }
        response.slowPartMs = std::pair { settings.holdMs + opto::slowFreshMs, settings.holdMs + opto::slowSeasonedMs };
    }
    return response;
}

Settings timingResponseShaping (const Settings& settings)
{
    Settings shaped;
    shaped.character = settings.character;
    shaped.comp = settings.comp;
    shaped.compFollow = settings.compFollow;
    shaped.compCurve = settings.compCurve;
    shaped.thresholdDb = settings.thresholdDb;
    shaped.ratio = settings.ratio;
    shaped.kneeDb = settings.kneeDb;
    shaped.attackMs = settings.attackMs;
    shaped.releaseMs = settings.releaseMs;
    shaped.autoRelease = settings.autoRelease;
    shaped.holdMs = settings.holdMs;
    shaped.rangeDb = settings.rangeDb;
    return shaped;
}

} // namespace comp1
