#!/usr/bin/env python3
"""Dynamics lab: model a Character and print the numbers the Engine's tests check, before writing C++.

Models the Clean Character after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
Design — A Tutorial and Analysis" (J. Audio Eng. Soc. 60(6)): feedforward, with a soft-knee gain computer
and a smooth branching peak detector in the log domain, with Hold, Auto Release and Range, and Auto Gain after it;
the VCA Character, an RMS Detection Level into Clean's gain computer and detector, and its Colour; the FET Character,
feedback detection solved within each sample, and its Colour; the Opto Character, FET's loop with a material-dependent
attack, a two-stage release and a history of its own, and its Colour; High Quality's Lookahead and fixed latency; Saturation; the Limiter; the Gate; and the Character Styles' defaults, their Comp Curves and the Comp Follow glide. Standard library only. See tools/dyn-lab/USAGE.md for every
command.
"""

import argparse
import functools
import math
import operator
import os
from dataclasses import dataclass, field, fields, replace
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
# ADR 0008 (as amended): FabFilter publishes no Colour; comp1's runs from none to the Character's tuned amount.
COLOUR_RANGE = (0, 100)
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
    """The compressor controls a Character reads: dB, :1 (math.inf for ∞), ms, Auto Release on or off and Colour in %;
    and the Character, one of CHARACTERS. Range 60 dB, its top, sets no limit."""
    threshold: float = -18.0
    ratio: float = 4.0
    knee: float = 6.0
    attack: float = 10.0
    release: float = 100.0
    hold: float = 0.0
    range: float = 60.0
    auto_release: bool = False
    character: str = "clean"
    colour: float = 100.0


