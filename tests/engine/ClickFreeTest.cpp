#include "Measure.h"
#include "comp1/Latency.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace comp1;

namespace
{

constexpr double toneFrequency = 200.0;
constexpr double toneSeconds = 1.5;
constexpr double onsetSeconds = 0.3;
// -12 dB: 6 dB over the default Threshold, so the compressor is working when a jump comes.
constexpr double toneAmplitude = 0.25;

using test::Host;

// The tone's amplitude at each moment, in seconds.
using Envelope = std::function<double (double)>;

// Plays a sine through the Engine as host would, in its cut's block sizes in turn, letting change(seconds, settings)
// edit the settings before each block, and returns the output. On two channels, the right is the left at half its
// amplitude, 2 radians ahead, so the channels' levels differ and the Mid and the Side each carry the tone.
test::Channels playTone (Host host,
                         const std::function<void (double, Settings&)>& change,
                         const Envelope& amplitude = [] (double) { return toneAmplitude; },
                         double seconds = toneSeconds,
                         int numChannels = 1)
{
    const double sampleRate = host.sampleRate;
    const int largest = *std::max_element (host.cut.begin(), host.cut.end());
    const auto length = static_cast<int> (seconds * sampleRate);

    Engine engine;
    engine.prepare (sampleRate, largest, numChannels);

    Settings settings;
    test::Channels output (static_cast<std::size_t> (numChannels));
    test::Channels block (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (largest)));
    std::vector<float*> channels;
    for (auto& channel : block)
        channels.push_back (channel.data());
    int n = 0;
    for (std::size_t b = 0; n + host.cut[b % host.cut.size()] <= length; ++b)
    {
        const int blockSize = host.cut[b % host.cut.size()];
        change (static_cast<double> (n) / sampleRate, settings);
        engine.setSettings (settings);
        for (std::size_t i = 0; i < static_cast<std::size_t> (blockSize); ++i, ++n)
        {
            const double time = static_cast<double> (n) / sampleRate;
            const double phase = 2.0 * std::numbers::pi * toneFrequency * n / sampleRate;
            for (std::size_t ch = 0; ch < block.size(); ++ch)
                block[ch][i] = static_cast<float> (amplitude (time) * (ch == 0 ? std::sin (phase) : 0.5 * std::sin (phase + 2.0)));
        }
        engine.process ({ channels.data(), numChannels, blockSize });
        for (std::size_t ch = 0; ch < block.size(); ++ch)
            output[ch].insert (output[ch].end(), block[ch].begin(), block[ch].begin() + blockSize);
    }
    return output;
}

// The output's discontinuity (Measure.h) from the onset on: the tone itself starts abruptly, and the compressor's
// first Attack moves with it.
double discontinuity (const test::Channels& output, double sampleRate)
{
    return test::discontinuity (output, toneFrequency, sampleRate, static_cast<std::size_t> (onsetSeconds * sampleRate));
}

using test::clickLimit;

// False until the onset has passed, then flips every 50 ms.
bool alternating (double seconds) { return seconds >= onsetSeconds && static_cast<int> ((seconds - onsetSeconds) / 0.05) % 2 == 0; }

// Every sample rate comp1 supports, each with a block cut in turn.
Host anyHost()
{
    return GENERATE (from_range (test::hosts()));
}

constexpr double infinity = std::numeric_limits<double>::infinity();

// A jump in one setting, between the values a and b.
struct Jump
{
    std::string setting;
    std::function<void (Settings&, bool)> set; // sets a when the bool is false, b when true
    // When it has any, the jump is measured against the steady tone with each of these settings, rather than against
    // a pure sine: Saturation's own harmonics sharpen the tone's corners past a sine's.
    std::vector<std::function<void (Settings&)>> steady = {};
    int numChannels = 1; // the track it is heard on
    Envelope amplitude = [] (double) { return toneAmplitude; };
};

// Saturation on at Drive 24 dB, which pushes the tone (-12 dB) 12 dB into the shape.
void saturating (Settings& s)
{
    s.saturation = true;
    s.driveDb = 24.0;
}

// Stereo Link and Stereo Placement, which only a stereo track hears, with Threshold low enough that the Mid and the
// Side are compressed too.
Jump onStereo (std::string setting, std::function<void (Settings&, bool)> set)
{
    return { std::move (setting),
             [set] (Settings& s, bool b) {
                 s.thresholdDb = -30.0;
                 set (s, b);
             },
             {},
             2 };
}

