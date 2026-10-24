# Performance

## CPU budget

`tests/cpu/CpuBudget.cpp` (`scripts/check.sh cpu`, its own CI step after the tests) measures the Engine's load in each CPU scene, stereo, at 48 and 96 kHz, in percent of real time: the median of five 4-second runs, each in a fresh Engine, timed by the thread's CPU time where the system gives it per thread (wall-clock time on Windows), so time spent waiting for a core isn't counted. The input is the shared noise bursts (`tests/engine/Measure.h`, `bursts`): seeded full-scale noise, loud for 100 ms of every 200 and 34 dB down for the rest, so the compressor, the Gate and the Limiter keep moving rather than settling. The scenes are a list of `Scene`s in `CpuBudget.cpp`, one per line, built from the Engine's public interface and the shared helpers in `Measure.h` (the worst case's settings among them), because the CPU A/B copies only `tests/cpu/` and `Measure.h` into the tree it compares with. Each prints its load, so a change shows what it did to each scene.

The scenes, and why each exists:

- **Defaults**, with a ceiling: what most instances of comp1 cost, Clean in Zero Latency.
- **Defaults, Threshold automated**, with a ceiling: settings published before every block, as the Plugin Shell publishes them while Host Automation moves a parameter, so the cost of taking new settings is measured with the block.
- **Worst case**, with a ceiling: every stage on, at its most costly (`worstCaseSettings`): FET at Colour 100%, High Quality at 8x Oversampling (the cap at each rate: 4x at 96 kHz) with Lookahead 5 ms, Saturation on, the Gate on and closing on the quiet bursts, Dry and Wet both at 0 dB, and the Limiter on and limiting, Output Gain +12 dB driving the loud bursts over its Ceiling. This is the scene that prices the Limiter's 8x, 48-tap true-peak estimator. `tests/engine/RenderScenesTest.cpp` checks that its Limiter, compressor and Gate all reduce the gain on the bursts.
- **Worst case, Limiter off**: the same settings with the Limiter off, so the gap to the Worst case is what the Limiter costs on, and the scene shows that off and settled it costs about nothing.
- **Clean, VCA, FET and Opto, each at Colour 100%**, Zero Latency: what each Character's detector and Colour cost over Clean.
- **VCA's, FET's and Opto's Style defaults**, Zero Latency: each Character at the defaults choosing its Style writes (`comp1/Style.h`), so FET's fast Attack and Opto's wide Knee are priced as a Style plays them.
- **Vocal's, Instrument's, Bus's and Drums' Style defaults**, Zero Latency: each Source Style as choosing it plays, its Character at its defaults, Vocal's with the Gate and the Limiter on and Bus's and Vocal's with Auto Release. The CPU A/B builds these scenes with `styled()` from the ref's own `comp1/Style.h`, so against a ref whose Source Style defaults differ their ratios compare different settings, not the same settings through two Engines.
- **Comp Follow on, Comp automated**: Comp swept by Host Automation while following, so Threshold and Ratio come from the Comp Curve and Auto Gain's estimate is worked out again every block.
- **Comp Follow gliding**: Comp Follow switched by Host Automation every 300 ms, the glide time, between stored settings far from the curve and Clean's curve at 100%, so its glide is always moving. The CPU A/B can't build these two scenes against a ref whose `Settings` has no Comp Follow, by design.
- **Gate at Post**: the defaults with the Gate on at Gate Position Post, Zero Latency, at the worst case's Gate Threshold, so it opens on every compressed loud burst and closes on every quiet one, by about 10 dB before the next loud burst opens it: what the Gate costs on the Wet path, after the compressor, over Defaults.
- **High Quality, 2x Oversampling**: the default High Quality, its Oversampling sections and delays.
- **Defaults, 32-sample blocks**: the per-block overhead, at the smallest block size hosts commonly use.
- **Worst case on silence** (timed after 10 s of nothing, when every filter and envelope has rung down) and **on subnormal input**: these fail the run when they cost more than 1.5 times the Worst case scene, the same settings on the bursts. They are held to their own settings, not to Defaults, because the Engine runs every stage it has whatever the input (the Limiter and the Oversampling sections included), so silence through the worst case still costs about 0.6 times the worst case on the bursts, several times Defaults. That catches arithmetic on subnormal numbers, many times slower than normal on x64; on arm64 it runs at full speed, so the Windows x64 run is the one that would catch it.

| CPU scene | Ceiling 48 / 96 kHz | Apple M3 48 kHz | 96 kHz | CI Windows x64 48 kHz | 96 kHz | CI macOS 48 kHz | 96 kHz |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Defaults | 4% / 7% | 0.40% | 0.81% | 1.18% | 2.21% | 0.58% | 1.33% |
| Defaults, Threshold automated | 4% / 7% | 0.40% | 0.85% | 1.14% | 2.25% | 0.76% | 1.35% |
| Worst case | 17% / 28% | 2.81% | 4.57% | 5.51% | 9.30% | 4.06% | 8.32% |
| Worst case, Limiter off | — | 1.94% | 2.84% | 5.00% | 7.33% | 2.98% | 4.43% |
| Clean, Colour 100% | — | 0.40% | 0.81% | 1.11% | 2.21% | 0.57% | 1.38% |
| VCA, Colour 100% | — | 0.53% | 1.07% | 1.75% | 3.49% | 1.11% | 2.48% |
| FET, Colour 100% | — | 0.72% | 1.45% | 2.07% | 4.12% | 1.40% | 3.17% |
| Opto, Colour 100% | — | 0.86% | 1.73% | 2.05% | 4.11% | 1.92% | 3.82% |
| VCA's Style defaults | — | 0.53% | 1.07% | 1.28% | 2.54% | 1.16% | 2.35% |
| FET's Style defaults | — | 0.75% | 1.51% | 1.54% | 3.08% | 1.73% | 3.58% |
| Opto's Style defaults | — | 0.82% | 1.64% | 1.80% | 3.61% | 2.13% | 4.66% |
| Vocal's Style defaults | — | 0.45% | 0.90% | — | — | — | — |
| Instrument's Style defaults | — | 0.80% | 1.62% | — | — | — | — |
| Bus's Style defaults | — | 0.54% | 1.07% | — | — | — | — |
| Drums' Style defaults | — | 0.74% | 1.49% | — | — | — | — |
| Comp Follow on, Comp automated | — | 0.43% | 0.87% | — | — | — | — |
| Comp Follow gliding | — | 0.44% | 0.88% | — | — | — | — |
| Gate at Post | — | 0.45% | 0.90% | — | — | — | — |
| High Quality, 2x Oversampling | — | 0.43% | 0.86% | 1.61% | 3.21% | 1.16% | 2.80% |
| Defaults, 32-sample blocks | — | 0.40% | 0.82% | 1.11% | 2.22% | 0.71% | 1.73% |
| Worst case on silence | — | 1.62% | 2.41% | 3.98% | 6.47% | 2.55% | 5.23% |
| Worst case on subnormal input | — | 1.38% | 2.09% | 3.76% | 5.97% | 2.38% | 4.42% |

The Apple M3 columns were measured 2026-10-21 on an 8-core Apple M3 with other agents' work light (load average about 2), after this tree's own build had finished. The CI columns are GitHub's `windows-latest` and `macos-latest` (arm64) runners, from the first CI run of the PR that added the CPU budget, 2026-10-21, read from the job summary where `scripts/check.sh cpu` writes the scene lines. Windows x64 was the slower runner on every scene, so the ceilings are about three times its figures, rounded up to a whole percent: Defaults' 1.18% and 2.21% give 4% and 7%, which the Threshold automated scene shares (1.14% and 2.25%), and the Worst case's 5.51% and 9.30% give 17% and 28%. The Worst case, Limiter off row's CI figures come from the first CI run of the PR that made the Limiter rest while off, 2026-10-21, where the Worst case read 6.43% and 10.17% on Windows x64 and 4.38% and 5.91% on macOS; the other rows' CI figures were taken before it, so the scenes with the Limiter off (all but the worst cases) cost a little less on CI now than they show. The Opto row was measured the same way in its own PR, before the Limiter rested, 2026-10-21: on the Apple M3 at load average 1.9, in a run where FET read 0.78% and 1.57%, and on CI from that PR's first run, where FET read 1.79% and 3.59% on Windows x64 and 1.80% and 3.38% on macOS. Opto costs a little more than FET: the same loop, solved over two parts, with its attack's pole worked out each sample. The three Style defaults rows were measured in their own PR, 2026-10-21, on the Apple M3 at load average 2.3, in a run where VCA, FET and Opto at Colour 100% read 0.53%, 0.74% and 0.81% at 48 kHz, and on CI from that PR's first run, where they read 1.29%, 1.56% and 1.78% on Windows x64 and 1.12%, 1.53% and 1.93% on macOS: each Style costs what its Character does at the defaults, its settings moving none of the work. The two Comp Follow rows were measured in their own PR, 2026-10-21, on the Apple M3 at load average 2.1, in a run where Defaults read 0.43% and 0.83%: following the Comp Curve, and its glide, cost about what Clean does at the defaults, since the curve and the glide are worked out once a run, not once a sample. Their CI columns wait for that PR's CI figures. The four Source Style defaults rows were measured in their own PR, 2026-10-21, on the Apple M3 at load average 2.9, in a run where Clean, VCA, FET and Opto at Colour 100% read 0.41%, 0.54%, 0.75% and 0.81% at 48 kHz: each costs what its Character does at the defaults, and Vocal's Gate, Limiter and Auto Release add little over Clean's (0.45% against Defaults' 0.44%). Their CI columns wait for that PR's CI figures too. The Gate at Post row's Apple M3 figures were measured 2026-10-24 at load average 2.9, in a run where Defaults read 0.44% and 0.87%: the Gate at Post, opening and closing with the bursts, costs about what Defaults does, a few percent more. Its CI columns are still to be measured.

