# The Compact View's slots follow the Style

The Compact View keeps one fixed layout for every Style. Comp and the Gain Reduction meter are its core, in every Style, and two Compact Slots beside Comp show the controls that Style is most often tuned by. Only what the Compact Slots hold changes with the Style; nothing moves.

A one-knob compressor is fast, but each kind of compressor is tuned by different controls beyond its amount: an opto by its emphasis and colour, a vocal leveller by its gate, a bus compressor by its attack, release and detection high-pass (docs/research/compact-view-style-controls.md). One set of controls for every Style would show some Styles controls they rarely need and hide the ones they do. A layout that changed with the Style would move controls under the user's hand and break muscle memory. A fixed layout whose slots follow the Style gives each Style its own controls at the same places.

## Layout

- **One fixed body for every Style:** Compact Slot A, then Comp (the 120 px hero), then Compact Slot B, then the Gain Reduction meter. Positions never animate.
- **A Style change crossfades the Compact Slots' contents** at an existing Staple motion duration, instantly under reduced motion.
- **The header and footer are unchanged** and never duplicated in the body. The window stays 780×420. The Expanded View is untouched.

## The core

- **Comp**, labelled "Comp" in every Style. In the Compact View only, it has a **Detection Level ring**, like Threshold's in the Expanded View, and a **Comp Follow-off cue**: while Comp Follow is off, its arc dims to the Global Bypass scale (0.45), and its tooltip says that Threshold and Ratio are set directly and that grabbing Comp makes them follow again (ADR 0004). The cue adds no new element.
- **The Gain Reduction meter.**

There is no tone control in the core, and the Gate is a Compact Slot's, not the core's.

## Compact Slots

- **Two Compact Slots, each a 66 px primary plus a Companion** under it: a toggle or a 30 px knob. A Compact Slot is never empty, so every Style fills the same rectangles.
- **Compression and saturation settings only.** Never Threshold or Ratio, since editing them turns Comp Follow off (ADR 0004); never Dry or Wet; never a footer function. Every control is an existing parameter, the same one the Expanded View shows.
- **What a Compact Slot shows follows only the Style parameter**, never a Preset or A/B Compare. A Style change writes the Style's defaults as it does today (ADR 0002); the Compact Slots don't change that.
- **The High-pass:** as a whole Compact Slot, it is its Frequency as the primary and its on toggle as the Companion, with Slope not shown and left at its default. As a Companion, it is the on toggle alone.
- **Hidden controls get no indicator.** A control engaged but not shown in the current Style's Compact Slots isn't marked anywhere in the Compact View.

| Style | Compact Slot A | Compact Slot B |
|---|---|---|
| Clean | Attack + Knee | Release + Auto Release |
| VCA | Attack + High-pass on | Release + Auto Release |
| FET | Attack + Colour | Release + Hold |
| Opto | Colour + Range | High-pass Frequency + on |
| Vocal | Gate Threshold + Gate on | Release + Auto Release |
| Instrument | Attack + Knee | Release + Auto Release |
| Bus | Attack + High-pass on | Release + Auto Release |
| Drums | Attack + Hold | Drive + Saturation on |

Each entry is the primary, then its Companion.

## Accessibility

- **Tab order is fixed** for every Style: Compact Slot A's primary, its Companion, Comp, Compact Slot B's primary, its Companion, then the Gain Reduction meter.
- **Each control's accessible name is its parameter's name.**
- **Nothing is announced on a Style change.** The Style chip already announces the new Style.

## Considered Options

- **Three Compact Slots**, the Gate always present as a third. Rejected: the Gate suits the Vocal leveller, and elsewhere its space is better spent on timing or tone; a third slot crowds the 780 px body.
- **Chip rows**, such as stepped Attack values as on bus compressors. Rejected: a row of steps is a different shape from a knob, so the Compact Slots would stop being identical rectangles, and it can't show a value between its steps set in the Expanded View or by Host Automation.
- **A tone control in the core.** Rejected: most simplified compressor UIs have no tone control, and tone earns a place only on the Styles whose class ties colour to its use (Opto and Drums). Clean has no Colour.
- **Per-Style labels for the hero.** Rejected: no simplified UI renames its amount control with a mode, and a label that changed with the Style would rename Comp under the user's hand. It stays "Comp", the glossary's term.
- **A merged High-pass knob, off at its minimum.** Rejected: the High-pass's on and its Frequency are separate parameters, so one knob would write two parameters in one gesture and hide the stored Frequency while off. A Companion toggle is just the on parameter.
- **A footer dot for hidden engaged controls.** Rejected: the footer is unchanged in the Compact View, and a dot would say that something hidden is engaged without saying what. The Expanded View shows every control.
