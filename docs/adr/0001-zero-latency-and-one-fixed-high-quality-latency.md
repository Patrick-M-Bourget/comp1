# Zero Latency by default, and one fixed latency in High Quality

comp1 has two Processing Modes. Zero Latency, the default, has no Oversampling and no Lookahead and reports 0, so comp1 can sit on live tracking. High Quality reports one fixed latency per sample rate, made of the largest allowed Oversampling's filters, plus 5 ms of Lookahead, plus the Limiter's own lookahead (about 1 ms, set in the lab). Oversampling and Lookahead vary inside that budget, with the audio delayed internally, so the latency the host sees never changes while the user adjusts them, and delay compensation never glitches.

This departs from eq1's ADR 0001 ("Zero Latency must mean zero, no oversampling") on purpose. A compressor's Colour and Limiter alias in ways no matched filter design can fix, and transient control needs Lookahead. comp1 keeps eq1's rule for its default mode and adds a mode that trades latency for both.

## Considered Options

- **Latency that follows the settings.** Rejected: every change to Oversampling or Lookahead changes the reported latency, and many hosts glitch or re-buffer when it moves.
- **Oversampling in Zero Latency too.** Rejected: its filters cost latency, so the mode would no longer be zero.
- **A Limiter that uses the compressor's Lookahead.** Rejected: with Lookahead at 0 ms, the Limiter's true-peak promise would break.

## Consequences

- **Oversampling cap.** The internal rate stays at or below about 384 kHz: up to 8x at 44.1/48 kHz, 4x at 88.2/96 kHz and 2x at 176.4/192 kHz. The choices offered are limited the same way, so the fixed latency in samples doesn't grow with the sample rate.
- **The Limiter in each mode.** In Zero Latency it is a zero-attack gain computer plus a safety clip at the Ceiling, on sample peak. In High Quality it uses its fixed lookahead and true-peak detection.
- **Switching.** A Processing Mode switch or an Oversampling change does a short fade-out, swap and fade-in (about 10–20 ms, set in the lab). A crossfade between paths of different delay would comb-filter. Lookahead changes only move an internal delay, which is smoothed.
- **Alignment.** The Dry path and Global Bypass are delayed to the reported latency.
