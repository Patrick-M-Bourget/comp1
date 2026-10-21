# dyn-lab

A Python model of comp1's Characters. It prints the figures the Engine's tests check, so a ticket brief can cite a command and its output exactly. It needs only the standard library: no installs, no numpy. Run it from the repo root.

It models the Clean Character, with High Quality's Lookahead and latency, and the Saturation stage (see [Saturation](#saturation)). Clean follows Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor Design — A Tutorial and Analysis". It is feedforward, with a soft-knee gain computer (the paper's eq. 4) and a smooth branching peak detector in the log domain (eq. 12), plus Hold. Each Attack and Release is a single pole, so 63% (1 − 1/e) of a step takes one time constant and 90% takes ln 10 ≈ 2.303 of them. It isn't the paper's decoupled detector (eq. 17): that one runs Release through the attack pole as well, so gain comes back more slowly than Release says.

Hold freezes the Gain Reduction for Hold ms whenever it stops rising, mid-Attack included, before Release begins. Any rise restarts the count.

Auto Release, when on, lengthens Release after sustained compression and leaves it short after transients. See [Auto Release](#auto-release).

Range caps the Gain Reduction the gain computer asks for, before the detector smooths it. So the Transfer Curve rises with the input again, Range below it, once the Gain Reduction reaches Range, and Attack, Release and Hold take the same times over a smaller step. At 60 dB, its top, Range sets no limit (ADR 0008).

Every option has a default, and a setting outside its range exits with an error that names the range. The ranges are named once in `dynlab.py` (`THRESHOLD_RANGE`, `RATIO_RANGE` and so on), each with its source: Pro-C 3's documentation, ADR 0008 for Threshold, Release, Range and Drive, or ADR 0001 for Lookahead. Lookahead is a Processing Mode setting, read in High Quality only, not one of the compressor's. The check harness's `docs` stage fails when one differs from the plugin's, `engine/include/comp1/Settings.h`'s `parameterRanges` (`LAB_RANGES` in `plugkit.conf`).

| Setting | Range | Default |
|---|---|---|
| Threshold | −60 to 0 dB | −18 dB |
| Ratio | 1 to ∞ (`inf` or `∞`) | 4 |
| Knee | 0 to 72 dB | 6 dB |
| Attack | 0.005 to 250 ms | 10 ms |
| Release | 5 to 5000 ms | 100 ms |
| Hold | 0 to 500 ms | 0 ms |
| Range | 0 to 60 dB | 60 dB |
| Drive | 0 to 24 dB | 6 dB |
| Auto Release | off or on (`--auto-release`) | off |
| Lookahead | 0 to 5 ms | 0 ms |

## `transfer`

Prints the Transfer Curve, the output level for each steady input level. The input grid runs from 12 dB below the Knee to 24 dB above it, in 3 dB steps, and always includes Threshold, both edges of the Knee, and the level where the Gain Reduction reaches Range when the grid reaches it (rows marked `range` from there). A final line gives the measured slope at each end of the grid: 1 below the Knee, 1/Ratio above it, and 1 again once Range is reached.

    python3 tools/dyn-lab/dynlab.py transfer
    python3 tools/dyn-lab/dynlab.py transfer --threshold -24 --ratio 4 --knee 12
    python3 tools/dyn-lab/dynlab.py transfer --threshold -12 --ratio inf --knee 0
    python3 tools/dyn-lab/dynlab.py transfer --range 6

## `step`

Prints the step response at each sample rate (44.1, 48 and 96 kHz by default). The Detection Level steps from 20 dB below Threshold to 20 dB above it to measure Attack, then back down to measure Release. Each step starts from steady state. For each rate it reports:

- Attack: the time to reach 63% and 90% of the final Gain Reduction;
- Release: the time to give back 63% and 90% of it, counted from when Release begins;
- Hold: the time the Gain Reduction stays put before Release begins.

At Ratio 1:1 or Range 0 dB there is no Gain Reduction to measure, and the row says so.

With `--auto-release`, the step down follows steady compression, so the sustain state starts full and Release starts at its longest, 5 times Release, shortening as the sustain state discharges. Attack and Hold are unchanged.

`--lookahead ms` (0 by default) models High Quality's Lookahead: the detector meets each step that many ms before the listener hears it in the delayed audio. Attack's times count from the step the listener hears, so they come Lookahead earlier, and the 63% time is negative when Lookahead is longer than Attack: the Gain Reduction starts before the step. Hold and Release are durations, counted from when the detector meets the step down as before, so Lookahead doesn't move them.

    python3 tools/dyn-lab/dynlab.py step
    python3 tools/dyn-lab/dynlab.py step --attack 10 --release 100 --hold 0
    python3 tools/dyn-lab/dynlab.py step --attack 0.5 --release 50 --hold 20 --rates 44100 192000
    python3 tools/dyn-lab/dynlab.py step --range 6
    python3 tools/dyn-lab/dynlab.py step --auto-release
    python3 tools/dyn-lab/dynlab.py step --lookahead 5
    python3 tools/dyn-lab/dynlab.py step --lookahead 2 --attack 0.5 --release 50 --hold 20

## `latency`

Prints each Processing Mode's latency in whole host samples, at every sample rate comp1 supports (`plugkit.conf`'s `SAMPLE_RATES`, 44.1 to 192 kHz) or at the `--rates` given. Zero Latency reports 0. High Quality reports one fixed latency per rate, whatever the settings (ADR 0001), the sum of three parts, each a named constant in `dynlab.py` with its reason:

