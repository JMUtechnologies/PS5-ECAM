#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
spec = importlib.util.spec_from_file_location("suite", Path(__file__).parents[1] / "performance-suite.py")
suite = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite)


class SuiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = self.root / "suite.json"
        self.case = {"name": "test scene", "system": "ds",
                     "console_checks": dict.fromkeys(suite.CHECKS, "pass"), "pairs": []}
        for repeat in range(3):
            pair = {}
            for side in ("baseline", "candidate"):
                stem = f"{side}-{repeat}"
                metadata = dict(system="ds", game_id="homebrew", game_version="1", scene="fixed",
                                console_model="base", firmware="4.50", hen="test", settings={"scale": 1},
                                cache_state="warm", capture_method="synthetic test", frame_kind="emulated", build=side)
                (self.root / (stem + ".json")).write_text(json.dumps(metadata))
                # Different traces per repeat; each matched pair uses the same workload.
                step = 10 + repeat * .01
                text = "elapsed_s,frame_ms\n" + "".join(f"{i * step / 1000:.9f},{step}\n" for i in range(1, 1001))
                (self.root / (stem + ".csv")).write_text(text)
                pair[side + "_csv"] = stem + ".csv"
                pair[side + "_meta"] = stem + ".json"
            self.case["pairs"].append(pair)

    def evaluate(self, cases=None, **kwargs):
        self.manifest.write_text(json.dumps({"schema_version": 1, "cases": cases or [self.case]}))
        return suite.evaluate(self.manifest, warmup=0, duration=5, min_seconds=1, min_frames=100, **kwargs)

    def test_repeated_success(self):
        report = self.evaluate()
        self.assertEqual(report["acceptance"], "passed")
        case = report["cases"][0]
        self.assertEqual(case["repeat_count"], 3)
        self.assertEqual(case["summary"]["p99_ms"]["worsening_median_percent"], 0)

    def test_one_regressed_pair_fails(self):
        path = self.root / "candidate-1.csv"
        path.write_text("elapsed_s,frame_ms\n" + "".join(f"{i * .02:.9f},20\n" for i in range(1, 501)))
        report = self.evaluate()
        self.assertEqual(report["acceptance"], "failed")
        self.assertEqual(report["cases"][0]["regressed_pairs"], 1)

    def test_untested_and_failed_console_checks(self):
        del self.case["console_checks"]["audio"]
        self.assertEqual(self.evaluate()["acceptance"], "incomplete")
        self.case["console_checks"]["audio"] = "fail"
        self.assertEqual(self.evaluate()["acceptance"], "failed")

    def test_capture_cannot_be_reused(self):
        self.case["pairs"][1] = self.case["pairs"][0].copy()
        result = self.evaluate()
        self.assertTrue(result["invalid"])
        self.assertIn("duplicate", result["cases"][0]["error"])

    def test_bad_case_does_not_hide_good_case(self):
        report = self.evaluate([self.case, {"name": "missing input", "system": "switch", "pairs": []}])
        self.assertEqual(len(report["cases"]), 2)
        self.assertEqual(report["cases"][0]["acceptance"], "passed")
        self.assertTrue(report["invalid"])

    def test_all_system_gate(self):
        report = self.evaluate(require_all=True)
        self.assertEqual(report["missing_systems"], ["3ds", "switch", "wiiu"])
        self.assertTrue(report["invalid"])

    def test_paths_cannot_escape(self):
        for path in ("../outside.csv", "/tmp/outside.csv", ""):
            self.case["pairs"][0]["baseline_csv"] = path
            with self.subTest(path=path):
                self.assertTrue(self.evaluate()["invalid"])

    def test_symlink_cannot_escape(self):
        (self.root / "external").symlink_to(self.root.parent, target_is_directory=True)
        self.case["pairs"][0]["baseline_csv"] = "external/outside.csv"
        self.assertTrue(self.evaluate()["invalid"])

    def test_repeats_cannot_change_build_or_context(self):
        path = self.root / "candidate-2.json"
        metadata = json.loads(path.read_text())
        metadata["build"] = "another candidate"
        path.write_text(json.dumps(metadata))
        self.assertIn("build pair", self.evaluate()["cases"][0]["error"])

    def test_duplicate_names(self):
        self.assertTrue(self.evaluate([self.case, self.case])["invalid"])

    def test_html_escapes_untrusted_evidence(self):
        self.case["name"] = "<script>alert('x')</script>"
        report = self.evaluate()
        rendered = suite.render_html(report)
        self.assertNotIn("<script>", rendered)
        self.assertIn("&lt;script&gt;", rendered)
        self.assertIn("Content-Security-Policy", rendered)
        self.assertNotIn("src=", rendered)

    def test_template_covers_four_systems_without_fake_results(self):
        template = suite.template()
        self.assertEqual({c["system"] for c in template["cases"]}, set(suite.perf.SYSTEMS))
        self.assertTrue(all(len(c["pairs"]) == 3 for c in template["cases"]))
        self.assertTrue(all(set(c["console_checks"].values()) == {"untested"} for c in template["cases"]))

    def test_cli_outputs_and_exit_codes(self):
        self.evaluate()
        args = [str(self.manifest), "--warmup-s", "0", "--duration-s", "5", "--min-seconds", "1", "--min-frames", "100"]
        for output_format in ("html", "json", "markdown"):
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(suite.main(args + ["--format", output_format]), 0)
            self.assertTrue(output.getvalue())
        self.case["console_checks"]["audio"] = "untested"
        self.evaluate()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(suite.main(args), 1)
            self.assertEqual(suite.main(args + ["--require-all-systems"]), 2)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(suite.main(args + ["--duration-s", "nan"]), 2)


if __name__ == "__main__":
    unittest.main()
