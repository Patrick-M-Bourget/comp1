#!/usr/bin/env python3
"""Dynamics lab: model a Character and print the numbers the Engine's tests check, before writing C++.

Models the Clean Character after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
Design — A Tutorial and Analysis" (J. Audio Eng. Soc. 60(6)): feedforward, with a soft-knee gain computer
and a smooth branching peak detector in the log domain, with Hold, Auto Release and Range, and Auto Gain after it;
and High Quality's Lookahead and fixed latency. Standard library only. See tools/dyn-lab/USAGE.md for every command.
"""

import argparse
import math
import os
from dataclasses import dataclass, fields, replace
from fractions import Fraction
from math import inf

# The compressor's parameter ranges, low to high, named once. The check harness's docs stage fails when one
# differs from the Engine's parameterRanges (plugkit.conf's LAB_RANGES), comparing the two as written, so
# infinity is inf on both sides. Each follows FabFilter's public Pro-C 3 documentation where it gives one,
# and docs/adr/0008-threshold-and-release-ranges-are-comp1s-own.md where it doesn't.
# ADR 0008: the Pro-C 2 manual's version history gives the minimum; 0 dB is full scale on the Detection Level.
THRESHOLD_RANGE = (-60, 0)
# Pro-C 3 manual, "Dynamics controls": from 1:1 (no compression) to infinity (a limiter).
RATIO_RANGE = (1, inf)
# Pro-C 3 manual, "Dynamics controls": "from 0 dB (hard knee) to 72 dB (soft knee)".
KNEE_RANGE = (0, 72)
# Pro-C 3 manual, "Time controls": "ranging from 0.005 ms (very fast) to 250 ms (very slow)".
ATTACK_RANGE = (0.005, 250)
# ADR 0008: FabFilter publishes no Release range.
RELEASE_RANGE = (5, 5000)
# Pro-C 3 product page: "Hold (up to 500 ms)"; the manual gives no Hold range.
HOLD_RANGE = (0, 500)
# ADR 0008: FabFilter publishes no Range limits; 60 dB is the most the lowest Threshold at Ratio ∞ can take off.
RANGE_RANGE = (0, 60)
# ADR 0001: High Quality's fixed latency holds 5 ms of Lookahead. A Processing Mode setting, not a Character's, so it
# stays out of Settings.
LOOKAHEAD_RANGE = (0, 5)
LOOKAHEAD_DEFAULT = 0.0

# The sample rates the Engine's timing tests run at.
SAMPLE_RATES = (44100, 48000, 96000)

# Every command line command: name -> (add its options to an argparse parser, run it on the parsed args).
COMMANDS = {}


def command(name, options=lambda parser: None):
    """Registers the decorated run(args) as name, with options(parser) adding its arguments."""
    def register(run):
        COMMANDS[name] = (options, run)
        return run
    return register


# --- Clean: gain computer -----------------------------------------------------------------------

@dataclass
class Settings:
    """The compressor controls the Clean Character reads: dB, :1 (math.inf for ∞), ms and Auto Release on or off.
    Range 60 dB, its top, sets no limit."""
    threshold: float = -18.0
    ratio: float = 4.0
    knee: float = 6.0
    attack: float = 10.0
    release: float = 100.0
    hold: float = 0.0
    range: float = 60.0
    auto_release: bool = False


# Each Settings field's range and the unit it's shown in.
PARAMETERS = {
    "threshold": (THRESHOLD_RANGE, " dB"),
    "ratio": (RATIO_RANGE, ":1"),
    "knee": (KNEE_RANGE, " dB"),
    "attack": (ATTACK_RANGE, " ms"),
    "release": (RELEASE_RANGE, " ms"),
    "hold": (HOLD_RANGE, " ms"),
    "range": (RANGE_RANGE, " dB"),
}


def _shown(value):
    return "∞" if value == math.inf else f"{value:g}"


def check_range(name, value, value_range, unit):
    """Raises ValueError naming the setting and its range when value is outside it."""
    low, high = value_range
    if not low <= value <= high:
        raise ValueError(f"{name.capitalize()} {_shown(value)}{unit} is outside its range, "
                         f"{_shown(low)} to {_shown(high)}{unit}")


def check_ranges(settings):
    """Raises ValueError naming the first setting outside its range, and the range. A switch has no range."""
    for field in fields(settings):
        value = getattr(settings, field.name)
        if isinstance(value, bool):
            continue
        value_range, unit = PARAMETERS[field.name]
        check_range(field.name, value, value_range, unit)


def gain_computer(level_db, settings):
    """The Transfer Curve: output level in dB for a steady input level in dB (the paper's eq. 4).
    Unity below the Knee, 1/Ratio above it, and a quadratic across its width joining the two."""
    over = level_db - settings.threshold
    half_knee = settings.knee / 2
    slope = 1 / settings.ratio - 1  # 1/∞ is 0, so ∞:1 is flat above the Knee
    if over <= -half_knee:
        return level_db
    if over < half_knee:
        return level_db + slope * (over + half_knee) ** 2 / (2 * settings.knee)
    return settings.threshold + over / settings.ratio


def gain_reduction(level_db, settings):
    """The Gain Reduction in dB the Transfer Curve asks for at a steady input level: what the gain computer
    takes off, capped at Range."""
    return min(level_db - gain_computer(level_db, settings), settings.range)


def transfer(level_db, settings):
    """The Transfer Curve with Range: the output level in dB for a steady input level in dB. Past the level
    where the Gain Reduction reaches Range, the output rises with the input again, Range below it."""
    return level_db - gain_reduction(level_db, settings)


def range_edge(settings):
    """The input level in dB at which the Gain Reduction reaches Range, or None when it never does."""
    slope = 1 - 1 / settings.ratio
    if settings.range <= 0 or slope == 0:
        return None
    half_knee = settings.knee / 2
    over = settings.range / slope  # above the Knee, Gain Reduction is slope * over
    if over < half_knee:  # inside it, slope * (over + W/2)^2 / 2W
        over = math.sqrt(2 * settings.knee * settings.range / slope) - half_knee
    return settings.threshold + over


# --- Auto Release ---------------------------------------------------------------------------------

