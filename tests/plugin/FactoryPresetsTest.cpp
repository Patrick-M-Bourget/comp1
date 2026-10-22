// The Factory Presets: what the plugin bundles from presets/factory, and the gate each one passes, the content rules
// every Factory Preset follows: starting points built on a Style, compressing modestly. Each rule is a function of one Preset, or of the library for unique names, giving what is wrong
// with it or nothing; the gate runs every rule over the real library, and each rule is shown to reject a bad example.

#include "Measure.h"
#include "ParametersOnly.h"
#include "SessionHost.h"
#include "plugkit/ui/PresetSettings.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace
{

using FactoryPreset = plugkit::ui::PresetLibrary::FactoryPreset;
namespace ids = comp1::parameters;

std::vector<FactoryPreset> factoryPresets()
{
    const host::SessionHost host;
    return host.session.presetLibrary().factoryPresets();
}

// The Factory library's names are "<Category> – <Name>", with an en dash (U+2013).
const juce::String dash = juce::String::fromUTF8 (" \xe2\x80\x93 ");
const juce::String sidechainSuffix { " (Sidechain)" };

juce::String named (const char* utf8) { return juce::String::fromUTF8 (utf8); }

// A Preset as a Factory file holds it: a comp1 root at the current state version, with a PARAM for each setting.
FactoryPreset preset (const char* nameUtf8, std::initializer_list<std::pair<const juce::String, float>> settings = {})
{
    juce::ValueTree tree ("comp1");
    tree.setProperty ("version", comp1::PluginProcessor::stateVersion, nullptr);
    for (const auto& [id, value] : settings)
        tree.appendChild (juce::ValueTree ("PARAM").setProperty ("id", id, nullptr).setProperty ("value", value, nullptr), nullptr);
    return { named (nameUtf8), tree };
}

// The value a PARAM holds, as written in the file: a number, or inf or -inf for Ratio ∞:1 and Off.
float fileValue (const juce::ValueTree& setting)
{
    const auto text = setting.getProperty ("value").toString().trim();
    if (text == "inf")
        return std::numeric_limits<float>::infinity();
    if (text == "-inf")
        return -std::numeric_limits<float>::infinity();
    return static_cast<float> (text.getDoubleValue());
}

// The PARAM the Preset holds for id, or an invalid tree.
juce::ValueTree heldSetting (const FactoryPreset& p, const juce::String& id) { return p.preset.getChildWithProperty ("id", id); }

// The Preset's settings as the Engine gets them: its PARAMs put on a fresh set of host parameters as a Preset load
// puts them, the rest at their defaults, read as the Plugin Shell reads them.
comp1::Settings settingsOf (const FactoryPreset& p)
{
    host::ParametersOnly processor;
    plugkit::ui::applyPresetSettings (processor.parameters, plugkit::ui::everyParameterButGlobalBypass, p.preset);
    return comp1::parameters::SettingsReader (processor.parameters).read();
}

juce::String categoryOf (const FactoryPreset& p) { return p.name.upToFirstOccurrenceOf (dash, false, false); }

// --- The rules -----------------------------------------------------------------------------------------------

// Rule: it loads through the Session, and restores exactly the values its file holds, every other Preset setting at
// its default.
juce::String loadingProblem (const FactoryPreset& p)
{
    host::SessionHost host;
    // Every Preset setting moved off its default first, so one the Preset leaves out has to come back to it.
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        auto& parameter = host::parameter (*host.processor, id);
        host.edit (id, parameter.convertFrom0to1 (parameter.getDefaultValue() < 0.5f ? 1.0f : 0.0f));
    }
    if (! host.session.loadPreset (p.preset, p.name))
        return "doesn't load";
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        auto& parameter = host::parameter (*host.processor, id);
        if (const auto setting = heldSetting (p, id); setting.isValid())
        {
            const auto expected = fileValue (setting);
            // As exactly as a host parameter's normalised value carries it, an infinity exactly.
            const auto restored = host.value (id);
            const bool same = std::isinf (expected) ? juce::exactlyEqual (restored, expected)
                                                    : std::abs (restored - expected) <= std::max (1.0e-4f, std::abs (expected) * 1.0e-6f);
            if (! same)
                return id + " is " + juce::String (restored) + ", not the " + juce::String (expected) + " its file holds";
        }
        else if (! juce::exactlyEqual (parameter.getValue(), parameter.getDefaultValue()))
        {
            return id + " isn't at its default";
        }
    }
    return {};
}

// Rule: it is at the current state version and holds only PARAMs, each a Preset setting away from its default: no
// unknown ID, no Global Bypass, no A/B Compare child.
juce::String formatProblem (const FactoryPreset& p)
{
    if (static_cast<int> (p.preset.getProperty ("version", 0)) != comp1::PluginProcessor::stateVersion)
        return "not at state version " + juce::String (comp1::PluginProcessor::stateVersion);
    host::SessionHost host;
    const auto settingIds = host::presetSettingIds (*host.processor);
    for (const auto& child : p.preset)
    {
        if (child.hasType (plugkit::ui::ABCompare::stateType))
            return "holds an A/B Compare child";
        if (! child.hasType ("PARAM"))
            return "holds a " + child.getType().toString();
        const auto id = child.getProperty ("id").toString();
        if (! settingIds.contains (id))
            return "holds " + id + ", not a Preset setting";
        auto& parameter = host::parameter (*host.processor, id);
        if (juce::exactlyEqual (parameter.convertFrom0to1 (parameter.getDefaultValue()), fileValue (child)))
            return id + " is at its default";
    }
    return {};
}

