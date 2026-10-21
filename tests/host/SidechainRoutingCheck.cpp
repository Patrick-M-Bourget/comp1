// Loads a built comp1 the way a host does, through its format wrapper (VST3 or AU), and checks every main layout
// with every Sidechain: mono and stereo main, each with no, a mono or a stereo Sidechain. Each layout must load and
// play its main input with Output Gain applied, the Sidechain unheard in the output. Run by plugkit's check
// harness (validate).
//
//   comp1_sidechain_routing_check <path to comp1.vst3>
//   comp1_sidechain_routing_check AU   (the AU must be installed and registered)

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>

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
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (main);
            layout.inputBuses.add (sidechain);
            layout.outputBuses.add (main);
            const auto description = main.getDescription() + " main, "
                                     + (sidechain.isDisabled() ? juce::String ("no Sidechain") : sidechain.getDescription() + " Sidechain");
            if (! plugin->setBusesLayout (layout))
            {
                // Some wrappers can't switch a bus off; then there's nothing more to check for this layout.
                std::cout << (sidechain.isDisabled() ? "  skip  " : "  FAIL  ") << description << ": layout refused\n";
                failures += sidechain.isDisabled() ? 0 : 1;
                continue;
            }
            plugin->prepareToPlay (sampleRate, blockSize);
            if (! set (*plugin, "Output Gain", "-12"))
                return 1;
            expect (description + ", main input alone", gainDb (*plugin, false, -12.0), -12.0);
            expect (description + ", Sidechain playing", gainDb (*plugin, true, -12.0), -12.0);
            plugin->releaseResources();
        }
    std::cout << (failures == 0 ? "SUCCESS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