The Limiter's share of the worst case: off and settled, its gain computer rests (High Quality's only delays the audio and feeds the estimator), so the Worst case, Limiter off scene prices the rest of the Engine. On the Apple M3 the Worst case reads 2.81% and 4.57% and the Worst case, Limiter off 1.94% and 2.84%, so High Quality's Limiter, limiting, is about a third of the worst case, and off it costs about nothing.

**Ceilings.** Each ceiling is about three times the slowest CI runner's worst run, absolute, rounded up: high enough that runner noise doesn't fail the check, low enough to fail on a regression that multiplies the cost. They are absolute, not relative to a baseline, so a slow drift over many changes still trips them. The ceilings live in `CpuBudget.cpp` and in the table above, set from a PR's first CI run and a quiet-machine run, with their dates.

**Later tickets.** Each DSP ticket adds its CPU scene (one line in `CpuBudget.cpp`) and its render scene (one line in `tests/render/RenderScenes.h`, with what `RenderScenesTest.cpp` checks), and updates this table with its figures.

## CPU A/B

`scripts/check.sh cpu <ref>` says what a change did to each CPU scene's cost against any git ref (`origin/main` for a PR), on a machine busy with other builds too. Absolute loads on a busy Mac or a CI runner vary from run to run; the ratio of two builds timed side by side doesn't, because load hits both alike. It builds `<ref>`'s Engine in a temporary worktree under `build/cpu-ab`, with this tree's CPU scenes copied over (`tests/cpu/` and `tests/engine/Measure.h`), so both sides run the same scenes and only the Engine differs. A scene that needs a `Settings` field `<ref>`'s Engine lacks fails that build, by design: it can't be compared. Then, for 7 rounds, it runs every scene at 48 kHz through one build and then the other for about a second each (`comp1_cpu_budget run`), swapping which goes first every round, each run a fresh Engine timed by its thread's CPU time.

