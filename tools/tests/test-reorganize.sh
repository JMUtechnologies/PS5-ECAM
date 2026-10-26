#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A folder for each system, and put back (port/app/reorganize.cpp), in a folder of its own as /data:
# tools/tests/test-reorganize.cpp. Needs nothing but a C++20 compiler. Prints PASS lines; exits 1 on a
# failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'chmod -R u+rwx "$work" 2>/dev/null; rm -rf "$work"' EXIT
mkdir -p "$work/data"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" -DPS5CEMU_DATA="\"$work/data/ps5cemu\"" \
    "$root/tools/tests/test-reorganize.cpp" "$root/port/app/reorganize.cpp" -o "$work/test-reorganize"
"$work/test-reorganize" "$work/data"