// High Quality, where Lookahead is read, with a compressor quick enough to follow the tremolo below.
void lookingAhead (Settings& s)
{
    s.processingMode = ProcessingMode::HighQuality;
    s.thresholdDb = -40.0;
    s.ratio = 4.0;
    s.attackMs = 5.0;
    s.releaseMs = 50.0;
}

// The tone swelling and fading by 30 dB twenty times a second. On a steady tone the detector hears the same level at
// any Lookahead, so a jump would move nothing; here a jump moves what the detector hears to a level up to 9 dB away.
// Without Lookahead's glide that clicks at 44.1, 88.2 and 176.4 kHz, up to 3 times the limit; with it, it stays under
// the limit at every host.
double tremolo (double seconds)
{
    return toneAmplitude * std::pow (10.0, -15.0 * (1.0 - std::cos (2.0 * std::numbers::pi * 20.0 * seconds)) / 20.0);
}

// The Detection EQ's free band on, a Bell boosting 18 dB at the tone, which each free band row moves from.
void freeBandAtTone (Settings& s)
{
    s.detectionEq.freeBand.on = true;
    s.detectionEq.freeBand.filter = { .shape = plugkit::Shape::Bell, .gain = 18.0, .frequency = toneFrequency, .q = 1.0 };
}

// Colour is heard where the compressed tone comes within its drive of full scale, at the default Threshold, and as far
// over Threshold at any other (tools/dyn-lab/USAGE.md, "Colour"): the tone at +12 dB, 30 dB over the default
// Threshold, compressed by 22.5 dB to -10.5 dB and driven 23.7 dB, 13 dB into the shape.
double colourful (double) { return 4.0; }

void onVca (Settings& s) { s.character = Character::VCA; }
void onFet (Settings& s) { s.character = Character::FET; }
void onOpto (Settings& s) { s.character = Character::Opto; }
// High Quality at 8x, which plays each rate's cap: Colour oversampled in the After section.
void oversampledColour (Settings& s)
{
    s.processingMode = ProcessingMode::HighQuality;
    s.oversampling = Oversampling::x8;
}

// Threshold jumping from -10 dB to -40 dB on character, the tone (+12 dB) compressed by 16.5 and 39 dB: Colour's shape
// is driven 8 dB less and 22 dB more than at the default Threshold, so the jump moves it by 30 dB.
Jump trackedThresholdJump (void (*character) (Settings&), std::string setting)
{
    const auto at = [character] (double thresholdDb) {
        return [character, thresholdDb] (Settings& s) {
            character (s);
            s.thresholdDb = thresholdDb;
        };
    };
    return { std::move (setting),
             [character] (Settings& s, bool b) {
                 character (s);
                 s.thresholdDb = b ? -40.0 : -10.0;
             },
             { at (-10.0), at (-40.0) },
             1,
             colourful };
}

// The Limiter on, the tone (-12 dB) pushed 24 dB by Output Gain, 12 dB over the Ceiling's top, in mode.
void limitingTone (Settings& s, ProcessingMode mode)
{
    s.processingMode = mode;
    s.outputGainDb = 24.0;
    s.limiter = true;
}

// The Limiter switching on and off on the tone, in mode, against the steady tone limited, whose corners are sharper
// than a sine's.
Jump limiterJump (ProcessingMode mode, std::string setting)
{
    return { std::move (setting),
             [mode] (Settings& s, bool b) {
                 limitingTone (s, mode);
                 s.limiter = b;
             },
             { [mode] (Settings& s) { limitingTone (s, mode); } } };
}

// The Ceiling jumping across its range, the Limiter on, in mode: from no limiting to 18 dB of it, against the steady
// tone limited at the bottom.
Jump ceilingJump (ProcessingMode mode, std::string setting)
{
    const auto atBottom = [mode] (Settings& s) {
        s.processingMode = mode;
        s.limiter = true;
        s.ceilingDb = parameterRanges::ceiling.low;
    };
    return { std::move (setting),
             [mode, atBottom] (Settings& s, bool b) {
                 atBottom (s);
                 s.ceilingDb = b ? parameterRanges::ceiling.low : parameterRanges::ceiling.high;
             },
             { atBottom } };
}

// The Gate on, at a Gate Threshold the tone (-12 dB) sits 12 dB under, past the closing level: closed on it.
void gateClosed (Settings& s)
{
    s.gate.on = true;
    s.gate.thresholdDb = 0.0;
}