# The Engine's run grid: from its first sample the audio is cut into runs of this many samples (plugkit's
# runSamples). Auto Release moves a run at a time, from the first sample, as the Engine's does.
RUN_SAMPLES = 16
# Release after sustained compression, as a multiple of Release: with Auto Release on, the detector releases in
# Release × (1 + (AUTO_RELEASE_MULTIPLIER - 1) × sustain). USAGE.md gives the reasons for each constant.
AUTO_RELEASE_MULTIPLIER = 5.0
# The sustain state's time constants: it charges toward 1 while the Transfer Curve asks for Gain Reduction, and
# discharges toward 0 while it doesn't, each covering 1 - 1/e of the way in its time.
AUTO_RELEASE_CHARGE_MS = 1000.0
AUTO_RELEASE_DISCHARGE_MS = 2000.0
# How far past Release the shortest default burst may release, as a fraction of it.
AUTO_RELEASE_TRANSIENT_MARGIN = 0.01


def _pole(ms, fs):
    """A one-pole coefficient that covers 1 - 1/e of a step in ms milliseconds at fs."""
    return math.exp(-1000 / (ms * fs))


class AutoRelease:
    """The sustain state, from 0 to 1, and the release time it gives. Each sample, hear() counts whether the
    Transfer Curve asked for Gain Reduction; at the start of each run, start_run() moves the sustain state on by
    the run just heard, charging for the samples that asked and discharging for the rest."""

    def __init__(self, fs, sustain=0.0, heard=0):
        charge, discharge = _pole(AUTO_RELEASE_CHARGE_MS, fs), _pole(AUTO_RELEASE_DISCHARGE_MS, fs)
        self.charged = [charge ** n for n in range(RUN_SAMPLES + 1)]
        self.discharged = [discharge ** n for n in range(RUN_SAMPLES + 1)]
        self.sustain = sustain
        self.heard = heard

    def hear(self, compressing):
        self.heard += compressing

    def start_run(self):
        self.sustain = 1 - (1 - self.sustain) * self.charged[self.heard]
        self.sustain *= self.discharged[RUN_SAMPLES - self.heard]
        self.heard = 0

    def release_ms(self, release_ms):
        """The release time in ms for Release release_ms, as the sustain state stands."""
        return release_ms * (1 + (AUTO_RELEASE_MULTIPLIER - 1) * self.sustain)


# --- Clean: detector ------------------------------------------------------------------------------


class Detector:
    """The smooth branching peak detector of the paper's eq. 12, run in the log domain on the gain
    computer's output: it smooths the Gain Reduction the Transfer Curve asks for, capped at Range, rising
    with Attack and falling with Release, each one pole. Hold, which the paper lacks, keeps the Gain Reduction where it
    is for Hold ms whenever it stops rising, before Release begins; a rise at any point restarts it. With Auto
    Release on, Release is AutoRelease's, taken at the start of each run; Hold runs first either way.

    Not the paper's decoupled detector (eq. 17), whose release runs through the attack pole too and so
    gives back gain more slowly than Release says."""

    def __init__(self, settings, fs, gain_reduction=0.0, auto_release=None):
        """auto_release is the AutoRelease state to start from: empty by default."""
        self.settings = settings
        self.fs = fs
        self.attack = _pole(settings.attack, fs)
        self.release = _pole(settings.release, fs)
        self.hold = round(settings.hold * fs / 1000)
        self.held = 0
        self.gain_reduction = gain_reduction
        self.auto_release = auto_release or AutoRelease(fs)
        self.position = 0  # where the next sample falls in its run

    def process(self, level_db):
        """Takes one sample's Detection Level in dB and returns the Gain Reduction in dB (0 or more)."""
        if self.position == 0:
            self.auto_release.start_run()
            if self.settings.auto_release:
                self.release = _pole(self.auto_release.release_ms(self.settings.release), self.fs)
        self.position = (self.position + 1) % RUN_SAMPLES
        wanted = gain_reduction(level_db, self.settings)
        self.auto_release.hear(wanted > 0)
        if wanted > self.gain_reduction:
            self.held = 0
            pole = self.attack
        elif self.held < self.hold:
            self.held += 1
            return self.gain_reduction
        else:
            pole = self.release
        self.gain_reduction = pole * self.gain_reduction + (1 - pole) * wanted
        return self.gain_reduction


# --- Clean: step response -------------------------------------------------------------------------

STEP_DB = 20.0
# The fractions of a step the Engine's timing tests read: one time constant, and ln 10 of them.
ONE_TIME_CONSTANT = 1 - 1 / math.e
NINETY_PERCENT = 0.9


@dataclass
class Step:
    """A step response's times in ms. Release times count from when Release begins, after Hold."""
    attack_63: float
    attack_90: float
    release_63: float
    release_90: float
    hold: float


def _times(detector, level_db, start, end, fractions):
    """Feeds level_db until the Gain Reduction has moved each of fractions from start to end. Returns
    the samples taken to reach each, and the samples before it first moved at all."""
    reached, held, n = [None] * len(fractions), None, 0
    while reached[-1] is None:
        n += 1
        moved = (detector.process(level_db) - start) / (end - start)
        if held is None and moved > 0:
            held = n - 1
        for i, fraction in enumerate(fractions):
            if reached[i] is None and moved >= fraction:
                reached[i] = n
    return reached, held


def step_response(settings, fs, lookahead=LOOKAHEAD_DEFAULT):
    """Steps the Detection Level from STEP_DB below Threshold to STEP_DB above it and back, from steady
    state, and measures the Gain Reduction's Attack, Release and Hold. None when there is no Gain
    Reduction to measure (Ratio 1:1 or Range 0 dB). Steady state above Threshold is sustained compression, so with
    Auto Release on, its sustain state starts full: the longest release.

    With Lookahead (ms), the detector meets each step Lookahead before the listener hears it in the delayed audio.
    Attack's times count from the step the listener hears, so they come Lookahead earlier and can be negative: the
    Gain Reduction starts before the step. Hold and Release are durations from when the detector meets the step down,
    so Lookahead doesn't move them."""
    quiet, loud = settings.threshold - STEP_DB, settings.threshold + STEP_DB
    low, high = gain_reduction(quiet, settings), gain_reduction(loud, settings)
    if high - low < 1e-9:
        return None
    ms = 1000 / fs
    fractions = (ONE_TIME_CONSTANT, NINETY_PERCENT)
    (attack_63, attack_90), _ = _times(Detector(settings, fs, low), loud, low, high, fractions)
    sustained = AutoRelease(fs, sustain=1.0, heard=RUN_SAMPLES)
    (release_63, release_90), held = _times(Detector(settings, fs, high, sustained), quiet, high, low, fractions)
    return Step(attack_63 * ms - lookahead, attack_90 * ms - lookahead, (release_63 - held) * ms,
                (release_90 - held) * ms, held * ms)


