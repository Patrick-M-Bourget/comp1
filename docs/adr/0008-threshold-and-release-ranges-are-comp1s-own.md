# Threshold, Release, Range and Output Gain ranges are comp1's own

The spec follows Pro-C 3's parameter ranges, but FabFilter's public documentation gives no complete range for four of comp1's controls. comp1 sets these four itself:

- **Threshold: −60 to 0 dB.** The minimum keeps the only figure FabFilter publishes ("Extended Threshold range to -60 dB", Pro-C 2 manual, version history). The maximum is full scale: the Detection Level reads 0 dB for a full-scale sine, so a Threshold above 0 dB would almost never be crossed.
- **Release: 5 to 5000 ms.** FabFilter publishes no Release range. This span covers fast pumping at one end and slow levelling on every Character at the other. Opto's slow release stage is separate from Release (spec #1) and isn't bounded by it.
- **Range: 0 to 60 dB, default 60 dB.** FabFilter describes Range ("limits the maximum amount of applied gain change", Pro-C 3 manual, "Dynamics controls") but gives no limits. 60 dB is the most Gain Reduction the compressor can apply anyway: the lowest Threshold, −60 dB, at Ratio ∞ with a full-scale input. So the default sets no limit, and every Range below it does.
- **Output Gain: −36 to +36 dB, default 0 dB.** The Pro-C 3 manual describes the Output Level knob ("Input and output options") but gives no range. The range is symmetric, so 0 dB sits at its exact centre and the default passes audio through bit for bit. Its top leaves enough make-up headroom for deep compression, and its 72 dB span keeps the slider usable.

The other ranges follow Pro-C 3's documentation: Ratio 1:1 to ∞, Knee 0 to 72 dB and Attack 0.005 to 250 ms from the manual, and Hold up to 500 ms from the Pro-C 3 product page (the manual gives no Hold range). dyn-lab (`tools/dyn-lab/dynlab.py`) names each range once and cites its source.

## Considered Options

- **Reading the missing ranges off Pro-C 3's controls.** Rejected: values read off a screen can't be cited the way a document can, and they could change in a FabFilter update without anyone noticing. The spec follows Pro-C 3's ranges to keep comp1 familiar, not to stay bit-for-bit equal.
- **Release 10 to 2500 ms.** Rejected as too narrow at both ends.
- **A Range above 60 dB.** Rejected: no setting can reach it, so the top of the control would do nothing.
