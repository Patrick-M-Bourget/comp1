#pragma once

// comp1's Stereo Placement as plugkit routes it.

#include "comp1/Settings.h"

#include <plugkit/Placement.h>

namespace comp1
{

inline plugkit::StereoPlacement routed (StereoPlacement placement)
{
    switch (placement)
    {
        case StereoPlacement::Stereo: return plugkit::StereoPlacement::Stereo;
        case StereoPlacement::Mid: return plugkit::StereoPlacement::Mid;
        case StereoPlacement::Side: return plugkit::StereoPlacement::Side;
    }
    return plugkit::StereoPlacement::Stereo;
}

} // namespace comp1
