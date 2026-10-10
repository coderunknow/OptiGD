"""TOOL TESTS for tools/analyze_frametimes.py.

These tests use SYNTHETIC CSV files generated inside the test. They check the analyzer's
arithmetic, input validation, and warm-up handling. They say nothing about Geometry Dash
performance and must never be cited as gameplay evidence.
"""

import csv
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import analyze_frametimes as af  # noqa: E402


def write_csv(path: Path, intervals_us, tier="LowEnd", header=None):
    with path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(header if header is not None else af.EXPECTED_HEADER)
        for i, interval in enumerate(intervals_us):
            w.writerow([i, interval, 1000.0, 5.0, 0.0, 10, 5, 5, 0, tier])


class AnalyzerArithmeticTests(unittest.TestCase):
    """SYNTHETIC input: verifies the analyzer's arithmetic only."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_percentile_is_nearest_rank(self):
        values = [float(v) for v in range(1, 101)]  # 1..100
        self.assertEqual(af.percentile(values, 0.95), 95.0)
        self.assertEqual(af.percentile(values, 0.99), 99.0)
        self.assertEqual(af.percentile(values, 1.0), 100.0)

    def test_summary_of_constant_frames(self):
        path = self.dir / "const.csv"
        write_csv(path, [16666.0] * 200)
        s = af.summarize(af.load_csv(path, warmup_frames=0), target_fps=60.0)
        self.assertAlmostEqual(s["meanFrameMs"], 16.666, places=6)
        self.assertAlmostEqual(s["meanFps"], 1000.0 / 16.666, places=6)
        self.assertEqual(s["framesUsed"], 200)
        self.assertEqual(s["spikesOver33ms"], 0)
        self.assertEqual(s["framesOverBudget"], 0)
        self.assertEqual(s["status"], "measured")

    def test_spikes_and_budget_counts(self):
        path = self.dir / "mixed.csv"
        write_csv(path, [10000.0] * 90 + [20000.0] * 5 + [40000.0] * 5)
        s = af.summarize(af.load_csv(path, warmup_frames=0), target_fps=60.0)
        self.assertEqual(s["spikesOver33ms"], 5)
        self.assertEqual(s["framesOverBudget"], 10)

    def test_warmup_and_zero_intervals_are_excluded(self):
        path = self.dir / "warm.csv"
        write_csv(path, [0.0] + [50000.0] * 60 + [10000.0] * 40)
        s = af.summarize(af.load_csv(path, warmup_frames=61), target_fps=60.0)
        # Rows are frames 0..100. Frame 0 has interval 0; frames 1..60 are warm-up (excluded by
        # warmup_frames=61); frames 61..100 remain: 40 usable samples.
        self.assertEqual(s["framesUsed"], 40)
        self.assertAlmostEqual(s["meanFrameMs"], 10.0)
        self.assertEqual(s["spikesOver33ms"], 0)

    def test_compare_reports_relative_fps_change(self):
        base, cand = self.dir / "base.csv", self.dir / "cand.csv"
        write_csv(base, [20000.0] * 1800)  # 50 fps
        write_csv(cand, [16666.0] * 1800)  # ~60 fps
        result = af.compare(af.load_csv(base, 0), af.load_csv(cand, 0), 60.0, 1800, iterations=200)
        self.assertAlmostEqual(result["deltaMeanFpsPercent"], 20.0, delta=0.1)
        self.assertLess(result["deltaMeanFrameMs"], 0)
        ci = result["meanFrameDiffCI95"]
        self.assertLessEqual(ci["lowMs"], ci["highMs"])
        self.assertLess(ci["highMs"], 0)  # constant data: CI sits below zero

    def test_compare_refuses_short_runs(self):
        base, cand = self.dir / "b.csv", self.dir / "c.csv"
        write_csv(base, [16666.0] * 100)
        write_csv(cand, [16666.0] * 100)
        with self.assertRaises(ValueError):
            af.compare(af.load_csv(base, 0), af.load_csv(cand, 0), 60.0, 1800, iterations=10)

    def test_bootstrap_is_deterministic(self):
        a = [10.0, 12.0, 11.0, 13.0] * 20
        b = [9.0, 10.0, 9.5, 10.5] * 20
        first = af.bootstrap_mean_diff_ci(a, b, 300, seed=1)
        second = af.bootstrap_mean_diff_ci(a, b, 300, seed=1)
        self.assertEqual(first, second)


class AnalyzerInputValidationTests(unittest.TestCase):
    """SYNTHETIC input: verifies that malformed files are rejected."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_wrong_header_is_rejected(self):
        path = self.dir / "bad_header.csv"
        write_csv(path, [16666.0] * 5, header=["frame", "interval"])
        with self.assertRaises(af.FrameTimeFormatError):
            af.load_csv(path, 0)

    def test_wrong_column_count_is_rejected(self):
        path = self.dir / "short_row.csv"
        with path.open("w", newline="", encoding="utf-8") as fh:
            fh.write(",".join(af.EXPECTED_HEADER) + "\n1,16666\n")
        with self.assertRaises(af.FrameTimeFormatError):
            af.load_csv(path, 0)

    def test_empty_file_is_rejected(self):
        path = self.dir / "empty.csv"
        path.write_text("", encoding="utf-8")
        with self.assertRaises(af.FrameTimeFormatError):
            af.load_csv(path, 0)

    def test_cli_summary_prints_json(self):
        path = self.dir / "cli.csv"
        write_csv(path, [16666.0] * 100)
        import io
        from contextlib import redirect_stdout

        buf = io.StringIO()
        with redirect_stdout(buf):
            code = af.main(["summary", str(path), "--warmup-frames", "0"])
        self.assertEqual(code, 0)
        data = json.loads(buf.getvalue())
        self.assertEqual(data["framesUsed"], 100)

    def test_cli_reports_errors_with_exit_code(self):
        code = af.main(["summary", str(self.dir / "missing.csv")])
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
