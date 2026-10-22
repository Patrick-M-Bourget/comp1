#pragma once

#include "comp1/CompCurve.h"
#include "comp1/Settings.h"

#include <array>
#include <cstddef>
#include <optional>

namespace comp1
{

// The Styles: the starting points of the one picker, the four Characters first, then the four Source Styles
// (ADR 0002). Hosts save the Style parameter by index, so this order is frozen at the first release (ADR 0003): a new
// Style goes at the end, with a state migration, and none is ever reordered.
enum class Style
{
    Clean,
    VCA,
    FET,
    Opto,
    Vocal,
    Instrument,
    Bus,
    Drums,
};

inline constexpr int styleCount = 8;

// What choosing a Style writes besides its Character and Comp Curve (ADR 0002): its default Comp, with Comp Follow on,
// the compressor's Colour, Threshold, Ratio, Knee, Attack, Release, Auto Release, Hold and Range, the Gate's on/off and
// five settings, and the Limiter's on/off and Ceiling, in Settings' units. Never Auto Gain, the routing or the levels.
struct StyleDefaults
{
    double comp;
    double colour;
    double thresholdDb;
    double ratio;
    double kneeDb;
    double attackMs;
    double releaseMs;
    bool autoRelease;
    double holdMs;
    double rangeDb;
    Settings::Gate gate;
    bool limiter;
    double ceilingDb;
};

// A Style, as one row: the Character it plays, its Comp Curve, and its defaults. Every Style has defaults; the row
// leaves room for one without, which applyStyle() and the Plugin Shell's offeredStyles() pass over.
struct StyleDescription
{
    Character character;
    CompCurve compCurve;
    std::optional<StyleDefaults> defaults;
};

// Every Style's row, in Style's order. Each Style's Character, defaults and Comp Curve are dyn-lab's (python3
// tools/dyn-lab/dynlab.py styles, and compcurve --style <name>; its USAGE.md, "Styles", "Source Styles" and "Comp",
// gives the reasons). Clean's are a default Settings', so a new instance holds its own Style's defaults, except Comp
// Follow, which a default Settings holds off so an instance plays its stored settings, while choosing any Style turns it
// on. Every Style writes its default Comp, 50%, the middle of its Comp Curve, and every Style but Vocal writes the Gate
// and the Limiter off, at their defaults. Vocal is the Gate, then Clean with Auto Release, then the Limiter: it writes
// the Gate on at its defaults and the Limiter on at Ceiling 0 dBFS.
inline constexpr Settings::Gate gateOff {};
inline constexpr Settings::Gate gateOn { .on = true };
inline constexpr std::array<StyleDescription, styleCount> styles {
    StyleDescription { Character::Clean, cleanCompCurve, StyleDefaults { 50.0, 100.0, -18.0, 4.0, 6.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::VCA,
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 4.0 }, CompPoint { 100.0, -36.0, 10.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 4.0, 3.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::FET,
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 4.0 }, CompPoint { 100.0, -28.0, 20.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 4.0, 3.0, 0.5, 50.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::Opto,
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 3.0 }, CompPoint { 100.0, -42.0, 6.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 3.0, 12.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::Clean, // Vocal
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 3.0 }, CompPoint { 100.0, -36.0, 6.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 3.0, 6.0, 10.0, 100.0, true, 0.0, 60.0, gateOn, true, 0.0 } },
    StyleDescription { Character::Opto, // Instrument
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 4.0 }, CompPoint { 100.0, -36.0, 8.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 4.0, 6.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::VCA, // Bus
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 2.0 }, CompPoint { 100.0, -30.0, 4.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 2.0, 6.0, 30.0, 100.0, true, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::FET, // Drums
                       CompCurve { { CompPoint { 0.0, 0.0, 1.0 }, CompPoint { 50.0, -18.0, 4.0 }, CompPoint { 100.0, -30.0, 10.0 } } },
                       StyleDefaults { 50.0, 100.0, -18.0, 4.0, 3.0, 5.0, 50.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
};

inline constexpr const StyleDescription& describe (Style style) { return styles[static_cast<std::size_t> (style)]; }

// The Character a Style plays: the Engine plays a Character and never reads Style.
inline constexpr Character characterOf (Style style) { return describe (style).character; }

// The Comp Curve a Style's Comp follows: the Plugin Shell hands it to the Engine, which never reads Style.
inline constexpr const CompCurve& compCurveOf (Style style) { return describe (style).compCurve; }

// Whether a Style has defaults to write: every Style.
inline constexpr bool hasDefaults (Style style) { return describe (style).defaults.has_value(); }

// Writes a Style's Character, Comp Curve and defaults, Comp Follow on among them, into settings, touching nothing
// else, and returns true; a Style with no defaults changes nothing and returns false.
inline bool applyStyle (Style style, Settings& settings)
{
    const auto& row = describe (style);
    if (! row.defaults)
        return false;
    const auto& d = *row.defaults;
    settings.character = row.character;
    settings.compCurve = row.compCurve;
    settings.comp = d.comp;
    settings.compFollow = true;
    settings.colour = d.colour;
    settings.thresholdDb = d.thresholdDb;
    settings.ratio = d.ratio;
    settings.kneeDb = d.kneeDb;
    settings.attackMs = d.attackMs;
    settings.releaseMs = d.releaseMs;
    settings.autoRelease = d.autoRelease;
    settings.holdMs = d.holdMs;
    settings.rangeDb = d.rangeDb;
    settings.gate = d.gate;
    settings.limiter = d.limiter;
    settings.ceilingDb = d.ceilingDb;
    return true;
}

// settings with style's Character, Comp Curve and defaults (applyStyle), for a test or a scene that plays a Style.
inline Settings styled (Style style, Settings settings = {})
{
    applyStyle (style, settings);
    return settings;
}

} // namespace comp1
