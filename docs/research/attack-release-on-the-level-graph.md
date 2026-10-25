# Attack and Release on the Level and Gain Reduction Graph

Research for issue #215 (Attack and Release grabbers), gathered 2026-10-25. The question has two parts. First, how do compressors let users set Attack and Release directly on a display? Second, what would suit comp1's Level and Gain Reduction Graph? The issue's decisions are taken as given: the grabbers, if any, go on the Level and Gain Reduction Graph, not on the Transfer Curve inset. They are for the compressor's Attack and Release only, and a throwaway prototype comes before any slicing. The candidate designs below are input for that prototype, not decisions.

## Scope and method

**Sources.** Manufacturer manuals and online help only. Every claim cites its source by URL:

- the compressor named in the issue: FabFilter Pro-C 3's online help, plus the Pro-C 2 manual for contrast;
- compressors that draw time on a display: DMG Audio Compassion, DDMF Comprezzore, Bitwig Compressor+, iZotope Neutron 3's Compressor, Tokyo Dawn Labs Kotelnikov and Ableton Live's Compressor;
- one non-compressor, for the drag convention only: Steinberg's EnvelopeShaper.

Where a design goes beyond what a manual says, it is marked **inference**.

**Not found.** None of the compressor manuals checked documents dragging Attack or Release on a display. This is the main finding, and it shapes the rest of the document: no product could be copied, so the candidates combine documented parts. Logic Pro's sampler envelope pages could not be read past their navigation, so Logic is not cited. Waves, Cytomic, Sonnox, Pro Tools' Dyn3 and Ableton's Glue Compressor were not checked.

## comp1 today

These facts from the code and docs constrain every candidate below:

