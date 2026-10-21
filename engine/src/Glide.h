#pragma once

// How every gliding setting takes a new value: it glides there, or with snap (the settings in force at prepare()) it
// is there at once.

namespace comp1
{

// glide is anything with reset (target), to be there at once, and setTarget (target), to glide there: plugkit's
// Smoother, or the mode fade.
template <typename Glide, typename Target>
void glideTo (Glide& glide, const Target& target, bool snap)
{
    if (snap)
        glide.reset (target);
    else
        glide.setTarget (target);
}

} // namespace comp1
