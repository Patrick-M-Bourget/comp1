#pragma once

namespace comp1
{

// How strongly the Level and Gain Reduction Graph and the Spectrum display shade the Gain Reduction's area, as an
// alpha: faint enough that the grid and the lines show through it. Both displays dim it further under Global Bypass
// (GlobalBypassFade).
inline constexpr float gainReductionShadingAlpha = 0.35f;

} // namespace comp1
