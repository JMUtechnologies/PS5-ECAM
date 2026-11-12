#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# How games run on a PC: the feed tools/compat_feed.py makes from the compatibility list and the launcher
# preview's made-up reports (tools/launcher-preview/compat-reports.json), then port/app/compatibility.cpp
# reading it, a newer one, and a kept copy (tools/tests/test-compat.cpp); and site/build.py putting
# status.json on the site. Needs clang-18 (or g++), python3 and RapidJSON (tools/deps.py fetch rapidjson,
# else the system's). Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
python3 -B "$root/tools/compat_feed.py" --reports "$root/tools/launcher-preview/compat-reports.json" --out "$work/feed.json"
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -DFMT_HEADER_ONLY -I"$root/port" \
    -I"$root/.deps/rapidjson/include" "$root/tools/tests/test-compat.cpp" "$root/port/app/compatibility.cpp" -o "$work/test-compat"
mkdir -p "$work/data"
"$work/test-compat" "$work/feed.json" "$work/data"
# the site: status.json beside the table, every game the list has in it
python3 -B "$root/site/build.py" --compat "$root/tools/launcher-preview/compat-reports.json" --out "$work/site" >/dev/null
python3 -B - "$work/site/status.json" "$root/tools/compat-list.json" <<'EOF'
import json, sys
feed = json.load(open(sys.argv[1], encoding="utf-8"))
listed = len(json.load(open(sys.argv[2], encoding="utf-8"))["games"])
ok = feed["schema"] == 1 and len(feed["games"]) > listed and all(g["status"] in ("playable", "issues", "broken") for g in feed["games"])
print(("PASS" if ok else "FAIL") + f"  the site's status.json: {len(feed['games'])} games, the list's {listed} and the reports'")
sys.exit(0 if ok else 1)
EOF