# Each Settings field's range and the unit it's shown in.
PARAMETERS = {
    "threshold": (THRESHOLD_RANGE, " dB"),
    "ratio": (RATIO_RANGE, ":1"),
    "knee": (KNEE_RANGE, " dB"),
    "attack": (ATTACK_RANGE, " ms"),
    "release": (RELEASE_RANGE, " ms"),
    "hold": (HOLD_RANGE, " ms"),
    "range": (RANGE_RANGE, " dB"),
    "colour": (COLOUR_RANGE, "%"),
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
    """Raises ValueError naming the first setting outside its range, and the range. A switch has no range, and the
    Character is one of CHARACTERS."""
    if settings.character not in CHARACTERS:
        raise ValueError(f"Character {settings.character!r} is not one of {', '.join(CHARACTERS)}")
    for field in fields(settings):
        value = getattr(settings, field.name)
        if isinstance(value, (bool, str)):
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


def level_for_reduction(settings, reduction_db):
    """The input level in dB at which the Transfer Curve's Gain Reduction reaches reduction_db (above 0), or None when
    it never does: at Ratio 1:1, or past Range."""
    slope = 1 - 1 / settings.ratio
    if reduction_db <= 0 or slope == 0 or reduction_db > settings.range:
        return None
    half_knee = settings.knee / 2
    over = reduction_db / slope  # above the Knee, Gain Reduction is slope * over
    if over < half_knee:  # inside it, slope * (over + W/2)^2 / 2W
        over = math.sqrt(2 * settings.knee * reduction_db / slope) - half_knee
    return settings.threshold + over


def range_edge(settings):
    """The input level in dB at which the Gain Reduction reaches Range, or None when it never does."""
    return level_for_reduction(settings, settings.range)


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


# --- The Characters' Detection Levels -------------------------------------------------------------

# The Characters dyn-lab models.
CHARACTERS = ("clean", "vca", "fet", "opto")
# VCA's RMS time constant: the one-pole mean-square average covers 1 - 1/e of a step in this many ms. USAGE.md gives
# the reasons.
VCA_RMS_MS = 5.0
# A sine's peak is its RMS level plus this, 10·log10(2) dB, so a sine-calibrated RMS level, 10·log10(2·mean square),
# reads a sine's peak level. A constant-magnitude signal (a square wave) reads this much above its peak level.
SINE_CALIBRATION_DB = 10 * math.log10(2)


def _power(level_db):
    """The mean square of a sine whose peak reads level_db, times 2: 10^(level/10)."""
    return 10 ** (level_db / 10)


class PeakLevel:
    """Clean's Detection Level: each sample's peak level, 20·log10|x|. A steady level reads itself."""

    def __init__(self, fs, level_db):
        pass

    def steady(self, level_db):
        return level_db

    def sample(self, x):
        return 20 * math.log10(abs(x)) if x != 0 else -math.inf


class RmsLevel:
    """VCA's Detection Level: a one-pole mean-square average of the detection signal, with time constant VCA_RMS_MS,
    read sine-calibrated, 10·log10(2·mean square) dB, so a sine reads its peak level, as on Clean. It starts settled at
    level_db. A steady level is a signal of that RMS, such as a square wave SINE_CALIBRATION_DB under it."""

    def __init__(self, fs, level_db):
        self.pole = _pole(VCA_RMS_MS, fs)
        self.mean_square = _power(level_db) / 2

    def _level(self, square):
        self.mean_square = self.pole * self.mean_square + (1 - self.pole) * square
        return 10 * math.log10(2 * self.mean_square) if self.mean_square > 0 else -math.inf

    def steady(self, level_db):
        return self._level(_power(level_db) / 2)

    def sample(self, x):
        return self._level(x * x)


LEVELS = {"clean": PeakLevel, "vca": RmsLevel, "fet": PeakLevel, "opto": PeakLevel}
# The Characters whose detection, with an Internal Detection Source, hears the gain it applies: FET's loop.
FED_BACK = ("fet", "opto")


# --- FET: feedback detection ----------------------------------------------------------------------

# The most FET's loop may speed up Attack or Release: where the loop would move the Gain Reduction faster than one
# pole of the time over FET_SPEEDUP_MAX toward the Transfer Curve's Gain Reduction, it moves at that pace instead.
# USAGE.md gives the reasons.
FET_SPEEDUP_MAX = 4.0
# FET's reference Attack in ms, the fast setting at which `step --character fet` shows its sub-millisecond attack.
FET_REFERENCE_ATTACK_MS = 0.02


def fet_loop(previous_db, level_db, pole, settings):
    """FET's loop, solved within the sample: the Gain Reduction G that one step of a one-pole detector reaches from
    previous_db when its gain computer reads the detection sample's level less G itself,
    G = pole·previous + (1 - pole)·w(level - G). w is the Transfer Curve's inverse in the output's domain: for an output
    level y, the Gain Reduction that brings the input whose Transfer Curve output is y down to it, capped at Range, so at
    steady state the output is the Transfer Curve's. w never falls as y rises, so the solution is unique and lies
    between previous_db and the steady Gain Reduction; it is found region by region of the Transfer Curve in closed
    form."""
    return loop_from(pole * previous_db, 1 - pole, pole, level_db, settings)


def loop_from(a, b, keep, level_db, settings):
    """FET's loop in the form G = a + b·w(level - G): a is what the step keeps of the Gain Reduction held, and b is w's
    share; keep is 1 - b, given so each caller rounds it as its own pole. fet_loop's are a = pole·previous, b = 1 - pole
    and keep the pole; Opto's come from its two parts (opto_step)."""
    t, knee, q = settings.threshold, settings.knee, 1 / settings.ratio
    half = knee / 2
    if level_db - a <= t - half:
        loop = a  # below the Knee in the output's domain, where w is 0
    elif knee > 0 and a + b * (1 - q) * half >= level_db - t - q * half:
        # Across the Knee. With u = x - T + W/2 for the input x whose output is the loop's, the output's y - T + W/2 is
        # u + (q - 1)u²/2W, so the loop is keep·(q - 1)u²/2W + u = level - T + W/2 - a, a quadratic in u.
        e = level_db - t + half - a
        c = keep * (q - 1) / (2 * knee)
        u = 2 * e / (1 + math.sqrt(max(0.0, 1 + 4 * c * e)))
        loop = a + b * (1 - q) * u * u / (2 * knee)
    else:
        # Above it, w is (Ratio - 1)(y - T), written with 1/Ratio so that ∞:1 holds the output at Threshold.
        loop = (q * a + b * (1 - q) * (level_db - t)) / (q + b * (1 - q))
    # Range caps w, and so the loop's solution.
    return min(loop, a + b * settings.range)


def fet_step(previous_db, level_db, wanted_db, pole, settings):
    """One sample of FET's detector from previous_db, pole Attack's or Release's: the loop's solution (fet_loop), but
    never further than one pole of the time over FET_SPEEDUP_MAX moves toward wanted_db, the Transfer Curve's Gain
    Reduction at the detection sample's level, which is the loop's steady state."""
    loop = fet_loop(previous_db, level_db, pole, settings)
    fastest = pole ** FET_SPEEDUP_MAX
    capped = fastest * previous_db + (1 - fastest) * wanted_db
    return min(loop, capped) if wanted_db > previous_db else max(loop, capped)


# --- Opto: material-dependent attack, two-stage release ---------------------------------------------

# Opto's attack: Attack's time times a factor of how far the Transfer Curve's Gain Reduction is over the Gain Reduction
# held, each sample: OPTO_ATTACK_SLOWEST at no overshoot, OPTO_ATTACK_FASTEST at OPTO_ATTACK_SPAN_DB and over, and a
# smoothstep between them in the factor's logarithm, so Attack's own time at half the span. USAGE.md gives the reasons
# for each constant.
OPTO_ATTACK_SLOWEST = 2.0
OPTO_ATTACK_FASTEST = 0.5
OPTO_ATTACK_SPAN_DB = 20.0
# The overshoot at which the attack takes Attack's own time: half the span, where the smoothstep is half way.
OPTO_ATTACK_REFERENCE_DB = OPTO_ATTACK_SPAN_DB / 2
# Opto's release is two one-pole parts of the Gain Reduction: the fast one, OPTO_FAST_SHARE of it, at Release (or Auto
# Release's in its place), and the slow one, the rest, at a time that grows with Opto's history state, from
# OPTO_SLOW_FRESH_MS with none to OPTO_SLOW_SEASONED_MS when it is full. The history charges toward 1 while the Transfer
# Curve asks for Gain Reduction and discharges toward 0 while it doesn't, each covering 1 - 1/e of the way in its time.
OPTO_FAST_SHARE = 0.6
OPTO_SLOW_FRESH_MS = 500.0
OPTO_SLOW_SEASONED_MS = 5000.0
OPTO_HISTORY_CHARGE_MS = 3000.0
OPTO_HISTORY_DISCHARGE_MS = 5000.0
# How long `step --character opto` holds its loud level, from steady state under Threshold, before the step down. Opto's
# release depends on how long it compressed, so its step down follows a burst of this length (`bursts` gives others).
OPTO_STEP_HOLD_MS = 1000.0


def opto_attack_scale(overshoot_db):
    """The factor Opto's attack takes Attack's time by at an overshoot in dB: OPTO_ATTACK_SLOWEST at 0 dB and under, 1 at
    OPTO_ATTACK_REFERENCE_DB, OPTO_ATTACK_FASTEST at OPTO_ATTACK_SPAN_DB and over, smooth between."""
    x = min(max(overshoot_db / OPTO_ATTACK_SPAN_DB, 0.0), 1.0)
    return OPTO_ATTACK_SLOWEST * (OPTO_ATTACK_FASTEST / OPTO_ATTACK_SLOWEST) ** (x * x * (3 - 2 * x))


class History(AutoRelease):
    """Opto's history state, from 0 to 1 (its sustain): stepped on the run grid as Auto Release's sustain state is,
    charging for the samples of the run just heard that asked for Gain Reduction and discharging for the rest, in
    OPTO_HISTORY_CHARGE_MS and OPTO_HISTORY_DISCHARGE_MS."""

    def __init__(self, fs):
        super().__init__(fs)
        charge, discharge = _pole(OPTO_HISTORY_CHARGE_MS, fs), _pole(OPTO_HISTORY_DISCHARGE_MS, fs)
        self.charged = [charge ** n for n in range(RUN_SAMPLES + 1)]
        self.discharged = [discharge ** n for n in range(RUN_SAMPLES + 1)]

    def slow_ms(self):
        """The slow part's release time in ms, as the history stands."""
        return OPTO_SLOW_FRESH_MS + (OPTO_SLOW_SEASONED_MS - OPTO_SLOW_FRESH_MS) * self.sustain


def opto_step(fast_db, slow_db, level_db, wanted_db, fast_pole, slow_pole, settings, feedback):
    """One sample of Opto's two parts, from fast_db and slow_db: each one pole, the fast at fast_pole and the slow at
    slow_pole (both the scaled Attack's while attacking), toward its share of what the gain computer asks for. Fed back,
    the gain computer reads the detection sample's level less the Gain Reduction of the same sample, FET's loop solved
    within the sample (loop_from), and capped as FET's is: never further than each part's pole to the power
    FET_SPEEDUP_MAX moves toward its share of wanted_db, the loop's steady state. Feedforward, it asks for wanted_db.
    Returns the two parts."""
    share = OPTO_FAST_SHARE
    asked = wanted_db
    if feedback:
        a = fast_pole * fast_db + slow_pole * slow_db
        b = share * (1 - fast_pole) + (1 - share) * (1 - slow_pole)
        loop = loop_from(a, b, 1 - b, level_db, settings)
        fast_cap, slow_cap = fast_pole ** FET_SPEEDUP_MAX, slow_pole ** FET_SPEEDUP_MAX
        capped = (fast_cap * fast_db + (1 - fast_cap) * share * wanted_db,
                  slow_cap * slow_db + (1 - slow_cap) * (1 - share) * wanted_db)
        rising = wanted_db > fast_db + slow_db
        if (capped[0] + capped[1] < loop) if rising else (capped[0] + capped[1] > loop):
            return capped
        asked = (loop - a) / b
    return (fast_pole * fast_db + (1 - fast_pole) * share * asked,
            slow_pole * slow_db + (1 - slow_pole) * (1 - share) * asked)


# --- Clean: detector ------------------------------------------------------------------------------


class Detector:
    """The smooth branching peak detector of the paper's eq. 12, run in the log domain on the gain
    computer's output: it smooths the Gain Reduction the Transfer Curve asks for, capped at Range, rising
    with Attack and falling with Release, each one pole. Hold, which the paper lacks, keeps the Gain Reduction where it
    is for Hold ms whenever it stops rising, before Release begins; a rise at any point restarts it. With Auto
    Release on, Release is AutoRelease's, taken at the start of each run; Hold runs first either way.

    Not the paper's decoupled detector (eq. 17), whose release runs through the attack pole too and so
    gives back gain more slowly than Release says.

    Every Character's: its Detection Level (LEVELS) feeds the gain computer, and this detector smooths what that asks
    for, so hear_steady() and hear_sample() take the detection signal, and process() a Detection Level.

    On FET with an Internal Detection Source the detector is fed back: each step is FET's loop (fet_step) rather than
    the one pole, so the gain computer reads the Detection Level less the Gain Reduction of the same sample. Attack,
    Release, Hold and Auto Release branch as on Clean, on whether the Transfer Curve asks for more Gain Reduction than
    the detector holds, which is whether the loop's solution rises. With an External Detection Source (external) the
    gain applied never reaches the Sidechain, so FET is feedforward, as Clean is.

    Opto is fed back as FET is, with Hold and the branching as on Clean, but steps two parts (opto_step): Attack's pole
    is its time scaled by the overshoot (opto_attack_scale), for both parts, and Release's the fast part's, while the
    slow part's comes from Opto's history state (History), taken at the start of each run, as Auto Release's is."""

    def __init__(self, settings, fs, gain_reduction=0.0, auto_release=None, level_db=-math.inf, external=False):
        """auto_release is the AutoRelease state to start from: empty by default. The Character's Detection Level
        starts settled at level_db."""
        self.level = LEVELS[settings.character](fs, level_db)
        self.feedback = settings.character in FED_BACK and not external
        self.opto = settings.character == "opto"
        self.settings = settings
        self.fs = fs
        self.attack = _pole(settings.attack, fs)
        self.release = _pole(settings.release, fs)
        self.hold = round(settings.hold * fs / 1000)
        self.held = 0
        self.gain_reduction = gain_reduction
        self.auto_release = auto_release or AutoRelease(fs)
        self.position = 0  # where the next sample falls in its run
        # Opto's: its two parts, the Gain Reduction held split by their shares, and its history, empty.
        self.fast = OPTO_FAST_SHARE * gain_reduction
        self.slow = gain_reduction - self.fast
        self.history = History(fs)
        self.slow_pole = 0.0

    def process(self, level_db):
        """Takes one sample's Detection Level in dB and returns the Gain Reduction in dB (0 or more)."""
        if self.position == 0:
            self.auto_release.start_run()
            if self.settings.auto_release:
                self.release = _pole(self.auto_release.release_ms(self.settings.release), self.fs)
            self.history.start_run()
            self.slow_pole = _pole(self.history.slow_ms(), self.fs)
        self.position = (self.position + 1) % RUN_SAMPLES
        wanted = gain_reduction(level_db, self.settings)
        self.auto_release.hear(wanted > 0)
        self.history.hear(wanted > 0)
        attacking = wanted > self.gain_reduction
        if attacking:
            self.held = 0
            pole = self.attack
        elif self.held < self.hold:
            self.held += 1
            return self.gain_reduction
        else:
            pole = self.release
        if self.opto:
            slow_pole = self.slow_pole
            if attacking:
                pole = slow_pole = _pole(self.settings.attack * opto_attack_scale(wanted - self.gain_reduction), self.fs)
            self.fast, self.slow = opto_step(self.fast, self.slow, level_db, wanted, pole, slow_pole, self.settings,
                                             self.feedback)
            self.gain_reduction = self.fast + self.slow
        elif self.feedback:
            self.gain_reduction = fet_step(self.gain_reduction, level_db, wanted, pole, self.settings)
        else:
            self.gain_reduction = pole * self.gain_reduction + (1 - pole) * wanted
        return self.gain_reduction

    def hear_steady(self, level_db):
        """One sample of a steady level, sine-calibrated, through the Character's Detection Level and the detector."""
        return self.process(self.level.steady(level_db))

    def hear_sample(self, x):
        """One sample of the detection signal through the Character's Detection Level and the detector."""
        return self.process(self.level.sample(x))


# --- Clean: step response -------------------------------------------------------------------------

STEP_DB = 20.0
# The fractions of a step the Engine's timing tests read: one time constant, and ln 10 of them.
ONE_TIME_CONSTANT = 1 - 1 / math.e
NINETY_PERCENT = 0.9
# The Gain Reduction has begun to move once it is more than this fraction of the way: the Engine's tests read it off
# float output, whose rounding is about 1e-6 dB, under 2e-7 of the smallest step they measure, and count the same way
# (tests/engine/Measure.h, heldWithin). Clean's first step of Release is over it at every setting (1.04e-6 of the way
# at 5000 ms and 192 kHz), so Clean's figures are as with no threshold; on VCA the RMS level leaves it slowly, so the
# two must count alike.
MOVED_WITHIN = 1.0e-6


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
        moved = (detector.hear_steady(level_db) - start) / (end - start)
        if held is None and moved > MOVED_WITHIN:
            held = n - 1
        for i, fraction in enumerate(fractions):
            if reached[i] is None and moved >= fraction:
                reached[i] = n
    return reached, held


def step_response(settings, fs, lookahead=LOOKAHEAD_DEFAULT, external=False, quiet=None, loud=None):
    """Steps the Detection Level from STEP_DB below Threshold (quiet, in dB, when given) to STEP_DB above it (loud)
    and back, from steady state, and measures the Gain Reduction's Attack, Release and Hold. None when there is no Gain
    Reduction to measure (Ratio 1:1 or Range 0 dB). Steady state above Threshold is sustained compression, so with
    Auto Release on, its sustain state starts full: the longest release.

    With Lookahead (ms), the detector meets each step Lookahead before the listener hears it in the delayed audio.
    Attack's times count from the step the listener hears, so they come Lookahead earlier and can be negative: the
    Gain Reduction starts before the step. Hold and Release are durations from when the detector meets the step down,
    so Lookahead doesn't move them.

    With external, the step is the Sidechain's, on an External Detection Source, while the main stays steady: the Gain
    Reduction never reaches what the detector hears.

    Opto's release depends on how long it compressed, so on Opto the step down follows a burst OPTO_STEP_HOLD_MS long
    from steady state under Threshold (after_burst), its history and Auto Release's sustain state as the burst left
    them, rather than steady compression."""
    quiet = settings.threshold - STEP_DB if quiet is None else quiet
    loud = settings.threshold + STEP_DB if loud is None else loud
    low, high = gain_reduction(quiet, settings), gain_reduction(loud, settings)
    if high - low < 1e-9:
        return None
    ms = 1000 / fs
    fractions = (ONE_TIME_CONSTANT, NINETY_PERCENT)
    (attack_63, attack_90), _ = _times(Detector(settings, fs, low, level_db=quiet, external=external), loud, low, high,
                                       fractions)
    if settings.character == "opto":
        released = after_burst(settings, fs, OPTO_STEP_HOLD_MS, external, quiet, loud)
    else:
        released = Detector(settings, fs, high, AutoRelease(fs, sustain=1.0, heard=RUN_SAMPLES), loud, external)
    (release_63, release_90), held = _times(released, quiet, high, low, fractions)
    return Step(attack_63 * ms - lookahead, attack_90 * ms - lookahead, (release_63 - held) * ms,
                (release_90 - held) * ms, held * ms)


# --- Processing Mode: latency ---------------------------------------------------------------------

# High Quality reports one fixed latency per sample rate, whatever the settings (ADR 0001), so the host's delay
# compensation never moves while Oversampling or Lookahead do. It is three parts, each a delay in whole host samples
# at its own place in the chain; Zero Latency has none of them.

# The Oversampling reserve: the delay of Oversampling's two sections, OVERSAMPLING_SECTION_SAMPLES each (see
# "Oversampling" below). ADR 0001 caps the internal rate at about 384 kHz, so the largest factor halves each time the
# rate doubles, and the filters cost about the same number of host samples at every rate: a fixed count, not a time.
# A linear-phase filter delays by half its length at its own rate, so 2x stages of N taps, up and down, cost about
# N/2 host samples for the first, N/4 for the second and N/8 for the third: each section's 64 holds the three stages
# of 8x at up to about 73 taps each.
OVERSAMPLING_RESERVE_SAMPLES = 128
# The Lookahead budget: Lookahead's top, so every Lookahead fits within it. The audio is delayed by the whole budget
# and the detection signal by the budget less Lookahead, so moving Lookahead moves only the detection signal.
LOOKAHEAD_BUDGET_MS = LOOKAHEAD_RANGE[1]
# The Limiter reserve: High Quality's Limiter's own lookahead, about 1 ms (ADR 0001), time to bring the gain down
# smoothly ahead of a peak, whatever the compressor's Lookahead is. The true-peak estimator's delay and the lookahead
# window fill it exactly (lookahead_window; `limiter` prints both).
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
    """Runs the reference signal through the Character's model, Attack, Release and Hold as settings has them, then
    Auto Gain (the estimate unless auto_gain_db is given), and returns the long-term level difference in dB,
    output minus input: each segment's level difference (its power ratio), averaged over the segments. The
    average is in dB, as the estimate's is, so every level counts the same, the quiet ones as much as the loud."""
    makeup = 10 ** ((auto_gain(settings) if auto_gain_db is None else auto_gain_db) / 20)
    detector = Detector(settings, REFERENCE_RATE)
    differences = []
    for segment in reference_signal():
        power_in = power_out = 0.0
        for x in segment:
            y = x * 10 ** (-detector.hear_sample(x) / 20) * makeup
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


@functools.lru_cache(maxsize=None)
def anti_aliased(k, level_db, drive_db, factor=1):
    """The spectrum of the bin-centred tone k at level_db through the Saturation stage at drive_db with ADAA: at the
    base rate, or at factor in High Quality's oversampled section (oversampled_driven)."""
    if factor == 1:
        return shaped(k, level_db, drive_db, AntiAliased())
    return spectrum(oversampled_driven(tone(k, level_db), drive_db, factor))


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
    """At the base rate, ADAA's worst alias in dB relative to the fundamental is at most ceiling at every reference
    condition, and at the hardest (HARDEST) at least improvement dB under the naive shaper's. Oversampled, at each
    factor of passband_ceilings, the worst alias below the passband edge is at most that factor's ceiling, and the
    worst alias anywhere is no worse than at the base rate. USAGE.md gives the reasons."""
    ceiling: float
    improvement: float
    passband_ceilings: dict


ALIASING_TOLERANCE = AliasingTolerance(ceiling=-24.0, improvement=15.0,
                                       passband_ceilings={2: -45.0, 4: -60.0, 8: -75.0})


def worst_passband_alias_db(amplitudes, k):
    """The worst alias below the passband edge (OVERSAMPLING_PASSBAND_EDGE of Nyquist), in dB relative to the
    fundamental: the band the oversampled section's filters keep flat."""
    top = int(OVERSAMPLING_PASSBAND_EDGE * ALIAS_SAMPLES / 2)
    return worst_alias_db(amplitudes[:top + 1], k)


@dataclass
class Alias:
    """The worst alias at one condition, in dB relative to the fundamental: the naive shaper's and ADAA's, at the base
    rate; oversampled, ADAA's at factor too, anywhere and below the passband edge."""
    fs: float
    frequency: float  # the tone's, bin-centred
    drive: float
    naive: float
    adaa: float
    hardest: bool = False
    factor: int = 1
    oversampled: float = None
    passband: float = None

    @property
    def within(self):
        if self.factor > 1:
            return (self.passband <= ALIASING_TOLERANCE.passband_ceilings[self.factor]
                    and self.oversampled <= self.adaa)
        return (self.adaa <= ALIASING_TOLERANCE.ceiling
                and (not self.hardest or self.naive - self.adaa >= ALIASING_TOLERANCE.improvement))


def aliases(rates=ALIAS_RATES, frequencies=ALIAS_FREQUENCIES, drives=ALIAS_DRIVES, level_db=ALIAS_LEVEL, factor=1):
    """The worst alias at each condition, the hardest marked, at the base rate and, when factor is over 1, oversampled
    by it."""
    rows = []
    for fs in rates:
        for frequency in frequencies:
            k = tone_bin(frequency, fs)
            for drive in drives:
                naive = worst_alias_db(shaped(k, level_db, drive, residual), k)
                adaa = worst_alias_db(anti_aliased(k, level_db, drive), k)
                hardest = (frequency, drive) == HARDEST
                row = Alias(fs, k * fs / ALIAS_SAMPLES, drive, naive, adaa, hardest, factor)
                if factor > 1:
                    amplitudes = anti_aliased(k, level_db, drive, factor)
                    row.oversampled = worst_alias_db(amplitudes, k)
                    row.passband = worst_passband_alias_db(amplitudes, k)
                rows.append(row)
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


def harmonics(rates=ALIAS_RATES, frequencies=HARMONIC_FREQUENCIES, drives=ALIAS_DRIVES, level_db=ALIAS_LEVEL,
              factor=1, numbers=HARMONICS):
    """Each in-band harmonic of numbers (2nd to 5th by default) of each reference tone through ADAA, at the base rate
    or oversampled by factor, beside the ideal reference's."""
    rows = []
    for fs in rates:
        for frequency in frequencies:
            k = tone_bin(frequency, fs)
            for drive in drives:
                amplitudes = anti_aliased(k, level_db, drive, factor)
                for h in numbers:
                    if h * k < ALIAS_SAMPLES // 2:
                        adaa = 20 * math.log10(max(amplitudes[h * k], 1e-300))
                        rows.append(Harmonic(fs, k * fs / ALIAS_SAMPLES, drive, h, ideal_harmonic_db(h, level_db, drive), adaa))
    return rows


# --- Oversampling ---------------------------------------------------------------------------------

# The factors Oversampling offers, frozen in this order (ADR 0003), and ADR 0001's cap on the internal rate: a factor
# is allowed at a rate when the rate times the factor is at or under the cap, so the largest allowed factor halves as
# the rate doubles and the filters' delay in host samples stays the same at every rate.
OVERSAMPLING_FACTORS = (1, 2, 4, 8)
MAX_INTERNAL_RATE_HZ = 384000

# Each of the two oversampled sections on the Wet path (Before the detection stage and After the compressor) delays
# its linear part by exactly this many host samples, and pads its residual's up and down filters to the same total,
# so the sections' delay never depends on the factor: half the Oversampling reserve each.
OVERSAMPLING_SECTION_SAMPLES = OVERSAMPLING_RESERVE_SAMPLES // 2

# The filters: each 2x step is a linear-phase half-band FIR, Kaiser-windowed, cascaded for 4x and 8x, and up and down
# use the same design. Only the nonlinear residual passes through them (ADR 0009), never the linear part, so their
# passband edge needn't reach 20 kHz: below the edge the residual's harmonics pass flat, and above it they are
# inaudible at 44.1 kHz or gone at the higher rates, where the edge is far above 20 kHz.
#
# The passband edge, as a fraction of the host's Nyquist (17.64 kHz at 44.1 kHz). A half-band filter's transition is
# centred on the rate it halves to's Nyquist, so its stopband starts as far above that Nyquist as the passband edge
# is below it. 0.8 keeps the first stage's transition 0.1 of its rate wide: wider costs fewer taps, but folds more of
# the residual's top octave back into the band.
OVERSAMPLING_PASSBAND_EDGE = 0.8
# The stopband attenuation every stage reaches, in dB. Aliases the filters let through then sit 100 dB under the
# residual's own out-of-band harmonics: far under every alias ADAA leaves at the oversampled rate (the aliasing rows
# below), and within 40 dB of the float audio's own rounding, so a deeper stopband buys nothing measurable.
OVERSAMPLING_STOPBAND_DB = 100.0
# The Kaiser window's beta for that attenuation, Kaiser's formula for attenuations over 50 dB, 0.1102 (A - 8.7).
# Each stage's length is then the shortest half-band, 4m - 1 taps, whose measured stopband reaches the attenuation.
KAISER_BETA = 0.1102 * (OVERSAMPLING_STOPBAND_DB - 8.7)
# The measured passband edge: the highest frequency the cascade's gain stays within this many dB of unity up to. A
# half-band's passband ripple equals its stopband's, 10^(-100/20), about 1e-4 dB, so 0.001 dB marks where the
# transition starts.
PASSBAND_FLAT_DB = 0.001
# The points each response is measured at, across its band.
RESPONSE_POINTS = 2000


def allowed_factors(fs):
    """The Oversampling factors allowed at fs: those whose internal rate is at or under MAX_INTERNAL_RATE_HZ."""
    return tuple(factor for factor in OVERSAMPLING_FACTORS if factor * fs <= MAX_INTERNAL_RATE_HZ)


def max_oversampling(fs):
    """The largest Oversampling factor allowed at fs."""
    return max(allowed_factors(fs))


def factor_in_force(setting, fs):
    """The factor a setting plays at fs: the largest allowed factor at or under it."""
    return max(factor for factor in allowed_factors(fs) if factor <= setting)


def halfband(taps):
    """A Kaiser-windowed half-band lowpass of taps taps (4m - 1), its DC gain 1: the centre tap 1/2, every other tap
    on each side 0, the rest the ideal half-band's sin(πn/2)/(πn), windowed."""
    centre = (taps - 1) // 2
    window = bessel_i0(KAISER_BETA)
    return [0.5 if n == centre else 0.0 if (n - centre) % 2 == 0 else
            math.sin(math.pi * (n - centre) / 2) / (math.pi * (n - centre))
            * bessel_i0(KAISER_BETA * math.sqrt(1 - ((n - centre) / centre) ** 2)) / window
            for n in range(taps)]


def amplitude(h, f):
    """The zero-phase amplitude of the symmetric filter h at f, a fraction of its own rate."""
    centre = (len(h) - 1) // 2
    return h[centre] + 2 * sum(h[centre + j] * math.cos(2 * math.pi * f * j) for j in range(1, centre + 1) if h[centre + j])


def stage_passband(stage):
    """Stage stage's passband edge (0 for 1x to 2x), a fraction of its own rate, 2^(stage + 1) host rates."""
    return OVERSAMPLING_PASSBAND_EDGE / 2 / 2 ** (stage + 1)


def stopband_db(h, edge):
    """h's measured attenuation in dB across its stopband, from 1/2 - edge to its Nyquist, the least of it."""
    start = 0.5 - edge
    return min(-20 * math.log10(max(abs(amplitude(h, start + (0.5 - start) * i / RESPONSE_POINTS)), 1e-300))
               for i in range(RESPONSE_POINTS + 1))


_STAGES = {}


def stage_filter(stage):
    """Stage stage's half-band: the shortest, 4m - 1 taps, whose stopband reaches OVERSAMPLING_STOPBAND_DB."""
    if stage not in _STAGES:
        m = 1
        while stopband_db(halfband(4 * m - 1), stage_passband(stage)) < OVERSAMPLING_STOPBAND_DB:
            m += 1
        _STAGES[stage] = halfband(4 * m - 1)
    return _STAGES[stage]


def stages(factor):
    """The half-bands of factor's cascade, the first (host rate to 2x) first."""
    return [stage_filter(stage) for stage in range(factor.bit_length() - 1)]


@dataclass
class Pair:
    """factor's up and down filters: their delay together in host samples, the padding that rounds it up to whole host
    samples (in samples at the oversampled rate), and the section's padding left to OVERSAMPLING_SECTION_SAMPLES."""
    factor: int
    delay: int
    internal_padding: int

    @property
    def padding(self):
        return OVERSAMPLING_SECTION_SAMPLES - self.delay


def pair(factor):
    """factor's up and down filters' delay. A linear-phase stage of N taps delays by (N - 1) / 2 samples at its own
    rate, up and down alike; stage s runs at 2^(s + 1) host rates. Each half-band's delay is odd at its rate, so a
    cascade's can fall between host samples: padding at the oversampled rate rounds it up to whole ones."""
    total = sum((len(h) - 1) * factor // 2 ** (stage + 1) for stage, h in enumerate(stages(factor)))
    delay = -(-total // factor)
    return Pair(factor, delay, delay * factor - total)


def cascade_gain(factor, f):
    """The magnitude of factor's cascade (either way: up and down use the same design) at f Hz over the host rate."""
    gain = 1.0
    for stage, h in enumerate(stages(factor)):
        gain *= abs(amplitude(h, f / 2 ** (stage + 1)))
    return gain


def stopband_bands(factor):
    """The bands, in host rates, where factor's cascade must stop: within the passband edge of each multiple of the
    host rate, up to the oversampled Nyquist. The images of the host's passband land there going up, and what lies
    there coming down is what folds back into the passband."""
    edge = OVERSAMPLING_PASSBAND_EDGE / 2
    return [(k - edge, min(k + edge, factor / 2)) for k in range(1, factor // 2 + 1)]


def measured_stopband_db(factor):
    """The cascade's least attenuation in dB across its stopband bands."""
    return min(-20 * math.log10(max(cascade_gain(factor, start + (end - start) * i / RESPONSE_POINTS), 1e-300))
               for start, end in stopband_bands(factor) for i in range(RESPONSE_POINTS + 1))


def measured_passband_edge(factor):
    """The highest frequency, as a fraction of the host rate, up to which the cascade stays within PASSBAND_FLAT_DB
    of unity."""
    step = 0.5 / RESPONSE_POINTS
    f = 0.0
    while f + step <= 0.5 and abs(20 * math.log10(cascade_gain(factor, f + step))) <= PASSBAND_FLAT_DB:
        f += step
    return f


def _up(x, h):
    """x at twice its rate: zero-stuffed, then filtered by h at twice its gain, as a polyphase pair. Delayed by h's
    centre, (len(h) - 1) / 2 samples at the new rate."""
    even = [2 * h[i] for i in range(0, len(h), 2)][::-1]
    centre = (len(h) - 1) // 2
    padded = [0.0] * (len(even) - 1) + x
    out = []
    for n in range(len(x)):
        out.append(sum(map(operator.mul, even, padded[n:n + len(even)])))
        out.append(padded[n + len(even) - 1 - (centre - 1) // 2])
    return out


def _down(u, h):
    """u at half its rate: filtered by h, keeping every other sample, as a polyphase pair. Delayed by h's centre,
    (len(h) - 1) / 2 samples at u's rate."""
    even = [h[i] for i in range(0, len(h), 2)][::-1]
    centre = (len(h) - 1) // 2
    padded = [0.0] * (len(h) - 1) + u
    out = []
    for n in range(0, len(u), 2):
        out.append(sum(map(operator.mul, even, padded[n:n + len(h):2])) + 0.5 * padded[n + len(h) - 1 - centre])
    return out


def oversampled_section(x, residual_of, factor):
    """High Quality's oversampled section at factor (ADR 0009) on x: the linear part, x, delayed by the section's
    OVERSAMPLING_SECTION_SAMPLES; the residual worked out at the oversampled rate by residual_of (one call per
    oversampled sample, in order), from x through the up filters, then back through the down filters and padded to the
    same delay."""
    up = list(x)
    for h in stages(factor):
        up = _up(up, h)
    residual_up = [0.0] * pair(factor).internal_padding + [residual_of(s) for s in up]
    for h in reversed(stages(factor)):
        residual_up = _down(residual_up, h)
    section = OVERSAMPLING_SECTION_SAMPLES
    linear = [0.0] * section + list(x)
    residual_out = [0.0] * pair(factor).padding + residual_up
    return [linear[n] + residual_out[n] for n in range(len(x))]


def oversampled_driven(x, drive_db, factor):
    """The Saturation stage at factor in its oversampled section, with ADAA at the oversampled rate."""
    g = 10 ** (drive_db / 20)
    adaa = AntiAliased()
    return oversampled_section(x, lambda u: adaa(g * u) / g, factor)


def aliasing_options(parser):
    parser.add_argument("--drive", type=float, nargs="+", default=list(ALIAS_DRIVES), metavar="dB")
    parser.add_argument("--rates", type=float, nargs="+", default=list(ALIAS_RATES), metavar="Hz")
    parser.add_argument("--oversampling", type=int, default=1, choices=OVERSAMPLING_FACTORS, metavar="N",
                        help="the factor: 1 (the base rate, the default), 2, 4 or 8")


@command("aliasing", options=aliasing_options)
def run_aliasing(args):
    for drive in args.drive:
        check_drive(drive)
    _check_rates(args.rates)
    _check_factor(args.oversampling, args.rates)
    if not report_aliasing(args.rates, args.drive, args.oversampling):
        raise SystemExit(1)


def report_aliasing(rates=ALIAS_RATES, drives=ALIAS_DRIVES, factor=1):
    """The worst alias at each reference condition, naive and ADAA, and oversampled by factor when it is over 1; then
    ADAA's in-band harmonics, at factor, beside the ideal reference's, and its fundamental. True when every row is
    within its tolerance."""
    tolerance = ALIASING_TOLERANCE
    print(f"Saturation aliasing, bin-centred tones at {ALIAS_LEVEL:g} dBFS over {ALIAS_SAMPLES} samples: the worst "
          f"alias in dB relative to the fundamental")
    rows = aliases(rates, ALIAS_FREQUENCIES, drives, factor=factor)
    if factor == 1:
        print(f"tolerance: ADAA at most {tolerance.ceiling:g} dB, and at the hardest condition (*) at least "
              f"{tolerance.improvement:g} dB under naive")
        print(f"{'rate Hz':>8} {'tone Hz':>8} {'drive dB':>9} {'naive':>8} {'ADAA':>8} {'better':>8}  check")
        for row in rows:
            print(f"{row.fs:8g} {row.frequency:8.1f} {row.drive:9g} {row.naive:8.2f} {row.adaa:8.2f} "
                  f"{row.naive - row.adaa:8.2f}  {'within' if row.within else 'outside'}{' *' if row.hardest else ''}")
    else:
        ceiling = tolerance.passband_ceilings[factor]
        oversampled = f"{factor}x"
        print(f"Oversampling {factor}x, the residual alone through the up and down filters (ADR 0009). Tolerance: "
              f"below the passband edge ({OVERSAMPLING_PASSBAND_EDGE:g} of Nyquist) at most {ceiling:g} dB, and "
              f"anywhere no worse than ADAA at 1x")
        print(f"{'rate Hz':>8} {'tone Hz':>8} {'drive dB':>9} {'1x':>8} {oversampled:>8} {'passband':>9}  check")
        for row in rows:
            print(f"{row.fs:8g} {row.frequency:8.1f} {row.drive:9g} {row.adaa:8.2f} {row.oversampled:8.2f} "
                  f"{row.passband:9.2f}  {'within' if row.within else 'outside'}")
    at = "" if factor == 1 else f" at {factor}x"
    print(f"In-band harmonics through ADAA{at} against the ideal reference (the shaper {IDEAL_POINTS} points a cycle, "
          f"band-limited), peak dB; tolerance ±{HARMONIC_TOLERANCE_DB:g} dB")
    print(f"{'rate Hz':>8} {'tone Hz':>8} {'drive dB':>9} {'harmonic':>9} {'at Hz':>8} {'ideal':>8} {'ADAA':>8} "
          f"{'diff':>7}  check")
    bands = harmonics(rates, HARMONIC_FREQUENCIES, drives, factor=factor)
    for row in bands:
        if row.compared:
            print(f"{row.fs:8g} {row.frequency:8.1f} {row.drive:9g} {row.harmonic:9d} "
                  f"{row.harmonic * row.frequency:8.1f} {row.ideal:8.2f} {row.adaa:8.2f} {row.adaa - row.ideal:7.2f}  "
                  f"{'within' if row.within else 'outside'}")
    print(f"the even harmonics are absent from both, under {ABSENT_DB:g} dB: the shape is odd")
    print(f"The fundamental through ADAA{at} against the ideal reference's, peak dB (reported, not checked)")
    print(f"{'rate Hz':>8} {'tone Hz':>8} {'drive dB':>9} {'ideal':>8} {'ADAA':>8} {'diff':>7}")
    for row in harmonics(rates, HARMONIC_FREQUENCIES, drives, factor=factor, numbers=(1,)):
        print(f"{row.fs:8g} {row.frequency:8.1f} {row.drive:9g} {row.ideal:8.2f} {row.adaa:8.2f} "
              f"{row.adaa - row.ideal:7.2f}")
    return all(row.within for row in rows) and all(row.within for row in bands)


# --- Colour ---------------------------------------------------------------------------------------

# VCA's Colour mapping, from the Gain Reduction applied to a channel, in dB: the residual's weight is Colour times
# VCA_COLOUR_WEIGHT × (1 - e^(-GR / VCA_COLOUR_WEIGHT_DB)), and the drive is Drive's top × (1 - e^(-GR /
# VCA_COLOUR_DRIVE_DB)). Each is exactly 0 with no Gain Reduction and rises smoothly with it, so Colour follows the
# compression with no corner. USAGE.md gives the reasons for each constant.
VCA_COLOUR_WEIGHT = 0.11
VCA_COLOUR_WEIGHT_DB = 10.0
VCA_COLOUR_DRIVE_DB = 5.0


@dataclass
class ColourAt:
    """A Character's Colour at a Gain Reduction: the residual's weight, Colour included, the drive in dB, and the
    tracking in dB, COLOUR_THRESHOLD_REFERENCE_DB less Threshold, which the shape is driven by too."""
    weight: float
    drive: float
    tracking: float = 0.0

    @property
    def gain(self):
        """The total gain the compressed signal is shaped at, G: the drive's and the tracking's."""
        return 10 ** ((self.drive + self.tracking) / 20)


def _rising(reduction_db, scale_db):
    """1 - e^(-GR / scale): exactly 0 with no Gain Reduction, nearing 1 as it grows."""
    return -math.expm1(-reduction_db / scale_db) if reduction_db > 0 else 0.0


# FET's Colour mapping, VCA's form with its own constants, steeper: FET_COLOUR_WEIGHT, FET_COLOUR_WEIGHT_DB and
# FET_COLOUR_DRIVE_DB play VCA's parts. USAGE.md gives the reasons.
FET_COLOUR_WEIGHT = 0.25
FET_COLOUR_WEIGHT_DB = 10.0
FET_COLOUR_DRIVE_DB = 4.0

# Opto's Colour mapping, VCA's form with its own constants, the gentlest of the three: OPTO_COLOUR_WEIGHT,
# OPTO_COLOUR_WEIGHT_DB and OPTO_COLOUR_DRIVE_DB play VCA's parts. USAGE.md gives the reasons.
OPTO_COLOUR_WEIGHT = 0.075
OPTO_COLOUR_WEIGHT_DB = 10.0
OPTO_COLOUR_DRIVE_DB = 5.0

# Each Character with Colour: its weight, the Gain Reduction its weight rises over and the one its drive rises over.
COLOUR_MAPPINGS = {"vca": (VCA_COLOUR_WEIGHT, VCA_COLOUR_WEIGHT_DB, VCA_COLOUR_DRIVE_DB),
                   "fet": (FET_COLOUR_WEIGHT, FET_COLOUR_WEIGHT_DB, FET_COLOUR_DRIVE_DB),
                   "opto": (OPTO_COLOUR_WEIGHT, OPTO_COLOUR_WEIGHT_DB, OPTO_COLOUR_DRIVE_DB)}

# Colour's shape is driven relative to Threshold: the compressed signal is shaped at the drive plus this less
# Threshold, in dB, so a steady Gain Reduction at a Ratio gives the same Colour at every Threshold. It is the default
# Threshold, so at the defaults the drive alone sets it. USAGE.md gives the reasons.
COLOUR_THRESHOLD_REFERENCE_DB = -18.0


def colour_at(settings, reduction_db):
    """The Character's Colour at a Gain Reduction in dB, or None for a Character with none (Clean)."""
    if settings.character not in COLOUR_MAPPINGS:
        return None
    weight, weight_db, drive_db = COLOUR_MAPPINGS[settings.character]
    return ColourAt(settings.colour / 100 * weight * _rising(reduction_db, weight_db),
                    DRIVE_RANGE[1] * _rising(reduction_db, drive_db),
                    COLOUR_THRESHOLD_REFERENCE_DB - settings.threshold)


def _compressed(x, reduction_db):
    gain = 10 ** (-reduction_db / 20)
    return [gain * s for s in x]


def coloured(x, reduction_db, colour, residual_of):
    """Colour at the base rate: each sample y of x times the steady Gain Reduction, plus the weight times the residual
    of y at the total gain G, over G, w · r(G · y) / G (residual_of, one call per sample, in order), so the linear
    part passes sample-aligned (ADR 0005)."""
    compressed = _compressed(x, reduction_db)
    if colour.weight == 0:
        return compressed
    g = colour.gain
    return [y + colour.weight * residual_of(g * y) / g for y in compressed]


def oversampled_coloured(x, reduction_db, colour, factor):
    """Colour in High Quality at factor: x times the steady Gain Reduction, through the After section (ADR 0009), its
    residual w · r(G · u) / G worked out at the oversampled rate, with ADAA at that rate. With a weight of 0 it adds
    exactly nothing, and the section is a plain delay."""
    compressed = _compressed(x, reduction_db)
    g = colour.gain
    adaa = AntiAliased()
    return oversampled_section(compressed, lambda u: colour.weight * adaa(g * u) / g if colour.weight else 0.0, factor)


# The steady Gain Reductions `colour` reports, in dB.
COLOUR_REDUCTIONS = (0, 3, 6, 10, 20, 30)
# The steady sine `colour` measures the harmonics of: the bin-centred tone nearest 1 kHz at 48 kHz.
COLOUR_TONE = (1000, 48000)
# Where the steady Gain Reduction is 0 the sine sits this far under the Knee's lower edge, as the Transfer Curve's grid
# starts: well clear of it, so a detector reading a hair high still asks for none.
COLOUR_BELOW_KNEE_DB = 12.0
# The Engine's harmonics are within this many dB of the lab's (USAGE.md gives the reasons).
COLOUR_HARMONIC_TOLERANCE_DB = 0.1
# The Gain Reductions Colour's aliasing is checked at, in dB: from where Colour is first clearly heard to deep
# compression. Only ALIASING_TOLERANCE's ceiling is checked; its improvement doesn't hold for Colour (USAGE.md).
COLOUR_ALIAS_REDUCTIONS = (10, 20, 30)


def colour_level(settings, reduction_db):
    """The steady sine's level in dB for a Gain Reduction: the input level whose Gain Reduction it is on the Transfer
    Curve, or COLOUR_BELOW_KNEE_DB under the Knee for none. None when the settings never reach it."""
    if reduction_db == 0:
        return settings.threshold - settings.knee / 2 - COLOUR_BELOW_KNEE_DB
    return level_for_reduction(settings, reduction_db)


@dataclass
class ColourRow:
    """Colour at a steady Gain Reduction: the sine's level, the Colour, and its 2nd to 5th harmonics in dB relative to
    the fundamental (-inf when absent), and their THD in %."""
    reduction: float
    level: float
    colour: ColourAt
    harmonics: list
    thd: float


def colour_spectrum(x, reduction_db, colour, factor=1):
    """The spectrum of x coloured at a steady Gain Reduction: at the base rate, or at factor in the After section."""
    if factor == 1:
        return spectrum(coloured(x, reduction_db, colour, AntiAliased()))
    return spectrum(oversampled_coloured(x, reduction_db, colour, factor))


def colour_row(settings, reduction_db, k, factor=1):
    """Colour on the bin-centred tone k at the level whose Gain Reduction is reduction_db, held steady, at factor."""
    level = colour_level(settings, reduction_db)
    if level is None:
        return None
    colour = colour_at(settings, reduction_db)
    amplitudes = colour_spectrum(tone(k, level), reduction_db, colour, factor)
    relative = [amplitudes[h * k] / amplitudes[k] for h in HARMONICS]
    thd = 100 * math.sqrt(sum(a * a for a in relative))
    return ColourRow(reduction_db, level, colour, [20 * math.log10(a) if a > 1e-15 else -math.inf for a in relative],
                     thd)


@dataclass
class ColourAlias:
    """Colour's worst alias at one condition, in dB relative to the fundamental: the naive shaper's and ADAA's, at the
    base rate; oversampled, ADAA's at factor too, anywhere and below the passband edge. Checked as Saturation's rows
    are (Alias), but for the improvement on naive at the base rate, which is shown and not checked."""
    fs: float
    frequency: float  # the tone's, bin-centred
    reduction: float
    naive: float
    adaa: float
    factor: int = 1
    oversampled: float = None
    passband: float = None

    @property
    def within(self):
        if self.factor > 1:
            return (self.passband <= ALIASING_TOLERANCE.passband_ceilings[self.factor]
                    and self.oversampled <= self.adaa)
        return self.adaa <= ALIASING_TOLERANCE.ceiling


def colour_aliases(settings, rates=ALIAS_RATES, frequencies=ALIAS_FREQUENCIES, reductions=COLOUR_ALIAS_REDUCTIONS,
                   factor=1):
    """Colour's worst alias at each condition the settings reach, at the base rate and, when factor is over 1,
    oversampled by it."""
    rows = []
    for fs in rates:
        for frequency in frequencies:
            k = tone_bin(frequency, fs)
            for reduction in reductions:
                level = colour_level(settings, reduction)
                if level is None:
                    continue
                colour = colour_at(settings, reduction)
                x = tone(k, level)
                naive = worst_alias_db(spectrum(coloured(x, reduction, colour, residual)), k)
                adaa = worst_alias_db(colour_spectrum(x, reduction, colour), k)
                row = ColourAlias(fs, k * fs / ALIAS_SAMPLES, reduction, naive, adaa, factor)
                if factor > 1:
                    amplitudes = colour_spectrum(x, reduction, colour, factor)
                    row.oversampled = worst_alias_db(amplitudes, k)
                    row.passband = worst_passband_alias_db(amplitudes, k)
                rows.append(row)
    return rows


def colour_options(parser):
    transfer_options(parser)
    character_option(parser)
    parser.add_argument("--colour", type=float, metavar="%")
    parser.add_argument("--rates", type=float, nargs="+", default=list(ALIAS_RATES), metavar="Hz")
    parser.add_argument("--oversampling", type=int, default=1, choices=OVERSAMPLING_FACTORS, metavar="N",
                        help="the factor: 1 (the base rate, the default), 2, 4 or 8")


def _check_factor(factor, rates):
    """Raises ValueError when factor is over the cap at one of rates."""
    for fs in rates:
        if factor not in allowed_factors(fs):
            raise ValueError(f"Oversampling {factor}x is over the cap at {fs:g} Hz: "
                             f"{MAX_INTERNAL_RATE_HZ} Hz allows up to {max_oversampling(fs)}x there")


@command("colour", options=colour_options)
def run_colour(args):
    _check_rates(args.rates)
    _check_factor(args.oversampling, args.rates)
    if not report_colour(settings_from(args), args.rates, args.oversampling):
        raise SystemExit(1)


def _shown_db(value):
    return f"{value:8.2f}" if value > -math.inf else f"{'absent':>8}"


def report_colour(settings, rates=ALIAS_RATES, factor=1):
    """How the Character's Colour follows the Gain Reduction, then Colour's worst alias at each condition, at the base
    rate or oversampled by factor. True when every aliasing row is within its tolerance."""
    at = "" if factor == 1 else f", Oversampling {factor}x"
    print(f"Colour, {_transfer_heading(settings)}, Colour {settings.colour:g}%{at}")
    if colour_at(settings, 10.0) is None:
        print(f"{settings.character.capitalize()} has no Colour, at any setting")
        return True
    k = tone_bin(*COLOUR_TONE)
    print(f"a steady bin-centred sine of {k * COLOUR_TONE[1] / ALIAS_SAMPLES:.1f} Hz at {COLOUR_TONE[1]:g} Hz at the "
          f"level whose Gain Reduction it is, held steady (or {COLOUR_BELOW_KNEE_DB:g} dB under the Knee for none); "
          f"harmonics in dB relative to the fundamental; tolerance for the Engine's ±{COLOUR_HARMONIC_TOLERANCE_DB:g} dB")
    print(f"the shape driven at the drive plus the tracking, {_shown_tracking(settings)} dB "
          f"({COLOUR_THRESHOLD_REFERENCE_DB:g} dB less Threshold)"
          + ("" if factor == 1 else f"; the residual oversampled {factor}x in the After section"))
    print(f"{'GR dB':>6} {'input dB':>9} {'weight':>9} {'drive dB':>9} {'2nd':>8} {'3rd':>8} {'4th':>8} {'5th':>8} "
          f"{'THD %':>8}")
    for reduction in COLOUR_REDUCTIONS:
        row = colour_row(settings, reduction, k, factor)
        if row is None:
            print(f"{reduction:6g}  not reached at these settings")
            continue
        print(f"{reduction:6g} {row.level:9.2f} {row.colour.weight:9.6f} {row.colour.drive:9.4f} "
              f"{' '.join(_shown_db(h) for h in row.harmonics)} {row.thd:8.4f}")
    tolerance = ALIASING_TOLERANCE
    print(f"Colour aliasing, bin-centred tones at the level whose Gain Reduction it is, over {ALIAS_SAMPLES} samples: "
          f"the worst alias in dB relative to the fundamental")
    rows = colour_aliases(settings, rates, factor=factor)
    if factor == 1:
        print(f"tolerance: ADAA at most {tolerance.ceiling:g} dB; the improvement on naive is shown, not checked "
              f"(USAGE.md, \"Colour\")")
        print(f"{'rate Hz':>8} {'tone Hz':>8} {'GR dB':>6} {'input dB':>9} {'naive':>8} {'ADAA':>8} {'better':>8}  "
              f"check")
        for row in rows:
            print(f"{row.fs:8g} {row.frequency:8.1f} {row.reduction:6g} {colour_level(settings, row.reduction):9.2f} "
                  f"{row.naive:8.2f} {row.adaa:8.2f} {row.naive - row.adaa:8.2f}  "
                  f"{'within' if row.within else 'outside'}")
    else:
        oversampled = f"{factor}x"
        print(f"Oversampling {factor}x, the residual alone through the up and down filters (ADR 0009). Tolerance: below "
              f"the passband edge ({OVERSAMPLING_PASSBAND_EDGE:g} of Nyquist) at most "
              f"{tolerance.passband_ceilings[factor]:g} dB, and anywhere no worse than ADAA at 1x; better: {factor}x's "
              f"improvement on naive at 1x")
        print(f"{'rate Hz':>8} {'tone Hz':>8} {'GR dB':>6} {'input dB':>9} {'naive':>8} {'1x':>8} {oversampled:>8} "
              f"{'passband':>9} {'better':>8}  check")
        for row in rows:
            print(f"{row.fs:8g} {row.frequency:8.1f} {row.reduction:6g} {colour_level(settings, row.reduction):9.2f} "
                  f"{row.naive:8.2f} {row.adaa:8.2f} {row.oversampled:8.2f} {row.passband:9.2f} "
                  f"{row.naive - row.oversampled:8.2f}  {'within' if row.within else 'outside'}")
    return all(row.within for row in rows)


def _shown_tracking(settings):
    return f"{COLOUR_THRESHOLD_REFERENCE_DB - settings.threshold:g}"


def oversampling_options(parser):
    parser.add_argument("--rates", type=float, nargs="+", default=None, metavar="Hz",
                        help="default: plugkit.conf's SAMPLE_RATES")


@command("oversampling", options=oversampling_options)
def run_oversampling(args):
    sample_rates = supported_rates() if args.rates is None else args.rates
    _check_rates(sample_rates)
    if not report_oversampling(sample_rates):
        raise SystemExit(1)


def report_oversampling(sample_rates):
    """At each rate, the allowed factors and, for each, its stages' taps, the up and down filters' delay and padding,
    and the cascade's measured stopband and passband edge. True when every pair fits within
    OVERSAMPLING_SECTION_SAMPLES."""
    factors = ", ".join(f"{factor}x" for factor in OVERSAMPLING_FACTORS)
    print(f"Oversampling: {factors}, the internal rate at most {MAX_INTERNAL_RATE_HZ} Hz (ADR 0001)")
    print(f"each 2x stage a Kaiser half-band (beta {KAISER_BETA:.4f}), passband edge {OVERSAMPLING_PASSBAND_EDGE:g} of "
          f"Nyquist, stopband {OVERSAMPLING_STOPBAND_DB:g} dB; each section {OVERSAMPLING_SECTION_SAMPLES} host samples")
    print(f"{'rate Hz':>8} {'factor':>7} {'taps':>12} {'delay':>6} {'internal':>9} {'padding':>8} {'stopband dB':>12} "
          f"{'passband Hz':>12}  check")
    fits = True
    for fs in sample_rates:
        allowed = ", ".join(f"{factor}x" for factor in allowed_factors(fs))
        print(f"{fs:8g} allows {allowed}")
        for factor in allowed_factors(fs):
            filters = pair(factor)
            fits = fits and filters.padding >= 0
            taps = "+".join(str(len(h)) for h in stages(factor)) or "-"
            stopband = f"{measured_stopband_db(factor):12.2f}" if factor > 1 else f"{'-':>12}"
            passband = f"{measured_passband_edge(factor) * fs:12.1f}" if factor > 1 else f"{'-':>12}"
            named = f"{factor}x"
            print(f"{fs:8g} {named:>7} {taps:>12} {filters.delay:6d} {filters.internal_padding:9d} "
                  f"{filters.padding:8d} {stopband} {passband}  {'fits' if filters.padding >= 0 else 'overruns'}")
    print("delay: the up and down filters' together, in host samples, the internal padding (in samples at the "
          "oversampled rate) rounding it up to whole ones; padding: what is left to the section's")
    return fits


# --- Limiter --------------------------------------------------------------------------------------

# comp1's own: Pro-C 3 has no output Limiter whose range it could follow. The top is full scale, and the default,
# so the Limiter switched on at its default stops only overs; 30 dB down leaves room for a Ceiling far under any
# delivery spec's peak level, for stems and for headroom kept on purpose, with a span the slider can still set
# finely.
CEILING_RANGE = (-30, 0)
CEILING_DEFAULT = 0.0

# The true-peak estimator, High Quality's: an 8x interpolator, a windowed-sinc polyphase FIR, TRUE_PEAK_TAPS host
# samples long, Kaiser-windowed with TRUE_PEAK_BETA. Its cutoff is the host's Nyquist, so its phase 0 is the host
# sample itself and the estimate never reads under the sample peak; each other phase is normalised to unity gain at
# DC. It reads half its length ahead of the sample it estimates, so it delays by TRUE_PEAK_TAPS / 2 host samples at
# every rate, taken from the lookahead window, so the Limiter reserve and the latency stay as they are. Chosen from
# `limiter`'s full-band table (USAGE.md gives the reasons): the fewest phases, then the fewest taps, that keep white
# noise's and full-band noise's true peaks FULL_BAND_MARGIN_DB under the Ceiling at LIMITER_TRUE_PEAK_HEADROOM_DB.
TRUE_PEAK_FACTOR = 8
TRUE_PEAK_TAPS = 48
TRUE_PEAK_BETA = 6.0
# The reference the estimator is measured against, and the Limiter's output: 16x, 64 host samples long, Kaiser
# β 10, flat to within 0.001 dB up to 20 kHz at 44.1 kHz.
REFERENCE_PEAK_FACTOR = 16
REFERENCE_PEAK_TAPS = 64
REFERENCE_PEAK_BETA = 10.0
# High Quality's Limiter aims this far under the Ceiling, so the estimator's worst under-read can't take the true
# peak over it.
LIMITER_TRUE_PEAK_HEADROOM_DB = 0.3
# The one release both modes' Limiters return with, one pole in dB, chosen from `limiter`'s trade-off table.
LIMITER_RELEASE_MS = 200.0
# Under this many dB, the Limiter's Gain Reduction counts as none and the audio passes bit for bit: a gain this
# close to 1 moves no float sample.
LIMITER_SETTLED_DB = 1e-7


def bessel_i0(x):
    """The modified Bessel function of the first kind, order 0, by its power series."""
    total = term = 1.0
    k = 0
    while term > 1e-17 * total:
        k += 1
        term *= (x / (2 * k)) ** 2
        total += term
    return total


def interpolator(factor, taps, beta):
    """A polyphase interpolator by factor: for each phase p from 1 to factor - 1, the taps coefficients of
    x[m + j], j from 1 - taps / 2 to taps / 2, that give the signal at m + p / factor. A Kaiser-windowed sinc with its
    cutoff at the host's Nyquist, each phase normalised to unity gain at DC. Phase 0 is x[m] itself."""
    half = taps // 2
    phases = []
    for p in range(1, factor):
        t = [p / factor - j for j in range(1 - half, half + 1)]
        window = [bessel_i0(beta * math.sqrt(max(0.0, 1 - (u / half) ** 2))) / bessel_i0(beta) for u in t]
        kernel = [math.sin(math.pi * u) / (math.pi * u) * w for u, w in zip(t, window)]
        total = sum(kernel)
        phases.append([c / total for c in kernel])
    return phases


def estimator_delay():
    """The true-peak estimator's delay in host samples, the same at every rate."""
    return TRUE_PEAK_TAPS // 2


def lookahead_window(fs):
    """High Quality's lookahead window at fs in host samples: the Limiter reserve less the estimator's delay, so the
    two fill the reserve exactly."""
    return _whole_samples(LIMITER_RESERVE_MS, fs) - estimator_delay()


def _segment_peaks(channels, phases, taps):
    """Per host sample m, the loudest of every channel's x[m] and its interpolated values up to m + 1, the signal
    silent outside its length."""
    half = taps // 2
    length = len(channels[0])
    peaks = [0.0] * length
    for x in channels:
        padded = [0.0] * half + list(x) + [0.0] * half
        for m in range(length):
            window = padded[m + 1:m + 1 + taps]
            peak = max([abs(x[m])] + [abs(sum(map(operator.mul, c, window))) for c in phases])
            peaks[m] = max(peaks[m], peak)
    return peaks


def reference_true_peak(channels):
    """The 16x reference's true peak of every channel, linear: silence before the signal, and up to the last sample
    whose interpolation reads none past its end, where the audio would go on."""
    segments = _segment_peaks(channels, interpolator(REFERENCE_PEAK_FACTOR, REFERENCE_PEAK_TAPS, REFERENCE_PEAK_BETA),
                              REFERENCE_PEAK_TAPS)
    return max(segments[:len(segments) - REFERENCE_PEAK_TAPS // 2])


def estimated_true_peak(channels):
    """The true-peak estimator's reading of every channel, linear, over the whole signal."""
    return max(_segment_peaks(channels, interpolator(TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS, TRUE_PEAK_BETA), TRUE_PEAK_TAPS))


def _db(gain):
    return 20 * math.log10(gain) if gain > 0 else -math.inf


def _over_db(peak, ceiling):
    """The Gain Reduction in dB that brings peak to ceiling: 0 when it is under it."""
    return 20 * math.log10(peak / ceiling) if peak > ceiling else 0.0


def _gain(reduction_db):
    return 10 ** (-reduction_db / 20) if reduction_db > 0 else 1.0


def _settled(reduction_db):
    return 0.0 if reduction_db < LIMITER_SETTLED_DB else reduction_db


def limit_zero_latency(channels, ceiling_db, fs, release_ms=LIMITER_RELEASE_MS):
    """Zero Latency's Limiter, with no delay: a zero-attack gain computer on the loudest channel's sample peak, which
    brings each sample to the Ceiling at once and releases with one pole in dB, then a safety clip at the Ceiling.
    Returns the output and each sample's Gain Reduction in dB."""
    ceiling = 10 ** (ceiling_db / 20)
    pole = _pole(release_ms, fs)
    reduction = 0.0
    out = [[] for _ in channels]
    reductions = []
    for n in range(len(channels[0])):
        need = _over_db(max(abs(x[n]) for x in channels), ceiling)
        reduction = _settled(max(need, pole * reduction))
        gain = _gain(reduction)
        for x, y in zip(channels, out):
            y.append(max(-ceiling, min(ceiling, gain * x[n])))
        reductions.append(reduction)
    return out, reductions


def limit_high_quality(channels, ceiling_db, fs, release_ms=LIMITER_RELEASE_MS):
    """High Quality's Limiter, delaying by the Limiter reserve: the true-peak estimator on every channel, aiming
    LIMITER_TRUE_PEAK_HEADROOM_DB under the Ceiling, then over the lookahead window W, a sliding maximum of the Gain
    Reduction asked for over W + 1 samples, the release (one pole in dB), and their average over W + 1 samples, so the
    gain is fully down when a peak arrives in the audio, delayed by the estimator's delay plus W. Each sample's
    estimate is the loudest of its own segment and the one before, so a peak between two samples brings both down.
    Returns the output and each output sample's Gain Reduction in dB."""
    ceiling = 10 ** ((ceiling_db - LIMITER_TRUE_PEAK_HEADROOM_DB) / 20)
    pole = _pole(release_ms, fs)
    delay, window = estimator_delay(), lookahead_window(fs)
    span = window + 1
    segments = _segment_peaks(channels, interpolator(TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS, TRUE_PEAK_BETA), TRUE_PEAK_TAPS)
    length = len(segments)
    need = [_over_db(max(segments[q], segments[q - 1] if q > 0 else 0.0), ceiling) for q in range(length)]
    released = []
    reductions = []
    last = 0.0
    for q in range(length):
        held = max(need[max(0, q - window):q + 1])
        last = _settled(max(held, pole * last))
        released.append(last)
        recent = released[max(0, q - window):q + 1]
        reductions.append(sum(recent) / span if any(recent) else 0.0)
    total = delay + window
    heard = [0.0] * delay + reductions[:length - delay]  # output sample n takes the reduction worked out at n - delay
    out = [[0.0] * total + [x[n - total] * _gain(heard[n]) for n in range(total, length)] for x in channels]
    return out, heard


LIMITERS = {"zero latency": limit_zero_latency, "high quality": limit_high_quality}


# --- Limiter: test signals --------------------------------------------------------------------------

# Each test signal of the true-peak figures is this many host samples long, and every tone and the noise fade in
# over LIMITER_FADE_MS on a raised cosine, so they are band-limited from their first sample: an abrupt start has
# content up to Nyquist, where no interpolator, the reference's included, can say where the true peak lies.
LIMITER_SIGNAL_SAMPLES = 2048
LIMITER_FADE_MS = 1.0
# The band-limited noise: NOISE_TONES sines, spaced evenly in log frequency over NOISE_BAND Hz, each at the same
# level, their phases from the seeded generator, scaled so its sample peak reads the level asked for.
NOISE_TONES = 64
NOISE_BAND = (20, 20000)
# The full-scale burst: BURST_START samples of silence, then a 1 kHz sine starting at its peak, at full scale. It
# starts abruptly, a transient, but at 1 kHz its content is far below Nyquist.
BURST_START = 256
BURST_FREQUENCY = 1000
# How far over the Ceiling the reference set's tones and noise are pushed, in dB.
LIMITER_PUSH_DB = 12.0


def _fade_in(fs, n):
    samples = LIMITER_FADE_MS * fs / 1000
    return 1.0 if n >= samples else 0.5 - 0.5 * math.cos(math.pi * n / samples)


def limiter_tone(frequency, level_db, fs, length=LIMITER_SIGNAL_SAMPLES):
    """A sine at frequency, its peak at level_db, faded in, from a phase of 0.3 radians."""
    amplitude = 10 ** (level_db / 20)
    return [amplitude * _fade_in(fs, n) * math.sin(2 * math.pi * frequency * n / fs + 0.3) for n in range(length)]


def band_limited_noise(level_db, fs, seed=REFERENCE_SEED, length=LIMITER_SIGNAL_SAMPLES):
    """Seeded noise band-limited to NOISE_BAND, its sample peak at level_db, faded in."""
    white = _white(seed)
    low, high = NOISE_BAND
    frequencies = [low * (high / low) ** (k / (NOISE_TONES - 1)) for k in range(NOISE_TONES)]
    phases = [math.pi * next(white) for _ in frequencies]
    noise = [sum(math.sin(2 * math.pi * f * n / fs + p) for f, p in zip(frequencies, phases)) * _fade_in(fs, n)
             for n in range(length)]
    scale = 10 ** (level_db / 20) / max(abs(x) for x in noise)
    return [x * scale for x in noise]


def full_scale_burst(fs, length=LIMITER_SIGNAL_SAMPLES):
    """Silence for BURST_START samples, then BURST_FREQUENCY Hz at full scale from its peak."""
    return [0.0 if n < BURST_START else math.cos(2 * math.pi * BURST_FREQUENCY * (n - BURST_START) / fs)
            for n in range(length)]


def reference_set(ceiling_db, fs):
    """The Limiter's reference signals at a Ceiling, as (name, one channel) pairs."""
    over = ceiling_db + LIMITER_PUSH_DB
    return [("full-scale burst", full_scale_burst(fs)),
            ("10 kHz tone", limiter_tone(10000, over, fs)),
            ("19 kHz tone", limiter_tone(19000, over, fs)),
            ("noise", band_limited_noise(over, fs))]


# --- Limiter: the estimator's under-read ------------------------------------------------------------

# The band-limited test signals the estimator is measured on: a tone at each frequency and noise from each seed,
# at full scale, this many samples long.
UNDER_READ_FREQUENCIES = (1000, 5000, 10000, 15000, 19000, 20000)
UNDER_READ_SEEDS = (1, 2, 3)
UNDER_READ_SAMPLES = 1024


@dataclass
class UnderRead:
    """The estimator's worst under-read at a rate, in dB against the 16x reference, and the signal it was on."""
    fs: float
    db: float
    signal: str


def under_read(fs):
    """The estimator's worst under-read at fs over the band-limited test signals: how far its reading of each
    signal's true peak is under the reference's, in dB."""
    tones = [(f"{f / 1000:g} kHz tone", limiter_tone(f, 0.0, fs, UNDER_READ_SAMPLES)) for f in UNDER_READ_FREQUENCIES]
    noises = [(f"noise, seed {seed}", band_limited_noise(0.0, fs, seed, UNDER_READ_SAMPLES))
              for seed in UNDER_READ_SEEDS]
    signals = tones + noises
    worst = None
    for name, signal in signals:
        reading = _db(reference_true_peak([signal])) - _db(estimated_true_peak([signal]))
        if worst is None or reading > worst.db:
            worst = UnderRead(fs, reading, name)
    return worst


# --- Limiter: full-band signals ------------------------------------------------------------------------

# White noise and differenced white noise (most of its power near Nyquist), from each seed, at full scale, this many
# samples long, each pushed each of FULL_BAND_PUSHES_DB over the Ceiling: unlike the band-limited test signals, they
# have content right up to Nyquist, where the estimator under-reads most. The candidates are the estimator designs,
# (factor, taps), the table compares, at LIMITER_TRUE_PEAK_HEADROOM_DB. The chosen design keeps every true peak at
# least FULL_BAND_MARGIN_DB under the Ceiling, a margin for seeds and material the table doesn't try.
FULL_BAND_SEEDS = (1, 2, 3, 7)
FULL_BAND_SAMPLES = 2048
FULL_BAND_PUSHES_DB = (0, 12, 36)
FULL_BAND_CANDIDATES = ((4, 16), (4, 48), (8, 32), (8, 40), (8, 48), (16, 48))
FULL_BAND_MARGIN_DB = 0.1


def full_band_signals(seed, length=FULL_BAND_SAMPLES):
    """White noise from seed, and the same differenced and halved, as (name, one channel) pairs, each within full
    scale."""
    white = _white(seed)
    noise = [next(white) for _ in range(length + 1)]
    return [(f"white noise, seed {seed}", noise[:length]),
            (f"differenced noise, seed {seed}", [(noise[n + 1] - noise[n]) / 2 for n in range(length)])]


def full_band_over(fs, factor=None, taps=None, seeds=FULL_BAND_SEEDS, pushes=FULL_BAND_PUSHES_DB):
    """High Quality's Limiter's worst true peak at fs on the full-band signals, in dB against the Ceiling, and the
    signal and push it was on, with the estimator's factor and taps (TRUE_PEAK_FACTOR and TRUE_PEAK_TAPS by
    default)."""
    global TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS
    chosen = TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS
    TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS = factor or chosen[0], taps or chosen[1]
    try:
        worst = (-math.inf, "")
        for seed in seeds:
            for name, signal in full_band_signals(seed):
                for push in pushes:
                    gain = 10 ** (push / 20)
                    out, _ = limit_high_quality([[gain * x for x in signal]], 0.0, fs)
                    over = _db(reference_true_peak(out))
                    if over > worst[0]:
                        worst = (over, f"{name}, {push:g} dB over")
        return worst
    finally:
        TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS = chosen


# --- Limiter: peaks and timing ------------------------------------------------------------------------

@dataclass
class Peaks:
    """A reference signal's output peaks through one mode's Limiter, each in dB against the Ceiling."""
    signal: str
    mode: str
    sample: float
    true: float


def limiter_peaks(ceiling_db, fs):
    """Each reference signal's output peaks through each mode's Limiter at fs."""
    rows = []
    for name, signal in reference_set(ceiling_db, fs):
        for mode, limit in LIMITERS.items():
            out, _ = limit([signal], ceiling_db, fs)
            rows.append(Peaks(name, mode, _db(max(abs(x) for x in out[0])) - ceiling_db,
                              _db(reference_true_peak(out)) - ceiling_db))
    return rows


# The timing signal: a quiet level, the full-scale burst for BURST_MS, then the quiet level again, long enough for
# the release, through a Ceiling of TIMING_CEILING_DB, so the burst is limited by about 12 dB. The quiet level
# alternates in sign at TIMING_QUIET_DB, far under the Ceiling, so each sample's Gain Reduction reads off it.
TIMING_CEILING_DB = -12.0
TIMING_QUIET_DB = -40.0
BURST_MS = 50.0
TIMING_RELEASE_MS = 600.0


@dataclass
class Timing:
    """One mode's Limiter on the timing signal at fs: in dB, the Gain Reduction on the burst's first
    sample in the audio heard and the most it reaches over the burst; in ms, the release's 63% and 90%, counted from
    the first quiet sample heard after the burst."""
    fs: float
    mode: str
    first: float
    most: float
    release_63: float
    release_90: float


def timing_signal(fs):
    """The quiet level, BURST_START samples of it, the burst for BURST_MS, then the quiet level for
    TIMING_RELEASE_MS; and the burst's first and last samples."""
    quiet = 10 ** (TIMING_QUIET_DB / 20)
    start, end = BURST_START, BURST_START + round(BURST_MS * fs / 1000)
    length = end + round(TIMING_RELEASE_MS * fs / 1000)
    signal = [(quiet if n % 2 == 0 else -quiet) if n < start or n >= end
              else math.cos(2 * math.pi * BURST_FREQUENCY * (n - start) / fs) for n in range(length)]
    return signal, start, end


def limiter_timing(mode, fs):
    """mode's Limiter's timing on the timing signal at fs, as Timing."""
    signal, start, end = timing_signal(fs)
    out, _ = LIMITERS[mode]([signal], TIMING_CEILING_DB, fs)
    delay = latency_of_limiter(mode, fs)
    heard = [_db(abs(signal[n - delay])) - _db(abs(out[0][n])) for n in range(delay, len(signal))]
    most = max(heard[start:end])
    times = []
    for fraction in (ONE_TIME_CONSTANT, NINETY_PERCENT):
        n = next(n for n in range(end, len(heard)) if heard[end - 1] - heard[n] >= fraction * heard[end - 1])
        times.append((n - end + 1) * 1000 / fs)
    return Timing(fs, mode, heard[start], most, *times)


def latency_of_limiter(mode, fs):
    """The Limiter's delay in host samples: the Limiter reserve in High Quality, none in Zero Latency."""
    return _whole_samples(LIMITER_RESERVE_MS, fs) if mode == "high quality" else 0


# --- Limiter: release ---------------------------------------------------------------------------------

# The releases the trade-off table compares, in ms.
RELEASE_CANDIDATES = (20, 50, 100, 200, 500, 1000)
# The trade-off's two figures. Recovery: after a 6 dB limit, the time until the Gain Reduction is back under
# RECOVERED_DB, about the smallest change in level a listener notices on music. Distortion: THD+N of a bin-centred
# tone near 50 Hz, pushed 6 dB over the Ceiling, measured once it has settled, where the gain ripples most between
# the tone's peaks; LIMITER_DISTORTION_LIMIT is the most a limiter on a bass note may add before it is heard as
# fuzz rather than as level.
TRADE_OFF_LIMIT_DB = 6.0
RECOVERED_DB = 1.0
DISTORTION_RATE = 48000
DISTORTION_SAMPLES = 16384
DISTORTION_BIN = 17  # 49.8 Hz at 48 kHz over DISTORTION_SAMPLES
LIMITER_DISTORTION_LIMIT = 0.01


@dataclass
class ReleaseTradeOff:
    """A release's recovery time in ms and each mode's distortion on the 50 Hz tone, a fraction."""
    release: float
    recovery: float
    distortion: dict


def recovery_ms(release_ms):
    """The time in ms the Gain Reduction takes to fall from TRADE_OFF_LIMIT_DB to under RECOVERED_DB, one pole in
    dB."""
    return release_ms * math.log(TRADE_OFF_LIMIT_DB / RECOVERED_DB)


def distortion(mode, release_ms):
    """THD+N of the tone near 50 Hz, TRADE_OFF_LIMIT_DB over a 0 dB Ceiling, through mode's Limiter at release_ms:
    the power that isn't the tone's against the tone's, as an amplitude ratio, over a second length, the first
    having let the gain settle."""
    n_samples, k = DISTORTION_SAMPLES, DISTORTION_BIN
    amplitude = 10 ** (TRADE_OFF_LIMIT_DB / 20)
    tone = [amplitude * math.sin(2 * math.pi * k * n / n_samples) for n in range(2 * n_samples)]
    out, _ = LIMITERS[mode]([tone], 0.0, DISTORTION_RATE, release_ms)
    y = out[0][n_samples:]
    real = sum(v * math.cos(2 * math.pi * k * n / n_samples) for n, v in enumerate(y))
    imaginary = sum(v * math.sin(2 * math.pi * k * n / n_samples) for n, v in enumerate(y))
    tone_power = (2 * math.hypot(real, imaginary) / n_samples) ** 2 / 2
    total_power = sum(v * v for v in y) / n_samples
    return math.sqrt(max(total_power - tone_power, 0.0) / tone_power)


def release_trade_off(candidates=RELEASE_CANDIDATES):
    return [ReleaseTradeOff(r, recovery_ms(r), {mode: distortion(mode, r) for mode in LIMITERS}) for r in candidates]


# --- Commands -------------------------------------------------------------------------------------

def _ratio(text):
    if text.strip().lower() in ("inf", "∞"):
        return math.inf
    return float(text)


# The compressor's options default to None, given or not: settings_from fills each one left out from the Style's
# defaults (--style), or from Settings' when no Style is given.
def transfer_options(parser):
    parser.add_argument("--threshold", type=float, metavar="dB")
    parser.add_argument("--ratio", type=_ratio, metavar="N|inf")
    parser.add_argument("--knee", type=float, metavar="dB")
    parser.add_argument("--range", type=float, metavar="dB")


def timing_options(parser):
    transfer_options(parser)
    parser.add_argument("--attack", type=float, metavar="ms")
    parser.add_argument("--release", type=float, metavar="ms")
    parser.add_argument("--hold", type=float, metavar="ms")
    parser.add_argument("--rates", type=float, nargs="+", default=list(SAMPLE_RATES), metavar="Hz")
    parser.add_argument("--lookahead", type=float, default=LOOKAHEAD_DEFAULT, metavar="ms")


def character_option(parser):
    """--character, and --style, whose defaults the other options start from."""
    parser.add_argument("--character", choices=CHARACTERS)
    parser.add_argument("--style", choices=STYLE_DEFAULTS,
                        help="start from a Character Style's defaults (STYLE_DEFAULTS); the other options override them")


def transfer_command_options(parser):
    transfer_options(parser)
    character_option(parser)


def step_options(parser):
    timing_options(parser)
    character_option(parser)
    parser.add_argument("--auto-release", action="store_true", default=None)
    parser.add_argument("--external", action="store_true")
    parser.add_argument("--over", type=float, default=STEP_DB, metavar="dB",
                        help=f"how far over Threshold the step goes ({STEP_DB:g} dB by default)")


def _check_rates(rates):
    if any(fs <= 0 for fs in rates):
        raise ValueError("every sample rate must be above 0 Hz")


def settings_from(args):
    """The Settings the parsed args name, checked against their ranges: the Style's defaults (--style), or Settings'
    without one, with each option given in place of its default."""
    style = getattr(args, "style", None)
    defaults = Settings() if style is None else STYLE_DEFAULTS[style].settings
    settings = replace(defaults, **{field.name: getattr(args, field.name) for field in fields(Settings)
                                    if getattr(args, field.name, None) is not None})
    check_ranges(settings)
    return settings


def _character_heading(settings):
    """The Character, when it isn't Clean, which every heading leaves unnamed."""
    return "" if settings.character == "clean" else f"{settings.character.upper()}, "


def _transfer_heading(settings):
    return (f"{_character_heading(settings)}Threshold {settings.threshold:g} dB, Ratio {_shown(settings.ratio)}:1, Knee {settings.knee:g} dB, "
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


# FET's Transfer Curve is measured rather than worked out: its loop runs at each steady level, at FET_TRANSFER_RATE and
# the default Attack, from no Gain Reduction until the Gain Reduction moves less than FET_SETTLED_DB in a sample. What
# is left to go is then under 1e-9 dB, far under the 0.01 dB the curve is printed to.
FET_TRANSFER_RATE = 48000
FET_SETTLED_DB = 1e-12


def steady_output(level_db, settings):
    """The Character's steady output level in dB for a steady input of level_db: the Transfer Curve, or on FET, its
    loop run to steady state."""
    if settings.character not in FED_BACK:
        return transfer(level_db, settings)
    detector = Detector(settings, FET_TRANSFER_RATE, level_db=level_db)
    previous = 0.0
    while True:
        reduction = detector.hear_steady(level_db)
        if abs(reduction - previous) < FET_SETTLED_DB:
            return level_db - reduction
        previous = reduction


def transfer_curve(settings):
    """The Transfer Curve on its grid, always including Threshold, both Knee edges, and the level where the
    Gain Reduction reaches Range when that falls on the grid. On FET, each level's output is measured
    (steady_output)."""
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
        rows.append((level, steady_output(level, settings), region))

    def slope(lower):
        return (steady_output(lower + GRID_SPACING, settings) - steady_output(lower, settings)) / GRID_SPACING

    return TransferCurve(rows, slope(lowest), slope(highest - GRID_SPACING))


@command("transfer", options=transfer_command_options)
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
    if settings.character == "vca":
        print(f"levels are VCA's RMS Detection Level, sine-calibrated: the steady test signal is a square wave whose "
              f"peak is {SINE_CALIBRATION_DB:.4f} dB under each level")
    if settings.character in FED_BACK:
        clean = replace(settings, character="clean")
        largest = max(abs(out - transfer(level, clean)) for level, out, _ in curve.rows)
        print(f"measured: {settings.character.upper()}'s loop run at each level to steady state, at {FET_TRANSFER_RATE:g} Hz and Attack "
              f"{settings.attack:g} ms; it is at most {largest:.1e} dB from Clean's Transfer Curve")


@command("step", options=step_options)
def run_step(args):
    settings = settings_from(args)
    check_range("lookahead", args.lookahead, LOOKAHEAD_RANGE, " ms")
    _check_rates(args.rates)
    if args.over <= 0:
        raise ValueError("the step must go over Threshold: --over above 0 dB")
    report_step(settings, args.rates, args.lookahead, args.external, args.over)


def report_step(settings, sample_rates=SAMPLE_RATES, lookahead=LOOKAHEAD_DEFAULT, external=False, over=STEP_DB):
    """The step response's times at each sample rate, for a level step from STEP_DB under Threshold to over dB over it:
    the detection signal's own, or with external, the Sidechain's on an External Detection Source while the main stays
    steady."""
    around = f"±{STEP_DB:g} dB around" if over == STEP_DB else f"from {STEP_DB:g} dB under to {over:g} dB over"
    print(f"Step {around} Threshold, {_transfer_heading(settings)}, {_timing_heading(settings)}, "
          f"Auto Release {'on' if settings.auto_release else 'off'}, Lookahead {lookahead:g} ms")
    if external:
        print("External Detection Source: the Sidechain steps while the main stays steady, so the Gain Reduction never "
              "reaches the detector")
    print("times in ms; Attack counts from the step in the delayed audio the listener hears, which the detector meets "
          "Lookahead earlier; Hold counts from when the detector meets the step down, and Release from when it "
          "begins, after Hold; 63% is 1 - 1/e")
    print(f"{'rate Hz':>8} {'attack 63%':>11} {'attack 90%':>11} {'release 63%':>12} {'release 90%':>12} {'hold':>9}")
    if settings.character == "opto":
        print(f"Opto: the step down follows {OPTO_STEP_HOLD_MS:g} ms at the loud level (OPTO_STEP_HOLD_MS), from steady "
              f"state under Threshold")
    for fs in sample_rates:
        step = step_response(settings, fs, lookahead, external, loud=settings.threshold + over)
        if step is None:
            print(f"{fs:8g}  no Gain Reduction to measure at Ratio 1:1 or Range 0 dB")
            continue
        print(f"{fs:8g} {step.attack_63:11.4f} {step.attack_90:11.4f} {step.release_63:12.4f} "
              f"{step.release_90:12.4f} {step.hold:9.4f}")


# The Ratios and Attacks `speedup` compares FET's times with Clean's at, and the partial step's levels: from
# PARTIAL_LOUD_DB over Threshold to PARTIAL_QUIET_DB over it and back, both above the Knee at the defaults, so the loop
# speeds Release up too.
SPEEDUP_RATIOS = (1.5, 2, 4, 8, 20, math.inf)
SPEEDUP_ATTACKS = (Settings().attack, 0.5, FET_REFERENCE_ATTACK_MS)
SPEEDUP_RATE = 48000
PARTIAL_LOUD_DB, PARTIAL_QUIET_DB = 20.0, 10.0


def speedup_options(parser):
    parser.add_argument("--rates", type=float, nargs="+", default=[SPEEDUP_RATE], metavar="Hz")


@command("speedup", options=speedup_options)
def run_speedup(args):
    _check_rates(args.rates)
    report_speedup(args.rates)


def report_speedup(sample_rates=(SPEEDUP_RATE,)):
    """FET's step times beside Clean's at each Ratio and Attack, and the speed-up at 63%; then the partial step's,
    whose Release the loop speeds up too."""
    print(f"FET's speed-up against Clean, capped at {FET_SPEEDUP_MAX:g}x (FET_SPEEDUP_MAX); the defaults otherwise; "
          f"times in ms, as `step` measures them")
    print(f"{'rate Hz':>8} {'attack ms':>10} {'ratio':>6} {'clean 63%':>10} {'clean 90%':>10} {'FET 63%':>9} "
          f"{'FET 90%':>9} {'speed-up':>9}")
    for fs in sample_rates:
        for attack in SPEEDUP_ATTACKS:
            for ratio in SPEEDUP_RATIOS:
                clean = step_response(Settings(attack=attack, ratio=ratio), fs)
                fet = step_response(Settings(attack=attack, ratio=ratio, character="fet"), fs)
                print(f"{fs:8g} {attack:10g} {_shown(ratio):>6} {clean.attack_63:10.4f} {clean.attack_90:10.4f} "
                      f"{fet.attack_63:9.4f} {fet.attack_90:9.4f} {clean.attack_63 / fet.attack_63:9.2f}")
    print(f"the partial step: from {PARTIAL_LOUD_DB:g} dB over Threshold to {PARTIAL_QUIET_DB:g} dB over it, above the "
          f"Knee, then back; Release {Settings().release:g} ms, its times counted from when it begins")
    print(f"{'rate Hz':>8} {'ratio':>6} {'clean 63%':>10} {'clean 90%':>10} {'FET 63%':>9} {'FET 90%':>9} "
          f"{'speed-up':>9}")
    for fs in sample_rates:
        for ratio in SPEEDUP_RATIOS:
            levels = {"quiet": Settings().threshold + PARTIAL_QUIET_DB, "loud": Settings().threshold + PARTIAL_LOUD_DB}
            clean = step_response(Settings(ratio=ratio), fs, **levels)
            fet = step_response(Settings(ratio=ratio, character="fet"), fs, **levels)
            print(f"{fs:8g} {_shown(ratio):>6} {clean.release_63:10.4f} {clean.release_90:10.4f} "
                  f"{fet.release_63:9.4f} {fet.release_90:9.4f} {clean.release_63 / fet.release_63:9.2f}")


# The step sizes and Attacks `overshoot` prints Opto's attack at: steps from STEP_DB under Threshold to these over it.
OVERSHOOT_STEPS_DB = (5, 10, 20, 30)
OVERSHOOT_ATTACKS = (Settings().attack, 1.0)


def overshoot_options(parser):
    parser.add_argument("--rates", type=float, nargs="+", default=[SPEEDUP_RATE], metavar="Hz")


@command("overshoot", options=overshoot_options)
def run_overshoot(args):
    _check_rates(args.rates)
    report_overshoot(args.rates)


def report_overshoot(sample_rates=(SPEEDUP_RATE,)):
    """Opto's Attack times against the step's size, beside FET's, whose loop and cap Opto shares without the
    material-dependent attack."""
    print(f"Opto's attack against the step's size: steps from {STEP_DB:g} dB under Threshold to each size over it; the "
          f"defaults otherwise; times in ms, as `step` measures them")
    print(f"Attack's time is scaled by the overshoot, the Transfer Curve's Gain Reduction less the Gain Reduction held: "
          f"x{OPTO_ATTACK_SLOWEST:g} at 0 dB, x1 at {OPTO_ATTACK_REFERENCE_DB:g} dB, x{OPTO_ATTACK_FASTEST:g} from "
          f"{OPTO_ATTACK_SPAN_DB:g} dB; 'scale' is the step's first sample's")
    print(f"{'rate Hz':>8} {'attack ms':>10} {'over dB':>8} {'GR dB':>7} {'scale':>6} {'FET 63%':>9} {'FET 90%':>9} "
          f"{'Opto 63%':>9} {'Opto 90%':>9}")
    for fs in sample_rates:
        for attack in OVERSHOOT_ATTACKS:
            for over in OVERSHOOT_STEPS_DB:
                loud = Settings().threshold + over
                fet = step_response(Settings(attack=attack, character="fet"), fs, loud=loud)
                opto = step_response(Settings(attack=attack, character="opto"), fs, loud=loud)
                reduction = gain_reduction(loud, Settings())
                print(f"{fs:8g} {attack:10g} {over:8g} {reduction:7.2f} {opto_attack_scale(reduction):6.3f} "
                      f"{fet.attack_63:9.4f} {fet.attack_90:9.4f} {opto.attack_63:9.4f} {opto.attack_90:9.4f}")


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
    character_option(parser)


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


def after_burst(settings, fs, length_ms, external=False, quiet=None, loud=None):
    """A detector that has heard the Detection Level STEP_DB over Threshold (loud, in dB, when given) for length_ms (a
    sample at least) from steady state STEP_DB under it (quiet), the run grid starting with the burst."""
    quiet = settings.threshold - STEP_DB if quiet is None else quiet
    loud = settings.threshold + STEP_DB if loud is None else loud
    detector = Detector(settings, fs, gain_reduction(quiet, settings), level_db=quiet, external=external)
    for _ in range(max(round(length_ms * fs / 1000), 1)):
        detector.hear_steady(loud)
    return detector


def burst_release(settings, fs, length_ms):
    """Holds the Detection Level STEP_DB over Threshold for length_ms from steady state STEP_DB under it, the run
    grid starting with the burst, then drops it back, and measures the Release that follows. None when there is no
    Gain Reduction to measure (Ratio 1:1 or Range 0 dB)."""
    quiet, loud = settings.threshold - STEP_DB, settings.threshold + STEP_DB
    low = gain_reduction(quiet, settings)
    if gain_reduction(loud, settings) - low < 1e-9:
        return None
    detector = after_burst(settings, fs, length_ms)
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
    character_option(parser)
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
    if settings.character == "opto":
        print(f"Opto releases in two parts: {OPTO_FAST_SHARE:.0%} at Release (Auto Release's when on), the rest in "
              f"{OPTO_SLOW_FRESH_MS:g} to {OPTO_SLOW_SEASONED_MS:g} ms as its history grows; the history charges in "
              f"{OPTO_HISTORY_CHARGE_MS:g} ms, discharges in {OPTO_HISTORY_DISCHARGE_MS:g} ms")
    print("times in ms, counted from when Release begins, after Hold; 63% is 1 - 1/e")
    print(f"{'rate Hz':>8} {'burst ms':>9} {'off 63%':>10} {'off 90%':>10} {'on 63%':>10} {'on 90%':>10}")
    for fs in sample_rates:
        rows = bursts(settings, fs, lengths)
        if not rows:
            print(f"{fs:8g}  no Gain Reduction to measure at Ratio 1:1 or Range 0 dB")
        for burst in rows:
            print(f"{fs:8g} {burst.length:9g} {burst.off.release_63:10.4f} {burst.off.release_90:10.4f} "
                  f"{burst.on.release_63:10.4f} {burst.on.release_90:10.4f}")


def limiter_options(parser):
    parser.add_argument("--ceiling", type=float, default=CEILING_DEFAULT, metavar="dB")
    parser.add_argument("--rates", type=float, nargs="+", default=list(SAMPLE_RATES), metavar="Hz")


@command("limiter", options=limiter_options)
def run_limiter(args):
    check_range("ceiling", args.ceiling, CEILING_RANGE, " dBFS")
    _check_rates(args.rates)
    if not report_limiter(args.ceiling, args.rates):
        raise SystemExit(1)


def report_limiter(ceiling_db, sample_rates=SAMPLE_RATES):
    """The Limiter's figures at a Ceiling: per rate its parts, the estimator's under-read, each reference signal's
    peaks and the timing; then the release's trade-off. True when every peak holds the Ceiling and the headroom
    covers every under-read."""
    print(f"Limiter, Ceiling {ceiling_db:g} dBFS, release {LIMITER_RELEASE_MS:g} ms (LIMITER_RELEASE_MS) in both modes")
    print(f"High Quality: a {TRUE_PEAK_FACTOR}x true-peak estimator, {TRUE_PEAK_TAPS} taps (TRUE_PEAK_TAPS), then a "
          f"lookahead window; the two fill the Limiter reserve, and it aims {LIMITER_TRUE_PEAK_HEADROOM_DB:g} dB under "
          f"the Ceiling (LIMITER_TRUE_PEAK_HEADROOM_DB)")
    print("in host samples, and the estimator's worst under-read in dB against the 16x reference, on band-limited "
          "tones and noise")
    print(f"{'rate Hz':>8} {'reserve':>8} {'estimator':>10} {'window':>7} {'release':>8} {'headroom':>9} "
          f"{'under-read':>11}  signal")
    held = True
    for fs in sample_rates:
        worst = under_read(fs)
        covered = worst.db <= LIMITER_TRUE_PEAK_HEADROOM_DB
        held = held and covered
        print(f"{fs:8g} {_whole_samples(LIMITER_RESERVE_MS, fs):8d} {estimator_delay():10d} {lookahead_window(fs):7d} "
              f"{round(LIMITER_RELEASE_MS * fs / 1000):8d} {LIMITER_TRUE_PEAK_HEADROOM_DB:9g} {worst.db:11.3f}  "
              f"{worst.signal}{'' if covered else ', over the headroom'}")
    print("High Quality's worst true peak on the full-band signals, in dB against the Ceiling, per estimator "
          f"(factor x taps); the chosen one, {TRUE_PEAK_FACTOR}x{TRUE_PEAK_TAPS}, keeps {FULL_BAND_MARGIN_DB:g} dB "
          "under (FULL_BAND_MARGIN_DB)")
    print(f"{'rate Hz':>8} " + " ".join(f"{f'{f}x{t}':>9}" for f, t in FULL_BAND_CANDIDATES) + "  chosen's worst signal")
    for fs in sample_rates:
        cells = []
        for factor, taps in FULL_BAND_CANDIDATES:
            over, signal = full_band_over(fs, factor, taps)
            if (factor, taps) == (TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS):
                held = held and over <= -FULL_BAND_MARGIN_DB
                worst_signal = signal + ('' if over <= -FULL_BAND_MARGIN_DB else ', within the margin')
            cells.append(f"{over:9.3f}")
        print(f"{fs:8g} " + " ".join(cells) + f"  {worst_signal}")
    print(f"each reference signal's output peaks in dB against the Ceiling, the tones and noise pushed "
          f"{LIMITER_PUSH_DB:g} dB over it; true peak by the 16x reference")
    print(f"{'rate Hz':>8} {'signal':>17} {'mode':>13} {'sample peak':>12} {'true peak':>10}")
    for fs in sample_rates:
        for row in limiter_peaks(ceiling_db, fs):
            # Zero Latency promises the sample peak, High Quality the true peak.
            promised = row.sample if row.mode == "zero latency" else row.true
            within = promised <= 0.0
            held = held and within
            print(f"{fs:8g} {row.signal:>17} {row.mode:>13} {row.sample:12.3f} {row.true:10.3f}"
                  f"{'' if within else '  over the Ceiling'}")
    print(f"the timing signal: the full-scale burst between quiet levels, Ceiling {TIMING_CEILING_DB:g} dBFS; Gain "
          f"Reduction in dB on the burst's first sample heard and the most over it; the release in ms, counted from "
          f"the first quiet sample heard")
    print(f"{'rate Hz':>8} {'mode':>13} {'first':>8} {'most':>8} {'release 63%':>12} {'release 90%':>12}")
    for fs in sample_rates:
        for mode in LIMITERS:
            t = limiter_timing(mode, fs)
            print(f"{fs:8g} {mode:>13} {t.first:8.3f} {t.most:8.3f} {t.release_63:12.4f} {t.release_90:12.4f}")
    print(f"release trade-off, at {DISTORTION_RATE:g} Hz: recovery from a {TRADE_OFF_LIMIT_DB:g} dB limit to under "
          f"{RECOVERED_DB:g} dB, and THD+N on a {DISTORTION_BIN * DISTORTION_RATE / DISTORTION_SAMPLES:.1f} Hz tone "
          f"{TRADE_OFF_LIMIT_DB:g} dB over the Ceiling; the release is the shortest within "
          f"{100 * LIMITER_DISTORTION_LIMIT:g}% in both modes")
    print(f"{'release ms':>11} {'recovery ms':>12} {'zero latency %':>15} {'high quality %':>15}")
    for row in release_trade_off():
        print(f"{row.release:11g} {row.recovery:12.1f} {100 * row.distortion['zero latency']:15.3f} "
              f"{100 * row.distortion['high quality']:15.3f}{'  chosen' if row.release == LIMITER_RELEASE_MS else ''}")
    return held


# --- Gate -------------------------------------------------------------------------------------------

# The Gate's held level is the Detection Level held over GATE_LEVEL_HOLD_MS: the loudest of the last
# GATE_LEVEL_HOLD_MS of samples and the sample itself, so a low tone's cycle, whose level falls to nothing at each zero
# crossing, keeps the held level up. It opens when the held level reaches Gate Threshold, and closes once it is
# GATE_HYSTERESIS_DB under it, so a Detection Level wavering about Gate Threshold doesn't flip it to and fro. Both
# chosen from `gate`'s trade-off table: the shortest level hold that keeps every trade-off signal steady with some hysteresis, then
# the smallest hysteresis that does (USAGE.md gives the reasons).
GATE_LEVEL_HOLD_MS = 10.0
GATE_HYSTERESIS_DB = 6.0
# Under this many dB, the Gate's Gain Reduction counts as none and the audio passes bit for bit, as the Limiter's
# does (LIMITER_SETTLED_DB).
GATE_SETTLED_DB = LIMITER_SETTLED_DB


@dataclass
class Gate:
    """The Gate's controls: on or off, Gate Threshold and Gate Range in dB, Gate Attack, Gate Release and Gate Hold in
    ms. Its ranges are the compressor's own (GATE_PARAMETERS)."""
    on: bool = False
    threshold: float = -40.0
    range: float = 20.0
    attack: float = 1.0
    release: float = 100.0
    hold: float = 20.0


# Each Gate field's range, the compressor's of the same name, and the unit it's shown in.
GATE_PARAMETERS = {
    "threshold": (THRESHOLD_RANGE, " dB"),
    "range": (RANGE_RANGE, " dB"),
    "attack": (ATTACK_RANGE, " ms"),
    "release": (RELEASE_RANGE, " ms"),
    "hold": (HOLD_RANGE, " ms"),
}


def check_gate(gate):
    """Raises ValueError naming the first Gate setting outside its range, and the range."""
    for name, (value_range, unit) in GATE_PARAMETERS.items():
        check_range(f"gate {name}", getattr(gate, name), value_range, unit)


class GateDetector:
    """The Gate on one detection signal: the held level, the Gate's state, open or closed, and its Gain Reduction in dB.

    The held level is the loudest Detection Level of the sample and the level_hold_ms before it. The Gate opens when the
    held level reaches Gate Threshold, and closes once it falls hysteresis_db under it. Open, it asks for no Gain
    Reduction; closed, exactly Gate Range. Gate Hold keeps it open for Gate Hold ms after it would close, before Gate
    Release begins; a held level back above the closing level restarts the count. Gate Attack is how fast it opens and
    Gate Release how fast it closes: each one pole in dB, covering 1 - 1/e of the move in its time, as the compressor's
    Detector does. Settings are held fixed here; the Engine glides them a run at a time (RUN_SAMPLES).

    External with no Sidechain connected leaves the Gate open."""

    def __init__(self, gate, fs, gain_reduction=0.0, is_open=True, level_hold_ms=GATE_LEVEL_HOLD_MS,
                 hysteresis_db=GATE_HYSTERESIS_DB):
        self.gate = gate
        self.attack = _pole(gate.attack, fs)
        self.release = _pole(gate.release, fs)
        self.hold = round(gate.hold * fs / 1000)
        self.level_hold = round(level_hold_ms * fs / 1000)
        self.closing_db = gate.threshold - hysteresis_db
        self.window = []  # (sample number, level) pairs, each louder than every later one: their loudest first
        self.n = 0
        self.is_open = is_open
        self.held = 0
        self.gain_reduction = gain_reduction

    def held_level(self, level_db):
        """The held level after one more sample at level_db: the loudest of it and the level_hold samples before it."""
        self.n += 1
        while self.window and self.window[-1][1] <= level_db:
            self.window.pop()
        self.window.append((self.n, level_db))
        if self.window[0][0] < self.n - self.level_hold:
            self.window.pop(0)
        return self.window[0][1]

    def process(self, level_db):
        """Takes one sample's Detection Level in dB, or None for no Sidechain, and returns the Gain Reduction in
        dB (0 or more)."""
        if level_db is None:
            self.is_open, self.held = True, 0
        else:
            held = self.held_level(level_db)
            if held >= self.gate.threshold:
                self.is_open, self.held = True, 0
            elif held >= self.closing_db:
                self.held = 0
            elif self.is_open:
                if self.held < self.hold:
                    self.held += 1
                else:
                    self.is_open = False
        wanted = 0.0 if self.is_open else self.gate.range
        pole = self.attack if wanted < self.gain_reduction else self.release
        self.gain_reduction = pole * self.gain_reduction + (1 - pole) * wanted
        if self.gain_reduction < GATE_SETTLED_DB:
            self.gain_reduction = 0.0
        return self.gain_reduction

    def hear_steady(self, level_db):
        """One sample of a steady Detection Level: its Detection Level is the peak level, the level itself."""
        return self.process(level_db)


@dataclass
class GateStep:
    """The Gate's step response's times in ms: when Gate Release begins after the detection signal falls (Gate Hold plus
    the level hold), Gate Release's 63% and 90% from then, and Gate Attack's 63% and 90% after it rises; and the closed
    depth in dB."""
    release_begins: float
    release_63: float
    release_90: float
    attack_63: float
    attack_90: float
    depth: float


def gate_step(gate, fs):
    """Steps the detection signal from STEP_DB over Gate Threshold to STEP_DB under it, then back, each from steady
    state, and measures the closing and the opening. None at Gate Range 0 dB, with nothing to measure."""
    if gate.range <= 0:
        return None
    quiet, loud = gate.threshold - STEP_DB, gate.threshold + STEP_DB
    ms = 1000 / fs
    fractions = (ONE_TIME_CONSTANT, NINETY_PERCENT)
    closing = GateDetector(gate, fs)
    for _ in range(closing.level_hold + 1):
        closing.process(loud)
    (release_63, release_90), held = _times(closing, quiet, 0.0, gate.range, fractions)
    opening = GateDetector(gate, fs, gate.range, is_open=False)
    for _ in range(opening.level_hold + 1):
        opening.process(quiet)
    (attack_63, attack_90), _ = _times(opening, loud, gate.range, 0.0, fractions)
    # Settled: 30 Gate Release time constants leave e^-30 of the move, far under any figure printed.
    for _ in range(round(30 * gate.release * fs / 1000)):
        depth = closing.process(quiet)
    return GateStep(held * ms, (release_63 - held) * ms, (release_90 - held) * ms, attack_63 * ms, attack_90 * ms,
                    depth)


# --- Gate: the level hold's and the hysteresis's trade-off -------------------------------------------------

# The trade-off's candidates, and its signals: 20 Hz and 50 Hz tones, the lowest a detection signal commonly carries,
# and pink-like noise from the reference signal's seed, each GATE_RIPPLE_ABOVE_DB over Gate Threshold (the noise at
# the power of a sine whose peak reads that level, as the reference signal's is), at the Gate's defaults with Gate
# Hold 0, so the level hold and hysteresis alone hold it open. The ripple is the Gain Reduction's peak-to-peak over the second half of
# GATE_RIPPLE_SECONDS at GATE_TRADE_OFF_RATE: 0 when the Gate stays open throughout.
GATE_LEVEL_HOLD_CANDIDATES = (0, 5, 10, 15, 20, 25)
GATE_HYSTERESIS_CANDIDATES = (0, 2, 4, 6, 8)
GATE_RIPPLE_FREQUENCIES = (20, 50)
GATE_RIPPLE_ABOVE_DB = 0.1
GATE_RIPPLE_SECONDS = 0.3
GATE_TRADE_OFF_RATE = 48000
# How late the Gate starts closing: on a stop, the level hold; on a held level fading out at GATE_FADE_DB_PER_S (a note
# dying away over a second), the level hold plus the time the fade takes to cross the hysteresis.
GATE_FADE_DB_PER_S = 60.0


def _level_db(x):
    return 20 * math.log10(abs(x)) if x != 0 else -math.inf


def gate_trade_off_signals(gate, fs=GATE_TRADE_OFF_RATE):
    """The trade-off's signals, as (name, Detection Levels in dB) pairs."""
    length = round(GATE_RIPPLE_SECONDS * fs)
    level = gate.threshold + GATE_RIPPLE_ABOVE_DB
    amplitude = 10 ** (level / 20)
    signals = [(f"{f} Hz", [amplitude * math.sin(2 * math.pi * f * n / fs) for n in range(length)])
               for f in GATE_RIPPLE_FREQUENCIES]
    pink = _pink(_white(REFERENCE_SEED))
    noise = [next(pink) for _ in range(length)]
    scale = math.sqrt(10 ** (level / 10) / 2 / (sum(x * x for x in noise) / length))
    signals.append(("noise", [x * scale for x in noise]))
    return [(name, [_level_db(x) for x in signal]) for name, signal in signals]


@dataclass
class GateTradeOff:
    """One level hold and hysteresis: the ripple in dB on each trade-off signal, and in ms how late the Gate starts
    closing on a stop and on a fade."""
    level_hold: float
    hysteresis: float
    ripple: dict
    stop: float
    fade: float

    @property
    def steady(self):
        return all(r == 0 for r in self.ripple.values())


def _closing_late_ms(gate, fs, level_hold_ms, hysteresis_db, levels_db):
    """The ms from the first of levels_db under Gate Threshold until Gate Release begins, from steady state open."""
    detector = GateDetector(replace(gate, hold=0.0), fs, level_hold_ms=level_hold_ms, hysteresis_db=hysteresis_db)
    for _ in range(detector.level_hold + 1):
        detector.process(gate.threshold + STEP_DB)
    under = next(n for n, level in enumerate(levels_db) if level < gate.threshold)
    for n, level in enumerate(levels_db):
        if detector.process(level) > 0:
            return (n - under) * 1000 / fs
    return math.inf


def gate_trade_off(gate=Gate(), fs=GATE_TRADE_OFF_RATE):
    """The trade-off at every candidate level hold and hysteresis."""
    timed = replace(gate, hold=0.0)
    signals = gate_trade_off_signals(gate, fs)
    fade_seconds = ((STEP_DB + max(GATE_HYSTERESIS_CANDIDATES) + 1) / GATE_FADE_DB_PER_S
                    + max(GATE_LEVEL_HOLD_CANDIDATES) / 500)
    fade_samples = round(fade_seconds * fs)
    fade = [gate.threshold + STEP_DB - GATE_FADE_DB_PER_S * n / fs for n in range(fade_samples)]
    stop = [gate.threshold - STEP_DB] * round(max(GATE_LEVEL_HOLD_CANDIDATES) * 2 * fs / 1000)
    rows = []
    for level_hold in GATE_LEVEL_HOLD_CANDIDATES:
        for hysteresis in GATE_HYSTERESIS_CANDIDATES:
            ripple = {}
            for name, levels in signals:
                detector = GateDetector(timed, fs, level_hold_ms=level_hold, hysteresis_db=hysteresis)
                reductions = [detector.process(level) for level in levels][len(levels) // 2:]
                ripple[name] = max(reductions) - min(reductions)
            rows.append(GateTradeOff(level_hold, hysteresis, ripple,
                                     _closing_late_ms(gate, fs, level_hold, hysteresis, stop),
                                     _closing_late_ms(gate, fs, level_hold, hysteresis, fade)))
    return rows


def chosen_gate_constants(rows):
    """The shortest level hold at which some hysteresis keeps every signal steady, and the smallest such hysteresis."""
    steady = [row for row in rows if row.steady]
    level_hold = min(row.level_hold for row in steady)
    return level_hold, min(row.hysteresis for row in steady if row.level_hold == level_hold)


def gate_options(parser):
    defaults = Gate()
    parser.add_argument("--gate-threshold", type=float, default=defaults.threshold, metavar="dB")
    parser.add_argument("--gate-range", type=float, default=defaults.range, metavar="dB")
    parser.add_argument("--gate-attack", type=float, default=defaults.attack, metavar="ms")
    parser.add_argument("--gate-release", type=float, default=defaults.release, metavar="ms")
    parser.add_argument("--gate-hold", type=float, default=defaults.hold, metavar="ms")
    parser.add_argument("--rates", type=float, nargs="+", default=list(SAMPLE_RATES), metavar="Hz")


@command("gate", options=gate_options)
def run_gate(args):
    gate = Gate(True, args.gate_threshold, args.gate_range, args.gate_attack, args.gate_release, args.gate_hold)
    check_gate(gate)
    _check_rates(args.rates)
    report_gate(gate, args.rates)


def report_gate(gate, sample_rates=SAMPLE_RATES):
    """The Gate's step response at each sample rate, then the level hold's and hysteresis's trade-off."""
    print(f"Gate step ±{STEP_DB:g} dB around Gate Threshold, Gate Threshold {gate.threshold:g} dB, Gate Range "
          f"{gate.range:g} dB, Gate Attack {gate.attack:g} ms, Gate Release {gate.release:g} ms, Gate Hold "
          f"{gate.hold:g} ms; level hold {GATE_LEVEL_HOLD_MS:g} ms (GATE_LEVEL_HOLD_MS), hysteresis "
          f"{GATE_HYSTERESIS_DB:g} dB (GATE_HYSTERESIS_DB)")
    print("times in ms; the closing counts from the detection signal's step down, Gate Release from when it begins, "
          "after Gate Hold and the level hold; the opening from its step up; 63% is 1 - 1/e; depth in dB, closed and "
          "settled")
    print(f"{'rate Hz':>8} {'release begins':>15} {'release 63%':>12} {'release 90%':>12} {'attack 63%':>11} "
          f"{'attack 90%':>11} {'depth':>8}")
    for fs in sample_rates:
        step = gate_step(gate, fs)
        if step is None:
            print(f"{fs:8g}  no Gain Reduction to measure at Gate Range 0 dB")
            continue
        print(f"{fs:8g} {step.release_begins:15.4f} {step.release_63:12.4f} {step.release_90:12.4f} "
              f"{step.attack_63:11.4f} {step.attack_90:11.4f} {step.depth:8.4f}")
    rows = gate_trade_off(gate)
    chosen = (GATE_LEVEL_HOLD_MS, GATE_HYSTERESIS_DB)
    print(f"level hold and hysteresis trade-off at {GATE_TRADE_OFF_RATE:g} Hz, Gate Hold 0 ms: the Gain Reduction's "
          f"ripple in dB, peak to peak, on each signal {GATE_RIPPLE_ABOVE_DB:g} dB over Gate Threshold; how late in ms "
          f"the Gate starts closing on a stop and on a fade at {GATE_FADE_DB_PER_S:g} dB/s")
    names = list(rows[0].ripple)
    print(f"{'level hold ms':>12} {'hysteresis dB':>14} " + " ".join(f"{name:>8}" for name in names)
          + f" {'stop ms':>8} {'fade ms':>8}")
    for row in rows:
        mark = "  chosen" if (row.level_hold, row.hysteresis) == chosen else ""
        print(f"{row.level_hold:12g} {row.hysteresis:14g} " + " ".join(f"{row.ripple[n]:8.3f}" for n in names)
              + f" {row.stop:8.2f} {row.fade:8.2f}{mark}")


# --- Comp -----------------------------------------------------------------------------------------------

# Comp, in %: from compressing nothing to the most its Style's Comp Curve goes to.
COMP_RANGE = (0, 100)


@dataclass(frozen=True)
class CompPoint:
    """A point a Comp Curve passes through: Comp in %, Threshold in dB and Ratio in :1."""
    comp: float
    threshold: float
    ratio: float


@dataclass(frozen=True)
class CompCurve:
    """A Style's Comp Curve: three points, at Comp 0%, the Style's default Comp and 100%, with Threshold and 1/Ratio
    each running in a straight line from one point to the next. The first point's Ratio is 1:1, so 0% compresses
    nothing; each point's Threshold is below the one before and its Ratio above, so each Threshold on the Comp Curve has one
    Comp. The middle point is the Style's default Threshold and Ratio, which the default Comp lands on exactly."""
    points: tuple


def _between(comp_curve, comp):
    """The two points comp (clamped to COMP_RANGE) falls between, and how far from the first to the second, 0 to 1."""
    comp = min(max(comp, COMP_RANGE[0]), COMP_RANGE[1])
    for a, b in zip(comp_curve.points, comp_curve.points[1:]):
        if comp <= b.comp:
            return a, b, (comp - a.comp) / (b.comp - a.comp)
    return comp_curve.points[-2], comp_curve.points[-1], 1.0


def _mix(a, b, u):
    """a and b mixed as (1 - u) a + u b: exactly a at u = 0, and exactly b at u = 1, so the Comp Curve meets its points."""
    return (1 - u) * a + u * b


def comp_threshold(comp_curve, comp):
    """The Threshold in dB the Comp Curve sets at comp (%)."""
    a, b, u = _between(comp_curve, comp)
    return _mix(a.threshold, b.threshold, u)


def comp_ratio(comp_curve, comp):
    """The Ratio the Comp Curve sets at comp (%), :1, gliding as 1/Ratio between points so ∞:1 (0) can be one."""
    a, b, u = _between(comp_curve, comp)
    inverse = _mix(1 / a.ratio, 1 / b.ratio, u)
    return inf if inverse == 0 else 1 / inverse


def comp_for_threshold(comp_curve, threshold):
    """The Comp (%) on the Comp Curve nearest a Threshold in dB: the one that sets it, clamped to the Comp Curve's span, so a
    Threshold above the Comp Curve's start is 0% and one below its end is 100%."""
    points = comp_curve.points
    if threshold >= points[0].threshold:
        return COMP_RANGE[0]
    for a, b in zip(points, points[1:]):
        if threshold >= b.threshold:
            return _mix(a.comp, b.comp, (a.threshold - threshold) / (a.threshold - b.threshold))
    return COMP_RANGE[1]


# The Comp Follow glide: how long turning Comp Follow on (or off) takes to move Threshold, Ratio and Auto Gain from the
# stored settings onto the Comp Curve (or back), in ms (ADR 0004: a fixed time, so playback reproduces it). Long enough
# that a move from the stored settings to the Comp Curve, which may be 30 dB of Threshold, is heard as compression easing
# over rather than a jump: six times the 50 ms every other setting glides over. Short enough to have arrived within a
# beat at 200 bpm, so grabbing Comp still answers at once.
COMP_FOLLOW_GLIDE_MS = 300.0


def follow_share(fraction):
    """How far along the Comp Follow glide is, 0 to 1, at fraction (0 to 1) of COMP_FOLLOW_GLIDE_MS: a smoothstep,
    3 f^2 - 2 f^3, whose slope is 0 at each end, so the glide starts and ends without a corner."""
    f = min(max(fraction, 0.0), 1.0)
    return f * f * (3 - 2 * f)


# --- Styles ---------------------------------------------------------------------------------------------

@dataclass
class StyleDefaults:
    """What choosing a Character Style writes (ADR 0002): the compressor's Settings, its Character among them, its
    default Comp in % (and Comp Follow on), the Gate's, and the Limiter on or off with its Ceiling. Beside them, the
    Style's Comp Curve, and how far its Auto Gain may let the reference signal's level move along it, in dB."""
    settings: Settings
    comp: float
    comp_curve: CompCurve
    gate: Gate = field(default_factory=Gate)
    limiter: bool = False
    ceiling: float = CEILING_DEFAULT
    auto_gain_tolerance: float = AUTO_GAIN_TOLERANCE


# FET's Auto Gain tolerance along its Comp Curve, in dB: its worst shortfall there (2.36 dB, at 100%), rounded up to the
# half dB. Its 0.5 ms Attack catches the reference noise's crests, which an estimate from the Transfer Curve can't see,
# so the reference signal comes out quieter, the more so the more it compresses. The fast Attack is what FET's Style is
# for, so it stays, and this is the cost until Auto Gain's estimate knows Attack.
FET_AUTO_GAIN_TOLERANCE = 2.5

# Each Character Style's defaults, in the Style order (ADR 0003), Characters first; the Source Styles add theirs with
# their own ticket (#26). Clean's are the lab's defaults, so a new instance, which plays Clean, holds its own Style's.
# Every Style writes the Gate and the Limiter off, at their defaults: each is a tool for the material, not a part of a
# Character. Every default Comp is 50%, the middle of the knob, with as much room to compress more as less. USAGE.md
# ("Styles" and "Comp") gives each row's reasons and its `step --style` and `compcurve` figures.
STYLE_DEFAULTS = {
    "clean": StyleDefaults(Settings(), 50.0,
                           CompCurve((CompPoint(0.0, 0.0, 1.0), CompPoint(50.0, -18.0, 4.0), CompPoint(100.0, -36.0, 8.0)))),
    "vca": StyleDefaults(Settings(character="vca", knee=3.0), 50.0,
                         CompCurve((CompPoint(0.0, 0.0, 1.0), CompPoint(50.0, -18.0, 4.0), CompPoint(100.0, -36.0, 10.0)))),
    "fet": StyleDefaults(Settings(character="fet", knee=3.0, attack=0.5, release=50.0), 50.0,
                         CompCurve((CompPoint(0.0, 0.0, 1.0), CompPoint(50.0, -18.0, 4.0), CompPoint(100.0, -28.0, 20.0))),
                         auto_gain_tolerance=FET_AUTO_GAIN_TOLERANCE),
    "opto": StyleDefaults(Settings(character="opto", ratio=3.0, knee=12.0), 50.0,
                          CompCurve((CompPoint(0.0, 0.0, 1.0), CompPoint(50.0, -18.0, 3.0), CompPoint(100.0, -42.0, 6.0)))),
}


def _on_off(on):
    return "on" if on else "off"


@command("styles")
def run_styles(args):
    report_styles()


def report_styles():
    """Each Character Style's Character and the defaults choosing it writes."""
    print("Each Character Style's Character and defaults: its default Comp in %, the compressor's (Threshold, Ratio, Knee and Range in dB or :1, "
          "Attack, Release and Hold in ms, Colour in %), the Gate's (on or off, Gate Threshold, Gate Range, Gate Attack, "
          "Gate Release, Gate Hold) and the Limiter's (on or off, Ceiling in dBFS)")
    print(f"{'style':<6} {'character':<9} {'comp':>4} {'threshold':>9} {'ratio':>5} {'knee':>5} {'attack':>6} {'release':>7} "
          f"{'auto release':>12} {'hold':>5} {'range':>5} {'colour':>6} {'gate':>5} {'threshold':>9} {'range':>5} "
          f"{'attack':>6} {'release':>7} {'hold':>5} {'limiter':>7} {'ceiling':>7}")
    for name, style in STYLE_DEFAULTS.items():
        s, g = style.settings, style.gate
        print(f"{name:<6} {s.character:<9} {style.comp:4g} {s.threshold:9g} {_shown(s.ratio):>5} {s.knee:5g} {s.attack:6g} "
              f"{s.release:7g} {_on_off(s.auto_release):>12} {s.hold:5g} {s.range:5g} {s.colour:6g} "
              f"{_on_off(g.on):>5} {g.threshold:9g} {g.range:5g} {g.attack:6g} {g.release:7g} {g.hold:5g} "
              f"{_on_off(style.limiter):>7} {style.ceiling:7g}")


@dataclass
class CompRow:
    """A point of a Style's Comp Curve, at its other defaults: Comp in %, the Threshold and Ratio there, the Auto Gain
    estimate, and the reference signal's level difference, output minus input, with that Auto Gain, in dB."""
    comp: float
    threshold: float
    ratio: float
    estimate: float
    difference: float


# The Comps compcurve prints the Comp Curve at, in %.
COMP_STEPS = tuple(range(COMP_RANGE[0], COMP_RANGE[1] + 1, 10))


@functools.lru_cache(maxsize=None)
def comp_curve_rows(name):
    """The Character Style name's Comp Curve at each of COMP_STEPS, at the Style's other defaults."""
    style = STYLE_DEFAULTS[name]
    rows = []
    for comp in COMP_STEPS:
        threshold, ratio = comp_threshold(style.comp_curve, comp), comp_ratio(style.comp_curve, comp)
        settings = replace(style.settings, threshold=threshold, ratio=ratio)
        rows.append(CompRow(comp, threshold, ratio, auto_gain(settings), level_difference(settings)))
    return rows


def compcurve_options(parser):
    parser.add_argument("--style", choices=STYLE_DEFAULTS, required=True)
    parser.add_argument("--tolerance", type=float, metavar="dB",
                        help="check against this tolerance in place of the Style's own")


@command("compcurve", options=compcurve_options)
def run_compcurve(args):
    if not report_compcurve(args.style, args.tolerance):
        raise SystemExit(1)


def report_compcurve(name, tolerance=None):
    """A Style's Comp Curve at each of COMP_STEPS, and the Auto Gain check along it. True when every point holds."""
    style = STYLE_DEFAULTS[name]
    tolerance = style.auto_gain_tolerance if tolerance is None else tolerance
    s = style.settings
    print(f"Comp Curve, {name}: Threshold and 1/Ratio straight between "
          + ", ".join(f"Comp {p.comp:g}% ({p.threshold:g} dB, {_shown(p.ratio)}:1)" for p in style.comp_curve.points)
          + f"; default Comp {style.comp:g}%")
    print(f"reference signal at {name}'s defaults (Knee {s.knee:g} dB, Attack {s.attack:g} ms, Release {s.release:g} ms, "
          f"Hold {s.hold:g} ms, Range {s.range:g} dB), Auto Gain on: output minus input, within ±{tolerance:g} dB")
    print(f"{'comp':>5} {'threshold':>9} {'ratio':>6} {'estimate':>8} {'level':>6}")
    held = True
    for row in comp_curve_rows(name):
        within = abs(row.difference) <= tolerance
        held = held and within
        print(f"{row.comp:4g}% {row.threshold:9.2f} {row.ratio:6.2f} {row.estimate:+8.4f} {row.difference:+6.2f} "
              f"{'within' if within else 'outside'}")
    print(f"Comp Follow glides on and off over {COMP_FOLLOW_GLIDE_MS:g} ms, on a smoothstep (follow_share)")
    return held


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
