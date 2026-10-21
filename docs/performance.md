# Performance

## CPU budget

`tests/cpu/CpuBudget.cpp` (`scripts/check.sh cpu`, its own CI step after the tests) measures the Engine's load in each CPU scene, stereo, at 48 and 96 kHz, in percent of real time: the median of five 4-second runs, each in a fresh Engine, timed by the thread's CPU time where the system gives it per thread (wall-clock time on Windows), so time spent waiting for a core isn't counted. The input is the shared noise bursts (`tests/engine/Measure.h`, `bursts`): seeded full-scale noise, loud for 100 ms of every 200 and 34 dB down for the rest, so the compressor, the Gate and the Limiter keep moving rather than settling. The scenes are a list of `Scene`s in `CpuBudget.cpp`, one per line, built from the Engine's public interface and the shared helpers in `Measure.h` (the worst case's settings among them), because the CPU A/B copies only `tests/cpu/` and `Measure.h` into the tree it compares with. Each prints its load, so a change shows what it did to each scene.

The scenes, and why each exists:

- **Defaults**, with a ceiling: what most instances of comp1 cost, Clean in Zero Latency.
- **Defaults, Threshold automated**, with a ceiling: settings published before every block, as the Plugin Shell publishes them while Host Automation moves a parameter, so the cost of taking new settings is measured with the block.
- **Worst case**, with a ceiling: every stage on, at its most costly (`worstCaseSettings`): FET at Colour 100%, High Quality at 8x Oversampling (the cap at each rate: 4x at 96 kHz) with Lookahead 5 ms, Saturation on, the Gate on and closing on the quiet bursts, Dry and Wet both at 0 dB, and the Limiter on and limiting, Output Gain +12 dB driving the loud bursts over its Ceiling. This is the scene that prices the Limiter's 8x, 48-tap true-peak estimator. `tests/engine/RenderScenesTest.cpp` checks that its Limiter, compressor and Gate all reduce the gain on the bursts.
- **Clean, VCA and FET, each at Colour 100%**, Zero Latency: what each Character's detector and Colour cost over Clean.
- **High Quality, 2x Oversampling**: the default High Quality, its Oversampling sections and delays.
- **Defaults, 32-sample blocks**: the per-block overhead, at the smallest block size hosts commonly use.
- **Worst case on silence** (timed after 10 s of nothing, when every filter and envelope has rung down) and **on subnormal input**: these fail the run when they cost more than 1.5 times the Worst case scene, the same settings on the bursts. They are held to their own settings, not to Defaults, because the Engine runs every stage it has whatever the input (the Limiter and the Oversampling sections included), so silence through the worst case still costs about 0.6 times the worst case on the bursts, several times Defaults. That catches arithmetic on subnormal numbers, many times slower than normal on x64; on arm64 it runs at full speed, so the Windows x64 run is the one that would catch it.

| CPU scene | Ceiling 48 / 96 kHz | Apple M3 48 kHz | 96 kHz | CI Windows x64 48 kHz | 96 kHz | CI macOS 48 kHz | 96 kHz |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Defaults | 3% / 6% | 0.39% | 0.79% | pending | pending | pending | pending |
| Defaults, Threshold automated | 3% / 6% | 0.40% | 0.81% | pending | pending | pending | pending |
| Worst case | 100% / 100% | 2.58% | 4.48% | pending | pending | pending | pending |
| Clean, Colour 100% | — | 0.39% | 0.79% | pending | pending | pending | pending |
| VCA, Colour 100% | — | 0.66% | 1.32% | pending | pending | pending | pending |
| FET, Colour 100% | — | 0.85% | 1.70% | pending | pending | pending | pending |
| High Quality, 2x Oversampling | — | 0.69% | 1.39% | pending | pending | pending | pending |
| Defaults, 32-sample blocks | — | 0.40% | 0.80% | pending | pending | pending | pending |
| Worst case on silence | — | 1.51% | 2.36% | pending | pending | pending | pending |
| Worst case on subnormal input | — | 1.36% | 2.05% | pending | pending | pending | pending |

The Apple M3 columns were measured 2026-10-21 on an 8-core Apple M3 with other agents' work light (load average 2.4 to 3.4), after this tree's own build had finished. The CI columns are GitHub's `windows-latest` and `macos-latest` runners, from the job summary where `scripts/check.sh cpu` writes the scene lines.

The Limiter's share of the worst case: the Limiter runs whether on or off (its on/off crossfade needs both sides), so turning it off in the scene saves only the crossfade, about 1%. Timed on its own, outside the Engine, on the same bursts 12 dB up, High Quality's Limiter costs about 0.8% at 48 kHz and 1.6% at 96 kHz on the Apple M3: about a third of the worst case.

**Ceilings.** Each ceiling is about three times the slowest CI runner's worst run, absolute, rounded up: high enough that runner noise doesn't fail the check, low enough to fail on a regression that multiplies the cost. They are absolute, not relative to a baseline, so a slow drift over many changes still trips them. The ceilings live in `CpuBudget.cpp` and in the table above, set from a PR's first CI run and a quiet-machine run, with their dates.

**Later tickets.** Each DSP ticket adds its CPU scene (one line in `CpuBudget.cpp`) and its render scene (one line in `tests/render/RenderScenes.h`, with what `RenderScenesTest.cpp` checks), and updates this table with its figures.

## CPU A/B

`scripts/check.sh cpu <ref>` says what a change did to each CPU scene's cost against any git ref (`origin/main` for a PR), on a machine busy with other builds too. Absolute loads on a busy Mac or a CI runner vary from run to run; the ratio of two builds timed side by side doesn't, because load hits both alike. It builds `<ref>`'s Engine in a temporary worktree under `build/cpu-ab`, with this tree's CPU scenes copied over (`tests/cpu/` and `tests/engine/Measure.h`), so both sides run the same scenes and only the Engine differs. A scene that needs a `Settings` field `<ref>`'s Engine lacks fails that build, by design: it can't be compared. Then, for 7 rounds, it runs every scene at 48 kHz through one build and then the other for about a second each (`comp1_cpu_budget run`), swapping which goes first every round, each run a fresh Engine timed by its thread's CPU time.

It prints the load average before and after, then per scene the median after/before ratio (above 1 is slower), the lowest and highest round, how many rounds went up and down, and a verdict: **slower** or **faster** when the median is at least 3% from 1.0 and at least 6 of the 7 rounds went that way, **no clear change** otherwise. It is a report, not a gate: it exits 0 whatever the verdicts, and fails only when a build fails. Each run's loads stay in `build/cpu-ab/loads.tsv`.

This tree against itself, 2026-10-21 on the Apple M3 (load average about 3), read "no clear change" on every scene, medians 0.995 to 1.006.

## Null check

`scripts/check.sh null <ref>` shows what a change did to the Engine's output, sample for sample, against any git ref (`origin/main` for a PR). It builds `<ref>`'s render tool (`tests/render/RenderTool.cpp`, built with the tests and linked against the Engine only) in a temporary worktree under `build/null`, with the same compiler and flags as this tree, renders the render scenes through both Engines, and prints each scene's worst sample deviation in dBFS at each sample rate, or `exact` when the two renders are bit-identical. It fails above −120 dBFS, far below anything audible, when a scene's two renders differ in length, or when `<ref>` has a scene this tree hasn't; a scene only this tree has is `new` and passes. Both builds run on the machine the check runs on, so it compares within one platform. The renders stay in `build/null/current` and `build/null/ref`.

The render scenes (`tests/render/RenderScenes.h`) are data the tool and its test share, so a scene added there is rendered and compared on both sides. Each is stereo, one second of the shared noise bursts (full scale, 100 ms loud and 100 ms at −34 dB, seeded, so every run renders the same input) through 97-sample blocks, not a multiple of the Engine's 16-sample run, at 44.1, 96 and 192 kHz: the lowest rate, the one where 8x Oversampling is capped to 4x, and the highest. They play every host parameter off its default at least once, where it is heard: the compressor's settings (Ratio ∞ with Knee 0, Knee 72, Range 6, fast Attack, Release and Hold, Auto Release, Auto Gain, Output Gain), Threshold gliding from −18 to −40 dB and Auto Gain switched on a quarter of a second in; VCA and FET at full Colour and at 40%; Saturation after the compressor and before it at more Drive; Dry and Wet together; the Gate at its defaults and with every setting moved; the Limiter driven over its Ceiling in both Processing Modes and at a lower Ceiling; High Quality, its Lookahead, and Oversampling at 1x and 8x with FET's Colour and Saturation, the stages it runs; Stereo Link at 50%, Mid and Side; the Sidechain as Detection Source; each Detection EQ band on, moved from its defaults; Global Bypass switched on; and the CPU budget's worst case. `tests/engine/RenderScenesTest.cpp` keeps the list covering all of this, by host parameter, and every render repeatable.

Against itself (`HEAD`), 2026-10-21, every scene read `exact`. A change to one scene's setting one double ULP off (Knee 72, Range 6, Output Gain 6 or Stereo Link 50, each nudged by `std::nextafter`) still reads `exact`: it is under what a float sample can show. One float ULP off (Output Gain `std::nextafter (6.0f, 7.0f)`) reads −138.5 dBFS on that scene alone, at every rate, and passes the bar.
