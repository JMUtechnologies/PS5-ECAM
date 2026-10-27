#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Batch and repeat ECAM performance comparisons; emit JSON, Markdown or offline HTML."""
import argparse
import html
import json
from pathlib import Path
import statistics
import sys

import performance as perf

CHECKS = ("launch", "audio", "controls", "save_load", "quit_to_library", "relaunch")
METRICS = ("average_fps", "one_percent_low_fps", "p95_ms", "p99_ms")


def template():
    return {"schema_version": 1, "cases": [
        {"name": f"{system} matched scene", "system": system,
         "console_checks": dict.fromkeys(CHECKS, "untested"),
         "pairs": [{"baseline_csv": f"captures/{system}/baseline-{i}.csv",
                    "candidate_csv": f"captures/{system}/candidate-{i}.csv",
                    "baseline_meta": f"captures/{system}/baseline-{i}.json",
                    "candidate_meta": f"captures/{system}/candidate-{i}.json"}
                   for i in range(1, 4)]} for system in perf.SYSTEMS]}


def resolve(root, value):
    if not isinstance(value, str) or not value.strip():
        raise ValueError("capture paths must be nonempty strings")
    path = Path(value)
    if path.is_absolute():
        raise ValueError("capture paths must be relative to the suite manifest")
    path = (root / path).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("capture paths must stay inside the manifest directory")
    return path


def evaluate_case(case, root, warmup, duration, threshold, min_seconds, min_frames, repeats):
    if not isinstance(case, dict):
        raise ValueError("each case must be an object")
    name, system = case.get("name"), case.get("system")
    if not isinstance(name, str) or not name.strip() or system not in perf.SYSTEMS:
        raise ValueError("each case needs a name and a supported system")
    pairs = case.get("pairs")
    if not isinstance(pairs, list) or len(pairs) < repeats:
        raise ValueError(f"{name}: requires at least {repeats} matched pairs")
    checks = case.get("console_checks", {})
    if not isinstance(checks, dict) or set(checks) - set(CHECKS):
        raise ValueError(f"{name}: unknown console check")
    checks = {key: checks.get(key, "untested") for key in CHECKS}
    if any(value not in ("pass", "fail", "untested") for value in checks.values()):
        raise ValueError(f"{name}: checks must be pass, fail or untested")
    results, seen = [], set()
    reference = None
    builds = None
    for index, pair in enumerate(pairs, 1):
        if not isinstance(pair, dict):
            raise ValueError(f"{name}: each pair must be an object")
        runs = []
        for side in ("baseline", "candidate"):
            capture = resolve(root, pair.get(f"{side}_csv"))
            meta = resolve(root, pair.get(f"{side}_meta"))
            # Reusing a capture, even through another filename, is not another trial.
            run = perf.read_run(capture, meta, warmup, duration)
            digest = (side, run["sha256"]["csv"])
            if digest in seen:
                raise ValueError(f"{name}: duplicate {side} capture in repeat {index}")
            seen.add(digest)
            runs.append(run)
        baseline, candidate = runs
        context = {key: baseline["metadata"][key] for key in perf.CONTEXT}
        versions = (baseline["metadata"]["build"], candidate["metadata"]["build"])
        if context["system"] != system:
            raise ValueError(f"{name}: capture system does not match manifest")
        if reference is not None and (context != reference or versions != builds):
            raise ValueError(f"{name}: repeats must use the same context and build pair")
        reference, builds = context, versions
        results.append(perf.compare(baseline, candidate, threshold, min_seconds, min_frames))
    summary = {}
    for metric in METRICS:
        baseline = [r["baseline"]["metrics"][metric] for r in results]
        candidate = [r["candidate"]["metrics"][metric] for r in results]
        changes = [r["worsening_percent"][metric] for r in results]
        summary[metric] = {
            "baseline_median": statistics.median(baseline),
            "candidate_median": statistics.median(candidate),
            "worsening_median_percent": statistics.median(changes),
            "worsening_min_percent": min(changes), "worsening_max_percent": max(changes)}
    regressions = sum(r["status"] == "regression" for r in results)
    acceptance = ("failed" if regressions or "fail" in checks.values() else
                  "incomplete" if "untested" in checks.values() else "passed")
    return {"name": name, "system": system, "repeat_count": len(results),
            "performance_status": "regression" if regressions else "within_threshold",
            "regressed_pairs": regressions, "acceptance": acceptance,
            "console_checks": checks, "summary": summary, "pairs": results}


