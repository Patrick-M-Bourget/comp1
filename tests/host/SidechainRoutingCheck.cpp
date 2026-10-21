// Loads a built comp1 the way a host does, through its format wrapper (VST3 or AU), and checks every main layout
// with every Sidechain: mono and stereo main, each with no, a mono or a stereo Sidechain. Each layout must load and
// play its main input with Output Gain applied, the Sidechain unheard in the output, at Ratio 1:1 so the compressor
// leaves the level alone. With Detection Source External, a playing Sidechain must then duck the main by the Transfer
// Curve's Gain Reduction, and a silent one leave it alone. Through AU, no Sidechain is not applicable (see main()). Run
// by plugkit's check harness (validate).
//
//   comp1_sidechain_routing_check <path to comp1.vst3>
//   comp1_sidechain_routing_check AU   (the AU must be installed and registered)

#include "Layout.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <utility>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& formats, const juce::String& what)
{
    for (int f = 0; f < formats.getNumFormats(); ++f)
    {
        auto* format = formats.getFormat (f);
        const bool au = what == "AU";
        if (au != (format->getName() == "AudioUnit") || (! au && format->getName() != "VST3"))
            continue;
        juce::StringArray candidates;
        if (au)
            candidates = format->searchPathsForPlugins ({}, false, false);
        else
            candidates.add (what);
        for (const auto& candidate : candidates)
        {
            juce::OwnedArray<juce::PluginDescription> types;
            format->findAllTypesForFile (types, candidate);
            for (auto* type : types)
                if (type->name == "comp1")
                {
                    juce::String error;
                    if (auto instance = formats.createPluginInstance (*type, sampleRate, blockSize, error))
                        return instance;
                    std::cerr << "Couldn't load " << type->fileOrIdentifier << ": " << error << "\n";
                }
        }
    }
    return nullptr;
}

// Sets a host parameter, found by name, to a plain value through the plugin's own text conversion.
bool set (juce::AudioProcessor& plugin, const juce::String& name, const juce::String& text)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName (100) == name)
        {
            parameter->setValueNotifyingHost (parameter->getValueForText (text));
            return true;
        }
    std::cerr << "No parameter " << name << "\n";
    return false;
}

