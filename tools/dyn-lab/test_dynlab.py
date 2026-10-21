"""Tests for dyn-lab, at its six seams: the gain computer, the step response, Auto Release, Auto Gain, the latency
and the command line.

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

        # The compressor's settings, and Lookahead, which is Processing Mode's and kept out of Settings.
        ranges = {**{name: low_high for name, (low_high, _) in dynlab.PARAMETERS.items()},
                  "lookahead": dynlab.LOOKAHEAD_RANGE}
        defaults = {**vars(dynlab.Settings()), "lookahead": dynlab.LOOKAHEAD_DEFAULT}
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
                            (("step", "--lookahead", "6"), ("Lookahead", "0", "5"))):
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

    def test_latency_prints_a_row_for_every_supported_rate_and_the_mode_fade(self):
        result = run("latency")
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.split() and line.split()[0].isdigit()]
        self.assertEqual([float(row[0]) for row in rows], list(dynlab.supported_rates()))
        # 44.1 kHz's row, worked by hand above: Zero Latency, High Quality, then its three parts.
        self.assertEqual(rows[0][1:], ["0", "394", "128", "221", "45"])
        self.assertIn(f"{dynlab.MODE_FADE_MS:g} ms", result.stdout)

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

    def test_ratio_reads_inf_and_the_infinity_sign(self):
        for spelling in ("inf", "∞"):
            self.assertEqual(run("transfer", "--ratio", spelling).returncode, 0)


if __name__ == "__main__":
    unittest.main()
