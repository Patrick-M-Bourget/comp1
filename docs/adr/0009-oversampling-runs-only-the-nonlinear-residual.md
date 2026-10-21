# Oversampling runs only the nonlinear residual

In High Quality, a nonlinear stage keeps its linear part at the host rate and oversamples only its nonlinear residual, f(u) − u. The residual goes up through the Oversampling filters, is worked out at the oversampled rate, comes back down, and is added to the linear part, which is delayed by exactly as much. The compressor's gain stays at the host rate.

Saturation follows this today. Colour is meant to as well, but for now it runs inside the compressor at the host rate in both Processing Modes, anti-aliased by its ADAA alone (ADR 0005): oversampling it means the compressor handing its Colour residual to the After section, which #63 plans.

This extends ADR 0005's split of the waveshaper into a sample-aligned linear part and a residual that alone is anti-aliased. Filters that see only the residual can't touch the linear part. So the Wet path is exactly linear and aligned whenever nothing is nonlinear (Saturation off, no Colour), and Dry plus Wet nulls exactly against the input at every Oversampling, as it does at 1x. The filters' delay is a fixed count of host samples at every factor, padded to the same total, because ADR 0001's cap on the internal rate keeps it so. Nothing in the Wet path's delay moves with the factor, and the reported latency never changes.

## Considered Options

- **Oversampling the whole Wet path, with the null relaxed to a tolerance.** Rejected: the filters' passband ripple and their roll-off near Nyquist would colour the clean signal at every setting, Saturation off included, and the null would never be exact again.
- **The same, with the Dry path run through the same filter pair.** Rejected: it doubles the filters' cost, and the two paths match only while the Wet path is linear. Once a nonlinear stage sits between the filters, the Wet path's filtered signal no longer matches the Dry path's.

## Consequences

- Below the stage's knee, or with it off, an oversampled section is a plain delay, bit for bit, at every factor.
- The filters' passband needn't reach 20 kHz, since they carry only the residual's harmonics: dyn-lab sets their figures (`python3 tools/dyn-lab/dynlab.py oversampling`).
- Each nonlinear stage keeps its ADAA (ADR 0005), now running at the oversampled rate.
- A contributor "simplifying" to oversample the whole Wet path would break the exact null and colour the clean sound.