// Level change in dB, on each main channel, of a 100 Hz sine through the plugin (a different phase per channel),
// with a full-scale 60 Hz sine on every Sidechain channel when sidechainPlaying. The worst channel's: the one
// furthest from expectedDb.
double gainDb (juce::AudioProcessor& plugin, bool sidechainPlaying, double expectedDb)
{
    const int mainChannels = plugin.getMainBusNumOutputChannels();
    const int channels = std::max (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;
    std::vector<double> input (static_cast<size_t> (mainChannels)), output (static_cast<size_t> (mainChannels));
    for (int block = 0, n = 0; block < 64; ++block)
    {
        buffer.clear();
        for (int i = 0; i < blockSize; ++i, ++n)
        {
            for (int ch = 0; ch < mainChannels; ++ch)
            {
                const auto s = static_cast<float> (0.25 * std::sin (2.0 * std::numbers::pi * 100.0 * n / sampleRate + ch));
                buffer.setSample (ch, i, s);
                if (block >= 32)
                    input[static_cast<size_t> (ch)] += s * s;
            }
            for (int ch = mainChannels; ch < plugin.getTotalNumInputChannels(); ++ch)
                buffer.setSample (ch, i, sidechainPlaying ? static_cast<float> (std::sin (2.0 * std::numbers::pi * 60.0 * n / sampleRate)) : 0.0f);
        }
        plugin.processBlock (buffer, midi);
        if (block >= 32)
            for (int ch = 0; ch < mainChannels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    output[static_cast<size_t> (ch)] += buffer.getSample (ch, i) * buffer.getSample (ch, i);
    }
    double worst = expectedDb;
    for (int ch = 0; ch < mainChannels; ++ch)
    {
        const double db = 10.0 * std::log10 (output[static_cast<size_t> (ch)] / input[static_cast<size_t> (ch)]);
        if (std::abs (db - expectedDb) > std::abs (worst - expectedDb))
            worst = db;
    }
    return worst;
}

int failures = 0;

void expect (const juce::String& what, double measured, double expected)
{
    const bool ok = std::abs (measured - expected) <= 0.01;
    failures += ok ? 0 : 1;
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << ": " << juce::String (measured, 3) << " dB (expected "
              << juce::String (expected, 1) << ")\n";
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: comp1_sidechain_routing_check <comp1.vst3 path | AU>\n";
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager (formats);
    auto plugin = load (formats, argv[1]);
    if (plugin == nullptr)
    {
        std::cerr << "comp1 not found: " << argv[1] << "\n";
        return 1;
    }
    std::cout << "Main layouts and Sidechain routing through " << plugin->getPluginDescription().pluginFormatName << "\n";
    if (plugin->getBusCount (true) != 2)
    {
        std::cerr << "  FAIL  expected a main input and a Sidechain, found " << plugin->getBusCount (true) << " input buses\n";
        return 1;
    }

    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    for (const auto& main : { mono, stereo })
        for (const auto& sidechain : { mono, stereo, juce::AudioChannelSet::disabled() })
        {
            const auto layout = host::layout (main, sidechain);
            const auto description = main.getDescription() + " main, "
                                     + (sidechain.isDisabled() ? juce::String ("no Sidechain") : sidechain.getDescription() + " Sidechain");
            if (! plugin->setBusesLayout (layout))
            {
                // An AU has no disabled bus: its input elements are fixed in number and each always has a stream
                // format of one channel or more, so JUCE's AU wrapper enables every bus and refuses a 0-channel
                // format on the Sidechain's element. A host leaves the element unconnected instead, and the wrapper
                // hands the plugin silence on it: the "main input alone" checks with a mono or stereo Sidechain
                // cover that. So no Sidechain is not applicable through AU, and counts as neither a pass nor a
                // failure. Any other format must take the layout.
                const bool notApplicable = sidechain.isDisabled() && plugin->getPluginDescription().pluginFormatName == "AudioUnit";
                std::cout << (notApplicable ? "  n/a   " : "  FAIL  ") << description
                          << (notApplicable ? ": AU has no disabled bus\n" : ": layout refused\n");
                failures += notApplicable ? 0 : 1;
                continue;
            }
            plugin->prepareToPlay (sampleRate, blockSize);
            if (! set (*plugin, "Detection Source", "Internal") || ! set (*plugin, "Ratio", "1") || ! set (*plugin, "Output Gain", "-12"))
                return 1;
            expect (description + ", main input alone", gainDb (*plugin, false, -12.0), -12.0);
            expect (description + ", Sidechain playing", gainDb (*plugin, true, -12.0), -12.0);

            // Detection Source External, at settings that hold the Sidechain's peak: a full-scale one takes 18 dB off
            // at Threshold -18, Ratio ∞:1 and Knee 0 (python3 tools/dyn-lab/dynlab.py transfer --threshold -18
            // --ratio inf --knee 0), and a silent one nothing, though the main input is over Threshold. The silent
            // case goes first: Release, at 5 s, would still be giving back the playing case's Gain Reduction.
            for (const auto& [name, text] : { std::pair { "Detection Source", "External" }, { "Threshold", "-18" }, { "Ratio", "inf" },
                                              { "Knee", "0" }, { "Attack", "0.005" }, { "Release", "5000" }, { "Hold", "500" } })
                if (! set (*plugin, name, text))
                    return 1;
            expect (description + ", External, Sidechain silent", gainDb (*plugin, false, -12.0), -12.0);
            if (! sidechain.isDisabled())
                expect (description + ", External, Sidechain playing", gainDb (*plugin, true, -30.0), -30.0);
            plugin->releaseResources();
        }
    std::cout << (failures == 0 ? "SUCCESS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
