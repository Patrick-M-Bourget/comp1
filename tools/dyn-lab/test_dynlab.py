"""Tests for dyn-lab, at its seams: the gain computer, the step response, Auto Release, Auto Gain, VCA's RMS
Detection Level, the latency, the Limiter, Saturation, its aliasing, Colour, Oversampling and the command line.

    python3 tools/dyn-lab/test_dynlab.py
"""

import contextlib
import io
import math
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dynlab  # noqa: E402


class GainComputer(unittest.TestCase):
    def test_hard_knee_is_unity_below_threshold_and_one_over_ratio_above(self):
        clean = dynlab.Settings(threshold=-20, ratio=4, knee=0)
        self.assertAlmostEqual(dynlab.gain_computer(-30, clean), -30, places=9)
        self.assertAlmostEqual(dynlab.gain_computer(-20, clean), -20, places=9)
        # 10 dB over Threshold at 4:1 leaves 2.5 dB over it.
        self.assertAlmostEqual(dynlab.gain_computer(-10, clean), -17.5, places=9)

    def test_soft_knee_fades_in_across_its_width(self):
        # A 10 dB Knee runs from -25 to -15 dB. Worked from the paper's quadratic,
        # x + (1/R - 1)(x - T + W/2)^2 / 2W: at Threshold, -20 - 0.75 * 25 / 20.
        soft = dynlab.Settings(threshold=-20, ratio=4, knee=10)
        self.assertAlmostEqual(dynlab.gain_computer(-26, soft), -26, places=9)
        self.assertAlmostEqual(dynlab.gain_computer(-25, soft), -25, places=9)
        self.assertAlmostEqual(dynlab.gain_computer(-20, soft), -20.9375, places=9)
        self.assertAlmostEqual(dynlab.gain_computer(-15, soft), -18.75, places=9)
        self.assertAlmostEqual(dynlab.gain_computer(-5, soft), -16.25, places=9)

    def test_infinite_ratio_is_flat_above_the_knee(self):
        infinite = dynlab.Settings(threshold=-20, ratio=math.inf, knee=10)
        self.assertAlmostEqual(dynlab.gain_computer(-20, infinite), -21.25, places=9)
        for level in (-15, -10, 0, 12):
            self.assertAlmostEqual(dynlab.gain_computer(level, infinite), -20, places=9)
        hard = dynlab.Settings(threshold=-20, ratio=math.inf, knee=0)
        self.assertAlmostEqual(dynlab.gain_computer(0, hard), -20, places=9)

    def test_range_caps_the_gain_reduction_so_the_transfer_curve_rises_again_past_it(self):
        # Hard knee at 4:1 from -20 dB: 0.75 dB of Gain Reduction per dB over Threshold, capped at 6 dB, which
        # it reaches 8 dB over.
        capped = dynlab.Settings(threshold=-20, ratio=4, knee=0, range=6)
        self.assertAlmostEqual(dynlab.gain_reduction(-16, capped), 3, places=9)
        self.assertAlmostEqual(dynlab.gain_reduction(-12, capped), 6, places=9)
        self.assertAlmostEqual(dynlab.gain_reduction(0, capped), 6, places=9)
        self.assertAlmostEqual(dynlab.transfer(0, capped), -6, places=9)
        curve = dynlab.transfer_curve(capped)
        self.assertIn((-12, -18, "range"), [(level, round(out, 9), region) for level, out, region in curve.rows])
        self.assertAlmostEqual(curve.slope_above, 1, places=9)

    def test_range_0_leaves_the_signal_alone(self):
        for level in (-60, -18, 0):
            self.assertEqual(dynlab.gain_reduction(level, dynlab.Settings(range=0)), 0)


class StepResponse(unittest.TestCase):
    """A ±20 dB level step around Threshold: up for Attack, down for Release. The detector is one pole
    per branch, so 63% (1 - 1/e) of the way takes one time constant and 90% takes ln 10 of them."""

    def assertWithinOneSample(self, measured_ms, expected_ms, fs):
        # A time of exactly one sample's difference, worked in ms, can come out a hair over it.
        self.assertLessEqual(abs(measured_ms - expected_ms), 1000 / fs + 1e-9, f"at {fs} Hz")

    def test_attack_and_release_reach_63_percent_in_one_time_constant(self):
        for fs in dynlab.SAMPLE_RATES:
            for attack, release in ((10, 100), (0.5, 5), (250, 5000)):
                step = dynlab.step_response(dynlab.Settings(attack=attack, release=release, hold=0), fs)
                self.assertWithinOneSample(step.attack_63, attack, fs)
                self.assertWithinOneSample(step.attack_90, attack * math.log(10), fs)
                self.assertWithinOneSample(step.release_63, release, fs)
                self.assertWithinOneSample(step.release_90, release * math.log(10), fs)
                self.assertEqual(step.hold, 0)

    def test_release_begins_after_hold(self):
        for fs in dynlab.SAMPLE_RATES:
            for hold in (0.01, 1, 50, 500):
                step = dynlab.step_response(dynlab.Settings(attack=10, release=100, hold=hold), fs)
                self.assertWithinOneSample(step.hold, hold, fs)
                self.assertWithinOneSample(step.release_63, 100, fs)
                self.assertWithinOneSample(step.attack_63, 10, fs)

    def test_range_leaves_the_times_unchanged(self):
        # Range caps what the detector is asked for, before it smooths it, so each time is the same fraction of
        # a smaller step.
        for fs in dynlab.SAMPLE_RATES:
            step = dynlab.step_response(dynlab.Settings(attack=10, release=100, hold=20, range=6), fs)
            self.assertWithinOneSample(step.attack_63, 10, fs)
            self.assertWithinOneSample(step.release_90, 100 * math.log(10), fs)
            self.assertWithinOneSample(step.hold, 20, fs)

    def test_range_0_has_no_gain_reduction_to_measure(self):
        self.assertIsNone(dynlab.step_response(dynlab.Settings(range=0), 48000))

    def test_lookahead_brings_attack_forward_against_the_delayed_audio(self):
        # The detector meets the step Lookahead ms before the listener does: Attack's times, measured from the step in
        # the delayed audio, come Lookahead earlier, and Hold and Release, counted as before, don't move.
        for fs in dynlab.SAMPLE_RATES:
            step = dynlab.step_response(dynlab.Settings(attack=10, release=100, hold=20), fs, lookahead=5)
            self.assertWithinOneSample(step.attack_63, 10 - 5, fs)
            self.assertWithinOneSample(step.attack_90, 10 * math.log(10) - 5, fs)
            self.assertWithinOneSample(step.hold, 20, fs)
            self.assertWithinOneSample(step.release_63, 100, fs)

    def test_a_lookahead_longer_than_attack_reduces_gain_before_the_step_is_heard(self):
        for fs in dynlab.SAMPLE_RATES:
            step = dynlab.step_response(dynlab.Settings(attack=0.5, release=50, hold=20), fs, lookahead=2)
            self.assertWithinOneSample(step.attack_63, 0.5 - 2, fs)
            self.assertLess(step.attack_63, 0)