# --- Processing Mode: latency ---------------------------------------------------------------------

# High Quality reports one fixed latency per sample rate, whatever the settings (ADR 0001), so the host's delay
# compensation never moves while Oversampling or Lookahead do. It is three parts, each a delay in whole host samples
# at its own place in the chain; Zero Latency has none of them.

# Reserved for Oversampling (#10): the delay of its up- and down-sampling filters. ADR 0001 caps the internal rate at
# about 384 kHz, so the largest factor halves each time the rate doubles, and the filters cost about the same number
# of host samples at every rate: a fixed count, not a time. A linear-phase filter delays by half its length at its
# own rate, so 2x stages of N taps, up and down, cost about N/2 host samples for the first, N/4 for the second and N/8
# for the third: 128 holds the three stages of 8x at up to about 145 taps each, which #10 designs within it.
OVERSAMPLING_RESERVE_SAMPLES = 128
# The Lookahead budget: Lookahead's top, so every Lookahead fits within it. The audio is delayed by the whole budget
# and the detection signal by the budget less Lookahead, so moving Lookahead moves only the detection signal.
LOOKAHEAD_BUDGET_MS = LOOKAHEAD_RANGE[1]
# Reserved for the Limiter (#18): its own lookahead, about 1 ms (ADR 0001), time to bring the gain down smoothly
# ahead of a peak, whatever the compressor's Lookahead is.
LIMITER_RESERVE_MS = 1.0

# A Processing Mode switch fades the output out over MODE_FADE_MS, swaps paths in the silence, and fades the new one
# in over MODE_FADE_MS, each fade on a smoothstep curve; it never crossfades between the two paths, whose delays
# differ (ADR 0001). Smoothstep starts and ends flat, with no corner in the gain, so the fade adds no click: over
# 10 ms its spectrum falls away fast above about 100 Hz. 10 ms is the shortest of ADR 0001's 10–20 ms, so the whole
# switch is a dip of about 20 ms, which reads as the switch itself rather than as a dropout.
MODE_FADE_MS = 10.0

MODES = ("zero latency", "high quality")
PLUGKIT_CONF = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "plugkit.conf")


def supported_rates():
    """The sample rates comp1 supports, in Hz: plugkit.conf's SAMPLE_RATES, the one list pluginval and the tests run
    at."""
    with open(PLUGKIT_CONF) as conf:
        for line in conf:
            if line.startswith("SAMPLE_RATES="):
                return tuple(int(rate) for rate in line.strip().split("=", 1)[1].split(","))
    raise ValueError(f"{PLUGKIT_CONF} has no SAMPLE_RATES")


def _whole_samples(ms, fs):
    """ms at fs in host samples, rounded up so the time always fits. Exact: a rate like 44.1 kHz times 1 ms is 44.1,
    which binary floating point holds a hair over or under."""
    return math.ceil(Fraction(ms) * Fraction(fs) / 1000)


def high_quality_parts(fs):
    """High Quality's latency at fs, in host samples: the Oversampling reserve, the Lookahead budget and the Limiter
    reserve, in that order."""
    return (OVERSAMPLING_RESERVE_SAMPLES, _whole_samples(LOOKAHEAD_BUDGET_MS, fs),
            _whole_samples(LIMITER_RESERVE_MS, fs))


def latency(mode, fs):
    """The latency a Processing Mode (one of MODES) reports at fs, in host samples."""
    if mode not in MODES:
        raise ValueError(f"Processing Mode {mode!r} is not one of {', '.join(MODES)}")
    return sum(high_quality_parts(fs)) if mode == "high quality" else 0


# --- Clean: Auto Gain ----------------------------------------------------------------------------

# The reference level distribution: Detection Levels from -40 to 0 dB in 2 dB steps, each equally likely.
# Its top is full scale, where a sine's peak reads 0 dB; 40 dB below that is quiet, but well above noise.
REFERENCE_LEVELS = tuple(range(-40, 1, 2))


def auto_gain(settings):
    """The Auto Gain estimate in dB: the negated average Gain Reduction of the Transfer Curve, with Range,
    over REFERENCE_LEVELS. It depends only on Threshold, Ratio, Knee and Range, never on the audio."""
    return sum(gain_reduction(level, settings) for level in REFERENCE_LEVELS) / len(REFERENCE_LEVELS)


# How far the reference signal's level may move with Auto Gain on, in dB, over the settings USAGE.md lists.
AUTO_GAIN_TOLERANCE = 1.0
# The reference signal: one segment per reference level, lowest first, each this long, at this rate, from
# this seed.
REFERENCE_SEGMENT_SECONDS = 0.5
REFERENCE_RATE = 48000
REFERENCE_SEED = 1


def _white(seed):
    """Endless white noise in [-1, 1), from a 32-bit linear congruential generator (Numerical Recipes'
    constants): simple to write again in C++ and the same on every platform."""
    state = seed & 0xFFFFFFFF
    while True:
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        yield state / 2 ** 31 - 1


def _pink(white):
    """Pink-like noise from white: Paul Kellet's three-pole "economy" filter (music-dsp.org), within 0.5 dB
    of -3 dB/octave above about 10 Hz."""
    b0 = b1 = b2 = 0.0
    for w in white:
        b0 = 0.99765 * b0 + 0.0990460 * w
        b1 = 0.96300 * b1 + 0.2965164 * w
        b2 = 0.57000 * b2 + 1.0526913 * w
        yield b0 + b1 + b2 + 0.1848 * w