// The tone in and out of the Gate: 80 ms 18 dB over its Gate Threshold, then 80 ms 18 dB under it, each change a 5 ms
// raised cosine, so it opens and closes on the tone. Each cycle is an odd length against the jumps' 50 ms, so they
// land at a different point of the opening and closing each time.
constexpr double gatingThresholdDb = -30.0;
double gatingAmplitude (double seconds)
{
    constexpr double held = 0.08, change = 0.005;
    const double loud = toneAmplitude, quiet = toneAmplitude * std::pow (10.0, -36.0 / 20.0);
    const double t = std::fmod (seconds, 2.0 * (held + change));
    const auto fade = [] (double from, double to, double x) { return from + (to - from) * (1.0 - std::cos (std::numbers::pi * x)) / 2.0; };
    if (t < held)
        return loud;
    if (t < held + change)
        return fade (loud, quiet, (t - held) / change);
    if (t < 2.0 * held + change)
        return quiet;
    return fade (quiet, loud, (t - 2.0 * held - change) / change);
}

// The Gate opening and closing on the tone, with Gate Hold 0 ms so it closes well within each quiet part.
void gating (Settings& s)
{
    s.gate.on = true;
    s.gate.thresholdDb = gatingThresholdDb;
    s.gate.holdMs = 0.0;
}

// A jump in one of the Gate's times while it opens and closes on the tone: set (s, b) sets the time, a when b is
// false. Measured against the tone gated steadily at each, whose openings and closings are corners of their own.
Jump gateTimeJump (std::string setting, void (*set) (Settings&, bool))
{
    return { std::move (setting),
             [set] (Settings& s, bool b) {
                 gating (s);
                 set (s, b);
             },
             { [set] (Settings& s) {
                  gating (s);
                  set (s, false);
              },
               [set] (Settings& s) {
                   gating (s);
                   set (s, true);
               } },
             1,
             gatingAmplitude };
}

// A Style change mid-compression, at Colour 100%: from's Character and defaults, then to's, every one of them applied
// at once from the Style table, as the Plugin Shell hands them over. The Character crossfades, and every setting the
// Style writes glides or fades as it does alone. Against the steady tone at each Style.
Jump styleJump (Style from, Style to, std::string setting)
{
    return { std::move (setting),
             [from, to] (Settings& s, bool b) { applyStyle (b ? to : from, s); },
             { [from] (Settings& s) { applyStyle (from, s); }, [to] (Settings& s) { applyStyle (to, s); } },
             1,
             colourful };
}

Jump placementJump (StereoPlacement a, StereoPlacement b, std::string setting)
{
    return onStereo (std::move (setting), [a, b] (Settings& s, bool toB) { s.stereoPlacement = toB ? b : a; });
}