// Rule: no two Factory Presets share a name.
juce::String duplicateName (const std::vector<FactoryPreset>& library)
{
    std::set<juce::String> names;
    for (const auto& p : library)
        if (! names.insert (p.name).second)
            return "named twice: " + p.name;
    return {};
}

// Rule: but for Default, the name is "<Category> – <Name>", the Category one of these eight.
juce::String namingProblem (const FactoryPreset& p)
{
    static const juce::StringArray categories { "Vocals", "Drums", "Bass", "Guitar", "Keys", "Mix Bus", "Master", "Utility" };
    if (p.name == "Default")
        return {};
    if (! p.name.contains (dash))
        return "not named <Category>" + dash + "<Name>";
    const auto category = categoryOf (p);
    const auto name = p.name.fromFirstOccurrenceOf (dash, false, false);
    if (! categories.contains (category))
        return "no such Category: " + category;
    if (name.trim().isEmpty() || name != name.trim() || name.contains (dash))
        return "no single Name after the Category";
    return {};
}

// Rule: with Comp Follow on, it holds no Threshold, Ratio or Auto Gain, which Comp Follow ignores or holds on.
juce::String compFollowProblem (const FactoryPreset& p)
{
    if (! settingsOf (p).compFollow)
        return {};
    for (const auto& id : { ids::thresholdId, ids::ratioId, ids::autoGainId })
        if (heldSetting (p, id).isValid())
            return "holds " + id + " with Comp Follow on";
    return {};
}

bool isSidechainPreset (const FactoryPreset& p) { return p.name.endsWith (sidechainSuffix); }

// Rule: Detection Source External exactly when the name ends " (Sidechain)"; and such a Preset holds Comp Follow and
// Auto Gain off, so with nothing routed nothing lifts or lowers the signal.
juce::String sidechainProblem (const FactoryPreset& p)
{
    const auto settings = settingsOf (p);
    const bool external = settings.detectionSource == comp1::DetectionSource::External;
    if (external && ! isSidechainPreset (p))
        return "Detection Source External, without" + sidechainSuffix + " in its name";
    if (! external && isSidechainPreset (p))
        return "named" + sidechainSuffix + ", with Detection Source Internal";
    if (isSidechainPreset (p) && settings.compFollow)
        return "a Sidechain Preset with Comp Follow on";
    if (isSidechainPreset (p) && settings.autoGain)
        return "a Sidechain Preset with Auto Gain on";
    return {};
}

// Rule: High Quality only in the Master Category.
juce::String processingModeProblem (const FactoryPreset& p)
{
    if (settingsOf (p).processingMode == comp1::ProcessingMode::HighQuality && categoryOf (p) != "Master")
        return "High Quality outside Master";
    return {};
}

// --- The audio rules --------------------------------------------------------------------------------------------

using comp1::test::Channels;
constexpr double sampleRate = 48000.0;
constexpr int seconds = 2;

// The RMS level of samples in dB.
double rmsDb (const float* samples, int count)
{
    double power = 0.0;
    for (int i = 0; i < count; ++i)
        power += static_cast<double> (samples[i]) * samples[i];
    return 10.0 * std::log10 (power / count);
}

// Seeded pink-like noise on each channel at an RMS level of rmsLevelDb: white noise (comp1::test::noise) through Paul
// Kellet's three-pole pink filter, each channel its own, the same on every platform.
Channels pinkNoise (int numChannels, int numSamples, double rmsLevelDb, std::uint32_t seed = 7)
{
    auto audio = comp1::test::noise (numChannels, numSamples, seed);
    for (auto& channel : audio)
    {
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;
        for (auto& x : channel)
        {
            const double w = x;
            b0 = 0.99765 * b0 + 0.0990460 * w;
            b1 = 0.96300 * b1 + 0.2965164 * w;
            b2 = 0.57000 * b2 + 1.0526913 * w;
            x = static_cast<float> (b0 + b1 + b2 + 0.1848 * w);
        }
        const auto gain = static_cast<float> (std::pow (10.0, (rmsLevelDb - rmsDb (channel.data(), numSamples)) / 20.0));
        for (auto& x : channel)
            x *= gain;
    }
    return audio;
}

// What the plugin plays from a stereo input with the Preset loaded, at 48 kHz in blocks of 512, as a host plays it:
// with a Sidechain connected and silent, or with none connected. Its latency comes back too.
struct Played
{
    Channels output;
    int latency = 0;
};

Played playedThroughPlugin (const FactoryPreset& p, const Channels& input, bool sidechainConnected)
{
    constexpr int blockSize = 512;
    host::SessionHost host;
    auto& processor = *host.processor;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (sidechainConnected ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));
    processor.prepareToPlay (sampleRate, blockSize);
    REQUIRE (host.session.loadPreset (p.preset, p.name));
    const int numSamples = static_cast<int> (input.front().size());
    Played played { Channels (2, std::vector<float> (static_cast<std::size_t> (numSamples))), processor.getLatencySamples() };
    juce::AudioBuffer<float> buffer (processor.getTotalNumInputChannels(), blockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int count = std::min (blockSize, numSamples - start);
        buffer.setSize (buffer.getNumChannels(), count, false, false, true);
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, input[static_cast<std::size_t> (ch)].data() + start, count);
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy_n (buffer.getReadPointer (ch), count, played.output[static_cast<std::size_t> (ch)].data() + start);
    }
    return played;
}

