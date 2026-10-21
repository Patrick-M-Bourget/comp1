# Threshold, Release, Range and Output Gain ranges, and Stereo Link's default, are comp1's own

The spec follows Pro-C 3's parameter ranges, but FabFilter's public documentation gives no complete range for four of comp1's controls. comp1 sets these four itself, and Drive, for its Saturation stage, too:

- **Threshold: −60 to 0 dB.** The minimum keeps the only figure FabFilter publishes ("Extended Threshold range to -60 dB", Pro-C 2 manual, version history). The maximum is full scale: the Detection Level reads 0 dB for a full-scale sine, so a Threshold above 0 dB would almost never be crossed.
- **Release: 5 to 5000 ms.** FabFilter publishes no Release range. This span covers fast pumping at one end and slow levelling on every Character at the other. Opto's slow release stage is separate from Release (spec #1) and isn't bounded by it.
- **Range: 0 to 60 dB, default 60 dB.** FabFilter describes Range ("limits the maximum amount of applied gain change", Pro-C 3 manual, "Dynamics controls") but gives no limits. 60 dB is the most Gain Reduction the compressor can apply anyway: the lowest Threshold, −60 dB, at Ratio ∞ with a full-scale input. So the default sets no limit, and every Range below it does.
- **Output Gain: −36 to +36 dB, default 0 dB.** The Pro-C 3 manual describes the Output Level knob ("Input and output options") but gives no range. The range is symmetric, so 0 dB sits at its exact centre and the default passes audio through bit for bit. Its top leaves enough make-up headroom for deep compression, and its 72 dB span keeps the slider usable.
- **Drive: 0 to 24 dB, default 6 dB.** Drive is level-compensated, so it sets only how far the signal is pushed into the Saturation shape, whose knee is full scale at 0 dB: at 0 dB a signal at or below full scale passes untouched, and at 24 dB it starts 24 dB under full scale, for heavy distortion. The default, 6 dB, shapes only the top 6 dB.

Stereo Link's range, 0 to 100%, is the Pro-C 3 manual's ("Sidechain section": the first half of the knob runs from 0%, fully unlinked, to 100%, fully linked), but the manual gives no default. comp1 sets it itself:

- **Stereo Link: default 100%.** Both channels share one Gain Reduction, so compressing a stereo track never shifts its image, which an unlinked compressor does whenever one side is louder than the other. A user who wants the channels to move apart turns it down.

The Detection EQ's Slopes take their top from the Pro-C 3 manual, which gives its side chain filters "slopes up to 96 dB/oct and also the Brickwall slope" ("Side chain section") but not the steps between. comp1 steps them every 6 dB/oct from 6 dB/oct, a whole filter order each, and leaves out Brickwall. The manual gives no range for the side chain EQ's Frequency, Gain or Q, so those are comp1's own too; `parameterRanges` names each with its reason.

The other ranges follow Pro-C 3's documentation: Ratio 1:1 to ∞, Knee 0 to 72 dB and Attack 0.005 to 250 ms from the manual, and Hold up to 500 ms from the Pro-C 3 product page (the manual gives no Hold range). dyn-lab (`tools/dyn-lab/dynlab.py`) names each range once and cites its source.

## Considered Options

- **Reading the missing ranges off Pro-C 3's controls.** Rejected: values read off a screen can't be cited the way a document can, and they could change in a FabFilter update without anyone noticing. The spec follows Pro-C 3's ranges to keep comp1 familiar, not to stay bit-for-bit equal.
- **Release 10 to 2500 ms.** Rejected as too narrow at both ends.
- **A Range above 60 dB.** Rejected: no setting can reach it, so the top of the control would do nothing.
- **Stereo Link 0% by default.** Rejected: an unlinked default moves the stereo image as soon as the two channels' levels differ, which is rarely what compressing a stereo track is for.