const std::vector<Jump>& jumps()
{
    static const std::vector<Jump> all {
        // The Gate's, first: on and off while it closes on the tone; its Gate Threshold across the tone and its Gate
        // Range from none to the most while it is closed; its times while it opens and closes on the tone.
        { "Gate", [] (Settings& s, bool b) {
             gateClosed (s);
             s.gate.on = b;
         } },
        // Across the tone the Gate opens and closes on it by 20 dB, each one pole, which starts moving at full speed:
        // at the default Gate Attack (1 ms) and Gate Release (100 ms) that start is itself a corner over the limit at
        // 88.2 kHz and up, whatever the glide. How fast the Gate opens and closes is those settings' job, so the row
        // sets them slower.
        { "Gate Threshold", [] (Settings& s, bool b) {
             gateClosed (s);
             s.gate.attackMs = 10.0;
             s.gate.releaseMs = 300.0;
             s.gate.thresholdDb = b ? 0.0 : -40.0;
         } },
        { "Gate Range", [] (Settings& s, bool b) {
             gateClosed (s);
             s.gate.rangeDb = b ? 60.0 : 0.0;
         } },
        gateTimeJump ("Gate Attack", [] (Settings& s, bool b) { s.gate.attackMs = b ? 50.0 : 1.0; }),
        gateTimeJump ("Gate Release", [] (Settings& s, bool b) { s.gate.releaseMs = b ? 2000.0 : 20.0; }),
        gateTimeJump ("Gate Hold", [] (Settings& s, bool b) { s.gate.holdMs = b ? 500.0 : 0.0; }),
        { "Threshold", [] (Settings& s, bool b) { s.thresholdDb = b ? -40.0 : 0.0; } },
        { "Ratio", [] (Settings& s, bool b) { s.ratio = b ? infinity : 1.0; } },
        { "Knee", [] (Settings& s, bool b) { s.kneeDb = b ? 72.0 : 0.0; } },
        { "Attack", [] (Settings& s, bool b) { s.attackMs = b ? 250.0 : 1.0; } },
        { "Release", [] (Settings& s, bool b) { s.releaseMs = b ? 5000.0 : 20.0; } },
        { "Auto Release", [] (Settings& s, bool b) { s.autoRelease = b; } },
        { "Hold", [] (Settings& s, bool b) { s.holdMs = b ? 500.0 : 0.0; } },
        { "Range", [] (Settings& s, bool b) { s.rangeDb = b ? 60.0 : 0.0; } },
        { "Auto Gain", [] (Settings& s, bool b) { s.autoGain = b; } },
        // Comp Follow on and off, the stored settings compressing nothing and the curve at its deepest: every switch
        // after the first lands mid-glide and turns it round. Comp jumping end to end while following.
        { "Comp Follow", [] (Settings& s, bool b) {
             s.thresholdDb = 0.0;
             s.ratio = 1.0;
             s.comp = 100.0;
             s.compFollow = b;
         } },
        { "Comp, following", [] (Settings& s, bool b) {
             s.compFollow = true;
             s.comp = b ? 100.0 : 0.0;
         } },
        { "Saturation",
          [] (Settings& s, bool b) {
              saturating (s);
              s.saturation = b;
          },
          { saturating } },
        { "Drive",
          [] (Settings& s, bool b) {
              saturating (s);
              s.driveDb = b ? 24.0 : 0.0;
          },
          { saturating } },
        { "Saturation Position",
          [] (Settings& s, bool b) {
              saturating (s);
              s.saturationPosition = b ? SaturationPosition::Before : SaturationPosition::After;
          },
          { [] (Settings& s) {
               saturating (s);
               s.saturationPosition = SaturationPosition::Before;
           },
            saturating } },
        // Held, the uncompressed input plays in place of the compressed one.
        { "Detection Audition", [] (Settings& s, bool b) { s.detectionAudition = b; } },
        // On, the input plays in place of the compressed tone at Output Gain +12 dB, in each Processing Mode.
        { "Global Bypass", [] (Settings& s, bool b) {
             s.outputGainDb = 12.0;
             s.globalBypass = b;
         } },
        { "Global Bypass in High Quality", [] (Settings& s, bool b) {
             s.processingMode = ProcessingMode::HighQuality;
             s.outputGainDb = 12.0;
             s.globalBypass = b;
         } },
        { "Lookahead",
          [] (Settings& s, bool b) {
              lookingAhead (s);
              s.lookaheadMs = b ? 5.0 : 0.0;
          },
          { lookingAhead,
            [] (Settings& s) {
                lookingAhead (s);
                s.lookaheadMs = 5.0;
            } },
          1,
          tremolo },
        // The Detection EQ's, each moving across the tone or switching how it filters it.
        { "Detection EQ high-pass on", [] (Settings& s, bool b) {
             s.detectionEq.highPass.filter.frequency = 1000.0;
             s.detectionEq.highPass.on = b;
         } },
        { "Detection EQ high-pass Frequency", [] (Settings& s, bool b) {
             s.detectionEq.highPass.on = true;
             s.detectionEq.highPass.filter.frequency = b ? 2000.0 : 20.0;
         } },
        { "Detection EQ high-pass Slope", [] (Settings& s, bool b) {
             s.detectionEq.highPass.on = true;
             s.detectionEq.highPass.filter.frequency = 300.0;
             s.detectionEq.highPass.filter.slope = b ? 96.0 : 6.0;
         } },
        { "Detection EQ low-pass on", [] (Settings& s, bool b) {
             s.detectionEq.lowPass.filter.frequency = 50.0;
             s.detectionEq.lowPass.on = b;
         } },
        { "Detection EQ low-pass Frequency", [] (Settings& s, bool b) {
             s.detectionEq.lowPass.on = true;
             s.detectionEq.lowPass.filter.frequency = b ? 50.0 : 20000.0;
         } },
        { "Detection EQ low-pass Slope", [] (Settings& s, bool b) {
             s.detectionEq.lowPass.on = true;
             s.detectionEq.lowPass.filter.frequency = 150.0;
             s.detectionEq.lowPass.filter.slope = b ? 48.0 : 6.0;
         } },
        { "Detection EQ free band on", [] (Settings& s, bool b) {
             freeBandAtTone (s);
             s.detectionEq.freeBand.on = b;
         } },
        { "Detection EQ free band Shape", [] (Settings& s, bool b) {
             freeBandAtTone (s);
             s.detectionEq.freeBand.filter.shape = shapeOf (b ? FreeBandShape::HighShelf : FreeBandShape::Bell);
         } },
        { "Detection EQ free band Frequency", [] (Settings& s, bool b) {
             freeBandAtTone (s);
             s.detectionEq.freeBand.filter.frequency = b ? 20000.0 : 20.0;
         } },
        { "Detection EQ free band Gain", [] (Settings& s, bool b) {
             freeBandAtTone (s);
             s.detectionEq.freeBand.filter.gain = b ? 30.0 : -30.0;
         } },
        { "Detection EQ free band Q", [] (Settings& s, bool b) {
             freeBandAtTone (s);
             s.detectionEq.freeBand.filter.q = b ? 40.0 : 0.025;
         } },
        onStereo ("Stereo Link", [] (Settings& s, bool b) { s.stereoLink = b ? 100.0 : 0.0; }),
        placementJump (StereoPlacement::Stereo, StereoPlacement::Mid, "Stereo Placement, Stereo and Mid"),
        placementJump (StereoPlacement::Mid, StereoPlacement::Side, "Stereo Placement, Mid and Side"),
        placementJump (StereoPlacement::Side, StereoPlacement::Stereo, "Stereo Placement, Side and Stereo"),
        // Against the steady tone at each end: Colour's harmonics sharpen the tone's corners past a sine's.
        { "Colour",
          [] (Settings& s, bool b) {
              onVca (s);
              s.colour = b ? 100.0 : 0.0;
          },
          { onVca,
            [] (Settings& s) {
                onVca (s);
                s.colour = 0.0;
            } },
          1,
          colourful },
        // Threshold under compression at Colour 100%, which Colour tracks as it glides: against the steady tone at each
        // end, coloured alike by the tracking.
        trackedThresholdJump (onVca, "Threshold, VCA at Colour 100%"),
        trackedThresholdJump (onFet, "Threshold, FET at Colour 100%"),
        // Mid-compression, at Colour 100%: Clean's gain and VCA's gain and Colour crossfade.
        { "Character, Clean and VCA",
          [] (Settings& s, bool b) { s.character = b ? Character::VCA : Character::Clean; },
          { onVca, [] (Settings&) {} },
          1,
          colourful },
        // And Clean's gain and FET's loop and Colour.
        { "Character, Clean and FET",
          [] (Settings& s, bool b) { s.character = b ? Character::FET : Character::Clean; },
          { onFet, [] (Settings&) {} },
          1,
          colourful },
        // And Clean's gain and Opto's loop, its two-stage release and its Colour.
        { "Character, Clean and Opto",
          [] (Settings& s, bool b) { s.character = b ? Character::Opto : Character::Clean; },
          { onOpto, [] (Settings&) {} },
          1,
          colourful },
        // And FET's loop and Colour and Opto's: Opto's parts start from FET's Gain Reduction.
        { "Character, FET and Opto",
          [] (Settings& s, bool b) { s.character = b ? Character::Opto : Character::FET; },
          { onOpto, onFet },
          1,
          colourful },
        // A Style change: from Clean's Style to each other Character Style's, and between FET's and Opto's.
        styleJump (Style::Clean, Style::VCA, "Style, Clean and VCA"),
        styleJump (Style::Clean, Style::FET, "Style, Clean and FET"),
        styleJump (Style::Clean, Style::Opto, "Style, Clean and Opto"),
        styleJump (Style::FET, Style::Opto, "Style, FET and Opto"),
        // In High Quality at the largest Oversampling allowed, where Colour is oversampled: VCA's and FET's Colour
        // crossfade in the After section, each worked out on the compressed signal.
        { "Character, VCA and FET, oversampled",
          [] (Settings& s, bool b) {
              oversampledColour (s);
              s.character = b ? Character::FET : Character::VCA;
          },
          { [] (Settings& s) {
               oversampledColour (s);
               onVca (s);
           },
            [] (Settings& s) {
                oversampledColour (s);
                onFet (s);
            } },
          1,
          colourful },
        // A Placement change on VCA, oversampled: the Colour of the Placement leaving crossfades into the arriving
        // one's.
        { "Stereo Placement, Stereo and Mid, VCA oversampled",
          [] (Settings& s, bool b) {
              oversampledColour (s);
              onVca (s);
              s.stereoPlacement = b ? StereoPlacement::Mid : StereoPlacement::Stereo;
          },
          { [] (Settings& s) {
               oversampledColour (s);
               onVca (s);
           },
            [] (Settings& s) {
                oversampledColour (s);
                onVca (s);
                s.stereoPlacement = StereoPlacement::Mid;
            } },
          2,
          colourful },
        // Dry and Wet, each from Off and from far below unity, with the other at 0 dB, so the two paths sum.
        { "Dry, Off and 0 dB", [] (Settings& s, bool b) { s.dryDb = b ? 0.0 : -infinity; } },
        { "Dry, -24 and +12 dB", [] (Settings& s, bool b) { s.dryDb = b ? 12.0 : -24.0; } },
        { "Wet, Off and 0 dB", [] (Settings& s, bool b) {
             s.dryDb = 0.0;
             s.wetDb = b ? 0.0 : -infinity;
         } },
        { "Wet, -24 and +12 dB", [] (Settings& s, bool b) {
             s.dryDb = 0.0;
             s.wetDb = b ? 12.0 : -24.0;
         } },
        limiterJump (ProcessingMode::ZeroLatency, "Limiter"),
        limiterJump (ProcessingMode::HighQuality, "Limiter, High Quality"),
        ceilingJump (ProcessingMode::ZeroLatency, "Ceiling"),
        ceilingJump (ProcessingMode::HighQuality, "Ceiling, High Quality"),
    };
    return all;
}