It prints the load average before and after, then per scene the median after/before ratio (above 1 is slower), the lowest and highest round, how many rounds went up and down, and a verdict: **slower** or **faster** when the median is at least 3% from 1.0 and at least 6 of the 7 rounds went that way, **no clear change** otherwise. It is a report, not a gate: it exits 0 whatever the verdicts, and fails only when a build fails. Each run's loads stay in `build/cpu-ab/loads.tsv`.

This tree against itself, 2026-10-21 on the Apple M3 (load average about 3), read "no clear change" on every scene, medians 0.995 to 1.006.

## Null check

`scripts/check.sh null <ref>` shows what a change did to the Engine's output, sample for sample, against any git ref (`origin/main` for a PR). It builds `<ref>`'s render tool (`tests/render/RenderTool.cpp`, built with the tests and linked against the Engine only) in a temporary worktree under `build/null`, with the same compiler and flags as this tree, renders the render scenes through both Engines, and prints each scene's worst sample deviation in dBFS at each sample rate, or `exact` when the two renders are bit-identical. It fails above −120 dBFS, far below anything audible, when a scene's two renders differ in length, or when `<ref>` has a scene this tree hasn't; a scene only this tree has is `new` and passes. Both builds run on the machine the check runs on, so it compares within one platform. The renders stay in `build/null/current` and `build/null/ref`.

