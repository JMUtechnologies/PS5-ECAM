#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The reports on a PC (tools/tests/test-reports.cpp, port/app/reports.cpp): the scrubber, a report made
# from a log full of made-up personal data, the session's endings, the player's no, the outbox's limits;
# then the intake's own checks (tools/log-intake/test.mjs) when node is there. Needs clang-18 (or g++)
# and RapidJSON (tools/deps.py fetch rapidjson, else the system's). Prints PASS lines; exits 1 on a
# failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$root/port" \
    -I"$root/.deps/rapidjson/include" "$root/tools/tests/test-reports.cpp" "$root/port/app/reports.cpp" -o "$work/test-reports"
mkdir -p "$work/reports" "$work/data"
"$work/test-reports" "$work/reports"
# again with lstat refused, as the console's /data has it without a HEN (tools/tests/refuse-lstat.c)
"$compiler" -std=c++20 -O1 -I"$root/port" -I"$root/.deps/rapidjson/include" "$root/tools/tests/test-reports.cpp" "$root/port/app/reports.cpp" \
    -o "$work/test-reports-plain"
"${CC:-cc}" -shared -fPIC "$root/tools/tests/refuse-lstat.c" -o "$work/refuse-lstat.so" -ldl
REFUSE_LSTAT_UNDER="$work/data" LD_PRELOAD="$work/refuse-lstat.so" "$work/test-reports-plain" "$work/data" | grep -v '^PASS' || true
[[ ${PIPESTATUS[0]} -eq 0 ]]
if command -v node >/dev/null; then
    node "$root/tools/log-intake/test.mjs"
else
    echo "SKIP  the intake's checks (no node)"
fi