// Rule: 2 s of stereo pink noise at -18 dBFS RMS comes out finite, its peak no more than 12 dB above the input's.
juce::String loudnessProblem (const FactoryPreset& p)
{
    const auto input = pinkNoise (2, static_cast<int> (sampleRate) * seconds, -18.0);
    const auto played = playedThroughPlugin (p, input, true);
    for (const auto& channel : played.output)
        if (! std::all_of (channel.begin(), channel.end(), [] (float x) { return std::isfinite (x); }))
            return "output not finite";
    if (const auto overDb = 20.0 * std::log10 (comp1::test::samplePeak (played.output) / comp1::test::samplePeak (input)); overDb > 12.0)
        return "peak " + juce::String (overDb, 1) + " dB above the input's";
    return {};
}

// Rule: a Sidechain Preset, with no Sidechain connected, plays 2 s of stereo pink noise at -18 dBFS RMS within
// 0.01 dB of its input, in every 100 ms of it on each channel, its latency allowed for.
juce::String unroutedSidechainProblem (const FactoryPreset& p)
{
    if (! isSidechainPreset (p))
        return {};
    const int numSamples = static_cast<int> (sampleRate) * seconds;
    const auto input = pinkNoise (2, numSamples, -18.0);
    const auto played = playedThroughPlugin (p, input, false);
    constexpr int window = static_cast<int> (sampleRate / 10);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (int start = played.latency; start + window <= numSamples; start += window)
        {
            const auto differenceDb = rmsDb (played.output[ch].data() + start, window) - rmsDb (input[ch].data() + start - played.latency, window);
            if (! (std::abs (differenceDb) <= 0.01))
                return "with no Sidechain, its output is " + juce::String (differenceDb, 3) + " dB from its input";
        }
    return {};
}

// --- The Gain Reduction check -------------------------------------------------------------------------------

// The reference signals, synthetic and seeded, stereo at 48 kHz. The Factory Presets take material as gain-staged,
// since the Comp Curves' Thresholds are absolute: tracks around -18 dBFS RMS, peaks around -6 dBFS.
constexpr int referenceSamples = static_cast<int> (sampleRate) * 8;

// A track's: pink noise in phrases, 1.5 s sung and 0.5 s silent, each phrase in syllables at 4 Hz that swing 12 dB,
// at -18 dBFS RMS over the phrases.
Channels phrasedPinkNoise()
{
    auto audio = pinkNoise (2, referenceSamples, 0.0, 11);
    double power = 0.0;
    int sung = 0;
    for (int n = 0; n < referenceSamples; ++n)
    {
        const double t = n / sampleRate;
        const bool inPhrase = std::fmod (t, 2.0) < 1.5;
        const double syllable = std::pow (10.0, -12.0 / 20.0 * std::pow (std::sin (std::numbers::pi * 4.0 * t), 2.0));
        for (auto& channel : audio)
        {
            auto& x = channel[static_cast<std::size_t> (n)];
            x = inPhrase ? static_cast<float> (x * syllable) : 0.0f;
            power += static_cast<double> (x) * x;
        }
        sung += inPhrase ? 2 : 0;
    }
    const auto gain = static_cast<float> (std::pow (10.0, -18.0 / 20.0) / std::sqrt (power / sung));
    for (auto& channel : audio)
        for (auto& x : channel)
            x *= gain;
    return audio;
}

// Drums': a train of decaying pink-noise bursts at 120 bpm, each falling 1/e every 40 ms, peaking at -6 dBFS.
Channels decayingBursts()
{
    auto audio = pinkNoise (2, referenceSamples, 0.0, 13);
    for (auto& channel : audio)
        for (int n = 0; n < referenceSamples; ++n)
            channel[static_cast<std::size_t> (n)] *= static_cast<float> (std::exp (-std::fmod (n / sampleRate, 0.5) / 0.040));
    const auto gain = static_cast<float> (std::pow (10.0, -6.0 / 20.0) / comp1::test::samplePeak (audio));
    for (auto& channel : audio)
        for (auto& x : channel)
            x *= gain;
    return audio;
}

// A Mix Bus's and a Master's: steady pink noise at -14 dBFS RMS.
Channels steadyPinkNoise() { return pinkNoise (2, referenceSamples, -14.0, 17); }

// A sum of 32 sines spread evenly in log frequency from lowHz to highHz, seeded phases, each channel its own, so it
// holds nothing outside that band; at rmsLevelDb while sounding (of the time in seconds) is true, silent otherwise.
template <typename Sounding>
Channels toneBand (double lowHz, double highHz, double rmsLevelDb, Sounding sounding, std::uint32_t seed)
{
    constexpr int tones = 32;
    std::uint32_t state = seed;
    Channels audio (2, std::vector<float> (static_cast<std::size_t> (referenceSamples)));
    double power = 0.0;
    int on = 0;
    for (auto& channel : audio)
    {
        std::vector<double> frequencies, phases;
        for (int k = 0; k < tones; ++k)
        {
            state = 1664525u * state + 1013904223u;
            frequencies.push_back (lowHz * std::pow (highHz / lowHz, static_cast<double> (k) / (tones - 1)));
            phases.push_back (std::numbers::pi * (state / 2147483648.0 - 1.0));
        }
        for (int n = 0; n < referenceSamples; ++n)
        {
            if (! sounding (n / sampleRate))
                continue;
            double sum = 0.0;
            for (std::size_t k = 0; k < frequencies.size(); ++k)
                sum += std::sin (2.0 * std::numbers::pi * frequencies[k] * n / sampleRate + phases[k]);
            channel[static_cast<std::size_t> (n)] = static_cast<float> (sum);
            power += sum * sum;
            ++on;
        }
    }
    const auto gain = static_cast<float> (std::pow (10.0, rmsLevelDb / 20.0) / std::sqrt (power / on));
    for (auto& channel : audio)
        for (auto& x : channel)
            x *= gain;
    return audio;
}