def reference_signal():
    """The reference signal: pink-like noise in one segment per REFERENCE_LEVELS level, lowest first. Each
    segment is scaled to the power of a sine whose peak reads its Detection Level (half that peak squared),
    so the detector, which reads peaks, sees noise at about the level a sine there would show."""
    pink = _pink(_white(REFERENCE_SEED))
    length = round(REFERENCE_SEGMENT_SECONDS * REFERENCE_RATE)
    segments = []
    for level in REFERENCE_LEVELS:
        noise = [next(pink) for _ in range(length)]
        power = sum(x * x for x in noise) / length
        scale = math.sqrt(10 ** (level / 10) / 2 / power)
        segments.append([x * scale for x in noise])
    return segments


def level_difference(settings, auto_gain_db=None):
    """Runs the reference signal through the Clean model, Attack, Release and Hold as settings has them, then
    Auto Gain (the estimate unless auto_gain_db is given), and returns the long-term level difference in dB,
    output minus input: each segment's level difference (its power ratio), averaged over the segments. The
    average is in dB, as the estimate's is, so every level counts the same, the quiet ones as much as the loud."""
    makeup = 10 ** ((auto_gain(settings) if auto_gain_db is None else auto_gain_db) / 20)
    detector = Detector(settings, REFERENCE_RATE)
    differences = []
    for segment in reference_signal():
        power_in = power_out = 0.0
        for x in segment:
            level_db = 20 * math.log10(abs(x)) if x != 0 else -math.inf
            y = x * 10 ** (-detector.process(level_db) / 20) * makeup
            power_in += x * x
            power_out += y * y
        differences.append(10 * math.log10(power_out / power_in))
    return sum(differences) / len(differences)


# --- Saturation -----------------------------------------------------------------------------------

# ADR 0008 (as amended): Drive is comp1's own. At 0 dB a full-scale signal just reaches the shape's knee.
DRIVE_RANGE = (0, 24)


@dataclass
class Saturation:
    """The Saturation stage's controls: on or off, Drive in dB, and Saturation Position, before or after the
    compressor."""
    on: bool = False
    drive: float = 6.0
    position: str = "after"


def check_drive(drive):
    """Raises ValueError when Drive is outside its range, naming the range."""
    low, high = DRIVE_RANGE
    if not low <= drive <= high:
        raise ValueError(f"Drive {drive:g} dB is outside its range, {low:g} to {high:g} dB")


def _log_cosh(t):
    """ln cosh t for t >= 0, without overflow: t + ln(1 + e^-2t) - ln 2."""
    return t + math.log1p(math.exp(-2 * t)) - math.log(2)


def shape(u):
    """The soft-clip shape, f(u): exactly u for |u| <= 1; above it, 1 + tanh(|u| - 1) with u's sign, which leaves the
    line with the same slope (1) and curvature (0), so it is C²-smooth, and saturates toward 2."""
    t = abs(u) - 1
    if t <= 0:
        return u
    return math.copysign(1 + math.tanh(t), u)


def residual(u):
    """The shape's nonlinear residual, r(u) = f(u) - u: exactly 0 for |u| <= 1, then tanh(t) - t with u's sign, where
    t = |u| - 1."""
    t = abs(u) - 1
    if t <= 0:
        return 0.0
    return -math.copysign(t - math.tanh(t), u)


def residual_antiderivative(u):
    """R(u), the residual's antiderivative, in closed form: 0 for |u| <= 1, then ln cosh t - t²/2 (even, as r is
    odd), with t = |u| - 1. It is continuous at the knee, where both pieces are 0."""
    t = abs(u) - 1
    if t <= 0:
        return 0.0
    return _log_cosh(t) - t * t / 2


def driven(x, drive_db, residual_of):
    """Level-compensated Drive: x + r(g x) / g, with g = 10^(Drive/20), for each sample of x, with the residual
    worked out by residual_of (one call per driven sample, in order). Below the knee, the output is x exactly."""
    g = 10 ** (drive_db / 20)
    return [s + residual_of(g * s) / g for s in x]


# Two driven samples closer than this count as equal: the quotient falls back to the residual at their midpoint.
# Far above where the quotient's rounding matters, far below where the midpoint's error does (it is second order in
# the gap: under 1e-13 of the residual's curvature).
ADAA_EQUAL_WITHIN = 1e-6


class AntiAliased:
    """First-order antiderivative anti-aliasing (ADAA) of the residual only (ADR 0005): each driven sample's
    residual is the average of r over the line from the last driven sample to this one,
    (R(u[n]) - R(u[n-1])) / (u[n] - u[n-1]). The linear part passes sample-aligned, outside it. One per channel."""

    def __init__(self):
        self.previous = 0.0
        self.previous_antiderivative = 0.0

    def __call__(self, u):
        antiderivative = residual_antiderivative(u)
        if abs(u - self.previous) < ADAA_EQUAL_WITHIN:
            out = residual((u + self.previous) / 2)
        else:
            out = (antiderivative - self.previous_antiderivative) / (u - self.previous)
        self.previous, self.previous_antiderivative = u, antiderivative
        return out


def steady_curve_db(level_db, drive_db):
    """The steady-level curve: the output level in dB for a steady input of level_db (a constant sample value whose
    magnitude reads level_db, so ADAA's two samples are equal and it gives the residual itself)."""
    g = 10 ** (drive_db / 20)
    return 20 * math.log10(shape(g * 10 ** (level_db / 20)) / g)


@dataclass
class Chain:
    """The steady output level in dB of the compressor and Saturation for a steady input, and the Gain Reduction the
    compressor applies in it."""
    output: float
    gain_reduction: float


def chain(level_db, settings, saturation):
    """The steady compressor-plus-Saturation chain for a steady input of level_db. Before, the compressor hears the
    saturated signal (the Internal detection signal is its input); After, it hears the input, as with Saturation off,
    and Saturation shapes its output."""
    if not saturation.on:
        reduction = gain_reduction(level_db, settings)
        return Chain(level_db - reduction, reduction)
    if saturation.position == "before":
        saturated = steady_curve_db(level_db, saturation.drive)
        reduction = gain_reduction(saturated, settings)
        return Chain(saturated - reduction, reduction)
    reduction = gain_reduction(level_db, settings)
    return Chain(steady_curve_db(level_db - reduction, saturation.drive), reduction)


# The steady-level curve's grid, in dB: from STEADY_LOWEST to STEADY_HIGHEST, STEADY_SPACING apart, with the onset.
STEADY_LOWEST, STEADY_HIGHEST, STEADY_SPACING = -36.0, 6.0, 3.0


