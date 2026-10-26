# Checking performance changes without changing the console runtime

`tools/performance.py` compares two externally captured frame-time CSV files. It uses only
Python's standard library and runs on macOS, Linux and Windows. It does not connect to the PS5,
write saves or settings, change an emulator, or claim a game is compatible.

This is a review tool for DS, 3DS, Wii U and Switch improvements, including fixes brought over
from standalone emulator ports. It is not a performance optimization itself.

## Capture contract

Use an instrumented build or an external frame-time capture source that exports consecutive
frame intervals. ECAM does **not** currently export this CSV automatically. Ordinary periodic
FPS log lines and the sampling profiler's `profile.bin` are not compatible inputs. Do not
convert their averages into invented frame samples.

Each UTF-8 CSV has exactly these columns (the values below illustrate the format only):

```csv
elapsed_s,frame_ms
0.016666667,16.666667
0.033333334,16.666667
0.050000001,16.666667
```

`elapsed_s` is the end of that frame interval, in seconds since capture start. `frame_ms` is
that interval's length, in milliseconds. Timestamps must increase; subsequent intervals must
match their timestamp differences within 2% or 0.1 ms. Do not drop long frames. Logging every
frame to the console's disk can itself cause stalls: buffer any instrumented capture and
use the same instrumentation in both builds. This contribution adds no runtime instrumentation.

For each capture, create a JSON metadata file:

```json
{
  "system": "switch",
  "game_id": "your-title-id",
  "game_version": "your-game-and-update-version",
  "scene": "same save, location, camera and movement",
  "console_model": "your PS5 model",
  "firmware": "your firmware",
  "hen": "your HEN and version",
  "build": "exact ECAM commit and dependency pins or build manifest",
  "settings": {
    "internal_resolution": "1x",
    "output": "1080p60",
    "frame_generation": false,
    "mods": [],
    "note": "Include all effective performance, graphics and controller settings"
  },
  "cache_state": "warm",
  "capture_method": "name and version of capture source",
  "frame_kind": "emulated"
}
```

Systems: `ds`, `3ds`, `wiiu`, `switch`. Cache states: `cold`, `warm`. Frame kinds: `emulated`
or `presented`. Presented frames can include generated or repeated frames; they do not establish
emulation speed or responsiveness. Capture the emulator's frames with generation off when
evaluating CPU, JIT, shader-cache or memory changes. Keep cold-cache startup and warm-cache play
as separate comparisons. Do not delete the user's existing shader caches to make a test cold;
use disposable test data.

All context fields must match, including the full settings object. Only `build` may differ.
For a settings experiment, make a separate comparison protocol: this tool deliberately refuses
to label different settings as an equivalent workload. Metadata is supplied by the tester;
the tool cannot verify it against the console.

## Compare

```sh
python3 tools/performance.py baseline.csv candidate.csv \
  --baseline-meta baseline.json --candidate-meta candidate.json > comparison.md

python3 tools/performance.py baseline.csv candidate.csv \
  --baseline-meta baseline.json --candidate-meta candidate.json \
  --format json > comparison.json
```

Defaults discard 10 seconds of warmup and analyze the following 60 seconds. Only complete
intervals inside that window count. Captures must extend through the window's end. At least
30 measured seconds and 300 frames must remain. Override these explicitly for a justified
protocol with `--warmup-s`, `--duration-s`, `--min-seconds`, `--min-frames`.

The report includes duration, frame count, aggregate FPS (frames divided by total frame time),
median, nearest-rank p95/p99, worst frame, and 1% low FPS (1000 divided by the mean of the
slowest ceil(N/100) intervals). It checks average FPS, 1% low, p95 and p99 against a default
5% worsening threshold, set with `--max-regression-percent`. Positive deltas mean worse.
The worst frame is reported but not used alone to fail a comparison.

Exit codes: **0** within threshold, **1** regression, **2** invalid/incomparable/insufficient
input. Zero is not proof of improvement. Preserve reports even when the command returns 1.
JSON includes the window, complete supplied metadata and SHA-256 hashes of both input files.
Reports contain supplied context; review them for personal data before posting publicly.

## Console acceptance before merging an emulator change

1. Keep the known-good release and back up settings and saves. Use copies for experiments.
2. Change one implementation at a time; keep dependencies and defaults unchanged otherwise.
3. Run the same scene at least three times per build, alternating baseline and candidate.
   Compare each matched pair and report every result, including variability and failures.
4. Check first boot and warm boot, sound, controller input, in-game save/load, available save
   states, quit to library and relaunch. A higher FPS result cannot override broken behavior.
5. Smoke-test DS, 3DS, Wii U and Switch even for a change scoped to one system. Shared launcher,
   graphics or memory code can affect the others.
6. Keep experimental accuracy/performance options off by default until hardware results support
   changing the default. Attach exact commits, settings, reports and failure logs to the PR.

Host validation of this tool:

```sh
python3 -B tools/tests/test-performance.py
```

The tests use synthetic traces to check arithmetic and failure handling. They are not PS5
measurements and must never be presented as an emulator speedup or console acceptance.
