# ADAA on the nonlinear residual only

In Zero Latency, Colour and Saturation are anti-aliased with antiderivative anti-aliasing (ADAA), which needs no Oversampling and so adds no latency. Applied to the whole signal, first-order ADAA also delays it by half a sample and dulls the top end, even where the waveshaper is linear: −17 dB at 20 kHz at 44.1 kHz. That breaks "Zero Latency delivers 0" and the Dry/Wet null. So the waveshaper's output is split into its linear part, which passes sample-aligned, and its nonlinear residual, f(x) − x, and only the residual goes through ADAA.

## Consequences

- At Colour 0, or with no gain reduction, the Wet path is exactly linear and aligned. With the Gate off and no compression, Dry plus Wet nulls exactly against the input, so the Dry path needs no all-pass.
- Above Colour 0, the residual's half-sample offset is a measured quantity, with a tolerance the lab sets.
- A contributor "simplifying" to whole-signal ADAA would bring back the delay and the droop.

## Considered Options

- **Whole-signal ADAA with a fractional all-pass on the Dry path** (the spec's first draft). Rejected: the all-pass fixes the phase but not the high-frequency droop, and the null would never be exact.
- **Oversampling in Zero Latency.** Rejected by ADR 0001.
