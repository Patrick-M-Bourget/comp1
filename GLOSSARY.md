# comp1

A real-time compressor plugin for musicians and producers, the sibling of eq1. One Engine serves a quick Compact View, modeled on Waves Renaissance Vox, and a deep Expanded View, modeled on FabFilter Pro-C 3. Terms follow Pro-C's vocabulary, and eq1's where the two plugins share a feature, so users meet the words they already know.

## Styles

**Style**:
One of the eight starting points in a single picker: Clean, VCA, FET, Opto, Vocal, Instrument, Bus or Drums. Choosing a Style sets its Character and its defaults.
_Avoid_: Mode, type, algorithm, preset

**Character**:
One of the four Styles that define how the compressor behaves and sounds (Clean, VCA, FET or Opto): how it detects level, how its timing and knee respond, and its Colour.
_Avoid_: Model, engine, topology, Style (when only these four are meant)

**Source Style**:
One of the four Styles named for what they compress (Vocal, Instrument, Bus or Drums): a Character plus its own defaults and Comp Curve, with no sound of its own.
_Avoid_: Thin Style, preset style, Preset

**Colour**:
A Character's own distortion, which grows with its gain reduction, scaled from none to as tuned by the Colour control. Clean has none.
_Avoid_: Saturation (for a Character's own), grit, harmonics, drive

## Compressor

**Threshold**:
The Detection Level above which the compressor starts reducing gain.
_Avoid_: Trigger level

**Ratio**:
How many dB the input must rise above Threshold for the output to rise by 1 dB, up to ∞:1.

**Knee**:
The width of the region around Threshold over which compression fades in.

**Transfer Curve**:
The output level the compressor gives for each steady input level, set by Threshold, Ratio and Knee.
_Avoid_: Static curve, curve (unqualified), Comp Curve

**Attack**:
How quickly the compressor reduces gain once the Detection Level exceeds Threshold.

**Release**:
How quickly the compressor gives gain back once the Detection Level falls below Threshold.

**Auto Release**:
Release that follows the material: short after transients, longer after sustained compression.
_Avoid_: Program-dependent release, adaptive release

**Hold**:
How long gain reduction is held before Release begins.

**Range**:
The most gain reduction the compressor will apply, in dB.
_Avoid_: Depth, Dynamic Range, max GR

**Gain Reduction**:
How far the compressor, Gate or Limiter is lowering the level at this moment, in dB.
_Avoid_: GR (in user-facing text), attenuation

**Auto Gain**:
Automatic compensation of output level, estimated from the Transfer Curve rather than measured, so that compression is heard without a loudness bias.
_Avoid_: Make-up gain, gain compensation

**Dry** / **Wet**:
The separate levels of the unprocessed and the compressed signal, summed for parallel compression.
_Avoid_: Mix, blend

## Comp

**Comp**:
The single control that sets Threshold and Ratio together along the current Style's Comp Curve.
_Avoid_: Macro, amount, compression knob

**Comp Curve**:
The path from Comp to a Threshold and Ratio pair that each Style sets.
_Avoid_: Curve (unqualified), Transfer Curve, macro curve

**Comp Follow**:
Whether Threshold and Ratio follow Comp. On, they come from the Comp Curve and Auto Gain is on; off, they are set directly.
_Avoid_: Linked, Link, Comp Link

## Gate, Saturation and Limiter

**Gate**:
The downward expander before the compressor, which lowers the signal while its Detection Level is below Gate Threshold. Its controls are Gate Threshold, Gate Range, Gate Attack, Gate Release and Gate Hold.
_Avoid_: Expander, noise gate; Threshold, Range, Attack, Release or Hold (unqualified) for the Gate's

**Saturation**:
The standalone distortion stage the user drives, placed before or after the compressor.
_Avoid_: Colour, drive stage, distortion

**Drive**:
How hard the signal is pushed into Saturation, in dB.
_Avoid_: Saturation amount, input gain

**Saturation Position**:
Where Saturation sits on the Wet path: Before the compressor, which then reacts to the saturated signal, or After it, before Auto Gain.
_Avoid_: Saturation order, pre/post

**Limiter**:
The last stage, which never lets the output pass the Ceiling.
_Avoid_: Clipper, maximizer

**Ceiling**:
The highest level the Limiter lets through, in dBFS.
_Avoid_: Output ceiling, threshold (for the Limiter)

## Detection

**Sidechain**:
The plugin's separate audio input, used as an external signal for detection rather than for processing.
_Avoid_: Key input, SC

**Detection Source**:
The signal the compressor and Gate listen to: Internal (the main input) or External (the Sidechain).
_Avoid_: Trigger, key

**Detection EQ**:
The high-pass, low-pass and one free band that shape what the detector hears, without changing the processed sound.
_Avoid_: Sidechain EQ, key filter, SC EQ

**High-pass**:
The Detection EQ's band that takes out what lies below its Frequency, at its Slope, so low notes push the compressor less.
_Avoid_: Low cut (for the Detection EQ), HPF

**Low-pass**:
The Detection EQ's band that takes out what lies above its Frequency, at its Slope.
_Avoid_: High cut (for the Detection EQ), LPF

**Free band**:
The Detection EQ's third band, a Bell, Low Shelf or High Shelf with its own Frequency, Gain and Q.
_Avoid_: Mid band, peak band

**Detection Audition**:
Listening to the signal the detector hears instead of the plugin's output.
_Avoid_: Sidechain solo, listen

**Detection Level**:
The level of the detection signal, as the detector compares it with Threshold, in dB where a full-scale sine reads 0.
_Avoid_: Input level, sidechain level

**Stereo Link**:
How much the two channels share one gain reduction, from 0% (independent) to 100% (fully shared).
_Avoid_: Link (unqualified), channel link

**Stereo Placement**:
Which part of the stereo signal is compressed: Stereo, Mid or Side.
_Avoid_: Channel mode, M/S mode

## Whole-plugin

**Processing Mode**:
How the whole plugin trades latency against quality: Zero Latency, or High Quality with Oversampling and Lookahead.
_Avoid_: Phase mode, quality setting

**Oversampling**:
Running the processing at a multiple of the host's sample rate, in High Quality only.
_Avoid_: Upsampling, HQ factor

**Lookahead**:
How far the compressor sees ahead of the audio it processes, in High Quality only.
_Avoid_: Pre-delay, latency

**Compact View**:
The editor showing only Style, Gate, Comp, Output Gain and a Gain Reduction meter.
_Avoid_: Simple view, mini view, basic mode

**Expanded View**:
The editor showing every control, the scrolling level and Gain Reduction graph, and the Detection EQ.
_Avoid_: Advanced view, full view, pro mode

**Output Gain**:
The gain applied to the whole plugin's output, after Dry and Wet are summed and before the Limiter, in dB.
_Avoid_: Master gain, Gain (unqualified)

**Global Bypass**:
comp1's own click-free switch that passes the input through unprocessed, separate from the host's bypass.
_Avoid_: Bypass (unqualified) for the whole plugin

**Preset**:
A saved, named set of every setting that affects the sound: Factory (shipped with comp1) or User (saved by the user).
_Avoid_: Patch, program, snapshot, Style

**A/B Compare**:
Switching between two independent sets of the settings a Preset holds, in one plugin instance, to compare them.
_Avoid_: Snapshot, compare slots

**Host Automation**:
Parameter changes recorded and played back by the DAW hosting the plugin.
_Avoid_: Automation (unqualified)

**UI Scale**:
How large the whole editor is drawn, per instance; it changes the view, never the sound.
_Avoid_: Zoom, size

**Input Level**:
The main input's sample peak as the plugin receives it, before any stage, in dBFS. Not what the detector hears, which is the Detection Level.
_Avoid_: Input meter, pre level, Detection Level

**Output Level**:
What the plugin sends on, after every stage, per channel: its sample peak and its RMS over 300 ms, in dBFS.
_Avoid_: Output meter (for the level itself), post level, output volume

**Output Meter**:
The display of the output's sample peak and RMS per channel, with a Clip Light.
_Avoid_: Meter (unqualified), level meter, VU

**Clip Light**:
A per-channel indicator that lights when the output peaks above 0 dBFS and stays lit until clicked.
_Avoid_: Clip indicator, over light
