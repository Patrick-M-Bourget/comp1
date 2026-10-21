"""Tests for dyn-lab, at its seams: the gain computer, the step response, Auto Release, Auto Gain, the latency, the
Limiter, Saturation, its aliasing and the command line.

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


class Gate(unittest.TestCase):
    """The Gate: a hard gate on its key level, open at Gate Threshold and closed GATE_HYSTERESIS_DB under it, asking
    for no Gain Reduction open and exactly Gate Range closed, each move one pole in dB."""

    def assertWithinOneSample(self, measured_ms, expected_ms, fs):
        self.assertLessEqual(abs(measured_ms - expected_ms), 1000 / fs + 1e-9, f"at {fs} Hz")

    def test_closed_it_lowers_by_exactly_gate_range(self):
        for gate_range in (6, 20, 60):
            self.assertAlmostEqual(dynlab.gate_step(dynlab.Gate(range=gate_range), 48000).depth, gate_range, places=9)

    def test_gate_release_begins_after_gate_hold_and_the_key_hold(self):
        for fs in dynlab.SAMPLE_RATES:
            for hold in (0, 20, 500):
                step = dynlab.gate_step(dynlab.Gate(hold=hold), fs)
                self.assertWithinOneSample(step.release_begins, hold + dynlab.GATE_KEY_HOLD_MS, fs)

    def test_gate_attack_and_gate_release_reach_63_percent_in_one_time_constant(self):
        # The slowest settings once: settling a 5000 ms Gate Release for the depth is long.
        cases = [(fs, attack, release) for fs in dynlab.SAMPLE_RATES for attack, release in ((1, 100), (0.5, 50))]
        for fs, attack, release in cases + [(44100, 250, 5000)]:
            with self.subTest(fs=fs, attack=attack, release=release):
                    step = dynlab.gate_step(dynlab.Gate(attack=attack, release=release), fs)
                    self.assertWithinOneSample(step.attack_63, attack, fs)
                    self.assertWithinOneSample(step.attack_90, attack * math.log(10), fs)
                    self.assertWithinOneSample(step.release_63, release, fs)
                    self.assertWithinOneSample(step.release_90, release * math.log(10), fs)

    def test_gate_range_0_has_nothing_to_measure(self):
        self.assertIsNone(dynlab.gate_step(dynlab.Gate(range=0), 48000))

    def test_between_the_closing_level_and_gate_threshold_it_stays_as_it_is(self):
        gate = dynlab.Gate(hold=0)
        between = gate.threshold - dynlab.GATE_HYSTERESIS_DB / 2
        opened = dynlab.GateDetector(gate, 48000)
        closed = dynlab.GateDetector(gate, 48000, gate.range, is_open=False)
        for _ in range(48000):
            self.assertEqual(opened.process(between), 0.0)
            closed.process(between)
        self.assertAlmostEqual(closed.gain_reduction, gate.range, places=9)
        self.assertFalse(closed.is_open)

    def test_a_key_back_above_the_closing_level_restarts_gate_hold(self):
        fs = 48000
        gate = dynlab.Gate(hold=20)
        detector = dynlab.GateDetector(gate, fs)
        quiet, between = gate.threshold - 20, gate.threshold - dynlab.GATE_HYSTERESIS_DB / 2
        key_hold = round(dynlab.GATE_KEY_HOLD_MS * fs / 1000)
        for _ in range(key_hold + 1):
            detector.process(gate.threshold + 20)
        # 15 ms under the closing level, past the key hold but within Gate Hold, then one sample back between.
        for _ in range(key_hold + round(0.015 * fs)):
            detector.process(quiet)
        detector.process(between)
        # A whole key hold and Gate Hold again before Gate Release begins.
        for n in range(key_hold + round(0.020 * fs)):
            self.assertEqual(detector.process(quiet), 0.0, n)
        self.assertGreater(detector.process(quiet), 0.0)

    def test_with_no_key_it_opens_at_gate_attack_and_stays_open(self):
        fs = 48000
        gate = dynlab.Gate()
        detector = dynlab.GateDetector(gate, fs, gate.range, is_open=False)
        reductions = [detector.process(None) for _ in range(fs)]
        attack_63 = next(n for n, r in enumerate(reductions) if r <= gate.range / math.e)
        self.assertLessEqual(abs(attack_63 + 1 - gate.attack * fs / 1000), 1)
        self.assertEqual(reductions[-1], 0.0)

    def test_open_and_settled_it_asks_for_no_gain_reduction_exactly(self):
        gate = dynlab.Gate()
        detector = dynlab.GateDetector(gate, 48000, gate.range, is_open=False)
        reductions = [detector.process(gate.threshold) for _ in range(48000)]
        self.assertEqual(reductions[-1], 0.0)

    def test_the_constants_are_the_trade_offs_choice_and_keep_every_signal_steady(self):
        rows = dynlab.gate_trade_off()
        self.assertEqual(dynlab.chosen_gate_constants(rows), (dynlab.GATE_KEY_HOLD_MS, dynlab.GATE_HYSTERESIS_DB))
        # With no key hold every low signal ripples, and how late a stop closes is the key hold.
        for row in rows:
            if row.key_hold == 0:
                self.assertFalse(row.steady)
            self.assertAlmostEqual(row.stop, row.key_hold, delta=1000 / dynlab.GATE_TRADE_OFF_RATE)
            self.assertAlmostEqual(row.fade, row.key_hold + 1000 * row.hysteresis / dynlab.GATE_FADE_DB_PER_S,
                                   delta=1000 / dynlab.GATE_TRADE_OFF_RATE)


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
        # The Gate's, under the compressor's ranges of the same names.
        gate = dynlab.Gate()
        for name, (low_high, _) in dynlab.GATE_PARAMETERS.items():
            ranges[f"gate {name}"] = low_high
            defaults[f"gate {name}"] = getattr(gate, name)
        with open(os.path.join(LAB, "USAGE.md")) as doc:
            rows = {cells[0].lower(): cells[1:] for cells in
                    ([cell.strip() for cell in line.strip().strip("|").split("|")] for line in doc)
                    if cells[0].lower() in ranges}
        self.assertEqual(set(rows), set(ranges))
        for name, (low, high) in ranges.items():
            with self.subTest(name):
                shown_range, shown_default = rows[name]
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
                            (("limiter", "--ceiling", "1"), ("Ceiling", "-30", "0")),
                            (("gate", "--gate-threshold", "-61"), ("Gate threshold", "-60", "0")),
                            (("gate", "--gate-range", "61"), ("Gate range", "0", "60")),
                            (("gate", "--gate-attack", "0"), ("Gate attack", "0.005", "250")),
                            (("gate", "--gate-release", "1"), ("Gate release", "5", "5000")),
                            (("gate", "--gate-hold", "501"), ("Gate hold", "0", "500"))):
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