// The De-Esser's: sibilance, 5 to 10 kHz in 100 ms bursts four times a second at -20 dBFS RMS; and what it must leave
// alone, steady noise of 50 Hz to 2.8 kHz, all below 3 kHz, at -18 dBFS RMS.
Channels sibilantBursts()
{
    return toneBand (5000.0, 10000.0, -20.0, [] (double t) { return std::fmod (t, 0.25) < 0.1; }, 19);
}
Channels noiseBelow3kHz()
{
    return toneBand (50.0, 2800.0, -18.0, [] (double) { return true; }, 23);
}

// The compressor's median Gain Reduction in dB over the runs where the signal is present, the Preset's settings
// through a fresh Engine at 48 kHz. Present is an Input Level within 20 dB of the loudest run's: a phrase, or the
// body of a drum hit, not the gaps or a hit's fading tail.
double medianGainReductionDb (const FactoryPreset& p, const Channels& signal)
{
    constexpr double presentWithinDb = 20.0;
    const auto records = comp1::test::levelHistory (settingsOf (p), sampleRate, signal);
    const auto loudest = std::max_element (records.begin(), records.end(), [] (const auto& a, const auto& b) { return a.inputDb < b.inputDb; });
    REQUIRE (loudest != records.end());
    std::vector<double> reductions;
    for (const auto& record : records)
        if (record.inputDb > loudest->inputDb - presentWithinDb)
            reductions.push_back (record.gainReductionDb);
    REQUIRE_FALSE (reductions.empty());
    std::nth_element (reductions.begin(), reductions.begin() + static_cast<std::ptrdiff_t> (reductions.size() / 2), reductions.end());
    return reductions[reductions.size() / 2];
}

// The Presets whose purpose is to crush, or to compress hard under a Dry path: they may reach 30 dB. The batches add
// their Parallel Presets here.
const juce::StringArray& crushAndParallel()
{
    static const juce::StringArray names { named ("Drums \xe2\x80\x93 All-In Crush"), named ("Drums \xe2\x80\x93 Room Smash"),
                                           named ("Vocals \xe2\x80\x93 Parallel Thicken"), named ("Mix Bus \xe2\x80\x93 Parallel Lift") };
    return names;
}

const juce::String deEsser = named ("Vocals \xe2\x80\x93 De-Esser");

// The Gain Reduction a Preset's Category gives on that Category's reference signal, in dB: a sanity range, not a
// tuning target. One row per Category, then the exceptions.
struct Band
{
    double lowDb, highDb;
    Channels (*signal)();
};

std::optional<Band> bandOf (const FactoryPreset& p)
{
    static const std::vector<std::pair<juce::String, Band>> categories {
        { "Vocals", { 1.0, 10.0, phrasedPinkNoise } }, { "Bass", { 1.0, 10.0, phrasedPinkNoise } },
        { "Guitar", { 1.0, 10.0, phrasedPinkNoise } }, { "Keys", { 1.0, 10.0, phrasedPinkNoise } },
        { "Drums", { 2.0, 15.0, decayingBursts } },    { "Mix Bus", { 0.5, 5.0, steadyPinkNoise } },
        { "Master", { 0.5, 5.0, steadyPinkNoise } },
    };
    // Default and Utility have none: Default compresses as a new instance does, and a Utility Preset's job (a Gate,
    // a Limiter) isn't the compressor's. The De-Esser has its own check (deEsserProblem()).
    const auto category = std::find_if (categories.begin(), categories.end(), [&p] (const auto& row) { return row.first == categoryOf (p); });
    if (category == categories.end() || p.name == deEsser)
        return std::nullopt;
    auto band = category->second;
    const auto settings = settingsOf (p);
    // With nothing routed, a Sidechain Preset's detector hears silence; Comp at 0% compresses nothing.
    if (isSidechainPreset (p) || (settings.compFollow && juce::exactlyEqual (settings.comp, 0.0)))
        band.lowDb = band.highDb = 0.0;
    else if (crushAndParallel().contains (p.name))
        band.highDb = 30.0;
    return band;
}

// Rule: the compressor's median Gain Reduction on the Category's reference signal is within its band.
juce::String gainReductionProblem (const FactoryPreset& p)
{
    const auto band = bandOf (p);
    if (! band)
        return {};
    if (const auto reductionDb = medianGainReductionDb (p, band->signal()); reductionDb < band->lowDb || reductionDb > band->highDb)
        return "Gain Reduction " + juce::String (reductionDb, 2) + " dB, outside " + juce::String (band->lowDb, 1) + " to "
               + juce::String (band->highDb, 1) + " dB";
    return {};
}

// Rule: the De-Esser takes at least 3 dB off sibilant bursts, and under 1 dB off noise below 3 kHz.
juce::String deEsserProblem (const FactoryPreset& p)
{
    if (p.name != deEsser)
        return {};
    if (const auto onSibilance = medianGainReductionDb (p, sibilantBursts()); onSibilance < 3.0)
        return "only " + juce::String (onSibilance, 2) + " dB off sibilance";
    if (const auto belowSibilance = medianGainReductionDb (p, noiseBelow3kHz()); belowSibilance >= 1.0)
        return juce::String (belowSibilance, 2) + " dB off noise below 3 kHz";
    return {};
}

} // namespace

