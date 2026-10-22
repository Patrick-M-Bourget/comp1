#pragma once

#include "../host/Layout.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

// Drives a processor as a host does: through its parameters, its buses and its saved state.
namespace host
{

inline juce::RangedAudioParameter& parameter (juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() == id)
            return *ranged;
    FAIL ("No parameter " << id);
    throw std::logic_error ("unreachable");
}

// A parameter's plain value, and setting one, as a host does: through the normalised value.
inline float value (juce::AudioProcessor& processor, const juce::String& id)
{
    auto& p = parameter (processor, id);
    return p.convertFrom0to1 (p.getValue());
}

inline void set (juce::AudioProcessor& processor, const juce::String& id, float plain)
{
    auto& p = parameter (processor, id);
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
}

inline juce::MemoryBlock saved (juce::AudioProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    return block;
}

// A saved state decoded as setStateInformation() reads it: the block starts with a binary header, so its raw bytes
// never show the XML.
inline std::unique_ptr<juce::XmlElement> savedXml (juce::AudioProcessor& processor)
{
    const auto block = saved (processor);
    return juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
}

inline void restore (juce::AudioProcessor& processor, const juce::MemoryBlock& block)
{
    processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
}

// Records, for each parameter change the host hears, whether it came inside that parameter's gesture.
struct GestureRecorder final : juce::AudioProcessorListener
{
    std::set<int> open;
    std::vector<std::pair<int, bool>> changes; // the parameter's index, and whether its gesture was open
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { changes.emplace_back (index, open.contains (index)); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { open.insert (index); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { open.erase (index); }
};

inline void restore (juce::AudioProcessor& processor, const juce::XmlElement& xml)
{
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (xml, block);
    restore (processor, block);
}

} // namespace host