The render scenes (`tests/render/RenderScenes.h`) are data the tool and its test share, so a scene added there is rendered and compared on both sides. Each is stereo, one second of the shared noise bursts (full scale, 100 ms loud and 100 ms at −34 dB, seeded, so every run renders the same input) through 97-sample blocks, not a multiple of the Engine's 16-sample run, at 44.1, 96 and 192 kHz: the lowest rate, the one where 8x Oversampling is capped to 4x, and the highest. They play every host parameter off its default at least once, where it is heard: the compressor's settings (Ratio ∞ with Knee 0, Knee 72, Range 6, fast Attack, Release and Hold, Auto Release, Auto Gain, Output Gain), Threshold gliding from −18 to −40 dB and Auto Gain switched on a quarter of a second in; VCA and FET at full Colour and at 40%, Opto at full Colour and at 40% with Auto Release, and VCA's Colour tracking Threshold, at −50 dB and gliding there from the default; every Style's defaults but Clean's (a default Settings'), from the Style table, a Style change from Clean's to FET's a quarter of a second in, and one from Clean's to Vocal's, which switches on the Gate, Auto Release and the Limiter mid-signal; each Style following its Comp Curve at a Comp off its default, Comp Follow switched on and off a quarter of a second in with the stored Threshold and Ratio far from the curve and Auto Gain on, and Comp moving while following; Saturation after the compressor and before it at more Drive; Dry and Wet together; the Gate at its defaults and with every setting moved, and at Gate Position Post in Zero Latency with Dry in, in High Quality with Lookahead, moved there from Pre a quarter of a second in, and in High Quality on the Sidechain; the Limiter driven over its Ceiling in both Processing Modes and at a lower Ceiling; High Quality, its Lookahead, and Oversampling at 1x and 8x with FET's Colour and Saturation, the stages it runs; Stereo Link at 50%, Mid and Side; the Sidechain as Detection Source; each Detection EQ band on, moved from its defaults; Global Bypass switched on; and the CPU budget's worst case. `tests/engine/RenderScenesTest.cpp` keeps the list covering all of this, by host parameter, and every render repeatable.

Against itself (`HEAD`), 2026-10-21, every scene read `exact`. A change to one scene's setting one double ULP off (Knee 72, Range 6, Output Gain 6 or Stereo Link 50, each nudged by `std::nextafter`) still reads `exact`: it is under what a float sample can show. One float ULP off (Output Gain `std::nextafter (6.0f, 7.0f)`) reads −138.5 dBFS on that scene alone, at every rate, and passes the bar.

## Editor CPU

`tests/plugin/EditorCpuTest.cpp` times what the editor costs the message thread per tick with the Spectrum tab shown, against the Level tab. It is a benchmark, not a test: hidden (`[.editor-cpu][paint]`), so ctest never lists it and CI never runs it, and it prints its figures and fails on none of them. Build first, then run it on a quiet machine:

```
scripts/check.sh build
scripts/check.sh paint
```

`paint` builds the Plugin Shell tests and runs every `[paint]` test, this one among them; on a Mac busy with other work (load average over half its cores) it measures nothing and exits 3. `build/tests/comp1_plugin_tests "[.editor-cpu]"` runs it alone, whatever the load.

It opens the Expanded View through the editor harness on a stereo track at 48 kHz, Clean at Threshold −30 dB, playing seeded pink noise at −18 dB RMS, a steady signal every display has something to show of. Each tick's duration is fixed at 1/60 s (`fixTickSeconds`), so the Analyzer's smoothing and the meters' falls do the same work every run, whatever the machine's speed, and 1/60 s of audio (two 400-sample blocks) plays before each tick, as a host's audio thread feeds a 60 Hz tick. Per tick it times:

- **tick**: the thread's CPU time (`threadSeconds`, `tests/engine/Measure.h`) across the wait for the editor's next tick: every Client's tick (the Spectrum tab's FFTs among them) and the editor's other timers. The wait sleeps between timers, which the thread's CPU time doesn't count; on Windows, where it is wall-clock time, the sleeps are counted too, so its tick figures there mean nothing.
- **paint**: the display, its shown tab and all, painted whole at 2x (a Retina display's scale) into a native image, so by the renderer a window on the platform draws with (CoreGraphics on macOS), not JUCE's software one.

The two tabs take turns, five rounds of 120 timed ticks each after a second of untimed ones, so a change in the machine's load over the run falls on both. It prints each tab's mean and 95th percentile per tick for the tick, the paint and the two together, and the Spectrum tab's over the Level tab's.

| Per tick, Apple M3 | Level mean | Level p95 | Spectrum mean | Spectrum p95 |
| --- | --- | --- | --- | --- |
| tick | 0.11 ms | 0.18–0.22 ms | 0.10–0.11 ms | 0.16–0.17 ms |
| paint | 12.6–13.1 ms | 14.6–14.7 ms | 13.1–13.3 ms | 14.4–14.5 ms |
| tick + paint | 12.7–13.2 ms | 14.7–14.8 ms | 13.2–13.4 ms | 14.5–14.6 ms |

Measured 2026-10-24 on an 8-core Apple M3 (4 performance and 4 efficiency cores, macOS 27.0.1), two runs, each started at a 1-minute load average of 3.6 to 3.7 (other agents building) and ending at 2.5 to 2.8. The Spectrum tab costs 1.01 to 1.04 times the Level tab per tick, tick and paint together, by the mean, and 0.98 to 0.99 times by the 95th percentile. The tick is a small part of either, about 0.1 ms on both tabs, the Spectrum tab's FFTs among it. The paint is nearly all of it. These paint the whole display every tick, an upper bound: a window repaints only what a tick marked as changed.

**The Level tab's paint.** The Level and Gain Reduction Graph is nearly all of it (98% of the samples in a `sample` profile of this benchmark); the tab chips, the Transfer Curve inset, the grid, the dB labels and the edge fades together are a few percent. Of the graph, sampled 2026-10-24:

| Share of the Level tab's paint | Gradient fills | Now |
| --- | --- | --- |
| Input Level's area, its gradient fill | 43% | 22% |
| Output Level's trace | 22% | 29% |
| Input Level's trace | 17% | 31% |
| Gain Reduction's trace | 6% | 12% |
| Gain Reduction's area, its gradient fill | 5% | 1% |
| grid, edge fades, labels, the rest | 7% | 5% |

With a gradient fill, CoreGraphics works the gradient's colour out afresh for every pixel a path covers, and the Input Level's area under its trace covers most of the graph. Both areas now fill from a gradient drawn once (`CachedGradientFill`, `plugin/CachedGradientFill.h`), drawn again only when its colours, the graph's height or the device scale change. Measured the same way at this tree's parent, at load averages 3.8 and 3.1, the Level tab's paint was 17.6–17.7 ms by the mean (19.2–19.3 ms p95) and the Spectrum tab's 13.3–13.4 ms, the Spectrum tab 0.75 to 0.76 times the Level tab: the Level tab's paint is about 5 ms, 28%, less. What is left is the three traces, about 70% of it: CoreGraphics' stroking of three lines a point per pixel column, which a cheaper stroke would draw differently.

The fills draw within an 8-bit step of the gradient fills: CoreGraphics dithers a gradient by where each pixel lies, so a tile's dither isn't the window's, and at a part-covered edge it rounds the colour at a different step. In the `[.screens]` renders (`scripts/check.sh screens`) the Level tab's snapshots differ from the gradient fills' in about 13% of their pixels, all but 0.2% of them by one or two steps; the rest are no more than two runs of the same tree differ by, in the meters and traces the playing signal moves from run to run.

**Budget**, confirmed by the user 2026-10-24, not yet a gate: the Spectrum tab's tick and paint together cost at most 1.25 times the Level tab's, by the mean, in this benchmark: room for run-to-run noise and small additions, while a regression that makes the Spectrum tab the dearer one by a quarter fails it.