TEST_CASE ("Every file in the Factory folder is bundled as a Factory Preset, and nothing else is")
{
    // Compared by name: a Factory Preset is its file's name without the extension.
    juce::StringArray files;
    for (const auto& file : juce::File (juce::CharPointer_UTF8 (COMP1_FACTORY_PRESETS)).findChildFiles (juce::File::findFiles, false, "*.comp1preset"))
        files.add (file.getFileNameWithoutExtension());
    juce::StringArray bundled;
    for (const auto& p : factoryPresets())
        bundled.add (p.name);
    files.sort (false);
    bundled.sort (false);
    REQUIRE (files.contains ("Default"));
    CHECK (bundled == files);

    // And the browser lists each under Factory.
    const host::SessionHost host;
    juce::StringArray listed;
    for (const auto& entry : host.session.presetLibrary().listing())
        if (entry.folder == "Factory")
            listed.add (entry.name);
    listed.sort (false);
    CHECK (listed == files);
}

TEST_CASE ("Default loads as a new instance's state: every Preset setting at its default")
{
    const auto factory = factoryPresets();
    const auto found = std::find_if (factory.begin(), factory.end(), [] (const auto& p) { return p.name == "Default"; });
    REQUIRE (found != factory.end());
    CHECK (found->preset.getNumChildren() == 0);
    CHECK (loadingProblem (*found) == "");
}

TEST_CASE ("Every Factory Preset passes the gate")
{
    const auto factory = factoryPresets();
    REQUIRE_FALSE (factory.empty());
    CHECK (duplicateName (factory) == "");
    for (const auto& p : factory)
    {
        CAPTURE (p.name);
        CHECK (loadingProblem (p) == "");
        CHECK (formatProblem (p) == "");
        CHECK (namingProblem (p) == "");
        CHECK (compFollowProblem (p) == "");
        CHECK (sidechainProblem (p) == "");
        CHECK (processingModeProblem (p) == "");
        CHECK (loudnessProblem (p) == "");
        CHECK (unroutedSidechainProblem (p) == "");
        CHECK (gainReductionProblem (p) == "");
        CHECK (deEsserProblem (p) == "");
    }
}