def saturation_options(parser):
    transfer_options(parser)
    parser.add_argument("--drive", type=float, default=Saturation().drive, metavar="dB")
    parser.add_argument("--level", type=float, default=0.0, metavar="dB")


@command("saturation", options=saturation_options)
def run_saturation(args):
    check_drive(args.drive)
    report_saturation(settings_from(args), args.drive, args.level)


def report_saturation(settings, drive_db, level_db):
    """The steady-level curve at drive_db, then the compressor-plus-Saturation chain at level_db, Before and After."""
    onset = -drive_db
    print(f"Saturation steady-level curve, Drive {drive_db:g} dB: steady input; the shape leaves the line at {onset:g} dB")
    print(f"{'input dB':>9} {'output dB':>10}  region")
    count = round((STEADY_HIGHEST - STEADY_LOWEST) / STEADY_SPACING)
    grid = sorted({round(STEADY_LOWEST + i * STEADY_SPACING, 6) for i in range(count + 1)} | {onset})
    for level in grid:
        print(f"{level:9.2f} {steady_curve_db(level, drive_db):10.2f}  {'linear' if level <= onset else 'saturating'}")
    print(f"ceiling {20 * math.log10(2 / 10 ** (drive_db / 20)):.2f} dB: the shape saturates toward 2, over Drive")
    print(f"Compressor and Saturation, steady input {level_db:g} dB, Drive {drive_db:g} dB, {_transfer_heading(settings)}")
    print(f"{'position':>9} {'output dB':>10} {'reduction':>10}")
    for name, saturation in (("off", Saturation()),
                             ("before", Saturation(True, drive_db, "before")),
                             ("after", Saturation(True, drive_db, "after"))):
        steady = chain(level_db, settings, saturation)
        print(f"{name:>9} {steady.output:10.2f} {steady.gain_reduction:10.2f}")


# --- Saturation: aliasing -------------------------------------------------------------------------

