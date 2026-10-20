# dyn-lab

A Python model of comp1's Characters. It prints the figures the Engine's tests check, so a ticket brief can cite a command and its output exactly. It needs only the standard library: no installs, no numpy. Run it from the repo root.

It models only the Clean Character so far. Clean follows Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor Design — A Tutorial and Analysis". It is feedforward, with a soft-knee gain computer (the paper's eq. 4) and a smooth branching peak detector in the log domain (eq. 12), plus Hold. Each Attack and Release is a single pole, so 63% (1 − 1/e) of a step takes one time constant and 90% takes ln 10 ≈ 2.303 of them. It isn't the paper's decoupled detector (eq. 17): that one runs Release through the attack pole as well, so gain comes back more slowly than Release says.

Hold freezes the Gain Reduction for Hold ms whenever it stops rising, mid-Attack included, before Release begins. Any rise restarts the count.

Every option has a default, and a setting outside its range exits with an error that names the range. The ranges are named once in `dynlab.py` (`THRESHOLD_RANGE`, `RATIO_RANGE` and so on), each with its source: Pro-C 3's documentation, or ADR 0008 for Threshold and Release.

| Setting | Range | Default |
|---|---|---|
| Threshold | −60 to 0 dB | −18 dB |
| Ratio | 1 to ∞ (`inf` or `∞`) | 4 |
| Knee | 0 to 72 dB | 6 dB |
| Attack | 0.005 to 250 ms | 10 ms |
| Release | 5 to 5000 ms | 100 ms |
| Hold | 0 to 500 ms | 0 ms |

## `transfer`

Prints the Transfer Curve, the output level for each steady input level. The input grid runs from 12 dB below the Knee to 24 dB above it, in 3 dB steps, and always includes Threshold and both edges of the Knee. A final line gives the measured slope on each side of the Knee: 1 below it, 1/Ratio above it.

    python3 tools/dyn-lab/dynlab.py transfer
    python3 tools/dyn-lab/dynlab.py transfer --threshold -24 --ratio 4 --knee 12
    python3 tools/dyn-lab/dynlab.py transfer --threshold -12 --ratio inf --knee 0

## `step`

Prints the step response at each sample rate (44.1, 48 and 96 kHz by default). The Detection Level steps from 20 dB below Threshold to 20 dB above it to measure Attack, then back down to measure Release. Each step starts from steady state. For each rate it reports:

- Attack: the time to reach 63% and 90% of the final Gain Reduction;
- Release: the time to give back 63% and 90% of it, counted from when Release begins;
- Hold: the time the Gain Reduction stays put before Release begins.

At Ratio 1:1 there is no Gain Reduction to measure, and the row says so.

    python3 tools/dyn-lab/dynlab.py step
    python3 tools/dyn-lab/dynlab.py step --attack 10 --release 100 --hold 0
    python3 tools/dyn-lab/dynlab.py step --attack 0.5 --release 50 --hold 20 --rates 44100 192000

## Tests

    python3 tools/dyn-lab/test_dynlab.py

The tests also run each example above, exactly as written.