TEST_CASE ("The Factory gate rejects a Preset that doesn't load, or doesn't restore what its file holds")
{
    CHECK (loadingProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f }, { ids::ratioId, std::numeric_limits<float>::infinity() } })) == "");
    CHECK (loadingProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -500.0f } })).isNotEmpty()); // beyond its range
    auto foreign = preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } });
    foreign.preset = juce::ValueTree ("eq1");
    CHECK (loadingProblem (foreign).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a Preset at another version, holding a default, an unknown ID or an A/B Compare child")
{
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } })) == "");
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -18.0f } })).isNotEmpty());
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { "nonsense", 1.0f } })).isNotEmpty());
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::globalBypassId, 1.0f } })).isNotEmpty());
    auto older = preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } });
    older.preset.setProperty ("version", comp1::PluginProcessor::stateVersion - 1, nullptr);
    CHECK (formatProblem (older).isNotEmpty());
    auto compared = preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } });
    compared.preset.appendChild (juce::ValueTree (plugkit::ui::ABCompare::stateType), nullptr);
    CHECK (formatProblem (compared).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects two Presets of one name")
{
    CHECK (duplicateName ({ preset ("Drums \xe2\x80\x93 Kick"), preset ("Drums \xe2\x80\x93 Snare") }) == "");
    CHECK (duplicateName ({ preset ("Drums \xe2\x80\x93 Kick"), preset ("Drums \xe2\x80\x93 Kick") }).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a name outside the Category scheme")
{
    for (const auto* good : { "Default", "Vocals \xe2\x80\x93 One-Knob Lead", "Drums \xe2\x80\x93 Kick", "Bass \xe2\x80\x93 DI", "Guitar \xe2\x80\x93 Strum",
                              "Keys \xe2\x80\x93 Piano", "Mix Bus \xe2\x80\x93 Glue", "Master \xe2\x80\x93 Polish", "Utility \xe2\x80\x93 Noise Gate" })
    {
        CAPTURE (named (good));
        CHECK (namingProblem (preset (good)) == "");
    }
    for (const auto* bad : { "Kick Punch", "Drums - Kick", "Drums\xe2\x80\x93Kick", "Drum \xe2\x80\x93 Kick", "Synth \xe2\x80\x93 Lead",
                             "Drums \xe2\x80\x93  Kick", "Drums \xe2\x80\x93 ", "Drums \xe2\x80\x93 Kick \xe2\x80\x93 In" })
    {
        CAPTURE (named (bad));
        CHECK (namingProblem (preset (bad)).isNotEmpty());
    }
}

TEST_CASE ("The Factory gate rejects Threshold, Ratio or Auto Gain held with Comp Follow on")
{
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::compId, 60.0f } })) == "");
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -30.0f }, { ids::autoGainId, 1.0f } })) == "");
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::thresholdId, -30.0f } })).isNotEmpty());
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::ratioId, 3.0f } })).isNotEmpty());
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::autoGainId, 1.0f } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects Detection Source External without (Sidechain) in the name, the reverse, and a Sidechain Preset that lifts or lowers")
{
    constexpr float external = 1.0f;
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::thresholdId, -30.0f } })) == "");
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck", { { ids::detectionSourceId, external } })).isNotEmpty());
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)")).isNotEmpty());
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::compFollowId, 1.0f } })).isNotEmpty());
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::autoGainId, 1.0f } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects High Quality outside the Master Category")
{
    constexpr float highQuality = 1.0f;
    CHECK (processingModeProblem (preset ("Master \xe2\x80\x93 Loud", { { ids::processingModeId, highQuality } })) == "");
    CHECK (processingModeProblem (preset ("Mix Bus \xe2\x80\x93 Glue")) == "");
    CHECK (processingModeProblem (preset ("Mix Bus \xe2\x80\x93 Glue", { { ids::processingModeId, highQuality } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a Preset that lifts pink noise's peak by more than 12 dB")
{
    CHECK (loudnessProblem (preset ("Master \xe2\x80\x93 Loud", { { ids::outputGainId, 9.0f } })) == "");
    CHECK (loudnessProblem (preset ("Master \xe2\x80\x93 Loud", { { ids::outputGainId, 15.0f } })).isNotEmpty());
    CHECK (loudnessProblem (preset ("Master \xe2\x80\x93 Loud", { { ids::dryId, 12.0f }, { ids::wetId, 12.0f } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a Sidechain Preset that changes the signal with no Sidechain connected")
{
    static constexpr float external = 1.0f;
    const auto sidechained = [] (std::initializer_list<std::pair<const juce::String, float>> more) {
        auto made = preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::thresholdId, -40.0f }, { ids::ratioId, 8.0f } });
        for (const auto& [id, value] : more)
            made.preset.appendChild (juce::ValueTree ("PARAM").setProperty ("id", id, nullptr).setProperty ("value", value, nullptr), nullptr);
        return made;
    };
    CHECK (unroutedSidechainProblem (sidechained ({})) == "");
    CHECK (unroutedSidechainProblem (sidechained ({ { ids::gateId, 1.0f } })) == "");
    CHECK (unroutedSidechainProblem (sidechained ({ { ids::outputGainId, 0.1f } })).isNotEmpty());
    CHECK (unroutedSidechainProblem (sidechained ({ { ids::limiterId, 1.0f }, { ids::ceilingId, -20.0f } })).isNotEmpty());
    CHECK (unroutedSidechainProblem (sidechained ({ { ids::saturationId, 1.0f }, { ids::driveId, 24.0f } })).isNotEmpty());
    CHECK (unroutedSidechainProblem (sidechained ({ { ids::dryId, 0.0f } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a compressor Gain Reduction outside its Category's band")
{
    constexpr float on = 1.0f, vocal = 4.0f, bus = 6.0f, drums = 7.0f;
    // One knob at the middle of each Category's Style lands inside the band; the ends of Comp, or a Threshold above
    // the material, don't.
    CHECK (gainReductionProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::styleId, vocal }, { ids::compFollowId, on } })) == "");
    CHECK (gainReductionProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::styleId, vocal }, { ids::compFollowId, on }, { ids::compId, 100.0f } })).isNotEmpty());
    CHECK (gainReductionProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -2.0f } })).isNotEmpty());
    CHECK (gainReductionProblem (preset ("Drums \xe2\x80\x93 Snare", { { ids::styleId, drums }, { ids::compFollowId, on } })) == "");
    CHECK (gainReductionProblem (preset ("Drums \xe2\x80\x93 Snare", { { ids::thresholdId, -2.0f } })).isNotEmpty());
    CHECK (gainReductionProblem (preset ("Mix Bus \xe2\x80\x93 Glue", { { ids::styleId, bus }, { ids::compFollowId, on } })) == "");
    CHECK (gainReductionProblem (preset ("Mix Bus \xe2\x80\x93 Glue", { { ids::styleId, bus }, { ids::compFollowId, on }, { ids::compId, 100.0f } })).isNotEmpty());
    CHECK (gainReductionProblem (preset ("Master \xe2\x80\x93 Polish", { { ids::thresholdId, -2.0f } })).isNotEmpty());

    // Crushing is allowed only to the named Presets.
    const std::initializer_list<std::pair<const juce::String, float>> crush { { ids::styleId, drums }, { ids::thresholdId, -40.0f }, { ids::ratioId, 20.0f } };
    CHECK (gainReductionProblem (preset ("Drums \xe2\x80\x93 All-In Crush", crush)) == "");
    CHECK (gainReductionProblem (preset ("Drums \xe2\x80\x93 Snare", crush)).isNotEmpty());

    // Comp at 0% and an unrouted Sidechain Preset compress nothing, and a Preset that does under those names fails.
    CHECK (gainReductionProblem (preset ("Vocals \xe2\x80\x93 Open", { { ids::compFollowId, on }, { ids::compId, 0.0f } })) == "");
    CHECK (gainReductionProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, 1.0f }, { ids::thresholdId, -40.0f } })) == "");
    CHECK (gainReductionProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::thresholdId, -40.0f } })).isNotEmpty());

    // Default and Utility have no band.
    CHECK (gainReductionProblem (preset ("Default")) == "");
    CHECK (gainReductionProblem (preset ("Utility \xe2\x80\x93 Safety Limiter", { { ids::thresholdId, -60.0f }, { ids::ratioId, 20.0f } })) == "");
}

TEST_CASE ("The Factory gate rejects a De-Esser that misses sibilance, or compresses what's below it")
{
    const auto deEsserWith = [] (std::initializer_list<std::pair<const juce::String, float>> settings) {
        return preset ("Vocals \xe2\x80\x93 De-Esser", settings);
    };
    CHECK (deEsserProblem (deEsserWith ({ { ids::detectionHighPassOnId, 1.0f },
                                          { ids::detectionHighPassFrequencyId, 5000.0f },
                                          { ids::detectionHighPassSlopeId, 48.0f },
                                          { ids::thresholdId, -40.0f },
                                          { ids::attackId, 1.0f },
                                          { ids::releaseId, 50.0f } }))
           == "");
    CHECK (deEsserProblem (deEsserWith ({ { ids::detectionHighPassOnId, 1.0f },
                                          { ids::detectionHighPassFrequencyId, 5000.0f },
                                          { ids::detectionHighPassSlopeId, 48.0f },
                                          { ids::thresholdId, -40.0f },
                                          { ids::ratioId, 1.0f } }))
               .isNotEmpty());
    CHECK (deEsserProblem (deEsserWith ({ { ids::thresholdId, -40.0f } })).isNotEmpty());
    // Its own check, not the Vocals band.
    CHECK (gainReductionProblem (deEsserWith ({ { ids::thresholdId, -2.0f } })) == "");
}