class AutoRelease(unittest.TestCase):
    """Auto Release lengthens Release after sustained compression, up to AUTO_RELEASE_MULTIPLIER times it, and
    leaves it short after transients."""

    def test_off_every_burst_releases_in_release(self):
        for fs in dynlab.SAMPLE_RATES:
            for burst in dynlab.bursts(dynlab.Settings(), fs):
                with self.subTest(fs=fs, burst=burst.length):
                    self.assertLessEqual(abs(burst.off.release_63 - 100), 1000 / fs)
                    self.assertLessEqual(abs(burst.off.release_90 - 100 * math.log(10)), 1000 / fs)

    def test_on_a_longer_burst_never_releases_faster_than_a_shorter_one(self):
        for settings in (dynlab.Settings(), dynlab.Settings(attack=0.5, release=50, hold=20)):
            for fs in dynlab.SAMPLE_RATES:
                with self.subTest(settings=settings, fs=fs):
                    on = [burst.on for burst in dynlab.bursts(settings, fs, (0.5, 1, 3, 10, 30, 100, 300, 1000, 3000))]
                    for shorter, longer in zip(on, on[1:]):
                        self.assertGreaterEqual(longer.release_63, shorter.release_63)
                        self.assertGreaterEqual(longer.release_90, shorter.release_90)

    def test_on_the_shortest_burst_releases_within_the_margin_of_release(self):
        for fs in dynlab.SAMPLE_RATES:
            shortest = dynlab.bursts(dynlab.Settings(), fs)[0]
            with self.subTest(fs=fs):
                self.assertLessEqual(shortest.on.release_63, 100 * (1 + dynlab.AUTO_RELEASE_TRANSIENT_MARGIN))
                self.assertLessEqual(shortest.on.release_90, 100 * math.log(10) * (1 + dynlab.AUTO_RELEASE_TRANSIENT_MARGIN))

    def test_on_the_longest_burst_releases_several_times_slower(self):
        # The default bursts' longest, 3 s, charges the sustain state most of the way over its 1 s charge, so Release
        # starts near AUTO_RELEASE_MULTIPLIER times as long, and shortens as the sustain state discharges.
        for fs in dynlab.SAMPLE_RATES:
            longest = dynlab.bursts(dynlab.Settings(), fs)[-1]
            with self.subTest(fs=fs):
                self.assertGreater(longest.on.release_63, 100 * (dynlab.AUTO_RELEASE_MULTIPLIER - 1))
                self.assertLess(longest.on.release_63, 100 * dynlab.AUTO_RELEASE_MULTIPLIER)

    def test_a_step_from_steady_state_releases_slowest(self):
        # From steady state the sustain state is full: Release starts AUTO_RELEASE_MULTIPLIER times as long. Off,
        # the step is exactly today's.
        for fs in dynlab.SAMPLE_RATES:
            on = dynlab.step_response(dynlab.Settings(auto_release=True), fs)
            off = dynlab.step_response(dynlab.Settings(), fs)
            longest = dynlab.bursts(dynlab.Settings(), fs)[-1]
            with self.subTest(fs=fs):
                self.assertGreater(on.release_63, longest.on.release_63)
                self.assertLess(on.release_63, 100 * dynlab.AUTO_RELEASE_MULTIPLIER)
                self.assertLessEqual(abs(off.release_63 - 100), 1000 / fs)
                self.assertEqual((on.attack_63, on.attack_90, on.hold), (off.attack_63, off.attack_90, off.hold))

    def test_hold_runs_first(self):
        for fs in dynlab.SAMPLE_RATES:
            step = dynlab.step_response(dynlab.Settings(hold=20, auto_release=True), fs)
            with self.subTest(fs=fs):
                self.assertLessEqual(abs(step.hold - 20), 1000 / fs)

    def test_the_sustain_state_charges_and_discharges_in_its_own_times_at_every_rate(self):
        # Charged for AUTO_RELEASE_CHARGE_MS from empty, it is 1 - 1/e full; then discharged for
        # AUTO_RELEASE_DISCHARGE_MS, 1/e of that. It moves a run at a time, so to within a run's worth.
        def runs(ms, fs):
            return round(ms * fs / 1000 / dynlab.RUN_SAMPLES)

        for fs in dynlab.SAMPLE_RATES:
            with self.subTest(fs=fs):
                sustain = dynlab.AutoRelease(fs)
                for compressing, ms in ((True, dynlab.AUTO_RELEASE_CHARGE_MS), (False, dynlab.AUTO_RELEASE_DISCHARGE_MS)):
                    before = sustain.sustain
                    for _ in range(runs(ms, fs)):
                        for _ in range(dynlab.RUN_SAMPLES):
                            sustain.hear(compressing)
                        sustain.start_run()
                    expected = 1 - (1 - before) / math.e if compressing else before / math.e
                    self.assertAlmostEqual(sustain.sustain, expected, delta=dynlab.RUN_SAMPLES * 1000 / (fs * ms))


class AutoGain(unittest.TestCase):
    def test_the_estimate_is_the_average_gain_reduction_over_the_reference_levels(self):
        # Worked by hand at the defaults over -40 to 0 dB in 2 dB steps (21 levels). The Knee runs from -21 to
        # -15 dB: 0.75 (L + 21)^2 / 12 at -20, -18 and -16 dB is 0.0625, 0.5625 and 1.5625. Above it, 0.75 (L + 18)
        # from -14 to 0 dB sums to 66. Below it, nothing. (66 + 2.1875) / 21.
        self.assertAlmostEqual(dynlab.auto_gain(dynlab.Settings()), 68.1875 / 21, places=9)
        # Range 6 caps the six levels from -10 dB up at 6 dB: 3 + 4.5 + 6 * 6 above the Knee.
        self.assertAlmostEqual(dynlab.auto_gain(dynlab.Settings(range=6)), 45.6875 / 21, places=9)
        self.assertEqual(dynlab.auto_gain(dynlab.Settings(ratio=1)), 0)

    def test_the_estimate_is_far_below_the_textbook_makeup(self):
        # Making up the Gain Reduction at 0 dBFS gives 13.5 dB at the defaults: loud material would get louder.
        defaults = dynlab.Settings()
        self.assertAlmostEqual(dynlab.gain_reduction(0, defaults), 13.5, places=9)
        self.assertLess(dynlab.auto_gain(defaults), 13.5 / 3)

    def test_the_reference_signal_is_fixed_and_steps_through_the_reference_levels(self):
        first, again = dynlab.reference_signal(), dynlab.reference_signal()
        self.assertEqual(first, again)
        self.assertEqual(len(first), len(dynlab.REFERENCE_LEVELS))
        for level, segment in zip(dynlab.REFERENCE_LEVELS, first):
            with self.subTest(level):
                # A segment at a Detection Level has the power of a sine whose peak reads that level: half its
                # peak squared.
                power = sum(x * x for x in segment) / len(segment)
                self.assertAlmostEqual(10 * math.log10(power), level - 10 * math.log10(2), places=9)
                self.assertEqual(len(segment), round(dynlab.REFERENCE_SEGMENT_SECONDS * dynlab.REFERENCE_RATE))

    def test_with_auto_gain_the_reference_signal_keeps_its_level(self):
        for settings in (dynlab.Settings(), dynlab.Settings(ratio=math.inf, range=6)):
            with self.subTest(settings):
                self.assertLessEqual(abs(dynlab.level_difference(settings)), dynlab.AUTO_GAIN_TOLERANCE)

    def test_without_auto_gain_the_reference_signal_would_be_quieter(self):
        # The check can fail: the same signal compressed without Auto Gain loses about the estimate.
        defaults = dynlab.Settings()
        self.assertLess(dynlab.level_difference(defaults, auto_gain_db=0), -dynlab.AUTO_GAIN_TOLERANCE)


