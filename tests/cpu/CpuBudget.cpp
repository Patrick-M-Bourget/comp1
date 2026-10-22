// Measures the Engine's CPU load in each CPU scene and fails when a scene with a ceiling is over budget, or when
// silence or subnormal input costs more than quietCostRatio times the same settings on the bursts, the Worst case
// scene (docs/performance.md, "CPU budget").
// Run by scripts/check.sh cpu, on its own: timings taken while other tests run are meaningless.
//
// scripts/check.sh cpu <ref> (docs/performance.md, "CPU A/B") runs it scene by scene instead, alternating with <ref>'s
// build of this same file:
//   comp1_cpu_budget scenes                          the CPU scenes' names, one per line, in order
//   comp1_cpu_budget run <scene> <rate> <seconds>    the load of scene number <scene> (from 0) at <rate> Hz, timed for
//                                                    about <seconds> of wall-clock time, as one number
//
// The scenes are built only from the Engine's public interface and the shared helpers in tests/engine/Measure.h, which
// the CPU A/B copies into <ref>'s tree with this directory.

#include "../engine/Measure.h"
#include "comp1/Style.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#if ! defined(_WIN32)
    #include <time.h>
#endif

using namespace comp1;

namespace
{

constexpr int runs = 5;
constexpr double secondsPerRun = 4.0;

// What the input carries.
enum class Input
{
    Bursts,      // the shared noise bursts (Measure.h's bursts): loud for 100 ms of every 200, so every stage keeps moving
    ThenSilence, // half a second of the bursts, then nothing
    Subnormal,   // the bursts scaled into subnormal numbers
};

// Changes the settings for the block starting at a time in seconds.
using Automation = void (*) (Settings&, double seconds);

// Threshold swept by Host Automation, 6 dB either side of its default, once every 2 s: every block gets settings that
// differ from the last block's.
void automateThreshold (Settings& settings, double seconds)
{
    settings.thresholdDb = Settings {}.thresholdDb + 6.0 * std::sin (std::numbers::pi * seconds);
}

// Comp swept by Host Automation while following, 30% either side of its default, once every 2 s: Threshold and Ratio
// move along the Comp Curve every block, and Auto Gain's estimate with them.
void automateComp (Settings& settings, double seconds)
{
    settings.comp = Settings {}.comp + 30.0 * std::sin (std::numbers::pi * seconds);
}

// Comp Follow switched by Host Automation every 300 ms, the glide time, so it is always gliding, on or off, between the
// stored settings and the Comp Curve (followingFar's).
void automateCompFollow (Settings& settings, double seconds)
{
    settings.compFollow = static_cast<int> (seconds / 0.3) % 2 == 0;
}

// A ceiling, in percent of real time.
struct Ceiling
{
    double at48kHz, at96kHz;
};

// A CPU scene: what the Engine runs, on what, timed from startSeconds in. With automate, the settings are published
// before every block, as the Plugin Shell publishes them while Host Automation moves a parameter, and the publication
// is timed with the block.
struct Scene
{
    const char* name;
    Settings settings;
    int blockSize = 512;
    Input input = Input::Bursts;
    double startSeconds = 0.0;
    Automation automate = nullptr;
    std::optional<Ceiling> ceiling = {};
    bool quietReference = false; // the scene the quiet ones are held to: their settings, on the bursts
    bool quiet = false;          // held to quietCostRatio times the quiet reference's load
};

Settings withCharacter (Character character)
{
    Settings settings;
    settings.character = character;
    return settings;
}

Settings highQuality()
{
    Settings settings;
    settings.processingMode = ProcessingMode::HighQuality;
    return settings;
}

// Comp Follow on.
Settings following()
{
    Settings settings;
    settings.compFollow = true;
    return settings;
}

// Comp at 100%, Clean's Comp Curve at its deepest (-36 dB, 8:1), and the stored Threshold and Ratio compressing nothing:
// far from the Comp Curve, so Comp Follow's glide moves every setting it mixes.
Settings followingFar()
{
    auto settings = following();
    settings.comp = 100.0;
    settings.thresholdDb = 0.0;
    settings.ratio = 1.0;
    return settings;
}

// The worst case with the Limiter off: what the rest of the Engine costs, and so what the Limiter costs on.
Settings worstCaseLimiterOff()
{
    auto settings = test::worstCaseSettings();
    settings.limiter = false;
    return settings;
}

// The CPU scenes, in the order printed, one per line, so tickets adding scenes in parallel merge cleanly. The quiet
// reference comes before the quiet scenes.
std::vector<Scene> scenes()
{
    return {
        { .name = "Defaults", .settings = {}, .ceiling = Ceiling { 4.0, 7.0 } },
        { .name = "Defaults, Threshold automated", .settings = {}, .automate = automateThreshold, .ceiling = Ceiling { 4.0, 7.0 } },
        { .name = "Worst case", .settings = test::worstCaseSettings(), .ceiling = Ceiling { 17.0, 28.0 }, .quietReference = true },
        { .name = "Worst case, Limiter off", .settings = worstCaseLimiterOff() },
        { .name = "Clean, Colour 100%", .settings = withCharacter (Character::Clean) },
        { .name = "VCA, Colour 100%", .settings = withCharacter (Character::VCA) },
        { .name = "FET, Colour 100%", .settings = withCharacter (Character::FET) },
        { .name = "Opto, Colour 100%", .settings = withCharacter (Character::Opto) },
        { .name = "VCA's Style defaults", .settings = styled (Style::VCA) },
        { .name = "FET's Style defaults", .settings = styled (Style::FET) },
        { .name = "Opto's Style defaults", .settings = styled (Style::Opto) },
        { .name = "Vocal's Style defaults", .settings = styled (Style::Vocal) },
        { .name = "Instrument's Style defaults", .settings = styled (Style::Instrument) },
        { .name = "Bus's Style defaults", .settings = styled (Style::Bus) },
        { .name = "Drums' Style defaults", .settings = styled (Style::Drums) },
        { .name = "Comp Follow on, Comp automated", .settings = following(), .automate = automateComp },
        { .name = "Comp Follow gliding", .settings = followingFar(), .automate = automateCompFollow },
        { .name = "High Quality, 2x Oversampling", .settings = highQuality() },
        { .name = "Defaults, 32-sample blocks", .settings = {}, .blockSize = 32 },
        // Timed after 10 s of silence, long enough for every filter and envelope to have rung down below the smallest
        // normal float.
        { .name = "Worst case on silence", .settings = test::worstCaseSettings(), .input = Input::ThenSilence, .startSeconds = 10.0, .quiet = true },
        { .name = "Worst case on subnormal input", .settings = test::worstCaseSettings(), .input = Input::Subnormal, .quiet = true },
    };
}

// The processing time this thread has used, in seconds: CPU time where the system gives it per thread, so time the
// thread spends waiting for a core on a busy machine isn't counted; wall-clock time otherwise (Windows, whose thread
// times tick too coarsely to time a tenth of a second).
double threadSeconds()
{
#if defined(_WIN32)
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    timespec now {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double> (now.tv_sec) + 1.0e-9 * static_cast<double> (now.tv_nsec);
#endif
}

// The Engine's load in a scene, in percent of real time, in a fresh Engine at sampleRate: timed from the scene's start
// until audioSeconds of audio are timed or, given wallSeconds, until that much wall-clock time has passed. The input
// is made a tenth of a second at a time ahead of processing it, from 2 s of the bursts (ten of their periods, so the
// loop joins seamlessly), and only the processing is timed, by the thread's CPU time.
double loadPercent (const Scene& scene, double sampleRate, double audioSeconds, std::optional<double> wallSeconds = std::nullopt)
{
    const auto loopLength = static_cast<std::size_t> (10.0 * test::burstPeriodSeconds * sampleRate);
    const auto loop = test::bursts (2, static_cast<int> (loopLength), sampleRate);
    const auto block = static_cast<std::size_t> (scene.blockSize);
    const auto chunkBlocks = std::max<std::size_t> (1, static_cast<std::size_t> (0.1 * sampleRate) / block);
    test::Channels chunk (2, std::vector<float> (chunkBlocks * block));
    Engine engine;
    engine.prepare (sampleRate, scene.blockSize, 2);
    engine.setSettings (scene.settings);
    auto settings = scene.settings;
    const auto firstTimed = static_cast<std::size_t> (scene.startSeconds * sampleRate) / block;
    std::size_t next = 0; // the next block's number
    double busy = 0.0;
    std::size_t timedSamples = 0;
    std::optional<std::chrono::steady_clock::time_point> timingSince;
    const auto done = [&] {
        if (! timingSince)
            return false;
        if (wallSeconds)
            return std::chrono::duration<double> (std::chrono::steady_clock::now() - *timingSince).count() >= *wallSeconds;
        return static_cast<double> (timedSamples) >= audioSeconds * sampleRate;
    };
    while (! done())
    {
        for (std::size_t ch = 0; ch < chunk.size(); ++ch)
            for (std::size_t i = 0; i < chunk[ch].size(); ++i)
            {
                const std::size_t n = next * block + i;
                const float sample = loop[ch][n % loopLength];
                switch (scene.input)
                {
                    case Input::Bursts: chunk[ch][i] = sample; break;
                    case Input::ThenSilence: chunk[ch][i] = static_cast<double> (n) < 0.5 * sampleRate ? sample : 0.0f; break;
                    case Input::Subnormal: chunk[ch][i] = 1.0e-39f * sample; break;
                }
            }
        const bool timed = next >= firstTimed;
        if (timed && ! timingSince)
            timingSince = std::chrono::steady_clock::now();
        const double start = threadSeconds();
        for (std::size_t b = 0; b < chunkBlocks; ++b, ++next)
        {
            if (scene.automate != nullptr)
            {
                scene.automate (settings, static_cast<double> (next * block) / sampleRate);
                engine.setSettings (settings);
            }
            float* main[] = { chunk[0].data() + b * block, chunk[1].data() + b * block };
            engine.process ({ main, 2, scene.blockSize });
        }
        if (timed)
        {
            busy += threadSeconds() - start;
            timedSamples += chunk[0].size();
        }
    }
    return 100.0 * busy * sampleRate / static_cast<double> (timedSamples);
}

// The median of runs runs of secondsPerRun each, each in a fresh Engine.
double medianLoadPercent (const Scene& scene, double sampleRate)
{
    std::vector<double> loads;
    for (int run = 0; run < runs; ++run)
        loads.push_back (loadPercent (scene, sampleRate, secondsPerRun));
    std::sort (loads.begin(), loads.end());
    return loads[loads.size() / 2];
}

// The sample rates the budget is measured at.
constexpr double sampleRates[] = { 48000.0, 96000.0 };

// Silence and subnormal input may cost no more than this times the same settings on the bursts. Subnormal arithmetic
// is many times slower on x64, so a filter or envelope left to ring down into subnormal numbers is far over it.
constexpr double quietCostRatio = 1.5;

int budget()
{
    bool overBudget = false;
    std::printf ("Engine CPU load, stereo, in percent of real time: the median of %d runs of %.0f s, in 512-sample blocks unless named\n\n",
                 runs, secondsPerRun);
    const auto all = scenes();
    for (const double sampleRate : sampleRates)
    {
        double reference = 0.0;
        for (const auto& scene : all)
        {
            const double load = medianLoadPercent (scene, sampleRate);
            if (scene.quietReference)
                reference = load;
            std::printf ("%-32s %6.1f kHz  %6.2f%%", scene.name, sampleRate / 1000.0, load);
            if (scene.ceiling)
            {
                const double ceiling = sampleRate < 64000.0 ? scene.ceiling->at48kHz : scene.ceiling->at96kHz;
                const bool over = load > ceiling;
                overBudget = overBudget || over;
                std::printf ("  (ceiling %5.1f%%)%s", ceiling, over ? "  OVER" : "");
            }
            if (scene.quiet && load > quietCostRatio * reference)
            {
                overBudget = true;
                std::printf ("  OVER: more than %.1f x the same settings on the bursts", quietCostRatio);
            }
            std::printf ("\n");
        }
        std::printf ("\n");
    }
    std::printf ("%s\n", overBudget ? "Over the CPU budget" : "Within the CPU budget");
    return overBudget ? 1 : 0;
}

int usage()
{
    std::fprintf (stderr, "usage: comp1_cpu_budget [scenes | run <scene> <sample rate> <seconds>]\n");
    return 2;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc == 1)
        return budget();
    const std::string command = argv[1];
    const auto all = scenes();
    if (command == "scenes" && argc == 2)
    {
        for (const auto& scene : all)
            std::printf ("%s\n", scene.name);
        return 0;
    }
    if (command == "run" && argc == 5)
    {
        const int scene = std::atoi (argv[2]);
        const double sampleRate = std::atof (argv[3]), seconds = std::atof (argv[4]);
        if (scene < 0 || scene >= static_cast<int> (all.size()) || sampleRate <= 0.0 || seconds <= 0.0)
            return usage();
        std::printf ("%.6f\n", loadPercent (all[static_cast<std::size_t> (scene)], sampleRate, 0.0, seconds));
        return 0;
    }
    return usage();
}
