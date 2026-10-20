# Threshold and Release ranges are comp1's own

The spec follows Pro-C 3's parameter ranges, but FabFilter's public documentation gives no complete range for two of the compressor's controls. comp1 sets these two itself:

- **Threshold: −60 to 0 dB.** The minimum keeps the only figure FabFilter publishes ("Extended Threshold range to -60 dB", Pro-C 2 manual, version history). The maximum is full scale: the Detection Level reads 0 dB for a full-scale sine, so a Threshold above 0 dB would almost never be crossed.
- **Release: 5 to 5000 ms.** FabFilter publishes no Release range. This span covers fast pumping at one end and slow levelling on every Character at the other. Opto's slow release stage is separate from Release (spec #1) and isn't bounded by it.

The other ranges follow Pro-C 3's documentation: Ratio 1:1 to ∞, Knee 0 to 72 dB and Attack 0.005 to 250 ms from the manual, and Hold up to 500 ms from the Pro-C 3 product page (the manual gives no Hold range). dyn-lab (`tools/dyn-lab/dynlab.py`) names each range once and cites its source.

## Considered Options

- **Reading the two ranges off Pro-C 3's controls.** Rejected: values read off a screen can't be cited the way a document can, and they could change in a FabFilter update without anyone noticing. The spec follows Pro-C 3's ranges to keep comp1 familiar, not to stay bit-for-bit equal.
- **Release 10 to 2500 ms.** Rejected as too narrow at both ends.