- the Oversampling reserve (`OVERSAMPLING_RESERVE_SAMPLES`), 128 samples at every rate, reserved for Oversampling's filters (#10);
- the Lookahead budget (`LOOKAHEAD_BUDGET_MS`), Lookahead's top of 5 ms, rounded up;
- the Limiter reserve (`LIMITER_RESERVE_MS`), 1 ms rounded up, reserved for the Limiter's own lookahead (#18).

It also prints `MODE_FADE_MS`: a Processing Mode switch fades the output out over 10 ms, swaps paths in the silence, and fades the new path in over 10 ms, each on a smoothstep curve.

    python3 tools/dyn-lab/dynlab.py latency
    python3 tools/dyn-lab/dynlab.py latency --rates 44100 48000

## `autogain`

Prints the Auto Gain estimate for a Threshold, Ratio, Knee and Range, then checks it on the reference signal. It exits non-zero when the check fails.

**The estimate** is the negated average Gain Reduction of the Transfer Curve over the reference level distribution: the gain Auto Gain adds, in dB. It is worked out from the settings alone, never measured from the audio, so the Engine computes it the same way.

**The reference level distribution** (`REFERENCE_LEVELS`) is Detection Levels from −40 to 0 dB in 2 dB steps, each equally likely. It spans the levels mixed material moves through, from quiet passages to full scale, evenly, so no one Threshold is favoured. It is not the textbook make-up gain, the Gain Reduction at 0 dBFS: that assumes everything is full scale, and adds +13.5 dB at the defaults, where this adds +3.25 dB.

