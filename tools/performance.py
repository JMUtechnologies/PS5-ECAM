#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare explicitly labelled, externally captured ECAM frame-time traces.

This does not capture telemetry or modify a console. See docs/PERFORMANCE-TESTING.md.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

SYSTEMS = ("ds", "3ds", "wiiu", "switch")
CONTEXT = ("system", "game_id", "game_version", "scene", "console_model",
           "firmware", "hen", "settings", "cache_state", "capture_method", "frame_kind")
REQUIRED = CONTEXT + ("build",)
MAX_ROWS = 2_000_000


def number(value, name, positive=False):
    if isinstance(value, bool):
        raise ValueError(f"{name} must be a finite number")
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise ValueError(f"{name} must be a finite number") from None
    if not math.isfinite(result) or (result <= 0 if positive else result < 0):
        raise ValueError(f"{name} must be {'positive' if positive else 'nonnegative'} and finite")
    return result


def read_run(csv_path, metadata_path, warmup, duration):
    metadata = json.loads(Path(metadata_path).read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise ValueError("metadata must be an object")
    for key in REQUIRED:
        value = metadata.get(key)
        if key == "settings":
            if not isinstance(value, dict) or not value:
                raise ValueError("settings must be a nonempty object containing effective settings")
        elif not isinstance(value, str) or not value.strip():
            raise ValueError(f"metadata requires nonempty {key}")
    if metadata["system"] not in SYSTEMS:
        raise ValueError(f"system must be one of {', '.join(SYSTEMS)}")
    if metadata["cache_state"] not in ("cold", "warm"):
        raise ValueError("cache_state must be cold or warm")
    if metadata["frame_kind"] not in ("emulated", "presented"):
        raise ValueError("frame_kind must be emulated or presented")
    # Each row is one interval ending at elapsed_s, measured from capture start.
    # Validate the entire capture, including rows outside the measurement window.
    samples = []
    previous = None
    with Path(csv_path).open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["elapsed_s", "frame_ms"]:
            raise ValueError("CSV header must be exactly elapsed_s,frame_ms")
        for line, row in enumerate(reader, 2):
            if line > MAX_ROWS + 1:
                raise ValueError(f"capture exceeds {MAX_ROWS} rows")
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"CSV row {line} has the wrong number of columns")
            elapsed = number(row["elapsed_s"], f"row {line} elapsed_s")
            frame = number(row["frame_ms"], f"row {line} frame_ms", positive=True)
            if previous is not None:
                if elapsed <= previous:
                    raise ValueError(f"row {line}: elapsed_s must strictly increase")
                # A capture must contain consecutive frame intervals, not periodic FPS samples.
                if not math.isclose((elapsed - previous) * 1000, frame,
                                    rel_tol=0.02, abs_tol=0.1):
                    raise ValueError(f"row {line}: timestamp interval does not match frame_ms")
            previous = elapsed
            start = elapsed - frame / 1000
            if start < -0.0001:
                raise ValueError(f"row {line}: frame starts before capture")
            if start >= warmup - 1e-9 and (duration is None or elapsed <= warmup + duration + 1e-9):
                samples.append(frame)
    if not samples:
        raise ValueError("no complete frames in measurement window")
    if duration is not None and (previous is None or previous < warmup + duration - 0.0001):
        raise ValueError("capture ends before requested measurement window")
    ordered = sorted(samples)
    slow_count = max(1, math.ceil(len(ordered) * 0.01))
    seconds = sum(samples) / 1000
    metrics = {
        "frames": len(samples), "measured_s": seconds,
        "average_fps": len(samples) / seconds,
        "median_ms": statistics.median(samples),
        "p95_ms": ordered[math.ceil(len(ordered) * 0.95) - 1],
        "p99_ms": ordered[math.ceil(len(ordered) * 0.99) - 1],
        "worst_ms": ordered[-1],
        "one_percent_low_fps": 1000 / statistics.mean(ordered[-slow_count:]),
    }
    return {"metadata": metadata, "metrics": metrics,
            "sha256": {"csv": hashlib.sha256(Path(csv_path).read_bytes()).hexdigest(),
                       "metadata": hashlib.sha256(Path(metadata_path).read_bytes()).hexdigest()}}


