# plugkit: two modules, copied from eq1 until eq1 ships

comp1 shares eq1's infrastructure through `plugkit`, a library in its own repo, so comp1 never holds a private copy of eq1's code. It mirrors eq1's own split: `plugkit-dsp` is plain C++ with no JUCE (the Smoother and 16-sample run grid, matched filter designs, Placement), and `plugkit-ui` is the JUCE layer both plugins share: the UI kit (Staple, the spectrum and display layers, meter ballistics, the Output Meter, the editor test harness) and the Plugin Shell's Presets, A/B Compare and edit history (undo and redo). CI scripts and the check harness come with it.

eq1 does not change until it ships. Until then, everything in `plugkit` is a copy of eq1's code. Each copied piece, and every eq1 fix ported to `plugkit` by hand, is listed in eq1's migration ticket. After eq1 ships, it migrates onto `plugkit` in that one gated ticket, and its null check must stay `exact`.

## Consequences

- `plugkit` grows in slices, each just before comp1 needs it: CI and the check harness first, `plugkit-dsp` before comp1's Engine skeleton, and `plugkit-ui` before the Expanded View. The early DSP work isn't blocked on UI code.
- Until eq1 migrates, a bug fixed in eq1 must also be fixed in `plugkit`. The migration ticket's list is how that drift is tracked.

## Amended 2026-10-21: EqDisplay stays in eq1

EqDisplay is not shared. It and its layers (curves, handles, ghost) carry eq1's 24-slot Band model and its engine response maths, about 3,800 lines that no other plugin uses. `plugkit-ui` holds the generic layers instead (grid, spectrum, edge fades, display geometry), and comp1 builds its Detection EQ display on them (#29). `plugkit-ui` also takes the Plugin Shell's UI Scale state, with the Presets, A/B Compare and edit history, because it follows the same session rules. Each plugin pins JUCE: `plugkit-ui` links the JUCE that its consumer has already fetched.

## Amended 2026-10-23: comp1's Detection EQ display is gone

Superseded in part: comp1 no longer has a Detection EQ display. Its Detection EQ is set with the Detection section's controls, so the sentence above about building that display on `plugkit-ui`'s layers no longer applies. EqDisplay still stays in eq1, and the Spectrum display and the other graphs still build on the generic layers.

## Considered Options

- **Wrapping eq1's components behind an interface instead of copying them** (the spec's first draft). Rejected: impossible without changing eq1 before it ships.
- **comp1 vendoring its own copy until eq1 ships.** Rejected: two private copies drift with nothing tracking them, and the later move to a library is two migrations instead of one.