// The jumps in a Detection EQ setting, which Detection Audition plays too.
std::vector<Jump> detectionEqJumps()
{
    std::vector<Jump> eq;
    std::copy_if (jumps().begin(), jumps().end(), std::back_inserter (eq), [] (const Jump& j) { return j.setting.starts_with ("Detection EQ"); });
    return eq;
}

} // namespace

TEST_CASE ("Jumps in each compressor setting do not click", "[sweep]")
{
    const auto& jump = GENERATE (from_range (jumps()));
    const auto host = anyHost();
    CAPTURE (jump.setting, host.sampleRate, host.cut);
    const auto play = [&host, &jump] (const std::function<void (double, Settings&)>& change) {
        return playTone (host, change, jump.amplitude, toneSeconds, jump.numChannels);
    };
    const auto output = play ([&jump] (double time, Settings& s) { jump.set (s, alternating (time)); });
    double steady = 1.0;
    for (const auto& settings : jump.steady)
        steady = std::max (steady, discontinuity (play ([&settings] (double, Settings& s) { settings (s); }), host.sampleRate));
    CAPTURE (steady);
    CHECK (discontinuity (output, host.sampleRate) < clickLimit * steady);
}

namespace
{

// Mid-release, where only Release moves the gain. Each cycle a tone holds 9 dB of Gain Reduction for loudSeconds,
// long enough to charge Auto Release's sustain state most of the way, then fades over fadeSeconds to 1 dB under
// Threshold, so the gain comes back at Release's pace, and fades back up after quietSeconds. The Ratio is ∞:1 and the
// Knee hard, so the loud tone is held at Threshold and the quiet one asks for no Gain Reduction; Attack is slow enough
// not to put a corner of its own in the gain as the tone comes back. Each cycle is a whole number of periods and an
// eighth, so the setting jumps at a different point of the tone each time.
//
// A jump in how fast the gain comes back leaves the gain where it is but bends its slope: a corner, sharpest against
// the tone at the high sample rates and the shortest Release. On the steady tone of the test above, Release never
// gets to give much back, so a bare Release jump passes there; here it clicks at 176.4 and 192 kHz.
constexpr double midReleaseThresholdDb = -24.0;
constexpr double loudSeconds = 1.0, fadeSeconds = 0.005, quietSeconds = 0.200625;
constexpr double cycleSeconds = loudSeconds + 2.0 * fadeSeconds + quietSeconds;
constexpr int cycles = 8;

// The tone's amplitude through the cycles: loud, fading down, quiet, fading up, along raised cosines.
double midReleaseAmplitude (double seconds)
{
    const double loud = std::pow (10.0, (midReleaseThresholdDb + 9.0) / 20.0);
    const double quiet = std::pow (10.0, (midReleaseThresholdDb - 1.0) / 20.0);
    const double t = std::fmod (seconds, cycleSeconds);
    const auto fade = [] (double from, double to, double x) { return from + (to - from) * (1.0 - std::cos (std::numbers::pi * x)) / 2.0; };
    if (t < loudSeconds)
        return loud;
    if (t < loudSeconds + fadeSeconds)
        return fade (loud, quiet, (t - loudSeconds) / fadeSeconds);
    if (t < loudSeconds + fadeSeconds + quietSeconds)
        return quiet;
    return fade (quiet, loud, (t - loudSeconds - fadeSeconds - quietSeconds) / fadeSeconds);
}

// True from the end of each fade down to the start of the next fade up: the setting jumps as the gain starts coming
// back, and jumps back once it has.
bool midRelease (double seconds)
{
    const double t = std::fmod (seconds, cycleSeconds);
    return t >= loudSeconds + fadeSeconds && t < loudSeconds + fadeSeconds + quietSeconds;
}

struct MidRelease
{
    std::string setting;
    std::function<void (Settings&, bool, bool)> set; // sets it for whether it is mid-release, and whether that just began
};

const std::vector<MidRelease>& midReleaseJumps()
{
    static const std::vector<MidRelease> all {
        // From the longest Release to the shortest as the gain starts coming back, and back once it has.
        { "Release", [] (Settings& s, bool midRelease, bool) { s.releaseMs = midRelease ? 5.0 : 5000.0; } },
        // Auto Release on, then off, in turn, as the gain starts coming back: from Release to up to 5 times it, and
        // back.
        { "Auto Release",
          [] (Settings& s, bool, bool began) {
              s.releaseMs = 20.0;
              if (began)
                  s.autoRelease = ! s.autoRelease;
          } },
    };
    return all;
}

} // namespace