def evaluate(manifest_path, warmup=10, duration=60, threshold=5,
             min_seconds=30, min_frames=300, repeats=3, require_all=False):
    manifest_path = Path(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("suite requires schema_version 1")
    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("suite requires a nonempty cases array")
    # Keep valid cases visible when another case has missing or invalid evidence.
    results, names = [], set()
    for index, case in enumerate(cases, 1):
        try:
            result = evaluate_case(case, manifest_path.parent, warmup, duration, threshold,
                                   min_seconds, min_frames, repeats)
            if result["name"] in names:
                raise ValueError("case names must be unique")
            names.add(result["name"])
            results.append(result)
        except (ValueError, OSError, perf.csv.Error) as error:
            results.append({"name": case.get("name", f"case {index}") if isinstance(case, dict)
                            else f"case {index}", "performance_status": "invalid",
                            "acceptance": "incomplete", "error": str(error)})
    represented = {r["system"] for r in results if "system" in r}
    missing = sorted(set(perf.SYSTEMS) - represented) if require_all else []
    invalid = any(r["performance_status"] == "invalid" for r in results) or bool(missing)
    failed = any(r["acceptance"] == "failed" for r in results)
    incomplete = any(r["acceptance"] == "incomplete" for r in results)
    return {"schema_version": 1, "acceptance": "failed" if failed else
            "incomplete" if invalid or incomplete else "passed",
            "invalid": invalid, "missing_systems": missing,
            "protocol": {"warmup_s": warmup, "duration_s": duration,
                         "threshold_percent": threshold, "minimum_repeats": repeats},
            "cases": results}


def render_markdown(report):
    def safe(value):
        return html.escape(str(value)).replace("|", "\\|").replace("\n", " ").replace("\r", " ")
    lines = ["# ECAM regression suite", "", f"Console acceptance: **{report['acceptance']}**.",
             "", "| Case | Performance | Pairs regressed | Console acceptance |",
             "|---|---|---:|---|"]
    for case in report["cases"]:
        lines.append(f"| {safe(case['name'])} | {case['performance_status']} | "
                     f"{case.get('regressed_pairs', '—')} | {case['acceptance']} |")
        if "error" in case:
            lines.append(f"\nEvidence error: {safe(case['error'])}\n")
    if report["missing_systems"]:
        lines += ["", "Missing valid systems: " + ", ".join(report["missing_systems"])]
    lines += ["", "All pairs remain in the JSON report; one regressed pair fails the case.",
              "Console checks are tester attestations. Untested checks prevent acceptance.",
              "No result proves statistical significance or a speed improvement.", ""]
    return "\n".join(lines)


def render_html(report):
    esc = lambda value: html.escape(str(value), quote=True)
    sections = []
    for case in report["cases"]:
        content = f"<h2>{esc(case['name'])}</h2><p>{esc(case['performance_status'])} · Console: {esc(case['acceptance'])}</p>"
        if "error" in case:
            content += f"<p class='error'>{esc(case['error'])}</p>"
        else:
            content += "<table><caption>Repeated-run results</caption><thead><tr><th>Metric</th><th>Baseline median</th><th>Candidate median</th><th>Worse % median (range)</th></tr></thead><tbody>"
            for key, metric in case["summary"].items():
                content += (f"<tr><th>{esc(key)}</th><td>{metric['baseline_median']:.3f}</td>"
                            f"<td>{metric['candidate_median']:.3f}</td><td>{metric['worsening_median_percent']:+.2f} "
                            f"({metric['worsening_min_percent']:+.2f} to {metric['worsening_max_percent']:+.2f})</td></tr>")
            content += "</tbody></table><ul>"
            for key, value in case["console_checks"].items():
                content += f"<li>{esc(key)}: {esc(value)}</li>"
            content += "</ul><details><summary>Full evidence, settings and input hashes</summary><pre>"
            content += esc(json.dumps(case["pairs"], indent=2, allow_nan=False)) + "</pre></details>"
        sections.append("<section>" + content + "</section>")
    return ("<!doctype html><html lang='en'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<meta http-equiv='Content-Security-Policy' content=\"default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">"
            "<title>ECAM regression suite</title><style>body{font:16px system-ui;max-width:1100px;margin:2rem auto;padding:0 1rem;color:#172533;background:#f5f7fa}"
            "section{background:white;padding:1.4rem;margin:1rem 0;border:1px solid #ccd5df;border-radius:12px}"
            "table{border-collapse:collapse;width:100%;display:block;overflow:auto}th,td{padding:.65rem;border:1px solid #ccd5df;text-align:left}"
            "pre{white-space:pre-wrap;overflow-wrap:anywhere}.error{color:#a11818}summary{cursor:pointer}caption{text-align:left;font-weight:bold}</style>"
            f"<main><h1>ECAM regression suite</h1><p>Console acceptance: <strong>{esc(report['acceptance'])}</strong></p>"
            f"<p>Missing required systems: {esc(', '.join(report['missing_systems']) or 'none')}</p>"
            "<p>One regressed pair fails a case. Untested console checks prevent acceptance. "
            "Tester-supplied data is not independently verified. Repeated runs do not establish statistical significance.</p>"
            + "".join(sections) + "</main></html>")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path)
    parser.add_argument("--template", action="store_true")
    parser.add_argument("--format", choices=("json", "markdown", "html"), default="markdown")
    parser.add_argument("--warmup-s", type=float, default=10)
    parser.add_argument("--duration-s", type=float, default=60)
    parser.add_argument("--max-regression-percent", type=float, default=5)
    parser.add_argument("--min-seconds", type=float, default=30)
    parser.add_argument("--min-frames", type=int, default=300)
    parser.add_argument("--min-repeats", type=int, default=3)
    parser.add_argument("--require-all-systems", action="store_true")
    args = parser.parse_args(argv)
    if args.template:
        print(json.dumps(template(), indent=2))
        return 0
    try:
        if args.manifest is None:
            raise ValueError("provide a suite manifest or use --template")
        if args.min_frames < 1 or args.min_repeats < 1:
            raise ValueError("minimum frames and repeats must be positive")
        report = evaluate(args.manifest, perf.number(args.warmup_s, "warmup"),
                          perf.number(args.duration_s, "duration", True),
                          perf.number(args.max_regression_percent, "threshold"),
                          perf.number(args.min_seconds, "minimum seconds", True),
                          args.min_frames, args.min_repeats, args.require_all_systems)
        if args.format == "json":
            output = json.dumps(report, indent=2, allow_nan=False)
        else:
            output = render_html(report) if args.format == "html" else render_markdown(report)
        print(output)
        return 2 if report["invalid"] else 1 if report["acceptance"] != "passed" else 0
    except (ValueError, OSError) as error:
        print(f"performance-suite: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())