namespace
{

// What the plugin plays from a stereo input with the Preset loaded and a stereo key on the Sidechain, at 48 kHz in
// blocks of 512, as a host plays it.
Channels playedWithKey (const FactoryPreset& p, const Channels& input, const Channels& key)
{
    constexpr int blockSize = 512;
    host::SessionHost host;
    auto& processor = *host.processor;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));
    processor.prepareToPlay (sampleRate, blockSize);
    REQUIRE (host.session.loadPreset (p.preset, p.name));
    const int numSamples = static_cast<int> (input.front().size());
    Channels output (2, std::vector<float> (static_cast<std::size_t> (numSamples)));
    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int count = std::min (blockSize, numSamples - start);
        buffer.setSize (4, count, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            buffer.copyFrom (ch, 0, input[static_cast<std::size_t> (ch)].data() + start, count);
            buffer.copyFrom (ch + 2, 0, key[static_cast<std::size_t> (ch)].data() + start, count);
        }
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy_n (buffer.getReadPointer (ch), count, output[static_cast<std::size_t> (ch)].data() + start);
    }
    return output;
}

} // namespace

TEST_CASE ("Every Sidechain Preset dips the signal by at least 3 dB under a loud key on the Sidechain")
{
    // The Utility Sidechain Presets, each of which must be in the library.
    const auto factory = factoryPresets();
    for (const auto* name : { "Utility \xe2\x80\x93 Duck Under Voice (Sidechain)", "Utility \xe2\x80\x93 Kick Pump (Sidechain)" })
    {
        CAPTURE (named (name));
        CHECK (std::any_of (factory.begin(), factory.end(), [name] (const auto& p) { return p.name == named (name); }));
    }
    // Pink noise on the main at -18 dBFS RMS, and on the Sidechain at -12 dBFS RMS: loud, and in band for any Detection
    // EQ, a kick's lows as much as a voice's middle. Measured over the second second, once the dip has settled.
    const int numSamples = static_cast<int> (sampleRate) * seconds;
    const auto input = pinkNoise (2, numSamples, -18.0, 29);
    const auto key = pinkNoise (2, numSamples, -12.0, 31);
    const int settled = static_cast<int> (sampleRate);
    for (const auto& p : factory)
    {
        if (! isSidechainPreset (p))
            continue;
        CAPTURE (p.name);
        const auto output = playedWithKey (p, input, key);
        for (std::size_t ch = 0; ch < 2; ++ch)
            CHECK (rmsDb (input[ch].data() + settled, numSamples - settled) - rmsDb (output[ch].data() + settled, numSamples - settled) >= 3.0);
    }
}

TEST_CASE ("Duck Under Kick, the Bass Sidechain Preset, leaves the bass alone unrouted, and dips it under a kick on the Sidechain")
{
    const auto factory = factoryPresets();
    const auto found = std::find_if (factory.begin(), factory.end(), [] (const auto& p) {
        return p.name == named ("Bass \xe2\x80\x93 Duck Under Kick (Sidechain)");
    });
    REQUIRE (found != factory.end());
    CHECK (unroutedSidechainProblem (*found) == "");

    // A bass line, 40 to 200 Hz at -18 dBFS RMS, and on the Sidechain a kick at 120 bpm: 40 to 120 Hz in 100 ms hits
    // at -10 dBFS RMS. Or hi-hats instead, 5 to 10 kHz in 30 ms hits between the kicks at -10 dBFS RMS, which its
    // Detection Low-pass keeps it from hearing.
    const auto onKick = [] (double t) { return std::fmod (t, 0.5) < 0.1; };
    const auto onHat = [] (double t) { return std::fmod (t + 0.25, 0.5) < 0.03; };
    const auto bass = toneBand (40.0, 200.0, -18.0, [] (double) { return true; }, 29);
    const auto kick = toneBand (40.0, 120.0, -10.0, onKick, 31);
    const auto hats = toneBand (5000.0, 10000.0, -10.0, onHat, 37);
    const auto settings = settingsOf (*found);
    // The output's level against the bass's, in dB, over the times when (t) holds, after the first second.
    const auto changeDb = [&bass] (const Channels& output, auto when) {
        double in = 0.0, out = 0.0;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (int n = static_cast<int> (sampleRate); n < referenceSamples; ++n)
                if (when (n / sampleRate))
                {
                    const auto i = static_cast<std::size_t> (n);
                    in += static_cast<double> (bass[ch][i]) * bass[ch][i];
                    out += static_cast<double> (output[ch][i]) * output[ch][i];
                }
        return 10.0 * std::log10 (out / in);
    };
    CHECK (changeDb (comp1::test::played (settings, sampleRate, bass, { 512 }, &kick), onKick) <= -3.0);
    CHECK (changeDb (comp1::test::played (settings, sampleRate, bass, { 512 }, &hats), onHat) > -0.5);
}

