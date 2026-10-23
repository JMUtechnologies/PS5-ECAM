#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Where the DS's screens go on the TV, on a PC (tools/tests/test-screens.cpp,
# port/melonds/screen_place.cpp): every layout, swap, quarter turn and gap, on pictures of several
# sizes. Needs clang-18 (or g++). Prints PASS or FAIL lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -I"$root/port" "$root/tools/tests/test-screens.cpp" "$root/port/melonds/screen_place.cpp" \
    -o "$work/test-screens"
"$work/test-screens"
