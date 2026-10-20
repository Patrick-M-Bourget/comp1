# plugkit: two modules, copied from eq1 until eq1 ships

comp1 shares eq1's infrastructure through `plugkit`, a library in its own repo, so comp1 never holds a private copy of eq1's code. It mirrors eq1's own split: `plugkit-dsp` is plain C++ with no JUCE (the Smoother and 16-sample run grid, matched filter designs, Placement), and `plugkit-ui` is the JUCE layer both plugins share: the UI kit (Staple, the spectrum and display layers, meter ballistics, EqDisplay, the Output Meter, the editor test harness) and the Plugin Shell's Presets, A/B Compare and edit history (undo and redo). CI scripts and the check harness come with it.

eq1 does not change until it ships. Until then, everything in `plugkit` is a copy of eq1's code. Each copied piece, and every eq1 fix ported to `plugkit` by hand, is listed in eq1's migration ticket. After eq1 ships, it migrates onto `plugkit` in that one gated ticket, and its null check must stay `exact`.

## Consequences

- `plugkit` grows in slices, each just before comp1 needs it: CI and the check harness first, `plugkit-dsp` before comp1's Engine skeleton, and `plugkit-ui` before the Expanded View. The early DSP work isn't blocked on UI code.
- Until eq1 migrates, a bug fixed in eq1 must also be fixed in `plugkit`. The migration ticket's list is how that drift is tracked.

## Considered Options

- **Wrapping eq1's components behind an interface instead of copying them** (the spec's first draft). Rejected: impossible without changing eq1 before it ships.
- **comp1 vendoring its own copy until eq1 ships.** Rejected: two private copies drift with nothing tracking them, and the later move to a library is two migrations instead of one.
