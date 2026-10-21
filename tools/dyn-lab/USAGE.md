# dyn-lab

A Python model of comp1's Characters. It prints the figures the Engine's tests check, so a ticket brief can cite a command and its output exactly. It needs only the standard library: no installs, no numpy. Run it from the repo root.

It models the Clean, VCA and FET Characters (see [VCA](#vca) and [FET](#fet)), with High Quality's Lookahead, latency and Oversampling, the Saturation stage (see [Saturation](#saturation)), VCA's and FET's Colour (see [Colour](#colour)) the Limiter (see [Limiter](#limiter)) and the Gate (see [Gate](#gate)). Clean follows Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor Design — A Tutorial and Analysis". It is feedforward, with a soft-knee gain computer (the paper's eq. 4) and a smooth branching peak detector in the log domain (eq. 12), plus Hold. Each Attack and Release is a single pole, so 63% (1 − 1/e) of a step takes one time constant and 90% takes ln 10 ≈ 2.303 of them. It isn't the paper's decoupled detector (eq. 17): that one runs Release through the attack pole as well, so gain comes back more slowly than Release says.

Hold freezes the Gain Reduction for Hold ms whenever it stops rising, mid-Attack included, before Release begins. Any rise restarts the count.

Auto Release, when on, lengthens Release after sustained compression and leaves it short after transients. See [Auto Release](#auto-release).

Range caps the Gain Reduction the gain computer asks for, before the detector smooths it. So the Transfer Curve rises with the input again, Range below it, once the Gain Reduction reaches Range, and Attack, Release and Hold take the same times over a smaller step. At 60 dB, its top, Range sets no limit (ADR 0008).

Every option has a default, and a setting outside its range exits with an error that names the range. The ranges are named once in `dynlab.py` (`THRESHOLD_RANGE`, `RATIO_RANGE` and so on), each with its source: Pro-C 3's documentation, ADR 0008 for Threshold, Release, Range, Drive and Colour, ADR 0001 for Lookahead, or comp1's own reasons for the Limiter's Ceiling (`CEILING_RANGE`). The Gate's reuse the compressor's of the same names (`GATE_PARAMETERS`). Lookahead is a Processing Mode setting, read in High Quality only, not one of the compressor's. The check harness's `docs` stage fails when one differs from the plugin's, `engine/include/comp1/Settings.h`'s `parameterRanges` (`LAB_RANGES` in `plugkit.conf`).

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
| Colour | 0 to 100 % | 100 % |
| Auto Release | off or on (`--auto-release`) | off |
| Lookahead | 0 to 5 ms | 0 ms |
| Ceiling | −30 to 0 dBFS | 0 dBFS |
| Gate Threshold | −60 to 0 dB | −40 dB |
| Gate Range | 0 to 60 dB | 20 dB |
| Gate Attack | 0.005 to 250 ms | 1 ms |
| Gate Release | 5 to 5000 ms | 100 ms |
| Gate Hold | 0 to 500 ms | 20 ms |
| Character | `clean`, `vca` or `fet` (`--character`) | `clean` |

`--character` is taken by `transfer`, `step`, `bursts`, `autogain` and `colour`. Clean's figures are the same with it as without.

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

With `--external`, the step is the key's on an External Detection Source, while the main stays steady, so the Gain Reduction never reaches what the detector hears. Clean and VCA are feedforward either way, so their times are unchanged; FET's are Clean's (see [FET](#fet)).

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

- the Oversampling reserve (`OVERSAMPLING_RESERVE_SAMPLES`), 128 samples at every rate, the delay of Oversampling's two sections (see [`oversampling`](#oversampling));
- the Lookahead budget (`LOOKAHEAD_BUDGET_MS`), Lookahead's top of 5 ms, rounded up;
- the Limiter reserve (`LIMITER_RESERVE_MS`), 1 ms rounded up, the Limiter's own lookahead in High Quality: its true-peak estimator's delay and its lookahead window fill it exactly (see [Limiter](#limiter)).

It also prints `MODE_FADE_MS`: a Processing Mode switch fades the output out over 10 ms, swaps paths in the silence, and fades the new path in over 10 ms, each on a smoothstep curve.

    python3 tools/dyn-lab/dynlab.py latency
    python3 tools/dyn-lab/dynlab.py latency --rates 44100 48000

## `oversampling`

Prints, at every sample rate comp1 supports or at the `--rates` given, the Oversampling factors allowed there and, for each, its filters. It exits non-zero when a factor's filters overrun their section.

**The cap:** Oversampling offers 1x, 2x, 4x and 8x (`OVERSAMPLING_FACTORS`), and a factor is allowed where the internal rate, the rate times the factor, is at or under 384 kHz (`MAX_INTERNAL_RATE_HZ`, ADR 0001): 8x at 44.1 and 48 kHz, 4x at 88.2 and 96 kHz, and 2x at 176.4 and 192 kHz. A setting over the cap plays the largest allowed factor under it.

**The sections (ADR 0009):** only a nonlinear stage's residual, f(u) − u, is oversampled, never its linear part. High Quality has two oversampled sections, one ahead of the detection stage and one after the compressor, and each delays its linear part by exactly 64 host samples (`OVERSAMPLING_SECTION_SAMPLES`, half the Oversampling reserve). Its residual goes up through the filters, is worked out at the oversampled rate, comes back down, and is padded to the same 64. So nothing in the Wet path's delay moves with the factor.

**The filters:** each 2x step is a linear-phase half-band FIR with a Kaiser window, cascaded for 4x and 8x. Up and down use the same design. A half-band's transition is centred on the lower rate's Nyquist, so it is set by the passband edge alone, and every other tap is 0.

- **`OVERSAMPLING_PASSBAND_EDGE`, 0.8 of the host's Nyquist** (17.64 kHz at 44.1 kHz). Only the residual passes through the filters, so the edge can sit below 20 kHz without dulling the signal. Below it the residual's harmonics pass flat. Above it, at 44.1 and 48 kHz, they are at the top of hearing, and at the higher rates the edge is far above 20 kHz. A higher edge needs more taps for the same attenuation: 0.85 makes the first stage 87 taps instead of 67.
- **`OVERSAMPLING_STOPBAND_DB`, 100 dB**, which every stage reaches. What the filters let through then sits 100 dB under the residual's own images and out-of-band harmonics, far under every alias ADAA leaves at the oversampled rate (see [`aliasing`](#aliasing)) and near the float audio's own rounding. A deeper stopband buys nothing measurable.
- **`KAISER_BETA`, 10.06:** Kaiser's formula for over 50 dB of attenuation, 0.1102 (A − 8.7). Each stage is then the shortest half-band, 4m − 1 taps, whose measured stopband reaches 100 dB.

Each row gives each stage's taps, first stage first, and:

- **delay:** the up and down filters' delay together, in host samples. A stage of N taps delays by (N − 1) / 2 samples at its own rate, each way. At 4x and 8x the total falls between host samples, so **internal** padding, counted in samples at the oversampled rate, rounds it up to whole host samples.
- **padding:** what the section adds to make 64, in host samples.
- **stopband dB:** the cascade's least attenuation, measured over every band within the passband edge of a multiple of the host rate, up to the oversampled Nyquist. Going up, the host band's images land there. Coming down, what lies there folds back into the passband.
- **passband Hz:** the highest frequency the cascade stays within 0.001 dB of unity up to (`PASSBAND_FLAT_DB`), just above the design's edge.

The filters' figures are the same at every rate in host terms: the designs are fractions of the rate, and the cap keeps the oversampled rate's stages the same. 8x, at 44.1 and 48 kHz, has stages of 67, 27 and 23 taps and a delay of 43, so its section pads 21.

    python3 tools/dyn-lab/dynlab.py oversampling
    python3 tools/dyn-lab/dynlab.py oversampling --rates 44100 96000

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

## VCA

VCA is Clean with an RMS Detection Level. On each detection channel, a one-pole mean-square average of the detection signal, with time constant `VCA_RMS_MS`, is read sine-calibrated: 10·log10(2 · mean square) dB, so a sine reads its peak level, as it does on Clean. That level feeds Clean's gain computer and detector unchanged, so Threshold, Ratio, Knee, Range, Attack, Release, Hold and Auto Release all work as on Clean, and the Transfer Curve is Clean's. Auto Gain's estimate is the Transfer Curve's, so it is Clean's too.

**`VCA_RMS_MS`, 5 ms:** the averaging window, and so the shortest response the RMS stage allows. A step's level takes about one window to reach 63%, which lengthens the detector's own times. At the defaults, Attack's 63% is 12.1 ms rather than 10 ms, and Release's 111.8 ms rather than 100 ms. At Attack 0.5 ms the RMS stage sets the pace: 63% takes 1.7 ms. A longer window would lengthen every fast Attack further. A shorter one lets the level ripple more on bass: a sine's mean square ripples at twice its frequency, and 5 ms holds that ripple to +0.6/−0.7 dB at 100 Hz and ±0.07 dB at 1 kHz, before the detector's own Attack and Release smooth it.

**The steady test signal** is a square wave (the same magnitude every sample) whose peak is `SINE_CALIBRATION_DB`, 3.0103 dB, under the level it stands for. Its mean square is that of a sine at the level, with no ripple at all, so it reads that level exactly once settled. A constant-magnitude signal reads 3.01 dB above its peak level on VCA. The Engine's Transfer Curve, step and burst tests on VCA play this signal, and add 3.0103 dB back to the output level they read. `step` and `bursts` start from steady state, the mean square settled at the starting level. After the step down the Gain Reduction leaves its level slowly, as the RMS level falls away, so Hold reads a few samples, 0.04 to 0.06 ms, even at Hold 0 ms: it counts until the Gain Reduction has moved more than `MOVED_WITHIN`, 10⁻⁶ of the way, as the Engine's tests count off float output.

    python3 tools/dyn-lab/dynlab.py transfer --character vca
    python3 tools/dyn-lab/dynlab.py transfer --character vca --ratio inf
    python3 tools/dyn-lab/dynlab.py transfer --character vca --knee 0
    python3 tools/dyn-lab/dynlab.py transfer --character vca --knee 72
    python3 tools/dyn-lab/dynlab.py transfer --character vca --range 6
    python3 tools/dyn-lab/dynlab.py step --character vca
    python3 tools/dyn-lab/dynlab.py step --character vca --attack 0.5 --release 50 --hold 20
    python3 tools/dyn-lab/dynlab.py bursts --character vca

**Auto Release on VCA** releases a little later after a transient than on Clean: the RMS level falls away over its window after the burst ends, and the Transfer Curve keeps asking for Gain Reduction meanwhile, so a 1 ms burst releases in 106.7 ms with Auto Release on, 6.7% past Release, where Clean's is within `AUTO_RELEASE_TRANSIENT_MARGIN`. The margin is Clean's figure; VCA's is what the lab prints.

**Auto Gain on VCA:** the measured check runs the reference signal through the VCA model, without Colour, whose distortion Auto Gain doesn't compensate. It holds at every setting `autogain` lists, VCA's output coming out 0.09 to 0.65 dB quieter than the input: the RMS stage reads the noise's own level rather than holding near its peaks. It holds at Threshold −40 dB, Ratio ∞ too (−0.82 dB), where Clean's overshoots.

    python3 tools/dyn-lab/dynlab.py autogain --character vca
    python3 tools/dyn-lab/dynlab.py autogain --character vca --ratio inf
    python3 tools/dyn-lab/dynlab.py autogain --character vca --knee 0
    python3 tools/dyn-lab/dynlab.py autogain --character vca --knee 72
    python3 tools/dyn-lab/dynlab.py autogain --character vca --range 6
    python3 tools/dyn-lab/dynlab.py autogain --character vca --threshold -30
    python3 tools/dyn-lab/dynlab.py autogain --character vca --threshold -30 --ratio inf
    python3 tools/dyn-lab/dynlab.py autogain --character vca --threshold -6 --ratio 2

**A proposed default Knee for VCA, 3 dB** (for the Style defaults of #24; nothing applies it yet): firmer than Clean's 6 dB, for the precise, controlled onset VCA compression is chosen for on buses and drums. It needn't be harder: the RMS stage already rounds a transient's onset over its 5 ms, and a hard knee on a level held near Threshold would switch compression in and out at each small change of level.

## FET

FET is a feedback compressor. It reads a peak Detection Level, as Clean does, but with an Internal Detection Source the level its gain computer reads is the detection sample's level less the Gain Reduction being applied in that same sample: the output's level. The detector is Clean's smooth branching detector, with Hold and Auto Release, around that loop.

**The loop is solved within the sample** (`fet_loop`), so it never waits a sample to hear its own gain, and never rings. Each sample, the detector's one pole (Attack's or Release's) steps from the Gain Reduction it holds, G₀, to G = pole · G₀ + (1 − pole) · w(L − G), where L is the detection sample's level and w is the gain computer in the output's domain: for an output level y, the Gain Reduction that brings the input whose Transfer Curve output is y down to it, capped at Range. So at steady state the output is exactly Clean's Transfer Curve, at every Threshold, Ratio, Knee and Range. w never falls as y rises, so G is unique and lies between G₀ and the steady Gain Reduction. It is solved in closed form region by region of the Transfer Curve:

- below the Knee, in the output's domain, w is 0, and G is pole · G₀;
- across the Knee, w is the Knee's quadratic turned around, and G comes from a quadratic in the input's place in the Knee;
- above it, w is (Ratio − 1)(y − Threshold), which gives G directly, written with 1/Ratio so that ∞:1 holds the output at Threshold;
- Range caps w, and so G.

Attack applies while the solved Gain Reduction rises, and Release otherwise. That is when the Transfer Curve's Gain Reduction at L is over G₀, so Attack, Release, Hold and Auto Release branch exactly as on Clean.

**The speed-up and its cap, `FET_SPEEDUP_MAX`, 4×:** above the Knee the loop runs about Ratio times faster than its one pole, since every dB the Gain Reduction rises takes Ratio − 1 dB off what the gain computer asks for. At 4:1, Attack's 63% comes in 2.5 ms rather than 10 ms; at ∞:1 the loop would reach its steady state in a sample, whatever Attack says. So where the loop would move the Gain Reduction further in a sample than one pole of Attack (or Release) over 4 would toward the Transfer Curve's Gain Reduction, it moves that far instead: as if the loop's time constants were lengthened to hold it at the cap. Above the Knee, at Ratios over 4, that is exactly a one pole of a quarter of the time. 4× is the speed-up the loop reaches by itself at 4:1, a FET's classic Ratio, so the cap leaves Ratios up to 4 as the loop has them and keeps Attack the control of the timing at the higher ones: without it, Attack would do nothing at ∞:1. Release below the Knee is Clean's: there the gain computer asks for none, whatever the loop hears.

`speedup` prints the times beside Clean's, at 48 kHz (`--rates` for others). Attack's 63% and 90%, in ms:

| At Ratio | Clean, 10 ms | FET, 10 ms | Clean, 0.5 ms | FET, 0.5 ms | Clean, 0.02 ms | FET, 0.02 ms |
|---|---|---|---|---|---|---|
| 1.5 | 10.02, 23.04 | 6.69, 15.38 | 0.50, 1.17 | 0.35, 0.79 | 0.021, 0.063 | 0.021, 0.042 |
| 2 | 10.02, 23.04 | 5.02, 11.54 | 0.50, 1.17 | 0.27, 0.60 | 0.021, 0.063 | 0.021, 0.042 |
| 4 | 10.02, 23.04 | 2.52, 5.79 | 0.50, 1.17 | 0.15, 0.31 | 0.021, 0.063 | 0.021, 0.042 |
| 8 | 10.02, 23.04 | 2.56, 5.81 | 0.50, 1.17 | 0.15, 0.29 | 0.021, 0.063 | 0.021, 0.021 |
| 20 | 10.00, 23.04 | 2.63, 5.88 | 0.50, 1.17 | 0.15, 0.31 | 0.021, 0.063 | 0.021, 0.021 |
| ∞ | 10.02, 23.04 | 2.67, 5.94 | 0.50, 1.17 | 0.15, 0.31 | 0.021, 0.063 | 0.021, 0.021 |

Past 4:1 the cap holds the speed-up at 3.8 to 3.9×: the step starts far over Threshold, where the gain computer asks for more than Range, and there w is flat, so the loop speeds up less until the Gain Reduction nears its steady state. At 0.5 ms a pole is 24 samples, and a quarter of it 6, so the speed-up reads 3.4×. At 0.02 ms every time is a sample or two.

**The partial release:** from 20 dB over Threshold down to 10 dB over it, both above the Knee, the loop speeds Release up as well. At the default 100 ms Release, Release's 63% is 66.7 ms at 1.5:1, 50.4 ms at 2:1, 38.5 ms at 4:1 and 38.0 ms from 8:1 on. It is 2.6× faster at most, not 4×: the step down leaves the output under the Knee, where the gain computer asks for none, so Release runs at its own pace until the output is back over it. A step down to under the Knee, as `step` makes, releases as on Clean.

**The sub-millisecond attack:** at the reference Attack, `FET_REFERENCE_ATTACK_MS`, 0.02 ms, 90% of the step takes at most 0.045 ms at every rate, a sample or two: FET can catch a transient's first peak. At Attack 0.005 ms, Range's bottom, and ∞:1 the loop reaches its steady state within the first sample, and stays there.

**An External Detection Source** feeds FET's gain computer and detector from the key, which the gain applied never reaches, so there is no feedback: it is feedforward, as Clean is, with the same times as Clean's at the same settings. `step --character fet --external` prints them. FET's Colour still follows the Gain Reduction.

**With Lookahead** the loop reads the detection signal Lookahead early, times the gain it applies in that sample, so it acts ahead of the delayed audio, as Clean does: `step --lookahead` reports Attack's times against the delayed audio.

**`transfer --character fet`** measures each row by running the loop at that steady level, at 48 kHz (`FET_TRANSFER_RATE`) and the default Attack, from no Gain Reduction until it moves less than 10⁻¹² dB in a sample (`FET_SETTLED_DB`). Its last line gives the largest difference from Clean's Transfer Curve: under 10⁻⁹ dB at every setting listed. The steady test signal is Clean's, a square wave at each level.

**Auto Gain on FET:** the estimate is the Transfer Curve's, Clean's. The measured check runs the reference signal through FET's loop. It holds at every setting `autogain` lists, FET's output within 0.6 dB of the input, and at Threshold −40 dB, Ratio ∞ too (−0.17 dB), where Clean's overshoots: the loop catches the noise's peaks a few times faster than its Attack, so it holds the level nearer the Transfer Curve's.

**Auto Release on FET** is Clean's: a step down to under the Knee releases as on Clean, so `bursts --character fet` prints Clean's figures.

    python3 tools/dyn-lab/dynlab.py transfer --character fet
    python3 tools/dyn-lab/dynlab.py transfer --character fet --ratio inf
    python3 tools/dyn-lab/dynlab.py transfer --character fet --knee 0
    python3 tools/dyn-lab/dynlab.py transfer --character fet --knee 72
    python3 tools/dyn-lab/dynlab.py transfer --character fet --range 6
    python3 tools/dyn-lab/dynlab.py step --character fet
    python3 tools/dyn-lab/dynlab.py step --character fet --attack 0.5 --release 50 --hold 20
    python3 tools/dyn-lab/dynlab.py step --character fet --ratio 20
    python3 tools/dyn-lab/dynlab.py step --character fet --ratio inf
    python3 tools/dyn-lab/dynlab.py step --character fet --attack 0.02 --rates 44100 48000 88200 96000 176400 192000
    python3 tools/dyn-lab/dynlab.py step --character fet --external
    python3 tools/dyn-lab/dynlab.py step --character fet --lookahead 5
    python3 tools/dyn-lab/dynlab.py step --character fet --lookahead 2 --attack 0.5 --release 50 --hold 20
    python3 tools/dyn-lab/dynlab.py bursts --character fet
    python3 tools/dyn-lab/dynlab.py autogain --character fet
    python3 tools/dyn-lab/dynlab.py autogain --character fet --ratio inf
    python3 tools/dyn-lab/dynlab.py autogain --character fet --threshold -30 --ratio inf
    python3 tools/dyn-lab/dynlab.py speedup

### `speedup`

Prints FET's Attack times beside Clean's at each of `SPEEDUP_RATIOS` (1.5, 2, 4, 8, 20 and ∞) and Attacks of 10, 0.5 and 0.02 ms, with the speed-up at 63%, then the partial release, from 20 to 10 dB over Threshold (`PARTIAL_LOUD_DB`, `PARTIAL_QUIET_DB`), against Clean's. At 48 kHz by default, `--rates` for others.

    python3 tools/dyn-lab/dynlab.py speedup --rates 44100 96000

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

Prints the worst alias at each reference condition, for the naive shaper and for ADAA, and checks it against `ALIASING_TOLERANCE`. Then it prints ADAA's in-band harmonics beside an ideal reference's, and checks them against `HARMONIC_TOLERANCE_DB`, and last ADAA's fundamental beside the ideal's, which isn't checked. It exits non-zero when a row fails. `--oversampling N` (1 by default) runs the same conditions through High Quality's oversampled section at N: see [Oversampled](#oversampled).

**The reference conditions** are each rate (44.1 and 48 kHz by default, `--rates`), tone (1, 3, 5 and 10 kHz) and Drive (6, 12, 18 and 24 dB, `--drive`), at 0 dBFS. Each tone is a sine with an odd whole number of cycles over 4096 samples (`ALIAS_SAMPLES`), so it is bin-centred: a rectangular window has no leakage, and no alias of a harmonic lands on a harmonic's own bin, since the shape is odd. It runs for two lengths, and the second is measured, after ADAA's state has settled. The worst alias is the loudest bin holding no in-band harmonic, in dB relative to the fundamental. There is one level, full scale: Drive is level-compensated, so every figure relative to the fundamental depends only on the level plus the Drive, and a quieter level is the same as a lower Drive. Full scale gives each Drive its hardest case.

**`ALIASING_TOLERANCE`** has two parts:

- **The ceiling, −24 dB:** ADAA's worst alias is at most 24 dB under the fundamental at every reference condition. The worst is −25.5 dB (10 kHz at Drive 18 dB, at 48 kHz). Below 10 kHz, ADAA's aliases are all at least 24 dB down, and below 5 kHz at least 30 dB. First-order ADAA alone at the base rate can't do much better on a tone this high this hard driven: Oversampling's rows, below, go further.
- **The improvement, 15 dB:** at the hardest condition, the highest tone at the top of Drive's range (10 kHz at 24 dB, `HARDEST`), ADAA's worst alias is at least 15 dB under the naive shaper's. The naive shaper's is −9.8 dB there, ADAA's −32.6 dB at 44.1 kHz and −29.5 dB at 48 kHz, so it improves by 22.8 and 19.7 dB. 15 dB leaves room for small changes to the shape, and fails at once if the residual isn't anti-aliased. The tolerance is for the base rates it names: at 96 kHz a 10 kHz tone is a milder case, its naive aliases are already lower, and ADAA improves on them by only 11.5 dB, so `--rates 96000` fails it.

Lower tones gain less: 4.5 to 9 dB at 1 kHz, where the aliases are already 50 dB or more down, and 5 to 13 dB at 3 and 5 kHz.

**The residual's half-sample offset (ADR 0005):** ADAA averages the residual over a sample, so it delays it by half a sample and droops it toward Nyquist, while the linear part passes sample-aligned. The harmonics are the residual's alone (the linear part is a pure sine), so the in-band harmonics, 2nd to 5th, of the 1, 3 and 5 kHz tones show it, up to 15 kHz. The ideal reference is the shaper run at 4096 points a cycle (at least 80 times oversampled), then band-limited to Nyquist: the shape's own harmonics, with no aliasing and no averaging. The even harmonics are absent from both, as the shape is odd.

**`HARMONIC_TOLERANCE_DB`, ±4 dB:** ADAA's in-band harmonics are within 4 dB of the ideal's. The droop grows with the harmonic's frequency and at low Drive. At 15 kHz at 44.1 kHz it is 1.8 dB at Drive 24 dB, close to a half-sample average's sinc response (1.7 dB). At Drive 6 dB it is 3.8 dB, the worst: there the residual is only the tips of the peaks, where the averaging's straight line between samples cuts the peak's curve the most. Below 10 kHz the harmonics are within 1.3 dB. A harmonic past 15 kHz, at the 7 kHz tone's 21 kHz, would droop up to 8.4 dB: that is beyond the band the tolerance covers.

The offset moves the fundamental too, since the residual cancels part of it as the shape saturates: at 5 kHz at Drive 24 dB, ADAA's fundamental is 7.6 dB louder than the ideal's at 44.1 kHz, so a high tone is flattened less than a low one. Only the harmonics are checked.

    python3 tools/dyn-lab/dynlab.py aliasing
    python3 tools/dyn-lab/dynlab.py aliasing --drive 12
    python3 tools/dyn-lab/dynlab.py aliasing --drive 6 24 --rates 44100

#### Oversampled

With `--oversampling N`, each condition's tone also goes through High Quality's oversampled section at N (see [`oversampling`](#oversampling)): the linear part delayed by the section, and the residual through the up filters, ADAA at the oversampled rate, and the down filters. Each row prints ADAA's worst alias at 1x, at N, and at N below the passband edge (`OVERSAMPLING_PASSBAND_EDGE`, 0.8 of Nyquist). The harmonics and the fundamental are those at N. N must be allowed at every rate given.

The worst alias anywhere falls little at the highest tones. It is a residual harmonic just over the host's Nyquist, which the half-band's transition, centred on Nyquist, lets partly through, folding back to just under it: between the passband edge and Nyquist, above 17.6 kHz at 44.1 kHz. At 5 kHz at Drive 24 dB at 48 kHz it is −31.0 dB at 2x, −28.2 dB at 4x and −27.3 dB at 8x, against −26.2 dB at 1x. Below the passband edge, where the filters are flat, every factor clears the aliases far more.

**`ALIASING_TOLERANCE`'s figures per factor** (`passband_ceilings`): below the passband edge, the worst alias is at most −45 dB at 2x, −60 dB at 4x and −75 dB at 8x, at every reference condition. Anywhere, it is no worse than ADAA's at 1x. The worst below the edge are −48.1 dB at 2x (10 kHz at Drive 24 dB, 48 kHz), −62.8 dB at 4x and −79.9 dB at 8x (10 kHz at Drive 24 dB, 44.1 kHz). So each ceiling leaves about 3 dB of room, and each factor's ceiling fails the factor below it. The closest to 1x is 5 kHz at Drive 24 dB at 48 kHz, 1.0 dB under it at 8x.

The harmonics are within 0.9 dB of the ideal at 2x, 0.2 dB at 4x and 0.05 dB at 8x. The fundamental's shortfall at 5 kHz at Drive 24 dB, 7.6 dB at 1x at 44.1 kHz, is 3.5 dB at 2x, 1.2 dB at 4x and 0.3 dB at 8x.

    python3 tools/dyn-lab/dynlab.py aliasing --oversampling 2
    python3 tools/dyn-lab/dynlab.py aliasing --oversampling 4
    python3 tools/dyn-lab/dynlab.py aliasing --oversampling 8

## Colour

Colour is a Character's own distortion, which grows with its Gain Reduction. Clean has none, at any setting. VCA's is the Saturation stage's shape and its anti-aliased residual (above), unchanged, driven by the Gain Reduction applied to each channel. After the compressor applies its gain, each compressed sample y becomes y + w · r(g · y) / g, with r the residual anti-aliased (ADR 0005), so the linear part passes sample-aligned. The weight w and the drive g follow the Gain Reduction, sample by sample:

- the weight is Colour × `VCA_COLOUR_WEIGHT` × (1 − e^(−GR / `VCA_COLOUR_WEIGHT_DB`));
- the drive, in dB, is Drive's top, 24 dB, × (1 − e^(−GR / `VCA_COLOUR_DRIVE_DB`)), never above it.

Each is exactly 0 with no Gain Reduction, so then, or at Colour 0%, the output is the compressed signal exactly. Each rises smoothly with the Gain Reduction, with no corner as compression starts.

The constants, named once in `dynlab.py`:

- **`VCA_COLOUR_DRIVE_DB`, 5 dB:** the drive is 10.8 dB at 3 dB of Gain Reduction, 20.8 dB at 10 dB and 23.6 dB at 20 dB, so the harder the compressor works, the further its output is pushed into the shape. The shape is linear up to its knee, which the drive sets at full scale less the drive, so Colour is heard only where the compressed signal comes within the drive of full scale: at the defaults, from about 6 dB of Gain Reduction. A quieter compressed signal (a low Threshold) gets less Colour at the same Gain Reduction.
- **`VCA_COLOUR_WEIGHT`, 0.11, and `VCA_COLOUR_WEIGHT_DB`, 10 dB:** the residual's weight rises to 63% of its most at 10 dB of Gain Reduction and nears it by 30 dB. Together with the drive, they make Colour slight: 0.30% THD at 10 dB of Gain Reduction at Colour 100%, at the defaults, rising to 1.2% at 20 dB and 1.3% at 30 dB. That is a hint of density, well under where distortion reads as an effect, and leaves heavy distortion to Saturation.

**FET's Colour** has VCA's form, with its own constants, `FET_COLOUR_WEIGHT`, 0.25, `FET_COLOUR_WEIGHT_DB`, 10 dB, and `FET_COLOUR_DRIVE_DB`, 4 dB. It is steeper than VCA's: 1.06% THD at 10 dB of Gain Reduction at Colour 100%, at the defaults, 2.99% at 20 dB and 3.24% at 30 dB, where VCA's are 0.30%, 1.2% and 1.3%. A FET's distortion is part of what it is chosen for, on vocals and drums: heard as grit once it works hard, though still under Saturation's. The weight's most, 0.25, sets the level at 20 dB and over, about 3%. The drive rising over 4 dB rather than 5 dB pushes the compressed signal into the shape sooner, so 10 dB of Gain Reduction already gives about 1%, and 6 dB a trace (0.04%). FET's Colour follows the Gain Reduction it applies, and stays outside the loop: the detector hears the detection signal times the gain, never Colour.

The even harmonics are absent, as the shape is odd and a steady Gain Reduction is symmetric.

### `colour`

Prints, for a Character at a Colour (`--colour`, 100% by default) and at the compressor options `transfer` takes, how its Colour follows the Gain Reduction: the residual's weight and the drive at each steady Gain Reduction (`COLOUR_REDUCTIONS`, 0, 3, 6, 10, 20 and 30 dB), and the 2nd to 5th harmonics, in dB relative to the fundamental, and their THD, of a steady bin-centred sine compressed by that much. The sine is the bin-centred tone nearest 1 kHz at 48 kHz (`COLOUR_TONE`), at the level whose Gain Reduction it is on the Transfer Curve (`COLOUR_BELOW_KNEE_DB`, 12 dB under the Knee, for none), and its Gain Reduction is held steady, as an External Detection Source holding a steady Sidechain gives it. A row the settings never reach (past Range) says so. Clean prints that it has no Colour.

**`COLOUR_HARMONIC_TOLERANCE_DB`, ±0.1 dB:** the Engine's harmonics, played that way with the Sidechain a steady square wave, are within 0.1 dB of the lab's. The two compute the same thing, the Engine in float at its input and output: they agree within 0.005 dB, the lab's rounding as printed. A harmonic more than 100 dB under the fundamental is lost in float's rounding, and isn't compared.

Then it prints Colour's worst alias for bin-centred tones at the rates and frequencies `aliasing` uses (`--rates`), at 10, 20 and 30 dB of Gain Reduction (`COLOUR_ALIAS_REDUCTIONS`), each at the level whose Gain Reduction it is, for the naive shaper and for ADAA. It checks each against `ALIASING_TOLERANCE`'s ceiling and exits non-zero when a row is over it.

**`ALIASING_TOLERANCE` for Colour:** the ceiling holds by a wide margin. The worst alias at Colour 100% is −47.4 dB (10 kHz at 30 dB of Gain Reduction, at 48 kHz), since the residual's weight is small. The improvement over the naive shaper does not hold everywhere: it is 23.4 and 19.8 dB at 10 kHz at 10 dB of Gain Reduction, but 14.6 and 10.7 dB at 20 dB and 12.1 and 9.1 dB at 30 dB (44.1 and 48 kHz), under the 15 dB Saturation's hardest condition meets. There the driven signal is 12 to 16 dB over the knee, where first-order ADAA at the base rate gains least, as Saturation's own milder Drives do. The improvement is printed and not checked: the ceiling, which is what is heard, holds with 23 dB to spare. Oversampled, Colour's residual gains far more at 10 kHz: the PR for Oversampling gives the lab's figures, and Colour still runs at the host rate.

FET's Colour, with more than twice VCA's weight, aliases more, and the ceiling still holds with 15.8 dB to spare: its worst alias is −39.8 dB (10 kHz at 30 dB of Gain Reduction, at 48 kHz). Its improvement over the naive shaper is printed and not checked, as VCA's: at 10 kHz it is 25.7 and 16.6 dB at 10 dB of Gain Reduction, but 14.6 and 10.8 dB at 20 dB and 12.5 and 9.5 dB at 30 dB (44.1 and 48 kHz).

    python3 tools/dyn-lab/dynlab.py colour
    python3 tools/dyn-lab/dynlab.py colour --character vca
    python3 tools/dyn-lab/dynlab.py colour --character vca --colour 50
    python3 tools/dyn-lab/dynlab.py colour --character vca --rates 44100
    python3 tools/dyn-lab/dynlab.py colour --character fet
    python3 tools/dyn-lab/dynlab.py colour --character fet --colour 50

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

## Limiter

The Limiter is the last stage, after Output Gain, on every channel with one shared gain from the loudest channel. Each mode has its own:

- **Zero Latency:** a zero-attack gain computer on the sample peak. Each sample's Gain Reduction brings it to the Ceiling at once, then releases with one pole in dB. A safety clip at the Ceiling follows. It adds no delay, and no sample's magnitude exceeds the Ceiling.
- **High Quality:** true-peak detection and a lookahead, which together fill the Limiter reserve exactly, so the latency never changes. The true-peak estimator is a 4x interpolator (`TRUE_PEAK_FACTOR`), a Kaiser-windowed sinc polyphase FIR 16 host samples long (`TRUE_PEAK_TAPS`, `TRUE_PEAK_BETA`). Its cutoff is the host's Nyquist, so its phase 0 is the host sample itself. It reads 8 samples ahead of the sample it estimates, its delay at every rate. Each sample's estimate is the loudest of its own segment (the sample and the three points after it) and the one before, so a peak between two samples brings both down. The lookahead window, W, is the Limiter reserve less the estimator's delay: 37 samples at 44.1 kHz, 184 at 192 kHz. Over it, the Gain Reduction asked for goes through a sliding maximum over W + 1 samples, the release, and an average over W + 1 samples. So the gain moves smoothly and is fully down when a peak reaches the audio, which is delayed by the estimator's delay plus W. It aims `LIMITER_TRUE_PEAK_HEADROOM_DB` under the Ceiling.

Below `LIMITER_SETTLED_DB` (10⁻⁷ dB) the Gain Reduction counts as none, so once the release has settled, a signal that never reaches the Ceiling passes bit for bit.

The constants, named once in `dynlab.py`:

- **`CEILING_RANGE`, −30 to 0 dBFS, default 0 dBFS:** comp1's own. Pro-C 3 has no output Limiter whose range it could follow. Full scale at the top, and as the default, means the Limiter switched on stops only overs. 30 dB down leaves room for a Ceiling far under any delivery spec's peak level, for stems and for headroom kept on purpose.
- **`LIMITER_RELEASE_MS`, 200 ms, in both modes:** the shortest of the trade-off table's releases whose distortion on a 50 Hz tone stays within 1% (`LIMITER_DISTORTION_LIMIT`) in both modes. Between a low tone's peaks the gain recovers a little and is pulled down again, a ripple that grows as the release shortens: 1.9% at 100 ms, 6.6% at 20 ms. 1% is about where a limiter on a bass note starts to be heard as fuzz rather than as level. The cost is recovery: back from a 6 dB limit to under 1 dB in 358 ms.
- **`LIMITER_TRUE_PEAK_HEADROOM_DB`, 0.3 dB:** the estimator's worst under-read on the band-limited test signals is 0.22 dB (a 20 kHz tone at 48 kHz), so 0.3 dB covers it with 0.08 dB to spare for the gain's own movement. On the reference set, High Quality's true peaks come out 0.15 to 0.30 dB under the Ceiling.

**The band-limited test signals** are tones at 1, 5, 10, 15, 19 and 20 kHz and noise from three seeds, at full scale, 1024 samples each. The noise is 64 sines evenly spaced in log frequency from 20 Hz to 20 kHz, phases from the seeded generator, scaled to a sample peak. Every tone and the noise fade in over 1 ms (`LIMITER_FADE_MS`), on a raised cosine. An abrupt start has content up to Nyquist, where no interpolator, the reference's included, can say where the true peak lies: the same tones started abruptly, or white noise, read up to about 0.8 dB under. **The 16x reference** is the same design at 16x, 64 host samples long, Kaiser β 10, flat to within 0.001 dB up to 20 kHz at 44.1 kHz. It reads silence before a signal, and stops at the last sample whose interpolation reads nothing past the end, where the audio would go on.

### `limiter`

Prints the Limiter's figures at a Ceiling (`--ceiling dB`, 0 dBFS by default) and at each sample rate (44.1, 48 and 96 kHz by default, `--rates`). It exits non-zero when a reference signal passes the Ceiling or an under-read is over the headroom.

- **Its parts,** per rate, in host samples: the Limiter reserve, the estimator's delay, the lookahead window, the release, and the headroom, with the estimator's worst under-read and the signal it was on.
- **The reference set's peaks:** each signal's output sample peak and 16x true peak, in dB against the Ceiling, through each mode. Zero Latency promises the sample peak, High Quality the true peak. The signals are the full-scale burst (256 samples of silence, then a 1 kHz sine from its peak at full scale), a 10 kHz and a 19 kHz tone, and the band-limited noise, each pushed 12 dB over the Ceiling (`LIMITER_PUSH_DB`), 2048 samples.
- **The timing,** per mode: the full-scale burst for 50 ms between a quiet level (alternating samples at −40 dBFS, so each sample's Gain Reduction reads off it), through a Ceiling of −12 dBFS. The Gain Reduction on the burst's first sample in the audio heard, against the most it reaches over the burst: equal, the gain is fully down when the burst arrives. Then the release's 63% and 90%, counted from the first quiet sample heard. High Quality's average over the window rounds the release's start, so it comes out about 0.4 ms longer than Zero Latency's.
- **The release's trade-off:** each candidate release's recovery from a 6 dB limit and the distortion (THD+N) of a 49.8 Hz tone 6 dB over the Ceiling, in each mode, at 48 kHz. The tone is bin-centred over 16384 samples, and measured over a second length, once the gain has settled.

    python3 tools/dyn-lab/dynlab.py limiter
    python3 tools/dyn-lab/dynlab.py limiter --ceiling -6 --rates 44100
    python3 tools/dyn-lab/dynlab.py limiter --ceiling -30 --rates 192000

## Gate

The Gate is a hard gate on the main input, before the Dry/Wet split, so it lowers the Dry and the Wet path alike. Its key is the Detection Source after the Detection EQ, and one Gate gain goes on every main channel, from the louder channel's key. Each sample:

- **The key level** is the loudest Detection Level of the key over the sample and the `GATE_KEY_HOLD_MS` before it, in dB where a full-scale sine reads 0.
- **Open or closed:** the Gate opens when the key level reaches Gate Threshold, and closes once it is `GATE_HYSTERESIS_DB` under it. In between it stays as it is.
- **Gate Hold** keeps it open for Gate Hold ms after it would close, before Gate Release begins. A key level back above the closing level restarts the count.
- **The Gain Reduction:** open, it asks for none; closed, exactly Gate Range. Gate Attack is how fast it opens and Gate Release how fast it closes, each one pole in dB covering 1 − 1/e of the move in its time, as the compressor's detector does. Under `GATE_SETTLED_DB` (10⁻⁷ dB, the Limiter's `LIMITER_SETTLED_DB`) it counts as none, so an open Gate passes the audio bit for bit.
- **No key** (External with no Sidechain connected) leaves it open.

Gate Attack, Gate Release and Gate Hold run on samples. The model holds the settings fixed; the Engine glides them on the 16-sample run grid (`RUN_SAMPLES`), as it does the compressor's.

The constants, named once in `dynlab.py`, from `gate`'s trade-off table:

- **`GATE_KEY_HOLD_MS`, 10 ms:** a low tone's level falls to nothing at each zero crossing, so a key level read sample by sample closes the Gate on every half cycle. Holding the loudest level over a window bridges the crossings, at the cost of starting to close that much later after the key stops. The table's signals are a 20 Hz and a 50 Hz tone, the lowest a key commonly carries, and pink-like noise, each 0.1 dB over Gate Threshold (`GATE_RIPPLE_ABOVE_DB`), with Gate Hold 0 ms, so the key hold and hysteresis alone must hold the Gate open. 10 ms is the shortest key hold at which some hysteresis keeps all three steady: at 5 ms the 20 Hz tone ripples by 1.8 dB or more whatever the hysteresis, up to 8 dB.
- **`GATE_HYSTERESIS_DB`, 6 dB:** the smallest that keeps the 20 Hz tone steady at that key hold. A 10 ms window on a 20 Hz tone dips to 4.6 dB under the tone's peak at each zero crossing, so a closing level 6 dB down clears it. The key hold costs on a stop, the case a gate is for (a hit ends, the bleed between hits is lowered); the hysteresis costs only on a slow fade, where the Gate closes 6 dB lower, 100 ms later at 60 dB/s (`GATE_FADE_DB_PER_S`), letting a decaying tail go on a little further down rather than chopping it. A longer key hold with less hysteresis (15 ms and 2 dB) fades sooner but stops later.

### `gate`

Prints the Gate's step response at each sample rate (44.1, 48 and 96 kHz by default). The key steps from 20 dB above Gate Threshold to 20 dB below it and back, each from steady state. For each rate it reports:

- the closing: when Gate Release begins (Gate Hold plus the key hold), and Gate Release's 63% and 90% times from then;
- the opening: Gate Attack's 63% and 90% times;
- the closed depth, settled, which equals Gate Range.

At Gate Range 0 dB there is nothing to measure, and the row says so. Then it prints the key hold's and hysteresis's trade-off at 48 kHz, at the Gate Threshold, Gate Range, Gate Attack and Gate Release given, with Gate Hold 0 ms: for each candidate pair, the Gain Reduction's ripple, peak to peak, on each signal over its second half, and how late the Gate starts closing after a stop (the key hold) and on a key fading at 60 dB/s (the key hold plus the time to fall through the hysteresis), with the chosen pair marked.

    python3 tools/dyn-lab/dynlab.py gate
    python3 tools/dyn-lab/dynlab.py gate --rates 44100 48000 88200 96000 176400 192000
    python3 tools/dyn-lab/dynlab.py gate --gate-attack 0.5 --gate-release 50 --gate-hold 0 --rates 44100 48000 88200 96000 176400 192000
    python3 tools/dyn-lab/dynlab.py gate --gate-range 6 --rates 48000

## Tests

    python3 tools/dyn-lab/test_dynlab.py

The tests also run each example above, exactly as written.