TEST_CASE ("Jumps in each Detection EQ setting do not click in Detection Audition", "[sweep]")
{
    const auto& jump = GENERATE (from_range (detectionEqJumps()));
    const auto host = anyHost();
    CAPTURE (jump.setting, host.sampleRate, host.cut);
    const auto output = playTone (host, [&jump] (double time, Settings& s) {
        s.detectionAudition = true;
        jump.set (s, alternating (time));
    });
    CHECK (discontinuity (output, host.sampleRate) < clickLimit);
}

TEST_CASE ("Jumps in Stereo Placement do not click in Detection Audition", "[sweep]")
{
    const auto& jump = GENERATE (filter ([] (const Jump& j) { return j.setting.starts_with ("Stereo Placement"); }, from_range (jumps())));
    const auto host = anyHost();
    CAPTURE (jump.setting, host.sampleRate, host.cut);
    const auto output = playTone (
        host,
        [&jump] (double time, Settings& s) {
            s.detectionAudition = true;
            jump.set (s, alternating (time));
        },
        [] (double) { return toneAmplitude; },
        toneSeconds,
        jump.numChannels);
    CHECK (discontinuity (output, host.sampleRate) < clickLimit);
}

TEST_CASE ("Jumps in Release and Auto Release mid-release do not click", "[sweep]")
{
    const auto& jump = GENERATE (from_range (midReleaseJumps()));
    const auto host = anyHost();
    CAPTURE (jump.setting, host.sampleRate, host.cut);
    bool wasMidRelease = false;
    const auto output = playTone (
        host,
        [&jump, &wasMidRelease] (double time, Settings& s) {
            s.thresholdDb = midReleaseThresholdDb;
            s.ratio = infinity;
            s.kneeDb = 0.0;
            s.attackMs = 50.0;
            const bool now = midRelease (time);
            jump.set (s, now, now && ! wasMidRelease);
            wasMidRelease = now;
        },
        midReleaseAmplitude,
        cycles * cycleSeconds);
    CHECK (discontinuity (output, host.sampleRate) < clickLimit);
}

