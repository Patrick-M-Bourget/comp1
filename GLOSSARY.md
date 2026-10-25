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
Automatic compensation of output level, estimated from the settings (the Transfer Curve, the timing and the Character) rather than measured, so that compression is heard without a loudness bias.
_Avoid_: Make-up gain, gain compensation

**Timing Offset**:
How much higher than each reference level Auto Gain's estimate reads the Transfer Curve, in dB, for the Attack, Release, Auto Release, Hold and Character: what the timing does to the level the Transfer Curve alone predicts.

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
Whether Threshold and Ratio follow Comp. On, they come from the Comp Curve; off, they are set directly. Auto Gain is its own setting either way.
_Avoid_: Linked, Link, Comp Link

## Gate, Saturation and Limiter

**Gate**:
The downward expander, placed by Gate Position, which lowers the signal once its Held Level has fallen well below Gate Threshold, and passes it again when the Held Level reaches Gate Threshold. Its controls are Gate Threshold, Gate Range, Gate Attack, Gate Release, Gate Hold and Gate Position.
_Avoid_: Expander, noise gate; Threshold, Range, Attack, Release or Hold (unqualified) for the Gate's

**Gate Position**:
Where the Gate sits: Pre, on the input before Dry and Wet split, so it lowers both; or Post, on the Wet path directly after the compressor, so Dry is never gated, and with an Internal Detection Source it listens to the compressed signal. Switching it glides the Gate's Gain Reduction out, moves the Gate once that is exactly zero, and glides it back in, so while the Gate is on, every switch, Host Automation included, disengages it for a moment: about 120 ms fading out to exactly zero, then about 50 ms fading back in.
_Avoid_: Gate order, gate mode

**Gate Threshold**:
The Held Level at which the Gate opens. The Gate closes only once the Held Level falls a fixed hysteresis under it, so a level wavering about Gate Threshold doesn't flip it to and fro.
_Avoid_: Gate level, open level

**Gate Range**:
How far the Gate lowers the signal while closed, in dB.
_Avoid_: Gate depth, floor

**Gate Attack**:
How quickly the Gate opens once the Held Level reaches Gate Threshold.

**Gate Release**:
How quickly the Gate closes once Gate Hold has run out, after the Held Level fell the hysteresis under Gate Threshold.

**Gate Hold**:
How long the Gate stays open after the Held Level falls the hysteresis under Gate Threshold, before Gate Release begins.

**Held Level**:
The loudest Detection Level the Gate has heard over a short window ending now, which the Gate compares with Gate Threshold, so a low tone's zero crossings don't close it.
_Avoid_: Key level, key hold

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
The signal the compressor and Gate listen to: Internal (the main input, or for the Gate at Post the compressed signal) or External (the Sidechain).
_Avoid_: Trigger, key

**Detection EQ**:
The high-pass, low-pass and one free band that shape what the detector hears, without changing the processed sound. Set with the Expanded View's Detection section's controls, beside Detection Audition.
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
The level of the detection signal, before any feedback, in dB where a full-scale sine reads 0. Clean and VCA compare it with Threshold; FET compares it less the Gain Reduction it is applying.
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
Running Saturation's and Colour's nonlinear residuals at a multiple of the host's sample rate, in High Quality only: 1x, 2x, 4x or 8x, capped so the internal rate stays at or under about 384 kHz (ADR 0001, ADR 0009).
_Avoid_: Upsampling, HQ factor

**Lookahead**:
How far the compressor sees ahead of the audio it processes, in High Quality only.
_Avoid_: Pre-delay, latency

**Compact View**:
The editor showing only Comp, two Compact Slots and a Gain Reduction meter, with Style in the footer, Output Gain in the footer's output popover and Saturation's controls in its Saturation popover.
_Avoid_: Simple view, mini view, basic mode

**Compact Slot**:
One of the Compact View's two places beside Comp whose controls follow the Style: a primary control and its Companion.
_Avoid_: Slot (unqualified), panel, module, section

**Companion**:
A Compact Slot's smaller control, a toggle or a small knob, under its primary.
_Avoid_: Secondary control, sub-knob, satellite

**Expanded View**:
The editor showing every control, the Detection EQ's among them in the Detection section, and the display's two tabs: Level, the Level and Gain Reduction Graph with the Transfer Curve inset over it, and Spectrum, the Spectrum display.
_Avoid_: Advanced view, full view, pro mode

