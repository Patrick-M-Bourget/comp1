#!/usr/bin/env python3
"""Dynamics lab: model a Character and print the numbers the Engine's tests check, before writing C++.

Models the Clean Character after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
Design — A Tutorial and Analysis" (J. Audio Eng. Soc. 60(6)): feedforward, with a soft-knee gain computer
and a smooth branching peak detector in the log domain, with Hold. Standard library only. See
tools/dyn-lab/USAGE.md for every command.
"""

import argparse
import math
from dataclasses import dataclass, fields

# The compressor's parameter ranges, low to high, named once so a check can compare them with the
# plugin's. Each follows FabFilter's public Pro-C 3 documentation where it gives one, and
# docs/adr/0008-threshold-and-release-ranges-are-comp1s-own.md where it doesn't.
# ADR 0008: the Pro-C 2 manual's version history gives the minimum; 0 dB is full scale on the Detection Level.
THRESHOLD_RANGE = (-60, 0)
# Pro-C 3 manual, "Dynamics controls": from 1:1 (no compression) to infinity (a limiter).
RATIO_RANGE = (1, math.inf)
# Pro-C 3 manual, "Dynamics controls": "from 0 dB (hard knee) to 72 dB (soft knee)".
KNEE_RANGE = (0, 72)
# Pro-C 3 manual, "Time controls": "ranging from 0.005 ms (very fast) to 250 ms (very slow)".
ATTACK_RANGE = (0.005, 250)
# ADR 0008: FabFilter publishes no Release range.
RELEASE_RANGE = (5, 5000)
# Pro-C 3 product page: "Hold (up to 500 ms)"; the manual gives no Hold range.
HOLD_RANGE = (0, 500)

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
    """The compressor controls the Clean Character reads: dB, :1 (math.inf for ∞) and ms."""
    threshold: float = -18.0
    ratio: float = 4.0
    knee: float = 6.0
    attack: float = 10.0
    release: float = 100.0
    hold: float = 0.0


# Each Settings field's range and the unit it's shown in.
PARAMETERS = {
    "threshold": (THRESHOLD_RANGE, " dB"),
    "ratio": (RATIO_RANGE, ":1"),
    "knee": (KNEE_RANGE, " dB"),
    "attack": (ATTACK_RANGE, " ms"),
    "release": (RELEASE_RANGE, " ms"),
    "hold": (HOLD_RANGE, " ms"),
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


# --- Clean: detector ------------------------------------------------------------------------------

def _pole(ms, fs):
    """A one-pole coefficient that covers 1 - 1/e of a step in ms milliseconds at fs."""
    return math.exp(-1000 / (ms * fs))


class Detector:
    """The smooth branching peak detector of the paper's eq. 12, run in the log domain on the gain
    computer's output: it smooths the Gain Reduction the Transfer Curve asks for, rising with Attack and
    falling with Release, each one pole. Hold, which the paper lacks, keeps the Gain Reduction where it
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
        wanted = level_db - gain_computer(level_db, self.settings)
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
    Reduction to measure (Ratio 1:1)."""
    quiet, loud = settings.threshold - STEP_DB, settings.threshold + STEP_DB
    low, high = quiet - gain_computer(quiet, settings), loud - gain_computer(loud, settings)
    if high - low < 1e-9:
        return None
    ms = 1000 / fs
    fractions = (ONE_TIME_CONSTANT, NINETY_PERCENT)
    (attack_63, attack_90), _ = _times(Detector(settings, fs, low), loud, low, high, fractions)
    (release_63, release_90), held = _times(Detector(settings, fs, high), quiet, high, low, fractions)
    return Step(attack_63 * ms, attack_90 * ms, (release_63 - held) * ms, (release_90 - held) * ms, held * ms)


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
    return f"Threshold {settings.threshold:g} dB, Ratio {_shown(settings.ratio)}:1, Knee {settings.knee:g} dB"


# The Transfer Curve's grid: from GRID_BELOW dB under the Knee to GRID_ABOVE dB over it, GRID_SPACING apart.
GRID_BELOW, GRID_ABOVE, GRID_SPACING = 12.0, 24.0, 3.0


@dataclass
class TransferCurve:
    """The Transfer Curve on its grid, as (input dB, output dB, region) rows, and its measured slopes
    either side of the Knee in dB of output per dB of input."""
    rows: list
    slope_below: float
    slope_above: float


def transfer_curve(settings):
    """The Transfer Curve on its grid, always including Threshold and both Knee edges."""
    t, half_knee = settings.threshold, settings.knee / 2
    lowest, highest = t - half_knee - GRID_BELOW, t + half_knee + GRID_ABOVE
    count = round((highest - lowest) / GRID_SPACING)
    grid = {round(lowest + i * GRID_SPACING, 6) for i in range(count + 1)}
    rows = []
    for level in sorted(grid | {t - half_knee, t, t + half_knee}):
        region = "below" if level <= t - half_knee else "knee" if level < t + half_knee else "above"
        rows.append((level, gain_computer(level, settings), region))

    def slope(lower):
        return (gain_computer(lower + GRID_SPACING, settings) - gain_computer(lower, settings)) / GRID_SPACING

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
          f"above it {curve.slope_above:.4f} (1/Ratio is {1 / settings.ratio:.4f})")


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
            print(f"{fs:8g}  no Gain Reduction to measure at Ratio 1:1")
            continue
        print(f"{fs:8g} {step.attack_63:11.4f} {step.attack_90:11.4f} {step.release_63:12.4f} "
              f"{step.release_90:12.4f} {step.hold:9.4f}")


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