TEST_CASE ("While a compressor setting glides, the output is the same however the audio is cut into blocks", "[sweep]")
{
    const auto& jump = GENERATE (from_range (jumps()));
    // The reference plays the audio in blocks of 4096; that cut is left out, as it would equal the reference by
    // construction.
    const std::vector<int> wholeCut { 4096 };
    const auto cut = GENERATE_REF (filter ([&wholeCut] (const std::vector<int>& c) { return c != wholeCut; }, from_range (test::blockCuts())));
    CAPTURE (jump.setting, cut);
    // Noise, loud enough to be compressed throughout.
    const auto input = test::noise (2, 20000);
    Settings before, after;
    jump.set (before, false);
    jump.set (after, true);
    // The jump lands 100 samples in, part-way through a run.
    const test::SettingsChange jumped { .at = 100, .settings = after };
    CHECK (test::played (before, 48000.0, input, cut, nullptr, jumped) == test::played (before, 48000.0, input, wholeCut, nullptr, jumped));
}

TEST_CASE ("A Processing Mode switch, each way, mid-tone, does not click, at every Oversampling")
{
    const auto host = anyHost();
    // Dry Off, and Dry and Wet both at 0 dB, so both paths fade; and with the Limiter on, the tone pushed into it,
    // measured against the steady tone limited in each mode.
    const double dryDb = GENERATE (-infinity, 0.0);
    const bool limiter = GENERATE (false, true);
    // 1x, and 8x, which plays each rate's cap.
    const auto oversampling = GENERATE (Oversampling::x1, Oversampling::x8);
    CAPTURE (host.sampleRate, host.cut, dryDb, limiter, static_cast<int> (oversampling));
    const auto pushed = [dryDb, limiter, oversampling] (Settings& s, ProcessingMode mode) {
        s.dryDb = dryDb;
        if (limiter)
            limitingTone (s, mode);
        s.processingMode = mode;
        s.oversampling = oversampling;
    };
    const auto output = playTone (host, [&pushed] (double time, Settings& s) {
        pushed (s, alternating (time) ? ProcessingMode::HighQuality : ProcessingMode::ZeroLatency);
    });
    double steady = 1.0;
    for (const auto mode : { ProcessingMode::ZeroLatency, ProcessingMode::HighQuality })
        if (limiter)
            steady = std::max (steady, discontinuity (playTone (host, [&pushed, mode] (double, Settings& s) { pushed (s, mode); }), host.sampleRate));
    CHECK (discontinuity (output, host.sampleRate) < clickLimit * steady);
}

