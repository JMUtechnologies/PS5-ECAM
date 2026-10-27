#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("performance", Path(__file__).parents[1] / "performance.py")
perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf)


class PerformanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.csv = self.root / "frames.csv"
        self.meta = self.root / "run.json"
        self.metadata = dict(system="switch", game_id="test-homebrew", game_version="1",
                             scene="fixed camera", console_model="base", firmware="4.50",
                             hen="test", settings={"resolution": "1x", "frame_generation": False},
                             cache_state="warm", capture_method="test fixture",
                             frame_kind="emulated", build="baseline")
        self.meta.write_text(json.dumps(self.metadata))

    def run_data(self, frames, warmup=0, duration=None):
        elapsed = 0
        rows = ["elapsed_s,frame_ms"]
        for frame in frames:
            elapsed += frame / 1000
            rows.append(f"{elapsed:.9f},{frame}")
        self.csv.write_text("\n".join(rows) + "\n")
        return perf.read_run(self.csv, self.meta, warmup, duration)

    def test_weighted_fps_and_slow_tail(self):
        m = self.run_data([10] * 99 + [100])["metrics"]
        self.assertAlmostEqual(m["average_fps"], 100000 / 1090)
        self.assertEqual(m["one_percent_low_fps"], 10)
        self.assertEqual(m["p99_ms"], 10)
        self.assertEqual(m["worst_ms"], 100)

    def test_window_excludes_straddling_frames(self):
        self.assertEqual(self.run_data([100] * 20, .15, .5)["metrics"]["frames"], 4)

    def test_short_capture_rejected(self):
        with self.assertRaisesRegex(ValueError, "ends before"):
            self.run_data([10] * 10, 0, 1)

    def test_invalid_numbers(self):
        for value in ("nan", "inf", "-inf", "-1", "0", "oops"):
            self.csv.write_text(f"elapsed_s,frame_ms\n0.01,{value}\n")
            with self.subTest(value=value), self.assertRaises(ValueError):
                perf.read_run(self.csv, self.meta, 0, None)

    def test_missing_context(self):
        for field in perf.REQUIRED:
            data = self.metadata.copy()
            del data[field]
            self.meta.write_text(json.dumps(data))
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.run_data([10] * 10)

    def test_mismatched_context_rejected(self):
        baseline = self.run_data([10] * 100)
        for field in perf.CONTEXT:
            candidate = copy.deepcopy(baseline)
            candidate["metadata"][field] = "changed"
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "incomparable"):
                perf.compare(baseline, candidate, 5, .1, 1)

    def test_regression_and_improvement_directions(self):
        baseline = self.run_data([10] * 100)
        slow = self.run_data([12] * 100)
        fast = self.run_data([8] * 100)
        self.assertEqual(len(perf.compare(baseline, slow, 5, .1, 1)["regressions"]), 4)
        self.assertEqual(perf.compare(baseline, fast, 5, .1, 1)["status"], "within_threshold")

    def test_threshold_boundary(self):
        baseline = self.run_data([10] * 100)
        candidate = self.run_data([10.5] * 100)
        self.assertEqual(perf.compare(baseline, candidate, 5, .1, 1)["regressions"], [])

    def test_insufficient_evidence(self):
        run = self.run_data([10] * 100)
        for seconds, frames in ((30, 1), (.1, 300)):
            with self.assertRaisesRegex(ValueError, "insufficient"):
                perf.compare(run, run, 5, seconds, frames)

    def test_malformed_and_nonconsecutive_rows(self):
        for text in ("fps\n60\n", "elapsed_s,frame_ms\n0.01,10,extra\n",
                     "elapsed_s,frame_ms\n0.01,10\n0.01,10\n",
                     "elapsed_s,frame_ms\n0.01,10\n1,10\n",
                     "elapsed_s,frame_ms\n0.001,10\n"):
            self.csv.write_text(text)
            with self.subTest(text=text), self.assertRaises(ValueError):
                perf.read_run(self.csv, self.meta, 0, None)

    def test_all_four_systems_and_hashes(self):
        for system in perf.SYSTEMS:
            self.metadata["system"] = system
            self.meta.write_text(json.dumps(self.metadata))
            run = self.run_data([10] * 100)
            self.assertEqual(len(run["sha256"]["csv"]), 64)
            self.assertEqual(run["metadata"]["system"], system)

    def test_cli_exit_codes_and_json(self):
        self.run_data([10] * 8000)
        candidate = self.root / "candidate.csv"
        candidate.write_bytes(self.csv.read_bytes())
        args = [str(self.csv), str(candidate), "--baseline-meta", str(self.meta),
                "--candidate-meta", str(self.meta), "--format", "json"]
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(perf.main(args), 0)
        self.assertEqual(json.loads(output.getvalue())["schema_version"], 1)
        self.run_data([12] * 8000)
        candidate.write_bytes(self.csv.read_bytes())
        self.run_data([10] * 8000)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(perf.main(args), 1)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(perf.main(args + ["--duration-s", "nan"]), 2)

    def test_report_escapes_context(self):
        run = self.run_data([10] * 100)
        run["metadata"]["scene"] = "<b>one|two\nthree"
        report = perf.markdown(perf.compare(run, run, 5, .1, 1))
        self.assertIn("&lt;b&gt;one\\|two three", report)
        self.assertIn("not proof", report)


if __name__ == "__main__":
    unittest.main()

