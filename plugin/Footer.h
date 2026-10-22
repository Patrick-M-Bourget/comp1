#pragma once

#include "plugkit/ui/Session.h"
#include "plugkit/ui/UiScaleMenu.h"
#include "plugkit/ui/staple/controls/IconButton.h"

#include <juce_audio_processors/juce_audio_processors.h>

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

    static constexpr int buttonSize = 28, inset = 6;

private:
    staple::IconButton globalBypass { "Global Bypass", staple::Icon::power };
    juce::AudioProcessorValueTreeState::ButtonAttachment globalBypassAttachment;
    plugkit::ui::UiScaleMenu uiScale;
};

} // namespace comp1
