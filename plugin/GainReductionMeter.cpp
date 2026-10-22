#include "GainReductionMeter.h"

#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/staple/Fonts.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;
} // namespace

GainReductionMeter::GainReductionMeter (LevelHistory& h) : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), history (h)
{
    setName ("Gain Reduction");
    setTitle ("Gain Reduction");
    setTooltip ("Gain Reduction");
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (false, false);
    history.subscribe (*this);
}

GainReductionMeter::~GainReductionMeter()
{
    history.unsubscribe (*this);
}

double GainReductionMeter::position (double db) { return std::clamp (db / scaleDb, 0.0, 1.0); }

void GainReductionMeter::levelsRead (std::span<const LevelRecord> records)
{
    for (const auto& record : records)
        most = std::max (most, record.gainReductionDb);
}

void GainReductionMeter::tick (double seconds)
{
    const double level = std::max (0.0, ballistics.update (most, seconds));
    most = 0.0;
    if (juce::exactlyEqual (level, shown))
        return;
    // Repainted only where the bar was or is now.
    const auto area = barArea();
    const float was = area.getY() + area.getHeight() * static_cast<float> (position (shown));
    shown = level;
    const float now = area.getY() + area.getHeight() * static_cast<float> (position (shown));
    if (! juce::exactlyEqual (was, now))
        repaint (area.withY (area.getY() - 1.0f).withBottom (std::max (was, now) + 1.0f).getSmallestIntegerContainer());
}

juce::Rectangle<float> GainReductionMeter::barArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromTop (static_cast<float> (labelHeight) / 2.0f);
    area.removeFromBottom (static_cast<float> (labelHeight) / 2.0f);
    return area.removeFromRight (area.getWidth() / 2.0f).reduced (static_cast<float> (barInset) / 2.0f, 0.0f);
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const auto bar = barArea();
    // The rail, then the bar hanging from its top.
    g.setColour (colour::knobRingLane);
    g.fillRoundedRectangle (bar, size::r1 / 2.0f);
    if (shown > 0.0)
    {
        const auto filled = bar.withHeight (bar.getHeight() * static_cast<float> (position (shown)));
        g.setGradientFill (juce::ColourGradient::vertical (colour::dynRange, bar.getY(), colour::dynRangeInner, bar.getBottom()));
        g.fillRect (filled);
    }
    // The scale, every labelStepDb, to the bar's left.
    g.setFont (staple::font (size::fs1));
    g.setColour (colour::text3);
    const auto labels = getLocalBounds().toFloat().withRight (bar.getX() - 2.0f);
    for (double db = 0.0; db <= scaleDb; db += labelStepDb)
    {
        const float y = bar.getY() + bar.getHeight() * static_cast<float> (position (db));
        g.drawText (juce::String (juce::roundToInt (db)),
                    labels.withY (y - static_cast<float> (labelHeight) / 2.0f).withHeight (static_cast<float> (labelHeight)),
                    juce::Justification::centredRight,
                    false);
    }
}

std::unique_ptr<juce::AccessibilityHandler> GainReductionMeter::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::progressBar, [this] { return juce::String (shown, 1) + " dB"; });
}

} // namespace comp1
