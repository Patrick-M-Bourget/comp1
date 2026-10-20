# Frozen Style list

Hosts store automation and state as normalised values, so the meaning of every value of the Style parameter is frozen at the first release, as eq1's ADR 0003 does for Shape and Slope. The order is Clean, VCA, FET, Opto, Vocal, Instrument, Bus, Drums: the Characters first, then the Source Styles. There are no reserved entries.

## Consequences

- A new Style needs a new parameter value appended at the end, plus a state migration. Reordering is never allowed after release.
- The same rule covers every other enum that reaches state, such as Processing Mode and Stereo Placement. This is why Saturation has one shape in v1: more shapes need their own frozen list.