**Level and Gain Reduction Graph**:
The Expanded View's scrolling graph of the last 5 seconds of Input Level, Output Level and the compressor's Gain Reduction, on one dB scale, the newest at its right edge.
_Avoid_: Scope, waveform, history view

**Transfer Curve Graph**:
The graph of the Transfer Curve the Engine plays, output level over input level from -60 to 0 dB, with the Detection Level as a dot moving along it, which the Transfer Curve inset draws.
_Avoid_: I/O graph, transfer function plot

**Transfer Curve inset**:
The small Transfer Curve Graph over the top-left corner of the Level and Gain Reduction Graph, on the Level tab: its Threshold handle drags Threshold across, or Knee up and down with Alt, and its Ratio handle drags Ratio up and down; the arrow keys on it move Threshold and Ratio, and Alt with up and down moves Knee. It is the display's Transfer Curve view: there is no Transfer Curve tab.
_Avoid_: Mini curve, transfer widget

**Spectrum (display)**:
The Expanded View's Spectrum tab: the spectrum of the input tap and of the output tap overlaid, the gap between them, where the output is under the input, shaded as Gain Reduction. comp1 is single-band, so the gap is the same at every frequency.
_Avoid_: Analyzer (for the whole display), FFT view, spectrogram

**Threshold handle** / **Ratio handle**:
The Transfer Curve inset's two drag handles: the Threshold handle on the curve at the knee, where the input is Threshold, the Ratio handle at the curve's right end, where the input is 0 dB. Only they take the mouse. With Alt, the Threshold handle sets Knee instead, and the inset shades the knee region while Alt is held over it; it is still the Threshold handle.
_Avoid_: Knee handle (for the Threshold handle), dot (the Detection Level's)

**Output Gain**:
The gain applied to the whole plugin's output, after Dry and Wet are summed and before the Limiter, in dB.
_Avoid_: Master gain, Gain (unqualified)

**Output readout**:
The footer's reading of Output Gain plus Auto Gain's estimate while Auto Gain is in effect, in both views; a click opens the output popover, which holds Output Gain, Auto Gain, the Limiter and Ceiling.
_Avoid_: Output display, gain readout

**Saturation readout**:
The footer's reading of Drive, "SAT +6 dB", dimmed while Saturation is off, in both views, left of the Output readout; a click opens the Saturation popover, which holds Saturation's on switch, Saturation Position and Drive. The Expanded View's body has no Saturation controls: its Character section holds Colour alone.
_Avoid_: Drive readout, SAT chip

**Global Bypass**:
comp1's own click-free switch that passes the input through unprocessed, separate from the host's bypass.
_Avoid_: Bypass (unqualified) for the whole plugin

**Preset**:
A saved, named set of every setting that affects the sound: Factory (shipped with comp1) or User (saved by the user).
_Avoid_: Patch, program, snapshot, Style

**Loaded Preset**:
The Preset last loaded onto, or saved from, an A/B Compare side, shown by name. Each side has its own, or none until a Preset is loaded or saved on it.
_Avoid_: Current preset, active preset, selected preset

**Modified**:
An A/B Compare side whose settings differ from its Loaded Preset's; returning to the Preset's settings, by edits or undo, makes it unmodified again.
_Avoid_: Dirty, edited, changed (as a state)

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

**Input tap**:
The Engine's mono copy of the main input as the plugin receives it, before any stage, which the Spectrum display reads: the input's side of the gap.
_Avoid_: Pre tap, Input Level (which is its peak, in dBFS)

**Output tap**:
The Engine's mono copy of the Wet path directly after the compressor, before Colour, Saturation After, Auto Gain, Dry/Wet, Output Gain and the Limiter, which the Spectrum display reads: so the gap to the input tap is what the compressor (with the Gate and Saturation Before) took away, never Auto Gain or Output Gain.
_Avoid_: Post tap, Output Level (which is after every stage)

**Output Meter**:
The display of the output's sample peak and RMS per channel, with a Clip Light.
_Avoid_: Meter (unqualified), level meter, VU

**Clip Light**:
A per-channel indicator that lights when the output peaks above 0 dBFS and stays lit until clicked.
_Avoid_: Clip indicator, over light