- **The graph** (`plugin/LevelGraph.h`) scrolls. It shows the last `spanSeconds` (5 s), with the newest audio at the right edge. Each pixel column draws the most Gain Reduction of the records it covers. The Gain Reduction hangs from the top on the dB scale (0 to 60 dB). The graph takes no mouse input, is not a Tab stop, and a screen reader reads it as a read-only image.
- **Its time scale is coarse for Attack.** Each column covers `columnSeconds()`, which is 5 s over the width: about 3.7 ms per pixel on a graph about 1,350 px wide. That width is an estimate for the default 1400 px window (`PluginProcessor::expandedWindow`). At that scale, the default Attack of 10 ms is about 3 px wide, and Attack's whole range of 0.005 to 250 ms spans at most about 68 px. Release, 5 to 5000 ms, spans 1 px up to the whole graph. Both ranges come from `engine/include/comp1/Settings.h` and ADR 0008.
- **The Transfer Curve inset** (`plugin/TransferCurveInset.h`) sits `insetGap` (8 px) in from the graph's top-left corner, over the oldest audio. It is `side` (160 px) square, so it covers roughly the oldest 0.6 s of the graph. Only its Threshold handle and Ratio handle take a press, within `handleHitRadius` (10 px); a press anywhere else in the inset passes through to the graph below. The inset is a Tab stop, and its arrow keys step Threshold (left and right) and Ratio (up and down). A drag shows a readout card ("Threshold −18.00 dB") and counts as one undo step.
- **The Detection Level dot** is drawn on the inset's curve (`TransferCurveGraph`), not on the graph. It rises at once and falls at 20 dB/s.
- **Each level record** (`LevelRecord`, `engine/include/comp1/Engine.h`) holds the run's Input Level, Output Level, Gain Reduction and Detection Level, so the graph's history already contains what the detector heard. The Detection Level is a peak per 16-sample run, not per sample.
- **Auto Release** follows the formula Release × (1 + (M − 1) × sustain), with M = 5 (`AUTO_RELEASE_MULTIPLIER`). Release therefore always scales it, and the effective release runs from Release up to 5 × Release ([dyn-lab USAGE.md, "Auto Release"](../../tools/dyn-lab/USAGE.md#auto-release)).
- **Opto** has two timing behaviours ([dyn-lab USAGE.md, "Opto"](../../tools/dyn-lab/USAGE.md#opto), ADR 0002):
  - Its attack is Attack scaled by the overshoot, from 2× (slower) to 0.5× (faster).
  - Its release has two parts. The fast part is 60% of the Gain Reduction and releases at Release, or at Auto Release's pole when Auto Release is on. The slow part is 40% and releases at a time of Opto's own, from 500 to 5000 ms depending on how long Opto has compressed. Release does not move the slow part.
  - Every control works on every Character (ADR 0002), so no handle may be disabled on Opto.
- **FET's loop speeds Attack up.** At 4:1, an Attack of 0.5 ms reaches 63% in 0.15 ms (dyn-lab USAGE.md, the FET Style). The slope drawn on the trace is therefore not the slope Attack names.
- **Comp does not set timing.** Comp sets only Threshold and Ratio (glossary, "Comp"), so a timing grab never has to turn Comp Follow off (ADR 0004).
- **Timing moves Auto Gain.** Auto Gain's estimate reads Attack, Release and Auto Release through the Timing Offset (ADR 0010), so the Output readout moves during a timing drag.

## What other compressors do

### FabFilter Pro-C 3

- **What's grabbed: nothing for timing.** Attack is "the Attack knob", 0.005 to 250 ms, and "The Release knob sets the time that the compressor takes to recover from gain reduction". Hold is a slider. The time controls page never mentions the display ([Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols)).
- **What's drawn.** The level display "visualizes the input and output level, together with the applied gain change", and "The gain reduction itself is shown as a red line". The knee display shows "the Threshold, Ratio, Knee and Range settings", and its curve turns green to indicate the current input level. Neither display is described as taking a drag, and no attack or release visualisation is described ([Displays and metering](https://www.fabfilter.com/help/pro-c/using/displays)). Version 3's feature list adds nothing about the displays ([Introduction](https://www.fabfilter.com/help/pro-c/intro/fabfilterproc3)).
- **Auto and program-dependent release.** With Auto Release on, "the compressor adjusts the release time depending on the current amount of gain reduction". The styles "all have their own kind of program-dependency" ([Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols)). The help doesn't say how either is shown.
- **Pro-C 2** is the same: its manual describes the knee display as "visualizing" the settings, and the level display as drawing the gain reduction as a red line. The only display drags it documents are in the side chain EQ ([Pro-C 2 manual](https://www.fabfilter.com/downloads/pdf/help/ffproc2-manual.pdf), printed pp. 10 and 13 to 14, and its side chain section).

So the product comp1 is modeled on doesn't do this. Its Threshold and Ratio stay on knobs too; comp1's Transfer Curve inset handles already go beyond Pro-C.

### DMG Audio Compassion: a draggable history graph, coloured by timing stage

All of the following is from the [Compassion manual](https://dmgaudio.com/dl/DMGAudio_Compassion_Manual.pdf), §2.2.1 to §2.2.3, pp. 9 to 11.

- **What's grabbed.** The main graph is a scrolling waveform of the input between white "rails", with the output superposed in a darker colour. The gain-reduction signals run along its top and bottom. "Grab the curve and drag, to push transients through the rails, and the Threshold will be adjusted." "Grab the GR curves and drag them to increase or reduce the amount of GR, and the Ratio will be adjusted." So the history graph does take drags, but they are level drags, not time drags.
- **What's drawn for time.** "Green shows attack, Red shows release. As you increase the attack, you'll see more Green appear." A separate static Response graph "shows a rough sketch of Compassion's currently configured transient response. It allows you to see the shape of your attack and release curves." The knee graph can be dragged ("You can drag the graph to adjust the Knee values"); the manual says no such thing about the Response graph.
- **Scrolling.** "The Graph stops scrolling as you mouseover, to allow you to pause the signal and inspect it."
- **Auto release.** It "varies the release time between a Short and Long setting". "Clicking the AutoRelease button disables the Release controls."

### DDMF Comprezzore: a Transfer Curve over a 5-second history, with time-constant graphs

All of the following is from the [Comprezzore manual](https://ddmf.eu/pdfmanuals/ComprezzoreManual.pdf), pp. 2 to 4 and 6. This is the closest layout to comp1's.

- **The layout.** The transfer curve's nodes are drawn over a history of the input level and the gain reduction: "the input level of your audio material is shown in a moving image with a history of 5 seconds", and "the five-second history of the applied level reduction is also shown". The threshold node is dragged "diagonally" and the end node "vertically". The history is there so you "can directly see the influence of any parameter change, not only of the node positions but also of the attack and release timings".
- **Timing.** Attack and release each have three knobs, which set a start time constant, a target time constant and a transition time scale. "The display below the attack sliders will show a curve depicting the exact evolution of your attack time." The curves are set by the knobs; the manual describes no dragging on them. Hold's effect shows "immediately ... in the orange gain reduction signal".

### Bitwig Compressor+, Neutron 3, Kotelnikov and Ableton: time is shown, not grabbed

- **Bitwig Compressor+** has "an interactive blue line for adjusting Threshold". It states the principle outright: "Timing changes are only visualized in the gain reduction (and in the audio)." Its Auto Timing is "the amount that timing parameters (Attack, Release, and more) are automatically adjusted" ([Bitwig user guide, Dynamics](https://www.bitwig.com/userguide/latest/dynamic)).
- **iZotope Neutron 3's Compressor** says to "Use the Gain Reduction Trace to monitor and to set the response times (attack and release)". The trace is a guide for the knobs: the only display drag described is the Threshold slider handle. Auto Release "is always enabled when Vintage Mode is selected" ([Neutron 3 Compressor](https://s3.amazonaws.com/izotopedownloads/docs/neutron300/en/compressor/index.html)).
- **TDR Kotelnikov** splits its release into Release Peak, which sets how quickly gain recovers "after short overloads", and Release RMS, which sets how fast it recovers "after sustained gain reduction". It shows which one is acting with indicators: "Two virtual LEDs indicate the currently active release path." Both are knobs ([Kotelnikov manual](https://docs.tokyodawn.net/kotelnikov-manual/)).
- **Ableton Live's Compressor** has an Activity view that draws the input and the gain reduction. The manual describes no dragging in it. With Auto Release on, "the release time will adjust automatically based on the incoming audio" ([Live audio effect reference, Compressor](https://www.ableton.com/en/manual/live-audio-effect-reference/)).

### The drag convention, from outside compressors

Steinberg's EnvelopeShaper, a transient shaper, lets you "use the controls or drag the breakpoints on the graphical display". Its Length parameter (5 to 200 ms) "Sets the length of the attack phase" ([EnvelopeShaper](https://www.steinberg.help/r/cubase-pro/cubaseplugref/14.0/en/_shared/topics/plug_ref/envelope_shaper_r.html)). The page doesn't give drag axes. **Inference:** a time breakpoint dragged horizontally is the usual synth-envelope convention, and the one users will expect.

### What recurs

1. **No compressor checked lets you drag Attack or Release on a display.** Level parameters are dragged (DMG's Threshold and Ratio on the history graph, Comprezzore's and Bitwig's curves); time is set with knobs.
2. **Time is shown in one of two ways.** The live trace shows it, coloured by stage in DMG's case. Or a static sketch shows the configured response: DMG's Response graph and Comprezzore's time-constant curves.
3. **Pausing the history to inspect it** is documented once (DMG, on mouseover).
4. **Auto release:** DMG disables Release under it. Pro-C keeps Release as the scale of auto release ([Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols)), and so does comp1. Kotelnikov shows which release path is active. No source shows how a time handle should behave under auto or program-dependent release.

comp1 would be doing something the sources don't document. That is a reason the prototype matters, not a reason against the feature.

## Candidate designs for comp1

All three use the existing handle look (`handles::paint`), a readout card while dragging ("Attack 10.0 ms", "Release 100 ms"), Shift for fine drags, and one undo step per drag, inside a host gesture, as the inset's handles do. All three drag horizontally: further right means longer. **Inference** throughout.

### A. Freeze and grab the trace

Hovering the graph freezes its drawing, as DMG's graph does; the audio and the records keep running underneath. The most recent compression event in view gets two handles:

- an **Attack handle** on its rise, where the Gain Reduction reaches 63% of its peak;
- a **Release handle** on its recovery, where 63% has been given back.

Dragging one replays the frozen Detection Level history through the Character's timing at the new value, and draws the result as a ghost Gain Reduction trace over the real one. On release of the mouse, the graph resumes scrolling.

- **Auto Release on.** The replay includes Auto Release's sustain state, so the ghost shows the release the material would actually have had. The Release handle stays live, since Release scales Auto Release.
- **Opto.** The replay runs Opto's model, so the ghost shows the overshoot-scaled attack and the two-stage release. The Release handle sits on the fast part's recovery. The slow tail is drawn but doesn't move with the handle, because Release doesn't move it.
- **Clashes.** Events under the inset (the oldest 0.6 s) can't take handles, so the newest event in view is used, and that is never under the inset. The Detection Level dot is on the inset, so it doesn't overlap the handles. It still moves while the graph is frozen, which may read as inconsistent. The handles need a keyboard route: either a new Tab stop on the graph, or none, with the knobs as the keyboard route. They must not reuse the inset's arrow keys.
- **For.** This matches the issue's decision literally: the handles act on the trace's attack slope and release recovery, and the user sees what a change would have done to the audio they just heard.
- **Against:**
  - At about 3.7 ms per pixel, a 10 ms attack slope is about 3 px wide, so the Attack handle is hard to place and moves only a few pixels across most of its range. It needs drag sensitivity from the knobs' travel, not the graph's time scale.
  - With no compression in view there is nothing to grab.
  - A replay from 16-sample peak records only approximates the Engine. On FET and Opto it also needs the feedback loop, so the ghost and what is heard could disagree.
  - Freezing a graph users watch while playing may annoy them.
  - This is the costliest candidate to build.

### B. A Timing Response inset on the graph

A second small inset sits beside the Transfer Curve inset, also over the oldest audio, on the same slab style. It draws the compressor's Gain Reduction for a fixed test burst, computed from the Engine's timing model for the current Character: the rise at Attack, Hold, then the recovery at Release. The time axis is logarithmic, because Attack and Release together span 0.005 ms to 5 s. It has two handles:

- an **Attack handle** at the rise's 63% point;
- a **Release handle** at the recovery's 63% point.

Both drag horizontally along the log axis. This is DMG's Response graph and Comprezzore's time-constant curves, made draggable.

- **Auto Release on.** The recovery draws as a band from Release to 5 × Release (M), with the handle at Release. Alternatively, it could draw the curve for one sustain level, such as after a second of compression, as dyn-lab's `step --style` figures do.
- **Opto.** The rise draws as a band from 2× to 0.5× Attack, the overshoot's range, with the handle at Attack's own time. The release draws both parts; the slow part is a shaded tail from 500 to 5000 ms that the handle doesn't move.
- **Clashes.** Two insets in the top-left corner cover about 1.2 s of the oldest audio at the default width. Stacking the new one under the Transfer Curve inset covers less time but more of the Gain Reduction's dB depth. It would need its own Tab stop: left and right for Attack, and up and down for Release by analogy with the inset, or a choice of which handle is active. The Detection Level dot stays on the other inset and doesn't interact.
- **For:**
  - The target is stable: it doesn't scroll and is always grabbable, even in silence.
  - The log axis gives Attack a usable drag span.
  - It shows Opto's and Auto Release's behaviour honestly as ranges.
  - It is cheap: an analytic or one-shot model, with no history replay.
- **Against:**
  - It doesn't act on the trace itself, so it bends the issue's decision ("acting on the Gain Reduction trace's attack slope and release recovery"). The graph is only where it sits.
  - It also adds a second inset to a corner that already holds one.

### C. Rulers on the graph's own time scale

The graph keeps scrolling. A **Release ruler** hangs from its top edge at the right, a bracket reaching left from the newest audio, whose length is Release on the graph's own seconds per pixel. Its left end is the handle and drags horizontally. The user lines the ruler up against the recovery slopes scrolling past it, which share the same scale.

Attack is too short to rule at this scale. Its handle could sit at the ruler's right end, with a drag sensitivity from the knobs' travel; or it could be left to the knob, a partial answer.

- **Auto Release on.** The ruler draws its span from Release to 5 × Release, with the handle at Release.
- **Opto.** A second, unhandled bracket marks the slow part's current time, which comes from Opto's history (500 to 5000 ms), next to the Release ruler. This is in the spirit of Kotelnikov's active-path LEDs.
- **Clashes.**
  - The ruler covers the newest audio at the right edge and the top of the Gain Reduction there, which is exactly where users look.
  - At a long Release it reaches into the inset's corner (5000 ms is the whole graph), so it should be clamped short of the inset.
  - The Detection Level dot is unaffected.
  - The keys could go on a new Tab stop.
- **For.** It is the simplest candidate. It needs no simulation. It is exact on the graph's scale, so Release can be compared with the trace directly.
- **Against.**
  - Attack has no good home.
  - A bracket is a less familiar affordance than a handle on a curve.
  - It doesn't act on the trace either.

### Trade-offs at a glance

| | A. Freeze and grab | B. Timing Response inset | C. Rulers |
|---|---|---|---|
| Acts on the trace (the issue's decision) | yes | no, beside it | beside it, on its scale |
| Attack usable | poorly, about 3 px | yes, log axis | no |
| Works in silence | no | yes | yes |
| Auto Release | exact, by replay | drawn as a range | drawn as a range |
| Opto | exact, if the replay matches | bands for attack and slow tail | slow part as a second bracket |
| Covers | nothing new (it freezes) | about 0.6 s more of the oldest audio | the newest audio, top right |
| Cost | high (replay model, freezing) | medium (one-shot model, inset) | low |

**Inference:** B is the strongest candidate for Attack and for showing Opto and Auto Release honestly. A is the only one that fulfils the decision as written. Prototyping A's Release handle together with B's Attack handle, as a hybrid, is a reasonable fourth option if both appeal.

## What the throwaway prototype should test

1. **Can Attack be grabbed at all on the 5 s graph?** Place A's Attack handle on real compression events at 1, 10 and 50 ms. If users can't hit or read it, A needs B's log-axis inset for Attack.
2. **Does freezing on hover help or annoy** while music plays (A)? Compare it with handles that move with the scroll and a press that freezes.
3. **Does a range read as "Release still works"** with Auto Release on (all three)? Compare a band from Release to 5 × Release against a single curve at one sustain level.
4. **Does Opto read correctly?** Check that a movable fast release beside an immovable slow tail doesn't look broken, and that the attack band reads as "this varies", not as two values.
5. **Does the ghost trace match the sound** (A)? Compare the replayed Gain Reduction with the Engine's on FET and Opto, from 16-sample peak records.
6. **Corner crowding** (B): two insets in one corner, against one inset with a toggle between the Transfer Curve and the Timing Response.
7. **Keys and focus:** whether the timing handles need their own Tab stop, and which arrow keys move which, without changing the Transfer Curve inset's keys.
8. **Auto Gain:** whether the Output readout moving during a timing drag distracts, since the Timing Offset changes with every step.

The prototype can run on recorded `LevelRecord` streams and dyn-lab's models, with no Engine or plugin changes. Gate Attack and Gate Release stay out of scope, as the issue decided.

## Sources

- Ableton, Live manual, Live audio effect reference, Compressor: https://www.ableton.com/en/manual/live-audio-effect-reference/
- Bitwig, user guide, Dynamics (Compressor+): https://www.bitwig.com/userguide/latest/dynamic
- DDMF, Comprezzore manual: https://ddmf.eu/pdfmanuals/ComprezzoreManual.pdf
- DMG Audio, Compassion manual: https://dmgaudio.com/dl/DMGAudio_Compassion_Manual.pdf
- FabFilter, Pro-C 3 online help, Displays and metering: https://www.fabfilter.com/help/pro-c/using/displays
- FabFilter, Pro-C 3 online help, Time controls: https://www.fabfilter.com/help/pro-c/using/timecontrols
- FabFilter, Pro-C 3 online help, Introduction: https://www.fabfilter.com/help/pro-c/intro/fabfilterproc3
- FabFilter, Pro-C 2 manual: https://www.fabfilter.com/downloads/pdf/help/ffproc2-manual.pdf
- iZotope, Neutron 3 Help, Compressor: https://s3.amazonaws.com/izotopedownloads/docs/neutron300/en/compressor/index.html
- Steinberg, Cubase plug-in reference, EnvelopeShaper: https://www.steinberg.help/r/cubase-pro/cubaseplugref/14.0/en/_shared/topics/plug_ref/envelope_shaper_r.html
- Tokyo Dawn Labs, Kotelnikov manual: https://docs.tokyodawn.net/kotelnikov-manual/
