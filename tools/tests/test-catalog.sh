#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The library's catalogue on a PC (tools/tests/test-catalog.cpp, port/app/catalog.cpp): library.json
# written in one process and read in the next, every side's games, a game in an archive and its play
# time; a broken file. Needs clang-18 (or g++) and RapidJSON (tools/deps.py fetch rapidjson, else the
# system's). Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -Wno-deprecated-declarations -DPS5CEMU_DATA="\"$work/data\"" -I"$root/port" \
    -I"$root/.deps/rapidjson/include" "$root/tools/tests/test-catalog.cpp" "$root/port/app/catalog.cpp" -o "$work/test-catalog"
mkdir -p "$work/data"
"$work/test-catalog" write
"$work/test-catalog" read
echo "not json {" >"$work/data/library.json"
"$work/test-catalog" broken