TEST_CASE ("A parallel Factory Preset's Dry path is heard: with Wet Off, its output is its input")
{
    std::vector<FactoryPreset> parallel;
    for (const auto& p : factoryPresets())
        if (! std::isinf (settingsOf (p).dryDb))
            parallel.push_back (p);
    REQUIRE (std::any_of (parallel.begin(), parallel.end(), [] (const auto& p) { return p.name == named ("Drums \xe2\x80\x93 All-In Crush"); }));
    const auto input = pinkNoise (2, static_cast<int> (sampleRate) * seconds, -18.0);
    for (const auto& p : parallel)
    {
        CAPTURE (p.name);
        auto settings = settingsOf (p);
        settings.wetDb = -comp1::parameterRanges::inf;
        const auto latency = comp1::test::impulseDelaySamples (settings, sampleRate);
        REQUIRE (latency >= 0);
        const auto output = comp1::test::played (settings, sampleRate, input);
        CHECK (comp1::test::worstRelativeError (output, comp1::test::delayed (input, latency), std::pow (10.0, settings.dryDb / 20.0)) < 1.0e-6);
    }
}

TEST_CASE ("The limiting Master Presets hold a hot master's true peak to their -1 dBFS Ceiling, in High Quality")
{
    // Pink noise at -8 dBFS RMS, its true peak well over the Ceiling: a master pushed hard into the Limiter.
    const auto input = pinkNoise (2, referenceSamples, -8.0, 41);
    REQUIRE (comp1::test::truePeak (input) > comp1::test::gainOf (-1.0));
    const auto factory = factoryPresets();
    for (const auto* name : { "Master \xe2\x80\x93 Loud Limit", "Master \xe2\x80\x93 Warm Glue" })
    {
        CAPTURE (named (name));
        const auto found = std::find_if (factory.begin(), factory.end(), [name] (const auto& p) { return p.name == named (name); });
        REQUIRE (found != factory.end());
        const auto settings = settingsOf (*found);
        CHECK (settings.processingMode == comp1::ProcessingMode::HighQuality);
        CHECK (settings.limiter);
        CHECK (std::abs (settings.ceilingDb + 1.0) < 1.0e-4);
        const auto played = playedThroughPlugin (*found, input, false);
        CHECK (comp1::test::truePeak (played.output, played.latency) <= comp1::test::gainOf (-1.0));
    }
}

TEST_CASE ("The Factory library uses every Style and every feature at least once")
{
    // Default isn't counted: it holds a new instance's state, Comp Follow off among it.
    std::vector<FactoryPreset> factory;
    for (const auto& p : factoryPresets())
        if (p.name != "Default")
            factory.push_back (p);
    std::vector<comp1::Settings> library;
    for (const auto& p : factory)
        library.push_back (settingsOf (p));

    // Every Style: the one each file holds, or Clean, the Style parameter's default, when it holds none.
    static const juce::StringArray styleNames { "Clean", "VCA", "FET", "Opto", "Vocal", "Instrument", "Bus", "Drums" };
    std::set<int> styles;
    for (const auto& p : factory)
        styles.insert (heldSetting (p, ids::styleId).isValid() ? juce::roundToInt (fileValue (heldSetting (p, ids::styleId))) : 0);
    for (int style = 0; style < styleNames.size(); ++style)
    {
        CAPTURE (styleNames[style]);
        CHECK (styles.contains (style));
    }

    // Every feature, as the Engine gets it; each a gap until some Preset uses it.
    using comp1::Settings;
    const std::initializer_list<std::pair<const char*, bool (*) (const Settings&)>> features {
        { "the Gate", [] (const Settings& s) { return s.gate.on; } },
        { "the Limiter", [] (const Settings& s) { return s.limiter; } },
        { "Saturation Before", [] (const Settings& s) { return s.saturation && s.saturationPosition == comp1::SaturationPosition::Before; } },
        { "Saturation After", [] (const Settings& s) { return s.saturation && s.saturationPosition == comp1::SaturationPosition::After; } },
        { "Dry/Wet with both paths heard", [] (const Settings& s) { return ! std::isinf (s.dryDb) && ! std::isinf (s.wetDb); } },
        { "the Detection EQ's High-pass", [] (const Settings& s) { return s.detectionEq.highPass.on; } },
        { "the Detection EQ's Low-pass", [] (const Settings& s) { return s.detectionEq.lowPass.on; } },
        { "the Detection EQ's free band", [] (const Settings& s) { return s.detectionEq.freeBand.on; } },
        { "Detection Source External", [] (const Settings& s) { return s.detectionSource == comp1::DetectionSource::External; } },
        { "Stereo Placement Mid", [] (const Settings& s) { return s.stereoPlacement == comp1::StereoPlacement::Mid; } },
        { "Stereo Placement Side", [] (const Settings& s) { return s.stereoPlacement == comp1::StereoPlacement::Side; } },
        { "Stereo Link under 100%", [] (const Settings& s) { return s.stereoPlacement == comp1::StereoPlacement::Stereo && s.stereoLink < 100.0; } },
        { "Hold above 0 ms", [] (const Settings& s) { return s.holdMs > 0.0; } },
        { "Range under 60 dB", [] (const Settings& s) { return s.rangeDb < 60.0; } },
        { "Ratio inf:1", [] (const Settings& s) { return ! s.compFollow && std::isinf (s.ratio); } },
        { "Auto Release", [] (const Settings& s) { return s.autoRelease; } },
        { "Comp Follow off", [] (const Settings& s) { return ! s.compFollow; } },
        { "High Quality with Lookahead", [] (const Settings& s) { return s.processingMode == comp1::ProcessingMode::HighQuality && s.lookaheadMs > 0.0; } },
        { "Colour under 100%", [] (const Settings& s) { return s.colour < 100.0; } },
    };
    for (const auto& [feature, uses] : features)
    {
        CAPTURE (feature);
        CHECK (std::any_of (library.begin(), library.end(), uses));
    }
}