def compare(baseline, candidate, threshold, min_seconds, min_frames):
    mismatches = [key for key in CONTEXT
                  if baseline["metadata"][key] != candidate["metadata"][key]]
    if mismatches:
        raise ValueError("incomparable runs; metadata differs: " + ", ".join(mismatches))
    for label, run in (("baseline", baseline), ("candidate", candidate)):
        m = run["metrics"]
        if m["frames"] < min_frames or m["measured_s"] < min_seconds:
            raise ValueError(f"{label}: insufficient data; need {min_frames} frames and {min_seconds:g} measured seconds")
    changes = {}
    regressions = []
    # Positive values always mean worse; keep FPS and frame-time directions explicit.
    for name in ("average_fps", "one_percent_low_fps", "p95_ms", "p99_ms"):
        before = baseline["metrics"][name]
        after = candidate["metrics"][name]
        worse = (before - after) / before if name.endswith("fps") else (after - before) / before
        changes[name] = worse * 100
        if worse * 100 > threshold + 1e-9:
            regressions.append(name)
    return {"schema_version": 1, "status": "regression" if regressions else "within_threshold",
            "threshold_percent": threshold, "regressions": regressions,
            "worsening_percent": changes, "baseline": baseline, "candidate": candidate}


def markdown(report):
    meta = report["baseline"]["metadata"]
    def cell(value):
        return str(value).replace("\\", "\\\\").replace("|", "\\|").replace("\r", " ").replace("\n", " ").replace("<", "&lt;").replace(">", "&gt;")
    rows = ["# ECAM performance comparison", "",
            f"Result: **{report['status']}** (threshold: {report['threshold_percent']:g}%).",
            "", "| Context | Value |", "|---|---|"]
    for key in CONTEXT:
        rows.append(f"| {key} | {cell(json.dumps(meta[key], sort_keys=True) if key == 'settings' else meta[key])} |")
    rows += ["", "| Metric | Baseline | Candidate | Worse (%) |", "|---|---:|---:|---:|"]
    for key, before in report["baseline"]["metrics"].items():
        after = report["candidate"]["metrics"][key]
        change = report["worsening_percent"].get(key)
        delta = "—" if change is None else f"{change:+.2f}"
        rows.append(f"| {key} | {before:.3f} | {after:.3f} | {delta} |")
    rows += ["", "| Run | Build | CSV SHA-256 | Metadata SHA-256 |", "|---|---|---|---|"]
    for label in ("baseline", "candidate"):
        run = report[label]
        rows.append(f"| {label} | {cell(run['metadata']['build'])} | {run['sha256']['csv']} | {run['sha256']['metadata']} |")
    rows += ["", "This is a single-pair screening result, not proof of an improvement or statistical significance.",
             "Repeat matched runs. Presented frames can include generated frames; they do not measure emulation speed.",
             "Crashes, save integrity, audio and controls require separate console checks.", ""]
    return "\n".join(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--baseline-meta", required=True, type=Path)
    parser.add_argument("--candidate-meta", required=True, type=Path)
    parser.add_argument("--warmup-s", type=float, default=10)
    parser.add_argument("--duration-s", type=float, default=60)
    parser.add_argument("--max-regression-percent", type=float, default=5)
    parser.add_argument("--min-seconds", type=float, default=30)
    parser.add_argument("--min-frames", type=int, default=300)
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    args = parser.parse_args(argv)
    try:
        warmup = number(args.warmup_s, "warmup")
        duration = number(args.duration_s, "duration", positive=True)
        threshold = number(args.max_regression_percent, "threshold")
        min_seconds = number(args.min_seconds, "minimum seconds", positive=True)
        if args.min_frames < 1:
            raise ValueError("minimum frames must be positive")
        baseline = read_run(args.baseline, args.baseline_meta, warmup, duration)
        candidate = read_run(args.candidate, args.candidate_meta, warmup, duration)
        report = compare(baseline, candidate, threshold, min_seconds, args.min_frames)
        report["window"] = {"warmup_s": warmup, "duration_s": duration}
        print(json.dumps(report, indent=2, allow_nan=False) if args.format == "json" else markdown(report))
        return 1 if report["regressions"] else 0
    except (ValueError, OSError, csv.Error) as error:
        print(f"performance: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
