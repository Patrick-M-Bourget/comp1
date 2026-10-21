# Coding standards

Judgement calls for review. Mechanical rules live in plugkit's `scripts/check.sh` and the git hooks instead.

These are plugkit's, and every plugin built on plugkit shares them: a plugin's own `CODING_STANDARDS.md` starts with this file byte for byte, as it reads at the plugkit tag the plugin pins. The plugin's own rules follow it, after a blank line, under the heading `## <plugin>'s own rules` (the plugin's `PLUGIN_NAME`). `scripts/check.sh docs` checks the copy (plugkit's README.md, "Coding standards").

## Tests reach what they claim to test

A test's inputs must cover the region its name and comments claim. Check sweep bounds and step counts against that region, and make sure a test signal actually has content there.

- **Sweeps:** a sweep "across the hold at 0.95 × Nyquist" must run past 0.95 × Nyquist at every sample rate it is run at, with its step count computed from the range, not fixed.
- **Red without the change:** a test added with a fix or a guard fails when that code is taken out. Untouched samples, a property the code can't affect, or a check the code passes by construction make it pass either way. The PR shows the failing run as evidence, or the reviewer asks for one.
- **Paths on every OS:** a path a test compares or prints is written with `/` on every OS (`replaceCharacter ('\\', '/')` on a relative path), so a test that passes on macOS doesn't fail on Windows CI.
- **Fixtures near Nyquist:** a fixture that shapes a test signal (a band-limit, a reference filter) is exact over the whole range it is used in. plugkit's matched filter designs are inexact near Nyquist (eq1's docs/dsp/filter-design.md, "Test tolerances"), so a test band-limits with them only where they are accurate, or at a sample rate where they are (96 kHz for a 20 kHz High Cut).
- **Editor behaviour:** the editor and its keys are testable through the editor harness, which opens the editor in a window of its own; a test of the editor goes through it rather than being skipped. A check that depends on the mouse pointer sends the pointer again after the editor settles: JUCE's Desktop timer can move it to the machine's real mouse meanwhile. A wait on the pointer's rest (a hover card's) goes through the harness's helper that keeps sending it; one move and a fixed settle passes on macOS and fails on Windows CI. A check on where an animation ends (a pop-in, a fade, a panel's slide) waits for the animations to settle, or settles until the state it needs holds: a fixed settle can leave a tween part-way on a loaded machine, whose stalled timer thread fires a repeating timer once for all the frames it missed. A control's own hover and press are the exception: a test window isn't in front, so the pointer's events never reach it and `isMouseOver` reads the real mouse. Such a test calls the control's mouse handlers (or `paintButton (g, highlighted, down)`) directly and counts the repaints asked for.

## Accessibility is behaviour

What a control reports to a screen reader (its handler, role, title, value) is behaviour, even in a refactor: a change to it is named in the PR and pinned by a test. Moving a set of controls to one handler once gave action buttons a value equal to their title, read twice.

## Comments and docs state current behaviour

A comment or doc says what the code does now and why. History (what changed, what the limits used to be, which ticket changed them) belongs in commit messages. A pointer to an open ticket for planned work stays, as in `Bell ignores Slope until Bell Slope (eq1#19)`.

## Shared behaviour lives once

A control behaviour (drag and Shift-drag, double-click reset, hover and press light, enabled dimming, tweens, overlays, accessibility handlers) or a test helper (snapshots, synthetic mouse events, layouts) that a second component needs moves into the UI kit or the editor harness, and both call it. A DSP-test helper (every Shape or Stereo Placement, the sample rates checked, a full scene, seeded noise, a measurement, a model of a Band) that a second test needs lives in the tests' shared header, `Measure.h` or one beside it, which the CPU budget includes too; a test that wants a subset filters the shared list, or names its own and says why. A second copy written beside the first drifts: two of five copied drag handlers snapped back after a double-click.
