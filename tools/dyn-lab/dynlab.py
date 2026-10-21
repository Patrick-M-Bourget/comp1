#!/usr/bin/env python3
"""Dynamics lab: model a Character and print the numbers the Engine's tests check, before writing C++.

Models the Clean Character after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
Design — A Tutorial and Analysis" (J. Audio Eng. Soc. 60(6)): feedforward, with a soft-knee gain computer
and a smooth branching peak detector in the log domain, with Hold and Range, and Auto Gain after it. Standard
library only. See
tools/dyn-lab/USAGE.md for every command.
"""

import argparse
import math
from dataclasses import dataclass, fields
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
    """The compressor controls the Clean Character reads: dB, :1 (math.inf for ∞) and ms. Range 60 dB, its
    top, sets no limit."""
    threshold: float = -18.0
    ratio: float = 4.0
    knee: float = 6.0
    attack: float = 10.0
    release: float = 100.0
    hold: float = 0.0
    range: float = 60.0


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


def check_ranges(settings):
    """Raises ValueError naming the first setting outside its range, and the range."""
    for field in fields(settings):
        (low, high), unit = PARAMETERS[field.name]
        value = getattr(settings, field.name)
        if not low <= value <= high:
            raise ValueError(f"{field.name.capitalize()} {_shown(value)}{unit} is outside its range, "
                             f"{_shown(low)} to {_shown(high)}{unit}")


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


# --- Clean: detector ------------------------------------------------------------------------------

def _pole(ms, fs):
    """A one-pole coefficient that covers 1 - 1/e of a step in ms milliseconds at fs."""
    return math.exp(-1000 / (ms * fs))


class Detector:
    """The smooth branching peak detector of the paper's eq. 12, run in the log domain on the gain
    computer's output: it smooths the Gain Reduction the Transfer Curve asks for, capped at Range, rising
    with Attack and falling with Release, each one pole. Hold, which the paper lacks, keeps the Gain Reduction where it
    is for Hold ms whenever it stops rising, before Release begins; a rise at any point restarts it.

    Not the paper's decoupled detector (eq. 17), whose release runs through the attack pole too and so
    gives back gain more slowly than Release says."""

    def __init__(self, settings, fs, gain_reduction=0.0):
        self.settings = settings
        self.attack = _pole(settings.attack, fs)
        self.release = _pole(settings.release, fs)
        self.hold = round(settings.hold * fs / 1000)
        self.held = 0
        self.gain_reduction = gain_reduction

    def process(self, level_db):
        """Takes one sample's Detection Level in dB and returns the Gain Reduction in dB (0 or more)."""
        wanted = gain_reduction(level_db, self.settings)
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


def step_response(settings, fs):
    """Steps the Detection Level from STEP_DB below Threshold to STEP_DB above it and back, from steady
    state, and measures the Gain Reduction's Attack, Release and Hold. None when there is no Gain
    Reduction to measure (Ratio 1:1 or Range 0 dB)."""
    quiet, loud = settings.threshold - STEP_DB, settings.threshold + STEP_DB
    low, high = gain_reduction(quiet, settings), gain_reduction(loud, settings)
    if high - low < 1e-9:
        return None
    ms = 1000 / fs
    fractions = (ONE_TIME_CONSTANT, NINETY_PERCENT)
    (attack_63, attack_90), _ = _times(Detector(settings, fs, low), loud, low, high, fractions)
    (release_63, release_90), held = _times(Detector(settings, fs, high), quiet, high, low, fractions)
    return Step(attack_63 * ms, attack_90 * ms, (release_63 - held) * ms, (release_90 - held) * ms, held * ms)


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


def step_options(parser):
    defaults = Settings()
    transfer_options(parser)
    parser.add_argument("--attack", type=float, default=defaults.attack, metavar="ms")
    parser.add_argument("--release", type=float, default=defaults.release, metavar="ms")
    parser.add_argument("--hold", type=float, default=defaults.hold, metavar="ms")
    parser.add_argument("--rates", type=float, nargs="+", default=list(SAMPLE_RATES), metavar="Hz")


def settings_from(args):
    """The Settings the parsed args name, checked against their ranges."""
    settings = Settings(**{field.name: getattr(args, field.name) for field in fields(Settings)
                           if hasattr(args, field.name)})
    check_ranges(settings)
    return settings


def _transfer_heading(settings):
    return (f"Threshold {settings.threshold:g} dB, Ratio {_shown(settings.ratio)}:1, Knee {settings.knee:g} dB, "
            f"Range {settings.range:g} dB")


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
    if any(fs <= 0 for fs in args.rates):
        raise ValueError("every sample rate must be above 0 Hz")
    report_step(settings, args.rates)


def report_step(settings, sample_rates=SAMPLE_RATES):
    """The step response's times at each sample rate, for a level step STEP_DB either side of Threshold."""
    print(f"Step ±{STEP_DB:g} dB around Threshold, {_transfer_heading(settings)}, "
          f"Attack {settings.attack:g} ms, Release {settings.release:g} ms, Hold {settings.hold:g} ms")
    print("times in ms; Release counts from when it begins, after Hold; 63% is 1 - 1/e")
    print(f"{'rate Hz':>8} {'attack 63%':>11} {'attack 90%':>11} {'release 63%':>12} {'release 90%':>12} {'hold':>9}")
    for fs in sample_rates:
        step = step_response(settings, fs)
        if step is None:
            print(f"{fs:8g}  no Gain Reduction to measure at Ratio 1:1 or Range 0 dB")
            continue
        print(f"{fs:8g} {step.attack_63:11.4f} {step.attack_90:11.4f} {step.release_63:12.4f} "
              f"{step.release_90:12.4f} {step.hold:9.4f}")


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
