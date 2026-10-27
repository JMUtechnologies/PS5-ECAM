#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" "$root/tools/tests/test-frame-capture.cpp" -o "$work/test-capture"
"$work/test-capture" "$work/frames.csv"
python3 - "$root" "$work/frames.csv" <<'PY'
import json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / 'tools'))
import performance
csv = pathlib.Path(sys.argv[2])
metadata = dict(system='ds', game_id='synthetic-capture-test', game_version='test',
    scene='fixed synthetic timestamps', console_model='host test', firmware='none', hen='none',
    settings={'synthetic': True}, cache_state='warm', capture_method='unit-test buffer',
    frame_kind='presented', build='test')
meta = csv.with_suffix('.json')
meta.write_text(json.dumps(metadata))
run = performance.read_run(csv, meta, 0, .003)
assert run['metrics']['frames'] == 3
assert abs(run['metrics']['average_fps'] - 1000) < 1e-6
print('PASS native CSV accepted by performance comparison parser (synthetic fixture)')
PY