**The measured check** builds the reference signal: pink-like noise (a 32-bit linear congruential generator with a fixed seed, through Paul Kellet's three-pole pink filter), 0.5 s at each reference level in turn, lowest first, at 48 kHz. Each segment has the power of a sine whose peak reads its level: the detector reads peaks, and a full-scale sine reads 0 dB. It runs the signal through the Clean model at the default Attack, Release and Hold, with Auto Gain applied, and prints the long-term level difference, output minus input: each segment's difference, averaged in dB over the segments, as the estimate averages its levels.

**The tolerance** (`AUTO_GAIN_TOLERANCE`) is ±1 dB, about the smallest change in level a listener notices on music, so within it compression is heard without a loudness bias. The estimate is of the Transfer Curve's steady Gain Reduction, while the detector, with its fast Attack and slow Release, holds near each segment's peaks, so the signal comes out slightly louder, more so the more Gain Reduction there is: about 6% of the estimate. The tolerance holds while the estimate is up to about 15 dB, which covers every setting below. Deeper settings overshoot it: Threshold −40 dB at Ratio ∞ (20 dB of Auto Gain) comes out 1.24 dB louder, and Threshold −60 dB at Ratio ∞ fails outright.

These are the settings the check covers, the Engine's tests too:

    python3 tools/dyn-lab/dynlab.py autogain
    python3 tools/dyn-lab/dynlab.py autogain --ratio inf
    python3 tools/dyn-lab/dynlab.py autogain --knee 0
    python3 tools/dyn-lab/dynlab.py autogain --knee 72
    python3 tools/dyn-lab/dynlab.py autogain --range 6
    python3 tools/dyn-lab/dynlab.py autogain --threshold -30
    python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf
    python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2

## Saturation

The Saturation stage is one soft-clip shape, f(u), with level-compensated Drive. The shape is exactly linear for |u| ≤ 1. Above that it is 1 + tanh(|u| − 1), with u's sign, so it leaves the line with the same slope (1) and curvature (0): it is C²-smooth, and it saturates toward 2. Its nonlinear residual is r(u) = f(u) − u, which is 0 for |u| ≤ 1 and tanh(t) − t, with u's sign, above it, where t = |u| − 1. The residual's antiderivative is R(u) = ln cosh t − t²/2 above the knee, and 0 below it. Drive, in dB, gives g = 10^(Drive/20), and the stage's output is x + r(g·x)/g. So below the knee the output is the input exactly, and the knee sits at full scale at Drive 0 dB and Drive dB under it above that. The output saturates toward 2/g.

Only the residual is anti-aliased (ADR 0005), with first-order antiderivative anti-aliasing (ADAA): each driven sample's residual is (R(uₙ) − R(uₙ₋₁)) / (uₙ − uₙ₋₁), the average of r along the line between the two samples. When the two are within 10⁻⁶ of each other (`ADAA_EQUAL_WITHIN`), it is the residual at their midpoint instead. That is far above where the quotient's rounding matters, and the midpoint's error, second order in the gap, is far below anything measurable. The linear part, x, passes sample-aligned.

### `saturation`

Prints the steady-level curve at a Drive (6 dB by default): the output level for each steady input level, from −36 to +6 dB in 3 dB steps, plus the level where the shape leaves the line. A steady input is a constant sample value whose magnitude reads its level, so ADAA's two samples are equal and it gives the residual itself. Then it prints the steady output level and the Gain Reduction of the compressor with Saturation, at a steady input level (`--level`, 0 dB by default), for Saturation off, Before and After:

- **Before:** the compressor hears the saturated signal (the Internal detection signal is its input).
- **After:** the compressor hears the input, as with Saturation off, so its Gain Reduction is the same, and Saturation shapes its output.

The compressor options are `transfer`'s.

    python3 tools/dyn-lab/dynlab.py saturation
    python3 tools/dyn-lab/dynlab.py saturation --drive 12
    python3 tools/dyn-lab/dynlab.py saturation --drive 24
    python3 tools/dyn-lab/dynlab.py saturation --drive 12 --level 12

The last is the Engine's Before and After test. At the defaults the compressor's output is under the knee at Drive 12 dB until the input passes +6 dB, so a +12 dB input makes After saturate as well: Before −14.99 dB, After −10.52 dB, and Saturation off −10.50 dB.

### `aliasing`

Prints the worst alias at each reference condition, for the naive shaper and for ADAA, and checks it against `ALIASING_TOLERANCE`. Then it prints ADAA's in-band harmonics beside an ideal reference's, and checks them against `HARMONIC_TOLERANCE_DB`. It exits non-zero when a row fails.

**The reference conditions** are each rate (44.1 and 48 kHz by default, `--rates`), tone (1, 3, 5 and 10 kHz) and Drive (6, 12, 18 and 24 dB, `--drive`), at 0 dBFS. Each tone is a sine with an odd whole number of cycles over 4096 samples (`ALIAS_SAMPLES`), so it is bin-centred: a rectangular window has no leakage, and no alias of a harmonic lands on a harmonic's own bin, since the shape is odd. It runs for two lengths, and the second is measured, after ADAA's state has settled. The worst alias is the loudest bin holding no in-band harmonic, in dB relative to the fundamental. There is one level, full scale: Drive is level-compensated, so every figure relative to the fundamental depends only on the level plus the Drive, and a quieter level is the same as a lower Drive. Full scale gives each Drive its hardest case.

**`ALIASING_TOLERANCE`** has two parts:

- **The ceiling, −24 dB:** ADAA's worst alias is at most 24 dB under the fundamental at every reference condition. The worst is −25.5 dB (10 kHz at Drive 18 dB, at 48 kHz). Below 10 kHz, ADAA's aliases are all at least 24 dB down, and below 5 kHz at least 30 dB. First-order ADAA alone at the base rate can't do much better on a tone this high this hard driven: a tighter ceiling needs Oversampling (#10), whose rows extend the table.
- **The improvement, 15 dB:** at the hardest condition, the highest tone at the top of Drive's range (10 kHz at 24 dB, `HARDEST`), ADAA's worst alias is at least 15 dB under the naive shaper's. The naive shaper's is −9.8 dB there, ADAA's −32.6 dB at 44.1 kHz and −29.5 dB at 48 kHz, so it improves by 22.8 and 19.7 dB. 15 dB leaves room for small changes to the shape, and fails at once if the residual isn't anti-aliased. The tolerance is for the base rates it names: at 96 kHz a 10 kHz tone is a milder case, its naive aliases are already lower, and ADAA improves on them by only 11.5 dB, so `--rates 96000` fails it.

Lower tones gain less: 4.5 to 9 dB at 1 kHz, where the aliases are already 50 dB or more down, and 5 to 13 dB at 3 and 5 kHz.

**The residual's half-sample offset (ADR 0005):** ADAA averages the residual over a sample, so it delays it by half a sample and droops it toward Nyquist, while the linear part passes sample-aligned. The harmonics are the residual's alone (the linear part is a pure sine), so the in-band harmonics, 2nd to 5th, of the 1, 3 and 5 kHz tones show it, up to 15 kHz. The ideal reference is the shaper run at 4096 points a cycle (at least 80 times oversampled), then band-limited to Nyquist: the shape's own harmonics, with no aliasing and no averaging. The even harmonics are absent from both, as the shape is odd.

**`HARMONIC_TOLERANCE_DB`, ±4 dB:** ADAA's in-band harmonics are within 4 dB of the ideal's. The droop grows with the harmonic's frequency and at low Drive. At 15 kHz at 44.1 kHz it is 1.8 dB at Drive 24 dB, close to a half-sample average's sinc response (1.7 dB). At Drive 6 dB it is 3.8 dB, the worst: there the residual is only the tips of the peaks, where the averaging's straight line between samples cuts the peak's curve the most. Below 10 kHz the harmonics are within 1.3 dB. A harmonic past 15 kHz, at the 7 kHz tone's 21 kHz, would droop up to 8.4 dB: that is beyond the band the tolerance covers.

The offset moves the fundamental too, since the residual cancels part of it as the shape saturates: at 5 kHz at Drive 24 dB, ADAA's fundamental is 7.6 dB louder than the ideal's at 44.1 kHz, so a high tone is flattened less than a low one. Only the harmonics are checked.

    python3 tools/dyn-lab/dynlab.py aliasing
    python3 tools/dyn-lab/dynlab.py aliasing --drive 12
    python3 tools/dyn-lab/dynlab.py aliasing --drive 6 24 --rates 44100

## Auto Release

With Auto Release on, the detector releases in Release × (1 + (M − 1) × sustain). The sustain state runs from 0 to 1. It charges toward 1 while the Transfer Curve asks for Gain Reduction (the Detection Level is over the Knee's lower edge), and discharges toward 0 while it doesn't, each one pole with its own time constant in ms, so it is the same at every sample rate. Hold still runs first: Auto Release changes only the Release that follows it. Off, the detector is exactly as above.

The Engine moves on a grid of 16-sample runs from its first sample (`RUN_SAMPLES`), and so does the model. Each sample counts whether the Transfer Curve asked for Gain Reduction. At the start of each run, the sustain state charges for the samples of the run just heard that asked and discharges for the rest, and the release pole is worked out again from it. So the lab's figures and the Engine's agree to the sample.

The constants, named once in `dynlab.py`:

- **`AUTO_RELEASE_MULTIPLIER`, M = 5:** the longest release is 5 times Release, 500 ms at the default 100 ms. That is long enough to stop sustained compression pumping, and short enough that Release still sets the scale: the user's control keeps its meaning across its whole range, rather than Auto Release replacing it.
- **`AUTO_RELEASE_CHARGE_MS`, 1000 ms:** about two beats at 120 BPM. A transient (a drum hit, 10 to 100 ms over Threshold) charges the sustain state 1% to 10%, so its release stays near Release, while a held note or a dense mix over a bar charges it most of the way.
- **`AUTO_RELEASE_DISCHARGE_MS`, 2000 ms:** twice the charge, so a short gap in sustained material (a breath, the space between phrases) doesn't snap Release back short, which would pump. It is also well over the longest release at the default Release, so the sustain state doesn't fall away during the release it set.
- **`AUTO_RELEASE_TRANSIENT_MARGIN`, 1%:** how far past Release the shortest default burst, 1 ms, may release. It releases about 0.4% past it, at the defaults and at `--attack 0.5 --release 50 --hold 20`.

### `bursts`

Prints the Release that follows each of a list of bursts, with Auto Release off and on, at each sample rate (44.1, 48 and 96 kHz by default). From steady state 20 dB under Threshold, the Detection Level sits 20 dB over it for the burst's length, then drops back. The run grid starts with the burst. Release's 63% and 90% count from when Release begins, after Hold, from the Gain Reduction the burst reached. The default lengths, 1, 10, 100, 1000 and 3000 ms, run from a transient to sustained compression (`--lengths` sets others).

Off, every burst releases in Release's time. On, Release grows with the burst's length: at the defaults, 100.4 ms after 1 ms, 137 ms after 100 ms and 440 ms after 3000 ms. A step from steady state (`step --auto-release`) is the longest of all, 456 ms.

    python3 tools/dyn-lab/dynlab.py bursts
    python3 tools/dyn-lab/dynlab.py bursts --attack 0.5 --release 50 --hold 20
    python3 tools/dyn-lab/dynlab.py bursts --lengths 1 10 100 --rates 192000

## Tests

    python3 tools/dyn-lab/test_dynlab.py

The tests also run each example above, exactly as written.