TEST_CASE ("An Oversampling change, each way between 1x and the largest allowed, mid-tone, with Saturation or Colour on, does not click", "[sweep]")
{
    const auto host = anyHost();
    // Saturation Before and After, and Colour alone, on VCA and FET.
    const int scene = GENERATE (0, 1, 2, 3);
    CAPTURE (host.sampleRate, host.cut, scene);
    const auto largest = maxOversampling (host.sampleRate);
    const bool colour = scene >= 2;
    // In High Quality, Saturation on at Drive 12 dB with the tone at -6 dB, 6 dB into the shape, and Ratio 1:1, so the
    // shape works as hard at either position; or Colour, the tone compressed far enough for it to be heard.
    const auto saturatingAt = [scene, colour] (Settings& s, Oversampling oversampling) {
        s.processingMode = ProcessingMode::HighQuality;
        s.oversampling = oversampling;
        if (colour)
        {
            s.character = scene == 2 ? Character::VCA : Character::FET;
            return;
        }
        s.ratio = 1.0;
        s.saturation = true;
        s.driveDb = 12.0;
        s.saturationPosition = scene == 0 ? SaturationPosition::Before : SaturationPosition::After;
    };
    // Saturation's and Colour's harmonics sharpen the tone's corners past a sine's, so the change is measured against
    // the steady tone at each end.
    // Long enough for six changes after the onset.
    const Envelope loud = [colour] (double seconds) { return colour ? colourful (seconds) : 2.0 * toneAmplitude; };
    constexpr double seconds = onsetSeconds + 0.3;
    double steady = 1.0;
    for (const auto oversampling : { Oversampling::x1, largest })
        steady = std::max (steady, discontinuity (playTone (host, [&] (double, Settings& s) { saturatingAt (s, oversampling); }, loud, seconds), host.sampleRate));
    CAPTURE (steady);
    const auto output = playTone (host, [&] (double time, Settings& s) { saturatingAt (s, alternating (time) ? largest : Oversampling::x1); }, loud, seconds);
    CHECK (discontinuity (output, host.sampleRate) < clickLimit * steady);
}
