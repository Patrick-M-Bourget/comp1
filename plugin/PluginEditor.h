#pragma once

#include "PluginProcessor.h"
#include "plugkit/ui/EditorShell.h"
#include "plugkit/ui/OutputMeter.h"
#include "plugkit/ui/UiScaleMenu.h"
#include "plugkit/ui/staple/controls/IconButton.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace comp1
{

// The Expanded View's footer: Global Bypass at the far left, the power button showing Off while on, and the UI Scale
// menu at the right.
class Footer final : public juce::Component
{
public:
    Footer (juce::AudioProcessorValueTreeState& parameters, plugkit::ui::Session& session);

    void resized() override;

    plugkit::ui::UiScaleMenu& uiScaleMenu() { return uiScale; }

private:
    staple::IconButton globalBypass { "Global Bypass", staple::Icon::power };
    juce::AudioProcessorValueTreeState::ButtonAttachment globalBypassAttachment;
    plugkit::ui::UiScaleMenu uiScale;
};

// comp1's Expanded View, on plugkit's editor shell (EditorShell): its header, keys, UI Scale and window, with comp1's
// body and footer.
class PluginEditor final : public plugkit::ui::EditorShell
{
public:
    explicit PluginEditor (PluginProcessor& processor);

private:
    void layOutBody (juce::Rectangle<int> area) override;

    plugkit::ui::OutputMeter outputMeter;
    Footer footer;
};

} // namespace comp1