def fft(samples):
    """The discrete Fourier transform of a power-of-two count of real samples: iterative radix-2."""
    n = len(samples)
    bits = n.bit_length() - 1
    out = [complex(samples[int(f"{i:0{bits}b}"[::-1], 2)]) for i in range(n)] if bits else [complex(samples[0])]
    size = 2
    while size <= n:
        half = size // 2
        twiddles = [complex(math.cos(2 * math.pi * k / size), -math.sin(2 * math.pi * k / size)) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                a, b = out[start + k], out[start + k + half] * twiddles[k]
                out[start + k], out[start + k + half] = a + b, a - b
        size *= 2
    return out


# Each measurement's length: a power of two, so a tone of a whole number of cycles over it is bin-centred and the
# window can be rectangular, with no leakage.
ALIAS_SAMPLES = 4096


def tone_bin(frequency, fs):
    """The bin of the tone nearest frequency at fs: an odd one, so, the shape being odd, no alias of a harmonic lands
    on a harmonic's own bin."""
    k = round(frequency * ALIAS_SAMPLES / fs)
    return k if k % 2 else k + 1


def tone(k, level_db):
    """A sine of k whole cycles over ALIAS_SAMPLES, its peak at level_db, two lengths long: the first lets ADAA's
    state settle, the second is measured. It starts at 0, as ADAA's state does."""
    a = 10 ** (level_db / 20)
    return [a * math.sin(2 * math.pi * k * n / ALIAS_SAMPLES) for n in range(2 * ALIAS_SAMPLES)]


def spectrum(output):
    """Each bin's peak amplitude, from DC to Nyquist, over output's second length."""
    scale = 2 / ALIAS_SAMPLES
    return [abs(c) * scale for c in fft(output[ALIAS_SAMPLES:])[:ALIAS_SAMPLES // 2 + 1]]


def worst_alias_db(amplitudes, k):
    """The worst alias in dB relative to the fundamental: the loudest bin that holds no in-band harmonic."""
    harmonics = set(range(k, ALIAS_SAMPLES // 2 + 1, k))
    worst = max(a for b, a in enumerate(amplitudes) if b not in harmonics)
    return 20 * math.log10(max(worst, 1e-300) / amplitudes[k])


def shaped(k, level_db, drive_db, residual_of):
    """The spectrum of the bin-centred tone k at level_db through the Saturation stage at drive_db."""
    return spectrum(driven(tone(k, level_db), drive_db, residual_of))


# The reference conditions: each rate, tone and Drive, at one level, full scale. Drive is level-compensated, so the
# shape sees only the level plus the Drive, and every figure relative to the fundamental depends on that sum: a
# quieter level is a lower Drive. Full scale gives each Drive its hardest case.
ALIAS_RATES = (44100, 48000)
ALIAS_FREQUENCIES = (1000, 3000, 5000, 10000)
ALIAS_DRIVES = (6.0, 12.0, 18.0, 24.0)
ALIAS_LEVEL = 0.0


# The hardest reference condition: the highest tone at the top of Drive's range, at each rate.
HARDEST = (max(ALIAS_FREQUENCIES), DRIVE_RANGE[1])


@dataclass
class AliasingTolerance:
    """ADAA's worst alias in dB relative to the fundamental is at most ceiling at every reference condition, and at
    the hardest (HARDEST) at least improvement dB under the naive shaper's. USAGE.md gives the reasons."""
    ceiling: float
    improvement: float


ALIASING_TOLERANCE = AliasingTolerance(ceiling=-24.0, improvement=15.0)


@dataclass
class Alias:
    """The worst alias at one condition, in dB relative to the fundamental: the naive shaper's and ADAA's."""
    fs: float
    frequency: float  # the tone's, bin-centred
    drive: float
    naive: float
    adaa: float
    hardest: bool = False

    @property
    def within(self):
        return (self.adaa <= ALIASING_TOLERANCE.ceiling
                and (not self.hardest or self.naive - self.adaa >= ALIASING_TOLERANCE.improvement))


def aliases(rates=ALIAS_RATES, frequencies=ALIAS_FREQUENCIES, drives=ALIAS_DRIVES, level_db=ALIAS_LEVEL):
    """The worst alias at each condition, the hardest marked."""
    rows = []
    for fs in rates:
        for frequency in frequencies:
            k = tone_bin(frequency, fs)
            for drive in drives:
                naive = worst_alias_db(shaped(k, level_db, drive, residual), k)
                adaa = worst_alias_db(shaped(k, level_db, drive, AntiAliased()), k)
                hardest = (frequency, drive) == HARDEST
                rows.append(Alias(fs, k * fs / ALIAS_SAMPLES, drive, naive, adaa, hardest))
    return rows


# --- Saturation: the residual's half-sample offset ------------------------------------------------

# The ideal reference: the shaper run at IDEAL_POINTS points a cycle (at least 80 times oversampled at the reference
# tones), then band-limited to the rate's Nyquist, so its harmonics are the shape's own, with no aliases and no
# ADAA averaging.
IDEAL_POINTS = 4096
# The tones whose in-band harmonics are compared: the reference tones with a harmonic below Nyquist.
HARMONIC_FREQUENCIES = (1000, 3000, 5000)
HARMONICS = (2, 3, 4, 5)
# ADAA's in-band harmonics are within this many dB of the ideal reference's (USAGE.md gives the reasons).
HARMONIC_TOLERANCE_DB = 4.0
# The shape is odd, so its even harmonics are absent: under this, in dB, a harmonic isn't compared.
ABSENT_DB = -200.0


def ideal_harmonic_db(harmonic, level_db, drive_db):
    """The ideal reference's harmonic, its peak amplitude in dB: the shaper's output over one cycle of a sine at
    level_db, at IDEAL_POINTS points, correlated with the harmonic."""
    g, a = 10 ** (drive_db / 20), 10 ** (level_db / 20)
    total = 0j
    for i in range(IDEAL_POINTS):
        phase = 2 * math.pi * i / IDEAL_POINTS
        total += shape(g * a * math.sin(phase)) / g * complex(math.cos(harmonic * phase), -math.sin(harmonic * phase))
    return 20 * math.log10(max(abs(total) * 2 / IDEAL_POINTS, 1e-300))


@dataclass
class Harmonic:
    """One in-band harmonic of a reference tone through ADAA, and the ideal reference's, peak amplitudes in dB."""
    fs: float
    frequency: float  # the tone's, bin-centred
    drive: float
    harmonic: int
    ideal: float
    adaa: float

    @property
    def compared(self):
        return self.ideal > ABSENT_DB

    @property
    def within(self):
        return not self.compared or abs(self.adaa - self.ideal) <= HARMONIC_TOLERANCE_DB


def harmonics(rates=ALIAS_RATES, frequencies=HARMONIC_FREQUENCIES, drives=ALIAS_DRIVES, level_db=ALIAS_LEVEL):
    """Each in-band harmonic, 2nd to 5th, of each reference tone through ADAA, beside the ideal reference's."""
    rows = []
    for fs in rates:
        for frequency in frequencies:
            k = tone_bin(frequency, fs)
            for drive in drives:
                amplitudes = shaped(k, level_db, drive, AntiAliased())
                for h in HARMONICS:
                    if h * k < ALIAS_SAMPLES // 2:
                        adaa = 20 * math.log10(max(amplitudes[h * k], 1e-300))
                        rows.append(Harmonic(fs, k * fs / ALIAS_SAMPLES, drive, h, ideal_harmonic_db(h, level_db, drive), adaa))
    return rows


def aliasing_options(parser):
    parser.add_argument("--drive", type=float, nargs="+", default=list(ALIAS_DRIVES), metavar="dB")
    parser.add_argument("--rates", type=float, nargs="+", default=list(ALIAS_RATES), metavar="Hz")


@command("aliasing", options=aliasing_options)
def run_aliasing(args):
    for drive in args.drive:
        check_drive(drive)
    _check_rates(args.rates)
    if not report_aliasing(args.rates, args.drive):
        raise SystemExit(1)


def report_aliasing(rates=ALIAS_RATES, drives=ALIAS_DRIVES):
    """The worst alias at each reference condition, naive and ADAA, then ADAA's in-band harmonics beside the ideal
    reference's. True when every row is within its tolerance."""
    tolerance = ALIASING_TOLERANCE
    print(f"Saturation aliasing, bin-centred tones at {ALIAS_LEVEL:g} dBFS over {ALIAS_SAMPLES} samples: the worst "
          f"alias in dB relative to the fundamental")
    print(f"tolerance: ADAA at most {tolerance.ceiling:g} dB, and at the hardest condition (*) at least "
          f"{tolerance.improvement:g} dB under naive")
    print(f"{'rate Hz':>8} {'tone Hz':>8} {'drive dB':>9} {'naive':>8} {'ADAA':>8} {'better':>8}  check")
    rows = aliases(rates, ALIAS_FREQUENCIES, drives)
    for row in rows:
        print(f"{row.fs:8g} {row.frequency:8.1f} {row.drive:9g} {row.naive:8.2f} {row.adaa:8.2f} "
              f"{row.naive - row.adaa:8.2f}  {'within' if row.within else 'outside'}{' *' if row.hardest else ''}")
    print(f"In-band harmonics through ADAA against the ideal reference (the shaper {IDEAL_POINTS} points a cycle, "
          f"band-limited), peak dB; tolerance ±{HARMONIC_TOLERANCE_DB:g} dB")
    print(f"{'rate Hz':>8} {'tone Hz':>8} {'drive dB':>9} {'harmonic':>9} {'at Hz':>8} {'ideal':>8} {'ADAA':>8} "
          f"{'diff':>7}  check")
    bands = harmonics(rates, HARMONIC_FREQUENCIES, drives)
    for row in bands:
        if row.compared:
            print(f"{row.fs:8g} {row.frequency:8.1f} {row.drive:9g} {row.harmonic:9d} "
                  f"{row.harmonic * row.frequency:8.1f} {row.ideal:8.2f} {row.adaa:8.2f} {row.adaa - row.ideal:7.2f}  "
                  f"{'within' if row.within else 'outside'}")
    print(f"the even harmonics are absent from both, under {ABSENT_DB:g} dB: the shape is odd")
    return all(row.within for row in rows) and all(row.within for row in bands)


# --- Commands -------------------------------------------------------------------------------------

def _ratio(text):
    if text.strip().lower() in ("inf", "∞"):
        return math.inf
    return float(text)


def transfer_options(parser):
    defaults = Settings()
    parser.add_argument("--threshold", type=float, default=defaults.threshold, metavar="dB")
    parser.add_argument("--ratio", type=_ratio, default=defaults.ratio, metavar="N|inf")
    parser.add_argument("--knee", type=float, default=defaults.knee, metavar="dB")
    parser.add_argument("--range", type=float, default=defaults.range, metavar="dB")


def timing_options(parser):
    defaults = Settings()
    transfer_options(parser)
    parser.add_argument("--attack", type=float, default=defaults.attack, metavar="ms")
    parser.add_argument("--release", type=float, default=defaults.release, metavar="ms")
    parser.add_argument("--hold", type=float, default=defaults.hold, metavar="ms")
    parser.add_argument("--rates", type=float, nargs="+", default=list(SAMPLE_RATES), metavar="Hz")
    parser.add_argument("--lookahead", type=float, default=LOOKAHEAD_DEFAULT, metavar="ms")


def step_options(parser):
    timing_options(parser)
    parser.add_argument("--auto-release", action="store_true")


def _check_rates(rates):
    if any(fs <= 0 for fs in rates):
        raise ValueError("every sample rate must be above 0 Hz")


def settings_from(args):
    """The Settings the parsed args name, checked against their ranges."""
    settings = Settings(**{field.name: getattr(args, field.name) for field in fields(Settings)
                           if hasattr(args, field.name)})
    check_ranges(settings)
    return settings


def _transfer_heading(settings):
    return (f"Threshold {settings.threshold:g} dB, Ratio {_shown(settings.ratio)}:1, Knee {settings.knee:g} dB, "
            f"Range {settings.range:g} dB")


def _timing_heading(settings):
    return f"Attack {settings.attack:g} ms, Release {settings.release:g} ms, Hold {settings.hold:g} ms"


# The Transfer Curve's grid: from GRID_BELOW dB under the Knee to GRID_ABOVE dB over it, GRID_SPACING apart.
GRID_BELOW, GRID_ABOVE, GRID_SPACING = 12.0, 24.0, 3.0


@dataclass
class TransferCurve:
    """The Transfer Curve on its grid, as (input dB, output dB, region) rows, and its measured slopes
    at the bottom and top of the grid in dB of output per dB of input: 1 below the Knee, 1/Ratio above it,
    and 1 again past the level where the Gain Reduction reaches Range."""
    rows: list
    slope_below: float
    slope_above: float


def transfer_curve(settings):
    """The Transfer Curve on its grid, always including Threshold, both Knee edges, and the level where the
    Gain Reduction reaches Range when that falls on the grid."""
    t, half_knee = settings.threshold, settings.knee / 2
    lowest, highest = t - half_knee - GRID_BELOW, t + half_knee + GRID_ABOVE
    count = round((highest - lowest) / GRID_SPACING)
    grid = {round(lowest + i * GRID_SPACING, 6) for i in range(count + 1)} | {t - half_knee, t, t + half_knee}
    edge = range_edge(settings)
    if edge is not None and lowest <= edge <= highest:
        grid.add(round(edge, 6))
    rows = []
    for level in sorted(grid):
        if edge is not None and level >= edge:
            region = "range"
        else:
            region = "below" if level <= t - half_knee else "knee" if level < t + half_knee else "above"
        rows.append((level, transfer(level, settings), region))

    def slope(lower):
        return (transfer(lower + GRID_SPACING, settings) - transfer(lower, settings)) / GRID_SPACING

    return TransferCurve(rows, slope(lowest), slope(highest - GRID_SPACING))


@command("transfer", options=transfer_options)
def run_transfer(args):
    report_transfer(settings_from(args))


def report_transfer(settings):
    """The Transfer Curve's rows, and the slopes outside the Knee that the Engine's tests check."""
    curve = transfer_curve(settings)
    print(f"Transfer Curve, {_transfer_heading(settings)}")
    print(f"{'input dB':>9} {'output dB':>10} {'reduction':>10}  region")
    for level, out, region in curve.rows:
        print(f"{level:9.2f} {out:10.2f} {level - out:10.2f}  {region}")
    print(f"slope below the Knee {curve.slope_below:.4f} (unity is 1), "
          f"above it {curve.slope_above:.4f} (1/Ratio is {1 / settings.ratio:.4f}; 1 once Range is reached)")


@command("step", options=step_options)
def run_step(args):
    settings = settings_from(args)
    check_range("lookahead", args.lookahead, LOOKAHEAD_RANGE, " ms")
    _check_rates(args.rates)
    report_step(settings, args.rates, args.lookahead)


def report_step(settings, sample_rates=SAMPLE_RATES, lookahead=LOOKAHEAD_DEFAULT):
    """The step response's times at each sample rate, for a level step STEP_DB either side of Threshold."""
    print(f"Step ±{STEP_DB:g} dB around Threshold, {_transfer_heading(settings)}, {_timing_heading(settings)}, "
          f"Auto Release {'on' if settings.auto_release else 'off'}, Lookahead {lookahead:g} ms")
    print("times in ms; Attack counts from the step in the delayed audio the listener hears, which the detector meets "
          "Lookahead earlier; Hold counts from when the detector meets the step down, and Release from when it "
          "begins, after Hold; 63% is 1 - 1/e")
    print(f"{'rate Hz':>8} {'attack 63%':>11} {'attack 90%':>11} {'release 63%':>12} {'release 90%':>12} {'hold':>9}")
    for fs in sample_rates:
        step = step_response(settings, fs, lookahead)
        if step is None:
            print(f"{fs:8g}  no Gain Reduction to measure at Ratio 1:1 or Range 0 dB")
            continue
        print(f"{fs:8g} {step.attack_63:11.4f} {step.attack_90:11.4f} {step.release_63:12.4f} "
              f"{step.release_90:12.4f} {step.hold:9.4f}")


def latency_options(parser):
    parser.add_argument("--rates", type=float, nargs="+", default=None, metavar="Hz",
                        help="default: plugkit.conf's SAMPLE_RATES")


@command("latency", options=latency_options)
def run_latency(args):
    sample_rates = supported_rates() if args.rates is None else args.rates
    _check_rates(sample_rates)
    report_latency(sample_rates)


def report_latency(sample_rates):
    """Each Processing Mode's latency at each sample rate, High Quality's three parts, and the mode switch's fades."""
    print("Latency in host samples: Zero Latency, and High Quality, the sum of its three parts")
    print(f"High Quality: the Oversampling reserve ({OVERSAMPLING_RESERVE_SAMPLES} samples), the Lookahead budget "
          f"({LOOKAHEAD_BUDGET_MS:g} ms) and the Limiter reserve ({LIMITER_RESERVE_MS:g} ms), each rounded up to "
          f"whole samples")
    print(f"{'rate Hz':>8} {'zero latency':>13} {'high quality':>13} {'oversampling':>13} {'lookahead':>10} "
          f"{'limiter':>8}")
    for fs in sample_rates:
        oversampling, lookahead, limiter = high_quality_parts(fs)
        print(f"{fs:8g} {latency('zero latency', fs):13d} {latency('high quality', fs):13d} {oversampling:13d} "
              f"{lookahead:10d} {limiter:8d}")
    print(f"a Processing Mode switch fades out over {MODE_FADE_MS:g} ms and in over {MODE_FADE_MS:g} ms, "
          f"each on a smoothstep curve (MODE_FADE_MS)")


def autogain_options(parser):
    transfer_options(parser)


@command("autogain", options=autogain_options)
def run_autogain(args):
    if not report_autogain(settings_from(args)):
        raise SystemExit(1)


def report_autogain(settings):
    """The Auto Gain estimate, and the measured check on the reference signal. True when the check holds."""
    print(f"Auto Gain, {_transfer_heading(settings)}")
    print(f"estimate {auto_gain(settings):+.4f} dB: the average Gain Reduction over Detection Levels "
          f"{REFERENCE_LEVELS[0]} to {REFERENCE_LEVELS[-1]} dB, {REFERENCE_LEVELS[1] - REFERENCE_LEVELS[0]} dB apart")
    difference = level_difference(settings)
    within = abs(difference) <= AUTO_GAIN_TOLERANCE
    print(f"reference signal, Attack {settings.attack:g} ms, Release {settings.release:g} ms, "
          f"Hold {settings.hold:g} ms: output minus input {difference:+.2f} dB, "
          f"{'within' if within else 'outside'} the ±{AUTO_GAIN_TOLERANCE:g} dB tolerance")
    return within


# --- Auto Release: bursts -----------------------------------------------------------------------

# The default burst lengths in ms: from a transient to sustained compression.
BURST_LENGTHS = (1, 10, 100, 1000, 3000)


@dataclass
class Release:
    """A release's times in ms, counted from when Release begins, after Hold."""
    release_63: float
    release_90: float


@dataclass
class Burst:
    """The release after a burst of length ms, with Auto Release off and on."""
    length: float
    off: Release
    on: Release


def burst_release(settings, fs, length_ms):
    """Holds the Detection Level STEP_DB over Threshold for length_ms from steady state STEP_DB under it, the run
    grid starting with the burst, then drops it back, and measures the Release that follows. None when there is no
    Gain Reduction to measure (Ratio 1:1 or Range 0 dB)."""
    quiet, loud = settings.threshold - STEP_DB, settings.threshold + STEP_DB
    low = gain_reduction(quiet, settings)
    if gain_reduction(loud, settings) - low < 1e-9:
        return None
    detector = Detector(settings, fs, low)
    for _ in range(max(round(length_ms * fs / 1000), 1)):
        detector.process(loud)
    start = detector.gain_reduction
    (release_63, release_90), held = _times(detector, quiet, start, low, (ONE_TIME_CONSTANT, NINETY_PERCENT))
    ms = 1000 / fs
    return Release((release_63 - held) * ms, (release_90 - held) * ms)


def bursts(settings, fs, lengths=BURST_LENGTHS):
    """The release after each burst length in ms, with Auto Release off and on; an empty list when there is no Gain
    Reduction to measure."""
    off, on = replace(settings, auto_release=False), replace(settings, auto_release=True)
    if burst_release(off, fs, lengths[0]) is None:
        return []
    return [Burst(length, burst_release(off, fs, length), burst_release(on, fs, length)) for length in lengths]


def bursts_options(parser):
    timing_options(parser)
    parser.add_argument("--lengths", type=float, nargs="+", default=list(BURST_LENGTHS), metavar="ms")


@command("bursts", options=bursts_options)
def run_bursts(args):
    settings = settings_from(args)
    _check_rates(args.rates)
    if any(length <= 0 for length in args.lengths):
        raise ValueError("every burst length must be above 0 ms")
    report_bursts(settings, args.rates, args.lengths)


def report_bursts(settings, sample_rates=SAMPLE_RATES, lengths=BURST_LENGTHS):
    """The release after each burst at each sample rate, with Auto Release off and on."""
    print(f"Bursts {STEP_DB:g} dB over Threshold, then {STEP_DB:g} dB under it, {_transfer_heading(settings)}, "
          f"{_timing_heading(settings)}")
    print(f"Auto Release releases in Release × (1 + {AUTO_RELEASE_MULTIPLIER - 1:g} × sustain); sustain charges in "
          f"{AUTO_RELEASE_CHARGE_MS:g} ms, discharges in {AUTO_RELEASE_DISCHARGE_MS:g} ms")
    print("times in ms, counted from when Release begins, after Hold; 63% is 1 - 1/e")
    print(f"{'rate Hz':>8} {'burst ms':>9} {'off 63%':>10} {'off 90%':>10} {'on 63%':>10} {'on 90%':>10}")
    for fs in sample_rates:
        rows = bursts(settings, fs, lengths)
        if not rows:
            print(f"{fs:8g}  no Gain Reduction to measure at Ratio 1:1 or Range 0 dB")
        for burst in rows:
            print(f"{fs:8g} {burst.length:9g} {burst.off.release_63:10.4f} {burst.off.release_90:10.4f} "
                  f"{burst.on.release_63:10.4f} {burst.on.release_90:10.4f}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True, metavar="command",
                                     help=", ".join(COMMANDS))
    for name, (options, _) in COMMANDS.items():
        options(commands.add_parser(name))
    args = parser.parse_args()
    try:
        COMMANDS[args.command][1](args)
    except ValueError as error:
        parser.error(str(error))
