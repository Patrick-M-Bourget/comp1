#include "GlobalBypassFade.h"

#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

GlobalBypassFade::GlobalBypassFade (juce::AudioProcessorValueTreeState& parameters)
    : globalBypass (*parameters.getRawParameterValue (parameters::globalBypassId))
{
    fade = isOn() ? 1.0f : 0.0f;
}

bool GlobalBypassFade::isOn() const { return globalBypass.load (std::memory_order_relaxed) >= 0.5f; }

float GlobalBypassFade::after (float from, bool on, double seconds)
{
    const float step = static_cast<float> (seconds * 1000.0) / static_cast<float> (staple::tokens::motion::globalBypassFadeMs);
    return juce::jlimit (from - step, from + step, on ? 1.0f : 0.0f);
}

bool GlobalBypassFade::step (double seconds)
{
    const float moved = after (fade, isOn(), seconds);
    const bool changed = ! juce::exactlyEqual (moved, fade);
    fade = moved;
    return changed;
}

bool GlobalBypassFade::isMoving() const { return ! juce::exactlyEqual (fade, isOn() ? 1.0f : 0.0f); }

} // namespace comp1
