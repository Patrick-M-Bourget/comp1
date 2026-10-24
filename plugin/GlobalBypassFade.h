#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace comp1
{

// Global Bypass as a display shows it: how far into Global Bypass it draws, 0 to 1, gliding over the design system's
// globalBypassFadeMs as Global Bypass switches. A display steps it on the editor's tick (step) and draws what shows the
// compressor's work at dim() of its alpha; what shows the signal itself stays. It never hides anything: Global Bypass
// passes the input through, and the display still shows what the compressor would do, dimmed so it reads as not
// applied. Made under Global Bypass, as an editor opened under it is, it starts at 1, with no fade.
class GlobalBypassFade
{
public:
    // Copied from eq1's plugin/staple/Tokens.h (eq1 at dbb1127), its staple::tokens::curve::globalBypassScale (which
    // eq1's display reads through its curveStyle alias), until plugkit's tokens have it (#137). Under Global Bypass, the
    // share of their own alphas what shows the compressor's work keeps: dim enough to read as not applied, bright enough
    // to read.
    static constexpr float scale = 0.45f;

    explicit GlobalBypassFade (juce::AudioProcessorValueTreeState& parameters);

    // How far into Global Bypass it shows, 0 to 1.
    float value() const { return fade; }
    // The fade from `from` after seconds, toward 1 while Global Bypass is on and 0 while it is off: each tick's step,
    // a whole fade taking globalBypassFadeMs.
    static float after (float from, bool on, double seconds);
    // Moves the fade toward Global Bypass by seconds' share of the fade time: whether it moved.
    bool step (double seconds);
    // Whether it is still on its way to Global Bypass.
    bool isMoving() const;
    // alpha, dimmed toward scale of itself as the fade goes to 1.
    float dim (float alpha) const { return juce::jmap (fade, alpha, alpha * scale); }

private:
    bool isOn() const;

    const std::atomic<float>& globalBypass;
    float fade = 0.0f;
};

} // namespace comp1
