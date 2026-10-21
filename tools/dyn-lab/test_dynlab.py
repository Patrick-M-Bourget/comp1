"""Tests for dyn-lab, at its four seams: the gain computer, the step response, Auto Gain and the command line.

    python3 tools/dyn-lab/test_dynlab.py
"""

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
        self.assertLessEqual(abs(measured_ms - expected_ms), 1000 / fs, f"at {fs} Hz")

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

        with open(os.path.join(LAB, "USAGE.md")) as doc:
            rows = {cells[0]: cells[1:] for cells in
                    ([cell.strip() for cell in line.strip().strip("|").split("|")] for line in doc)
                    if cells[0].lower() in dynlab.PARAMETERS}
        self.assertEqual(set(rows), {name.capitalize() for name in dynlab.PARAMETERS})
        defaults = dynlab.Settings()
        for name, ((low, high), _) in dynlab.PARAMETERS.items():
            with self.subTest(name):
                shown_range, shown_default = rows[name.capitalize()]
                shown_low, shown_high = shown_range.split(" to ")
                self.assertEqual((number(shown_low), number(shown_high)), (low, high))
                self.assertEqual(number(shown_default), getattr(defaults, name))

    def test_out_of_range_settings_exit_non_zero_naming_the_parameter_and_its_range(self):
        for args, named in ((("step", "--attack", "300"), ("Attack", "0.005", "250")),
                            (("step", "--release", "1"), ("Release", "5", "5000")),
                            (("step", "--hold", "501"), ("Hold", "0", "500")),
                            (("transfer", "--threshold", "3"), ("Threshold", "-60", "0")),
                            (("transfer", "--ratio", "0.5"), ("Ratio", "1", "∞")),
                            (("transfer", "--knee", "80"), ("Knee", "0", "72")),
                            (("step", "--range", "61"), ("Range", "0", "60")),
                            (("autogain", "--range", "-1"), ("Range", "0", "60"))):
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

    def test_ratio_reads_inf_and_the_infinity_sign(self):
        for spelling in ("inf", "∞"):
            self.assertEqual(run("transfer", "--ratio", spelling).returncode, 0)


if __name__ == "__main__":
    unittest.main()