class Vca(unittest.TestCase):
    """VCA: an RMS Detection Level, sine-calibrated, into Clean's gain computer and detector."""

    def test_a_sine_reads_its_peak_level_and_a_square_wave_3_db_over_its_peak(self):
        fs = 48000
        # A sine high enough that the mean square's ripple, at twice its frequency, is far under 0.01 dB.
        sine = dynlab.RmsLevel(fs, -math.inf)
        amplitude = 10 ** (-10 / 20)
        for n in range(fs // 2):
            level = sine.sample(amplitude * math.sin(2 * math.pi * 12000 * n / fs))
        self.assertAlmostEqual(level, -10, delta=0.01)
        square = dynlab.RmsLevel(fs, -math.inf)
        for n in range(fs // 2):
            level = square.sample(amplitude if n % 2 else -amplitude)
        self.assertAlmostEqual(level, -10 + 3.0103, places=4)
        self.assertAlmostEqual(dynlab.SINE_CALIBRATION_DB, 3.0103, places=4)

    def test_the_mean_square_covers_63_percent_of_a_step_in_its_time_constant(self):
        fs = 48000
        rms = dynlab.RmsLevel(fs, -math.inf)
        samples = round(dynlab.VCA_RMS_MS * fs / 1000)
        for _ in range(samples):
            rms.steady(0.0)
        self.assertAlmostEqual(2 * rms.mean_square, 1 - 1 / math.e, places=3)

    def test_clean_is_the_default_and_its_figures_are_unchanged_by_the_option(self):
        self.assertEqual(dynlab.Settings().character, "clean")
        self.assertEqual(run("step").stdout, run("step", "--character", "clean").stdout)

    def test_vca_shares_cleans_transfer_curve(self):
        clean, vca = dynlab.Settings(), dynlab.Settings(character="vca")
        self.assertEqual(dynlab.transfer_curve(clean), dynlab.transfer_curve(vca))

    def test_the_rms_stage_lengthens_attack_and_release_by_about_its_time_constant(self):
        for fs in dynlab.SAMPLE_RATES:
            clean = dynlab.step_response(dynlab.Settings(), fs)
            vca = dynlab.step_response(dynlab.Settings(character="vca"), fs)
            self.assertGreater(vca.attack_63, clean.attack_63)
            self.assertLess(vca.attack_63, clean.attack_63 + dynlab.VCA_RMS_MS)
            self.assertGreater(vca.release_63, clean.release_63)
            self.assertLess(vca.release_63, clean.release_63 + 3 * dynlab.VCA_RMS_MS)

    def test_hold_still_runs_first(self):
        for fs in dynlab.SAMPLE_RATES:
            step = dynlab.step_response(dynlab.Settings(character="vca", hold=20), fs)
            self.assertLessEqual(abs(step.hold - 20), 1000 / fs)

    def test_with_auto_gain_the_reference_signal_keeps_its_level(self):
        self.assertLessEqual(abs(dynlab.level_difference(dynlab.Settings(character="vca"))), dynlab.AUTO_GAIN_TOLERANCE)


class Fet(unittest.TestCase):
    RATES = dynlab.SAMPLE_RATES
    # Settings whose loop crosses every region of the Transfer Curve: the Knee, above it, Range, and ∞:1.
    SETTINGS = ({}, {"ratio": math.inf}, {"knee": 0}, {"knee": 72}, {"range": 6}, {"ratio": 1.5, "knee": 24},
                {"threshold": -60, "ratio": math.inf, "knee": 0}, {"ratio": 1})

    def fet(self, **options):
        return dynlab.Settings(character="fet", **options)

    def test_the_loop_solves_its_own_equation_between_where_it_starts_and_its_steady_state(self):
        for options in self.SETTINGS:
            settings = self.fet(**options)
            w = lambda y: dynlab.gain_reduction(self.input_for(y, settings), settings)  # noqa: E731
            for level in range(-60, 13, 3):
                steady = dynlab.gain_reduction(level, settings)
                for previous in (0.0, steady / 2, steady, steady + 5):
                    with self.subTest(options=options, level=level, previous=previous):
                        g = dynlab.fet_loop(previous, level, 0.9, settings)
                        self.assertGreaterEqual(g, min(previous, steady) - 1e-9)
                        self.assertLessEqual(g, max(previous, steady) + 1e-9)
                        if settings.ratio != math.inf:
                            self.assertAlmostEqual(g, 0.9 * previous + 0.1 * w(level - g), places=9)

    @staticmethod
    def input_for(output_db, settings):
        """The input level whose Transfer Curve output is output_db, by bisection: an independent inverse."""
        low, high = output_db, output_db + 200
        for _ in range(200):
            middle = (low + high) / 2
            low, high = (middle, high) if dynlab.transfer(middle, settings) < output_db else (low, middle)
        return (low + high) / 2

    def test_its_steady_output_is_cleans_transfer_curve(self):
        for options in self.SETTINGS:
            with self.subTest(options=options):
                clean = dynlab.transfer_curve(dynlab.Settings(**options))
                fet = dynlab.transfer_curve(self.fet(**options))
                for (level, expected, region), (_, measured, fet_region) in zip(clean.rows, fet.rows):
                    self.assertAlmostEqual(measured, expected, delta=1e-8)
                    self.assertEqual(region, fet_region)
                self.assertAlmostEqual(fet.slope_above, clean.slope_above, places=6)

    def test_attack_runs_ratio_times_faster_up_to_the_cap(self):
        for ratio in (1.5, 2, 3):
            clean = dynlab.step_response(dynlab.Settings(ratio=ratio), 48000)
            fet = dynlab.step_response(self.fet(ratio=ratio), 48000)
            self.assertAlmostEqual(clean.attack_63 / fet.attack_63, ratio, delta=0.05 * ratio)
        for ratio in (8, 20, math.inf):
            clean = dynlab.step_response(dynlab.Settings(ratio=ratio), 48000)
            fet = dynlab.step_response(self.fet(ratio=ratio), 48000)
            speedup = clean.attack_63 / fet.attack_63
            self.assertLessEqual(speedup, dynlab.FET_SPEEDUP_MAX)
            self.assertGreater(speedup, 0.9 * dynlab.FET_SPEEDUP_MAX)

    def test_a_release_under_the_knee_and_hold_are_cleans(self):
        for options in ({}, {"attack": 0.5, "release": 50, "hold": 20}, {"ratio": math.inf}):
            for fs in self.RATES:
                clean = dynlab.step_response(dynlab.Settings(**options), fs)
                fet = dynlab.step_response(self.fet(**options), fs)
                self.assertEqual((fet.release_63, fet.release_90, fet.hold),
                                 (clean.release_63, clean.release_90, clean.hold))

    def test_a_release_above_the_knee_is_sped_up_too(self):
        levels = {"quiet": -18 + dynlab.PARTIAL_QUIET_DB, "loud": -18 + dynlab.PARTIAL_LOUD_DB}
        clean = dynlab.step_response(dynlab.Settings(ratio=4), 48000, **levels)
        fet = dynlab.step_response(self.fet(ratio=4), 48000, **levels)
        self.assertLess(fet.release_63, clean.release_63 / 2)

    def test_its_attack_is_under_a_millisecond_at_the_reference_attack_at_every_rate(self):
        for fs in dynlab.supported_rates():
            for ratio in (4, math.inf):
                step = dynlab.step_response(self.fet(attack=dynlab.FET_REFERENCE_ATTACK_MS, ratio=ratio), fs)
                self.assertLess(step.attack_90, 1.0)

    def test_the_loop_is_stable_at_the_fastest_attack_and_infinite_ratio(self):
        for fs in dynlab.supported_rates():
            settings = self.fet(attack=dynlab.ATTACK_RANGE[0], ratio=math.inf)
            detector = dynlab.Detector(settings, fs, level_db=-40)
            reductions = [detector.hear_steady(level) for level in [-40] * 10 + [0] * 50]
            steps = [b - a for a, b in zip(reductions, reductions[1:])]
            self.assertTrue(all(step >= 0 for step in steps))
            self.assertAlmostEqual(reductions[-1], 18.0, places=6)
            noise = dynlab._white(7)
            detector = dynlab.Detector(settings, fs)
            for _ in range(2000):
                reduction = detector.hear_sample(next(noise))
                self.assertTrue(math.isfinite(reduction) and reduction >= 0)

    def test_with_an_external_detection_source_it_is_feedforward_with_cleans_times(self):
        for options in ({}, {"attack": 0.5, "release": 50, "hold": 20}, {"ratio": math.inf}):
            for fs in self.RATES:
                self.assertEqual(dynlab.step_response(self.fet(**options), fs, external=True),
                                 dynlab.step_response(dynlab.Settings(**options), fs))

    def test_lookahead_brings_its_attack_forward_against_the_delayed_audio(self):
        for fs in self.RATES:
            plain = dynlab.step_response(self.fet(), fs)
            ahead = dynlab.step_response(self.fet(), fs, lookahead=5)
            self.assertAlmostEqual(ahead.attack_63, plain.attack_63 - 5, places=9)
            self.assertEqual(ahead.release_63, plain.release_63)

    def test_auto_release_bursts_release_as_on_clean(self):
        clean, fet = dynlab.bursts(dynlab.Settings(), 48000), dynlab.bursts(self.fet(), 48000)
        self.assertEqual(clean, fet)

    def test_with_auto_gain_the_reference_signal_keeps_its_level(self):
        for options in ({}, {"ratio": math.inf}, {"threshold": -40, "ratio": math.inf}):
            with self.subTest(options=options):
                self.assertLessEqual(abs(dynlab.level_difference(self.fet(**options))), dynlab.AUTO_GAIN_TOLERANCE)

    def test_its_colour_is_steeper_than_vcas_about_1_percent_at_10_db_and_3_at_20(self):
        k = dynlab.tone_bin(*dynlab.COLOUR_TONE)
        for reduction, thd in ((10, 1.0), (20, 3.0)):
            fet = dynlab.colour_row(self.fet(), reduction, k)
            vca = dynlab.colour_row(dynlab.Settings(character="vca"), reduction, k)
            self.assertAlmostEqual(fet.thd, thd, delta=0.1)
            self.assertGreater(fet.thd, 2 * vca.thd)
        self.assertEqual(dynlab.colour_at(self.fet(), 0), dynlab.ColourAt(0.0, 0.0))

    def test_every_colour_aliasing_row_is_under_the_ceiling(self):
        rows = dynlab.colour_aliases(self.fet())
        self.assertEqual(len(rows), len(dynlab.ALIAS_RATES) * len(dynlab.ALIAS_FREQUENCIES) * 3)
        self.assertTrue(all(row.within for row in rows))


class Colour(unittest.TestCase):
    """Colour: the Character's own distortion, the Saturation shape's residual weighted and driven by the Gain
    Reduction."""

    def test_clean_has_no_colour(self):
        for reduction in (0, 10, 60):
            self.assertIsNone(dynlab.colour_at(dynlab.Settings(), reduction))
        self.assertIn("no Colour", run("colour").stdout)

    def test_with_no_gain_reduction_the_weight_and_the_drive_are_exactly_0(self):
        colour = dynlab.colour_at(dynlab.Settings(character="vca"), 0)
        self.assertEqual((colour.weight, colour.drive), (0.0, 0.0))
        x = dynlab.tone(dynlab.tone_bin(1000, 48000), 0.0)
        self.assertEqual(dynlab.coloured(x, 0, colour, dynlab.AntiAliased()), x)

    def test_the_weight_and_the_drive_rise_with_the_gain_reduction_and_the_drive_stays_within_drives_range(self):
        vca = dynlab.Settings(character="vca")
        previous = dynlab.colour_at(vca, 0)
        for reduction in (0.1, 1, 3, 6, 10, 20, 30, 60):
            colour = dynlab.colour_at(vca, reduction)
            self.assertGreater(colour.weight, previous.weight)
            self.assertGreater(colour.drive, previous.drive)
            self.assertLessEqual(colour.drive, dynlab.DRIVE_RANGE[1])
            previous = colour

    def test_colour_scales_the_weight_and_leaves_the_drive(self):
        full = dynlab.colour_at(dynlab.Settings(character="vca"), 10)
        half = dynlab.colour_at(dynlab.Settings(character="vca", colour=50), 10)
        self.assertAlmostEqual(half.weight, full.weight / 2, places=12)
        self.assertEqual(half.drive, full.drive)
        self.assertEqual(dynlab.colour_at(dynlab.Settings(character="vca", colour=0), 10).weight, 0.0)

    def test_colour_is_slight_at_10_db_of_gain_reduction(self):
        row = dynlab.colour_row(dynlab.Settings(character="vca"), 10, dynlab.tone_bin(*dynlab.COLOUR_TONE))
        self.assertAlmostEqual(row.thd, 0.3, delta=0.02)
        # The shape is odd and the Gain Reduction steady, so the even harmonics are absent.
        self.assertEqual(row.harmonics[0], -math.inf)
        self.assertEqual(row.harmonics[2], -math.inf)

    def test_the_sine_sits_at_the_level_whose_gain_reduction_it_is(self):
        vca = dynlab.Settings(character="vca")
        for reduction in (3, 10, 20):
            self.assertAlmostEqual(dynlab.gain_reduction(dynlab.colour_level(vca, reduction), vca), reduction, places=9)
        self.assertEqual(dynlab.gain_reduction(dynlab.colour_level(vca, 0), vca), 0)
        self.assertIsNone(dynlab.colour_level(dynlab.Settings(character="vca", range=6), 10))

    def test_every_aliasing_row_is_under_the_ceiling(self):
        rows = dynlab.colour_aliases(dynlab.Settings(character="vca"))
        self.assertEqual(len(rows), len(dynlab.ALIAS_RATES) * len(dynlab.ALIAS_FREQUENCIES) * 3)
        self.assertTrue(all(row.within for row in rows))
        self.assertFalse(dynlab.ColourAlias(48000, 10000, 30, -10.0, dynlab.ALIASING_TOLERANCE.ceiling + 0.01).within)


class Latency(unittest.TestCase):
    def test_zero_latency_reports_0(self):
        for fs in dynlab.supported_rates():
            self.assertEqual(dynlab.latency("zero latency", fs), 0)

    def test_high_quality_is_its_three_parts_in_whole_host_samples(self):
        # Worked by hand. At 44.1 kHz, 5 ms is 220.5 samples and 1 ms is 44.1, each rounded up; at 48 and 192 kHz
        # both are whole.
        for fs, parts, total in ((44100, (128, 221, 45), 394), (48000, (128, 240, 48), 416),
                                 (192000, (128, 960, 192), 1280)):
            with self.subTest(fs):
                self.assertEqual(dynlab.high_quality_parts(fs), parts)
                self.assertEqual(dynlab.latency("high quality", fs), total)

    def test_the_rates_are_plugkit_confs(self):
        self.assertEqual(dynlab.supported_rates(), (44100, 48000, 88200, 96000, 176400, 192000))

    def test_the_lookahead_budget_is_the_top_of_lookaheads_range_rounded_up(self):
        for fs in dynlab.supported_rates():
            with self.subTest(fs):
                _, budget, _ = dynlab.high_quality_parts(fs)
                top = dynlab.LOOKAHEAD_RANGE[1] * fs / 1000
                self.assertTrue(top <= budget < top + 1)


class Limiter(unittest.TestCase):
    def test_the_estimators_delay_and_the_lookahead_window_fill_the_limiter_reserve_exactly(self):
        # Worked by hand: 16 taps read 8 samples ahead; the reserve is 45 samples at 44.1 kHz and 192 at 192 kHz.
        self.assertEqual(dynlab.estimator_delay(), 8)
        self.assertEqual(dynlab.lookahead_window(44100), 37)
        self.assertEqual(dynlab.lookahead_window(192000), 184)
        for fs in dynlab.supported_rates():
            with self.subTest(fs):
                _, _, reserve = dynlab.high_quality_parts(fs)
                self.assertEqual(dynlab.estimator_delay() + dynlab.lookahead_window(fs), reserve)

    def test_the_estimator_reads_a_peak_between_samples_and_never_under_the_sample_peak(self):
        phases = dynlab.interpolator(dynlab.TRUE_PEAK_FACTOR, dynlab.TRUE_PEAK_TAPS, dynlab.TRUE_PEAK_BETA)
        self.assertEqual(len(phases), dynlab.TRUE_PEAK_FACTOR - 1)
        for phase in phases:
            self.assertAlmostEqual(sum(phase), 1.0, places=12)
        # A sine at a quarter of the rate, faded in, its peaks halfway between samples: they read cos(pi/4), 3 dB under
        # the peak, 1, which the estimator's middle phase falls on.
        tone = [dynlab._fade_in(48000, n) * math.sin(math.pi / 2 * n + math.pi / 4) for n in range(512)]
        self.assertAlmostEqual(max(abs(x) for x in tone), math.cos(math.pi / 4), places=9)
        self.assertAlmostEqual(dynlab.estimated_true_peak([tone]), 1.0, delta=0.02)  # within its passband ripple
        self.assertAlmostEqual(dynlab.reference_true_peak([tone]), 1.0, delta=0.001)
        for x in ([0.5] * 64, [(-1) ** n * 0.5 for n in range(64)]):
            self.assertGreaterEqual(dynlab.estimated_true_peak([x]), 0.5)

    def test_the_headroom_covers_the_estimators_worst_under_read_at_every_rate(self):
        for fs in dynlab.SAMPLE_RATES:
            with self.subTest(fs):
                self.assertLessEqual(dynlab.under_read(fs).db, dynlab.LIMITER_TRUE_PEAK_HEADROOM_DB)

    def test_zero_latency_holds_the_sample_peak_and_high_quality_the_true_peak_to_the_ceiling(self):
        for ceiling in (0.0, -6.0, -30.0):
            for row in dynlab.limiter_peaks(ceiling, 44100):
                with self.subTest(ceiling=ceiling, signal=row.signal, mode=row.mode):
                    self.assertLessEqual(row.sample, 0.0)
                    if row.mode == "high quality":
                        self.assertLessEqual(row.true, 0.0)

    def test_zero_latency_alone_lets_the_true_peak_over(self):
        # Why High Quality has its estimator: a 19 kHz tone held to the Ceiling sample by sample peaks over it
        # between samples.
        rows = [row for row in dynlab.limiter_peaks(0.0, 44100) if row.signal == "19 kHz tone"]
        self.assertGreater(next(row.true for row in rows if row.mode == "zero latency"), 1.0)

    def test_the_gain_is_fully_down_when_the_burst_arrives_and_releases_in_the_release_time(self):
        for fs in dynlab.SAMPLE_RATES:
            for mode in dynlab.LIMITERS:
                with self.subTest(fs=fs, mode=mode):
                    timing = dynlab.limiter_timing(mode, fs)
                    self.assertEqual(timing.first, timing.most)
                    self.assertGreaterEqual(timing.most, -dynlab.TIMING_CEILING_DB)
                    # One pole: 63% in one time constant, 90% in ln 10 of them; High Quality's average over its
                    # window, about a millisecond, rounds the start.
                    self.assertAlmostEqual(timing.release_63, dynlab.LIMITER_RELEASE_MS, delta=1.0)
                    self.assertAlmostEqual(timing.release_90, dynlab.LIMITER_RELEASE_MS * math.log(10), delta=1.0)

    def test_below_the_ceiling_the_signal_passes_bit_for_bit(self):
        tone = dynlab.limiter_tone(1000, -1.0, 48000)
        for mode, limit in dynlab.LIMITERS.items():
            with self.subTest(mode):
                out, reductions = limit([tone], 0.0, 48000)
                delay = dynlab.latency_of_limiter(mode, 48000)
                self.assertEqual(out[0][delay:], tone[:len(tone) - delay])
                self.assertEqual(set(reductions), {0.0})

    def test_the_release_is_the_shortest_candidate_within_the_distortion_limit_in_both_modes(self):
        rows = dynlab.release_trade_off()
        within = [row.release for row in rows
                  if all(share <= dynlab.LIMITER_DISTORTION_LIMIT for share in row.distortion.values())]
        self.assertEqual(min(within), dynlab.LIMITER_RELEASE_MS)
        # The trade-off: a shorter release recovers sooner and distorts more.
        for shorter, longer in zip(rows, rows[1:]):
            self.assertLess(shorter.recovery, longer.recovery)
            for mode in dynlab.LIMITERS:
                self.assertGreater(shorter.distortion[mode], longer.distortion[mode])

    def test_the_report_fails_when_the_headroom_does_not_cover_the_under_read(self):
        headroom = dynlab.LIMITER_TRUE_PEAK_HEADROOM_DB
        try:
            dynlab.LIMITER_TRUE_PEAK_HEADROOM_DB = 0.0
            with contextlib.redirect_stdout(io.StringIO()) as printed:
                self.assertFalse(dynlab.report_limiter(0.0, (48000,)))
            self.assertIn("over the headroom", printed.getvalue())
        finally:
            dynlab.LIMITER_TRUE_PEAK_HEADROOM_DB = headroom


class Saturation(unittest.TestCase):
    def test_the_shape_is_the_line_up_to_the_knee_and_saturates_toward_2(self):
        for u in (-1, -0.5, 0, 0.25, 1):
            self.assertEqual(dynlab.shape(u), u)
            self.assertEqual(dynlab.residual(u), 0)
            self.assertEqual(dynlab.residual_antiderivative(u), 0)
        # 1 + tanh(1) at u = 2, and odd.
        self.assertAlmostEqual(dynlab.shape(2), 1.7615941559557649, places=12)
        self.assertAlmostEqual(dynlab.shape(-2), -1.7615941559557649, places=12)
        self.assertAlmostEqual(dynlab.residual(-2), 0.2384058440442351, places=12)
        self.assertAlmostEqual(dynlab.shape(40), 2, places=12)

    def test_the_shape_leaves_the_line_with_its_slope_and_curvature(self):
        # C²: past the knee, the first difference starts at the line's slope and the second at its curvature, 0.
        h = 1e-4
        for side in (1, -1):
            with self.subTest(side=side):
                at = [dynlab.shape(side * (1 + i * h)) for i in range(3)]
                self.assertAlmostEqual((at[1] - at[0]) / (side * h), 1, delta=1e-6)
                self.assertAlmostEqual((at[2] - 2 * at[1] + at[0]) / h ** 2, 0, delta=1e-3)

    def test_the_antiderivative_integrates_the_residual(self):
        # A midpoint sum of r over each span, against R's difference.
        for a, b in ((0.5, 3), (-3, 2), (2, 15), (-1.5, 0.2), (-16, -2)):
            n = 20000
            area = sum(dynlab.residual(a + (b - a) * (i + 0.5) / n) for i in range(n)) * (b - a) / n
            with self.subTest(a=a, b=b):
                self.assertAlmostEqual(dynlab.residual_antiderivative(b) - dynlab.residual_antiderivative(a), area, places=6)

    def test_adaa_averages_the_residual_and_falls_back_to_the_midpoint_for_a_near_equal_pair(self):
        adaa = dynlab.AntiAliased()
        adaa(0.5)
        # The average of r from 0.5 to 3, worked from the antiderivative: (ln cosh 2 - 2) / 2.5.
        self.assertAlmostEqual(adaa(3), (math.log(math.cosh(2)) - 2) / 2.5, places=12)
        out = adaa(3 + 1e-9)
        self.assertEqual(out, dynlab.residual(3 + 0.5e-9))

    def test_below_the_knee_the_stage_is_the_input_exactly(self):
        x = [0.9 * math.sin(n / 3) for n in range(200)]
        self.assertEqual(dynlab.driven(x, 0.0, dynlab.AntiAliased()), x)
        quiet = [s / 4 for s in x]  # under -12 dB, so under the knee at Drive 6 dB
        self.assertEqual(dynlab.driven(quiet, 6.0, dynlab.AntiAliased()), quiet)

    def test_the_steady_curve_is_level_compensated(self):
        # Below the knee, the line; at full scale and Drive 12 dB (g about 3.98), (1 + tanh(g - 1)) / g.
        self.assertAlmostEqual(dynlab.steady_curve_db(-20, 12), -20, places=12)
        g = 10 ** (12 / 20)
        self.assertAlmostEqual(dynlab.steady_curve_db(0, 12), 20 * math.log10((1 + math.tanh(g - 1)) / g), places=12)

    def test_before_the_compressor_hears_the_saturated_signal_after_it_hears_the_input(self):
        settings = dynlab.Settings()
        off = dynlab.chain(12, settings, dynlab.Saturation())
        before = dynlab.chain(12, settings, dynlab.Saturation(True, 12.0, "before"))
        after = dynlab.chain(12, settings, dynlab.Saturation(True, 12.0, "after"))
        self.assertEqual(after.gain_reduction, off.gain_reduction)
        self.assertAlmostEqual(before.gain_reduction, dynlab.gain_reduction(dynlab.steady_curve_db(12, 12), settings), places=12)
        self.assertLess(before.gain_reduction, off.gain_reduction)
        self.assertAlmostEqual(after.output, dynlab.steady_curve_db(off.output, 12), places=12)
        self.assertLess(after.output, off.output)
        self.assertGreater(abs(before.output - after.output), 1)


class Aliasing(unittest.TestCase):
    def test_the_fft_finds_a_bin_centred_sine_in_its_bin(self):
        n = 64
        spectrum = dynlab.fft([0.5 * math.sin(2 * math.pi * 5 * i / n) for i in range(n)])
        self.assertAlmostEqual(abs(spectrum[5]) * 2 / n, 0.5, places=12)
        self.assertAlmostEqual(max(abs(c) for b, c in enumerate(spectrum[: n // 2]) if b != 5), 0, places=12)

    def test_every_reference_condition_is_within_the_tolerance(self):
        rows = dynlab.aliases()
        self.assertEqual(len(rows), len(dynlab.ALIAS_RATES) * len(dynlab.ALIAS_FREQUENCIES) * len(dynlab.ALIAS_DRIVES))
        self.assertEqual(sum(row.hardest for row in rows), len(dynlab.ALIAS_RATES))
        for row in rows:
            with self.subTest(fs=row.fs, frequency=row.frequency, drive=row.drive):
                self.assertTrue(row.within)
                # ADAA is better than the naive shaper everywhere.
                self.assertLess(row.adaa, row.naive)

    def test_the_naive_shaper_is_over_the_ceiling_at_the_hardest_condition(self):
        # The tolerance can fail: without anti-aliasing, the hardest condition's worst alias is over the ceiling.
        for row in dynlab.aliases(frequencies=(10000,), drives=(24.0,)):
            self.assertGreater(row.naive, dynlab.ALIASING_TOLERANCE.ceiling)

    def test_at_the_knee_nothing_aliases(self):
        # At -6 dBFS and Drive 6 dB, the peak just reaches the knee: the output is the tone.
        (row,) = dynlab.aliases(rates=(48000,), frequencies=(5000,), drives=(6.0,), level_db=-6.0)
        self.assertLess(row.adaa, -200)

    def test_the_in_band_harmonics_are_within_the_tolerance_of_the_ideal(self):
        rows = dynlab.harmonics()
        compared = [row for row in rows if row.compared]
        self.assertEqual({row.harmonic for row in compared}, {3, 5})
        self.assertEqual({row.harmonic for row in rows if not row.compared}, {2, 4})
        for row in compared:
            with self.subTest(fs=row.fs, frequency=row.frequency, drive=row.drive, harmonic=row.harmonic):
                self.assertTrue(row.within)
                self.assertLess(row.harmonic * row.frequency, row.fs / 2)

    def test_the_droop_at_15_khz_is_about_a_one_sample_average(self):
        # The 5 kHz tone's 3rd harmonic at 44.1 kHz and Drive 24 dB droops about as a one-sample average does at
        # 15 kHz, sin(x) / x with x = pi f / fs: 1.7 dB.
        (row,) = [row for row in dynlab.harmonics(rates=(44100,), frequencies=(5000,), drives=(24.0,)) if row.harmonic == 3]
        x = math.pi * row.harmonic * row.frequency / row.fs
        self.assertAlmostEqual(row.adaa - row.ideal, 20 * math.log10(math.sin(x) / x), delta=0.25)


class Oversampling(unittest.TestCase):
    def test_the_cap_allows_8x_at_44_1_and_48_khz_4x_at_88_2_and_96_and_2x_at_176_4_and_192(self):
        # ADR 0001's table, worked by hand: the internal rate at or under 384 kHz.
        expected = {44100: 8, 48000: 8, 88200: 4, 96000: 4, 176400: 2, 192000: 2}
        self.assertEqual({fs: dynlab.max_oversampling(fs) for fs in dynlab.supported_rates()}, expected)
        self.assertEqual(dynlab.allowed_factors(96000), (1, 2, 4))

    def test_a_setting_over_the_cap_plays_the_cap(self):
        self.assertEqual(dynlab.factor_in_force(8, 96000), 4)
        self.assertEqual(dynlab.factor_in_force(8, 192000), 2)
        self.assertEqual(dynlab.factor_in_force(2, 192000), 2)
        self.assertEqual(dynlab.factor_in_force(1, 44100), 1)

    def test_each_stage_is_a_half_band_with_unity_gain_at_dc(self):
        for stage in range(3):
            h = dynlab.stage_filter(stage)
            centre = (len(h) - 1) // 2
            with self.subTest(stage):
                self.assertEqual(len(h) % 4, 3)
                self.assertEqual(h[centre], 0.5)
                self.assertEqual(h, h[::-1])
                self.assertTrue(all(h[centre + j] == 0 for j in range(2, centre + 1, 2)))
                self.assertAlmostEqual(sum(h), 1, delta=1e-4)
                self.assertGreaterEqual(dynlab.stopband_db(h, dynlab.stage_passband(stage)), dynlab.OVERSAMPLING_STOPBAND_DB)
                # The shortest that reaches it: 4 taps fewer falls short.
                shorter = dynlab.halfband(len(h) - 4)
                self.assertLess(dynlab.stopband_db(shorter, dynlab.stage_passband(stage)), dynlab.OVERSAMPLING_STOPBAND_DB)

    def test_every_pair_fits_within_its_section_in_whole_host_samples(self):
        self.assertEqual(dynlab.OVERSAMPLING_SECTION_SAMPLES * 2, dynlab.OVERSAMPLING_RESERVE_SAMPLES)
        self.assertEqual(dynlab.pair(1), dynlab.Pair(1, 0, 0))
        for factor in (2, 4, 8):
            with self.subTest(factor):
                filters = dynlab.pair(factor)
                self.assertGreaterEqual(filters.padding, 0)
                self.assertTrue(0 <= filters.internal_padding < factor)
                self.assertGreaterEqual(dynlab.measured_stopband_db(factor), dynlab.OVERSAMPLING_STOPBAND_DB)

    def test_up_then_down_passes_a_tone_in_the_passband_delayed_by_the_pair(self):
        # 3 kHz at 44.1 kHz, well inside the 17.6 kHz edge, comes back delayed by the pair's delay, within the
        # passband ripple.
        x = [math.sin(2 * math.pi * 3000 * n / 44100) for n in range(600)]
        for factor in (2, 4, 8):
            with self.subTest(factor):
                up = x
                for h in dynlab.stages(factor):
                    up = dynlab._up(up, h)
                self.assertEqual(len(up), factor * len(x))
                back = [0.0] * dynlab.pair(factor).internal_padding + up
                for h in reversed(dynlab.stages(factor)):
                    back = dynlab._down(back, h)
                delay = dynlab.pair(factor).delay
                self.assertLess(max(abs(back[n] - x[n - delay]) for n in range(200, 600)), 1e-4)

    def test_below_the_knee_the_section_is_its_input_delayed_by_the_section_exactly(self):
        x = [0.5 * math.sin(2 * math.pi * 5000 * n / 48000) for n in range(400)]
        section = dynlab.OVERSAMPLING_SECTION_SAMPLES
        for factor in (2, 4, 8):
            with self.subTest(factor):
                self.assertEqual(dynlab.oversampled_driven(x, 0.0, factor), [0.0] * section + x[:-section])

    def test_every_oversampled_reference_condition_is_within_its_factors_tolerance(self):
        for factor in (2, 4, 8):
            for row in dynlab.aliases(factor=factor):
                with self.subTest(factor=factor, fs=row.fs, frequency=row.frequency, drive=row.drive):
                    self.assertTrue(row.within)

    def test_each_factors_ceiling_fails_the_factor_below_it(self):
        # The ceilings are tight enough to tell the factors apart.
        def worst_passband(factor):
            if factor > 1:
                return max(row.passband for row in dynlab.aliases(factor=factor))
            return max(dynlab.worst_passband_alias_db(dynlab.anti_aliased(dynlab.tone_bin(f, fs), 0.0, drive), dynlab.tone_bin(f, fs))
                       for fs in dynlab.ALIAS_RATES for f in dynlab.ALIAS_FREQUENCIES for drive in dynlab.ALIAS_DRIVES)

        for factor in (2, 4, 8):
            with self.subTest(factor):
                self.assertGreater(worst_passband(factor // 2), dynlab.ALIASING_TOLERANCE.passband_ceilings[factor])

    def test_oversampling_closes_the_fundamentals_shortfall_at_5_khz(self):
        # At 1x the fundamental is 7.6 dB louder than the ideal at 44.1 kHz and Drive 24 dB. Each factor at least halves it.
        shortfalls = []
        for factor in (1, 2, 4, 8):
            (row,) = dynlab.harmonics(rates=(44100,), frequencies=(5000,), drives=(24.0,), factor=factor, numbers=(1,))
            shortfalls.append(row.adaa - row.ideal)
        self.assertAlmostEqual(shortfalls[0], 7.6, delta=0.05)
        for lower, higher in zip(shortfalls, shortfalls[1:]):
            self.assertLess(higher, lower / 2)


LAB = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(LAB))


def run(*args):
    return subprocess.run([sys.executable, os.path.join(LAB, "dynlab.py"), *args],
                          cwd=ROOT, capture_output=True, text=True)


class CommandLine(unittest.TestCase):
    def test_every_example_in_the_usage_doc_runs_as_shown(self):
        with open(os.path.join(LAB, "USAGE.md")) as doc:
            examples = [line.strip() for line in doc if line.strip().startswith("python3 tools/dyn-lab/dynlab.py")]
        commands = {example.split()[2] for example in examples}
        self.assertEqual(commands, set(dynlab.COMMANDS), "the usage doc shows every command")
        for example in examples:
            with self.subTest(example):
                result = subprocess.run(example, shell=True, cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.strip())

    def test_the_usage_doc_table_shows_each_range_and_default_the_lab_uses(self):
        def number(text):
            text = text.replace("−", "-").split()[0]
            return math.inf if text == "∞" else float(text)

        # The compressor's settings, Lookahead, which is Processing Mode's and kept out of Settings, Drive,
        # Saturation's, and Ceiling, the Limiter's.
        ranges = {**{name: low_high for name, (low_high, _) in dynlab.PARAMETERS.items()},
                  "lookahead": dynlab.LOOKAHEAD_RANGE, "drive": dynlab.DRIVE_RANGE, "ceiling": dynlab.CEILING_RANGE}
        defaults = {**vars(dynlab.Settings()), "lookahead": dynlab.LOOKAHEAD_DEFAULT, "drive": dynlab.Saturation().drive,
                    "ceiling": dynlab.CEILING_DEFAULT}
        with open(os.path.join(LAB, "USAGE.md")) as doc:
            rows = {cells[0]: cells[1:] for cells in
                    ([cell.strip() for cell in line.strip().strip("|").split("|")] for line in doc)
                    if cells[0].lower() in ranges}
        self.assertEqual(set(rows), {name.capitalize() for name in ranges})
        for name, (low, high) in ranges.items():
            with self.subTest(name):
                shown_range, shown_default = rows[name.capitalize()]
                shown_low, shown_high = shown_range.split(" to ")
                self.assertEqual((number(shown_low), number(shown_high)), (low, high))
                self.assertEqual(number(shown_default), defaults[name])

    def test_out_of_range_settings_exit_non_zero_naming_the_parameter_and_its_range(self):
        for args, named in ((("step", "--attack", "300"), ("Attack", "0.005", "250")),
                            (("step", "--release", "1"), ("Release", "5", "5000")),
                            (("step", "--hold", "501"), ("Hold", "0", "500")),
                            (("transfer", "--threshold", "3"), ("Threshold", "-60", "0")),
                            (("transfer", "--ratio", "0.5"), ("Ratio", "1", "∞")),
                            (("transfer", "--knee", "80"), ("Knee", "0", "72")),
                            (("step", "--range", "61"), ("Range", "0", "60")),
                            (("autogain", "--range", "-1"), ("Range", "0", "60")),
                            (("saturation", "--drive", "25"), ("Drive", "0", "24")),
                            (("aliasing", "--drive", "-1"), ("Drive", "0", "24")),
                            (("step", "--lookahead", "6"), ("Lookahead", "0", "5")),
                            (("colour", "--colour", "101"), ("Colour", "0", "100")),
                            (("limiter", "--ceiling", "1"), ("Ceiling", "-30", "0"))):
            with self.subTest(args):
                result = run(*args)
                self.assertNotEqual(result.returncode, 0)
                for word in named:
                    self.assertIn(word, result.stderr)

    def test_autogain_exits_non_zero_when_the_level_moves_past_the_tolerance(self):
        # Threshold -60 dB at Ratio inf asks for more than the estimate can keep level (USAGE.md says why).
        result = run("autogain", "--threshold", "-60", "--ratio", "inf")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside", result.stdout)

    def test_aliasing_exits_non_zero_when_a_row_is_outside_the_tolerance(self):
        # At 96 kHz a 10 kHz tone is a smaller fraction of the rate, and its naive aliases are lower, so ADAA's
        # improvement over them at Drive 24 dB is under the minimum.
        result = run("aliasing", "--drive", "24", "--rates", "96000")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside", result.stdout)

    def test_latency_prints_a_row_for_every_supported_rate_and_the_mode_fade(self):
        result = run("latency")
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.split() and line.split()[0].isdigit()]
        self.assertEqual([float(row[0]) for row in rows], list(dynlab.supported_rates()))
        # 44.1 kHz's row, worked by hand above: Zero Latency, High Quality, then its three parts.
        self.assertEqual(rows[0][1:], ["0", "394", "128", "221", "45"])
        self.assertIn(f"{dynlab.MODE_FADE_MS:g} ms", result.stdout)

    def test_oversampling_prints_every_allowed_factor_at_every_supported_rate(self):
        result = run("oversampling")
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.split()[0].isdigit() and line.split()[1:2] in (["1x"], ["2x"], ["4x"], ["8x"])]
        self.assertEqual([(float(row[0]), row[1]) for row in rows],
                         [(fs, f"{factor}x") for fs in dynlab.supported_rates() for factor in dynlab.allowed_factors(fs)])
        self.assertTrue(all(row[-1] == "fits" for row in rows))
        # 8x at 44.1 kHz: three stages, a delay of 43 host samples with 6 of internal padding, and 21 left to 64.
        self.assertEqual(rows[3][:6], ["44100", "8x", "67+27+23", "43", "6", "21"])

    def test_aliasing_rejects_a_factor_over_the_cap(self):
        result = run("aliasing", "--oversampling", "8", "--rates", "96000")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("4x", result.stderr)

    def test_bursts_prints_off_and_on_for_every_length_and_rate(self):
        result = run("bursts", "--lengths", "1", "1000", "--rates", "48000", "96000")
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.split()[:1] in (["48000"], ["96000"])]
        self.assertEqual([(row[0], row[1]) for row in rows], [("48000", "1"), ("48000", "1000"), ("96000", "1"), ("96000", "1000")])
        self.assertTrue(all(len(row) == 6 for row in rows))

    def test_bursts_rejects_a_length_that_is_not_above_0(self):
        result = run("bursts", "--lengths", "0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("burst", result.stderr)

    def test_character_is_clean_vca_or_fet(self):
        for command in ("transfer", "step", "bursts", "autogain", "colour"):
            with self.subTest(command):
                for character in ("vca", "fet"):
                    self.assertEqual(run(command, "--character", character,
                                         *(("--lengths", "1") if command == "bursts" else ())).returncode, 0)
                result = run(command, "--character", "opto")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("character", result.stderr)

    def test_limiter_prints_its_parts_and_each_modes_peaks_and_timing_for_every_rate(self):
        result = run("limiter", "--ceiling", "-6", "--rates", "44100", "96000")
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.split()[:1] in (["44100"], ["96000"])]
        # Each rate's reserve, estimator delay, window and release in samples, worked by hand.
        parts = [row[:6] for row in rows]
        self.assertIn(["44100", "45", "8", "37", "8820", "0.3"], parts)
        self.assertIn(["96000", "96", "8", "88", "19200", "0.3"], parts)
        # Per rate and mode, four reference signals' peaks and one timing row.
        for mode in ("zero", "high"):
            self.assertEqual(sum(1 for row in rows if mode in row), 2 * 5, mode)
        self.assertIn("chosen", result.stdout)
        self.assertNotIn("  over the", result.stdout)

    def test_ratio_reads_inf_and_the_infinity_sign(self):
        for spelling in ("inf", "∞"):
            self.assertEqual(run("transfer", "--ratio", spelling).returncode, 0)


if __name__ == "__main__":
    unittest.main()
