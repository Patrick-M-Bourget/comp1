#pragma once

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

// What choosing a Style writes besides its Character (ADR 0002): the compressor's Colour, Threshold, Ratio, Knee,
// Attack, Release, Auto Release, Hold and Range, the Gate's on/off and five settings, and the Limiter's on/off and
// Ceiling, in Settings' units. Never the routing or the levels.
struct StyleDefaults
{
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

// A Style, as one row: the Character it plays, and its defaults. A Source Style has none yet, and plays Clean, until
// the Source Styles ticket fills its row (#26).
struct StyleDescription
{
    Character character;
    std::optional<StyleDefaults> defaults;
};

// Every Style's row, in Style's order. The Character Styles' defaults are dyn-lab's (python3 tools/dyn-lab/dynlab.py
// styles; its USAGE.md, "Styles", gives the reasons). Clean's are a default Settings', so a new instance holds its own
// Style's defaults. Every Style writes the Gate and the Limiter off, at their defaults.
inline constexpr Settings::Gate gateOff {};
inline constexpr std::array<StyleDescription, styleCount> styles {
    StyleDescription { Character::Clean, StyleDefaults { 100.0, -18.0, 4.0, 6.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::VCA, StyleDefaults { 100.0, -18.0, 4.0, 3.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::FET, StyleDefaults { 100.0, -18.0, 4.0, 3.0, 0.5, 50.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::Opto, StyleDefaults { 100.0, -18.0, 3.0, 12.0, 10.0, 100.0, false, 0.0, 60.0, gateOff, false, 0.0 } },
    StyleDescription { Character::Clean, std::nullopt }, // Vocal
    StyleDescription { Character::Clean, std::nullopt }, // Instrument
    StyleDescription { Character::Clean, std::nullopt }, // Bus
    StyleDescription { Character::Clean, std::nullopt }, // Drums
};

inline constexpr const StyleDescription& describe (Style style) { return styles[static_cast<std::size_t> (style)]; }

// The Character a Style plays: the Engine plays a Character and never reads Style.
inline constexpr Character characterOf (Style style) { return describe (style).character; }

// Whether a Style has defaults to write: the Character Styles, so far.
inline constexpr bool hasDefaults (Style style) { return describe (style).defaults.has_value(); }

// Writes a Style's Character and defaults into settings, touching nothing else, and returns true; a Style with no
// defaults changes nothing and returns false.
inline bool applyStyle (Style style, Settings& settings)
{
    const auto& row = describe (style);
    if (! row.defaults)
        return false;
    const auto& d = *row.defaults;
    settings.character = row.character;
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

// settings with style's Character and defaults (applyStyle), for a test or a scene that plays a Style.
inline Settings styled (Style style, Settings settings = {})
{
    applyStyle (style, settings);
    return settings;
}

} // namespace comp1
