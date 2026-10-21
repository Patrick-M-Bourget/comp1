# dyn-lab

A Python model of comp1's Characters. It prints the figures the Engine's tests check, so a ticket brief can cite a command and its output exactly. It needs only the standard library: no installs, no numpy. Run it from the repo root.

It models only the Clean Character so far, with High Quality's Lookahead and latency. Clean follows Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor Design — A Tutorial and Analysis". It is feedforward, with a soft-knee gain computer (the paper's eq. 4) and a smooth branching peak detector in the log domain (eq. 12), plus Hold. Each Attack and Release is a single pole, so 63% (1 − 1/e) of a step takes one time constant and 90% takes ln 10 ≈ 2.303 of them. It isn't the paper's decoupled detector (eq. 17): that one runs Release through the attack pole as well, so gain comes back more slowly than Release says.

Hold freezes the Gain Reduction for Hold ms whenever it stops rising, mid-Attack included, before Release begins. Any rise restarts the count.

Auto Release, when on, lengthens Release after sustained compression and leaves it short after transients. See [Auto Release](#auto-release).

Range caps the Gain Reduction the gain computer asks for, before the detector smooths it. So the Transfer Curve rises with the input again, Range below it, once the Gain Reduction reaches Range, and Attack, Release and Hold take the same times over a smaller step. At 60 dB, its top, Range sets no limit (ADR 0008).

Every option has a default, and a setting outside its range exits with an error that names the range. The ranges are named once in `dynlab.py` (`THRESHOLD_RANGE`, `RATIO_RANGE` and so on), each with its source: Pro-C 3's documentation, ADR 0008 for Threshold, Release and Range, or ADR 0001 for Lookahead. Lookahead is a Processing Mode setting, read in High Quality only, not one of the compressor's. The check harness's `docs` stage fails when one differs from the plugin's, `engine/include/comp1/Settings.h`'s `parameterRanges` (`LAB_RANGES` in `plugkit.conf`).

| Setting | Range | Default |
|---|---|---|
| Threshold | −60 to 0 dB | −18 dB |
| Ratio | 1 to ∞ (`inf` or `∞`) | 4 |
| Knee | 0 to 72 dB | 6 dB |
| Attack | 0.005 to 250 ms | 10 ms |
| Release | 5 to 5000 ms | 100 ms |
| Hold | 0 to 500 ms | 0 ms |
| Range | 0 to 60 dB | 60 dB |
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
